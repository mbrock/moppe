#include <moppe/game/world_loading.hh>

#include <moppe/platform/platform.hh>
#include <moppe/profile.hh>

#include <moppe/map/terrain_evaluator.hh>
#include <moppe/terrain/river.hh>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <variant>

namespace moppe::game {
  namespace {
    int loading_preview_resolution (int terrain_resolution) {
      constexpr int maximum_preview_resolution = 513;
      return std::min (terrain_resolution, maximum_preview_resolution);
    }

    std::string grouped_number (int value) {
      std::string result = std::to_string (value);
      for (std::ptrdiff_t i = static_cast<std::ptrdiff_t> (result.size ()) - 3;
           i > 0;
           i -= 3)
        result.insert (static_cast<std::size_t> (i), ",");
      return result;
    }

    void append_cache_float (std::ostream& stream, float value) {
      stream << std::hex << std::bit_cast<std::uint32_t> (value) << std::dec;
    }

    std::string terrain_cache_path (const terrain::WorldRecipe& recipe) {
      const Vec3 extent = extent_value (recipe.extent ());
      std::ostringstream name;
      name << "terrain-" << platform::executable_build_id () << '-'
           << terrain::profile_id (recipe.generation_profile ()) << '-'
           << recipe.resolution () << '-' << recipe.seed ().value << "-extent-";
      append_cache_float (name, extent[0]);
      name << '-';
      append_cache_float (name, extent[1]);
      name << '-';
      append_cache_float (name, extent[2]);
      name << "-water-";
      append_cache_float (name, meters_value (recipe.water_datum ()));
      name << (recipe.topology () == terrain::Topology::Torus ? "-torus.map"
                                                              : "-bounded.map");
      return platform::cache_path (name.str ());
    }

    bool load_terrain_history (const std::string& path,
                               std::size_t samples,
                               std::vector<std::vector<float>>& history) {
      std::ifstream input (path, std::ios::binary);
      if (!input)
        return false;
      input.seekg (12 + static_cast<std::streamoff> (samples * sizeof (float)));
      char section[4] {};
      input.read (section, sizeof (section));
      if (input && std::memcmp (section, "LGR1", 4) == 0)
        input.seekg (static_cast<std::streamoff> (2 * samples * sizeof (float)),
                     std::ios::cur);
      else
        input.seekg (-static_cast<std::streamoff> (sizeof (section)),
                     std::ios::cur);
      char magic[4] {};
      std::uint32_t snapshot_count = 0;
      std::uint64_t sample_count = 0;
      input.read (magic, sizeof (magic));
      input.read (reinterpret_cast<char*> (&snapshot_count),
                  sizeof (snapshot_count));
      input.read (reinterpret_cast<char*> (&sample_count),
                  sizeof (sample_count));
      if (!input || std::memcmp (magic, "HST1", 4) != 0 ||
          sample_count != samples || snapshot_count > 64)
        return false;
      std::vector<std::vector<float>> loaded (snapshot_count,
                                              std::vector<float> (samples));
      for (std::vector<float>& snapshot : loaded)
        input.read (reinterpret_cast<char*> (snapshot.data ()),
                    static_cast<std::streamsize> (samples * sizeof (float)));
      if (!input)
        return false;
      history = std::move (loaded);
      return true;
    }

    void save_terrain_history (const std::string& path,
                               const std::vector<std::vector<float>>& history) {
      if (history.empty ())
        return;
      const std::uint64_t samples = history.front ().size ();
      if (samples == 0 || std::any_of (history.begin (),
                                       history.end (),
                                       [samples] (const auto& snapshot) {
                                         return snapshot.size () != samples;
                                       }))
        return;
      std::ofstream output (path, std::ios::binary | std::ios::app);
      const std::uint32_t snapshot_count =
        static_cast<std::uint32_t> (history.size ());
      output.write ("HST1", 4);
      output.write (reinterpret_cast<const char*> (&snapshot_count),
                    sizeof (snapshot_count));
      output.write (reinterpret_cast<const char*> (&samples), sizeof (samples));
      for (const std::vector<float>& snapshot : history)
        output.write (reinterpret_cast<const char*> (snapshot.data ()),
                      static_cast<std::streamsize> (samples * sizeof (float)));
    }

    // The field evaluator reports row completion from several worker
    // threads at once; only the furthest row should reach the status line.
    bool advance_watermark (std::atomic<int>& watermark, int row) {
      int observed = watermark.load ();
      while (observed < row)
        if (watermark.compare_exchange_weak (observed, row))
          return true;
      return false;
    }
  }

  // The state shared between the generation thread and the loading screen.
  // The worker writes status text and the newest terrain snapshot; the main
  // thread reads them once per frame.  There is no queue and no pacing: the
  // preview always shows the latest terrain the worker has produced.
  class WorldLoadingState {
  public:
    WorldLoadingState (const WorldParams& initial_world,
                       const terrain::WorldRecipe& initial_recipe)
        : preview_world (initial_world), seed (initial_recipe.seed ().value),
          preview_map (
            loading_preview_resolution (initial_recipe.resolution ()),
            loading_preview_resolution (initial_recipe.resolution ()),
            extent_value (initial_recipe.extent ()),
            initial_recipe.topology ()),
          clock_start (platform::now ()) {}

    void reset (const WorldParams& world, const terrain::WorldRecipe& recipe) {
      preview_world = world;
      seed = recipe.seed ().value;
      clock_start = platform::now ();
      capture_done = false;
      terrain_visible = false;
      generation_complete = false;
      {
        const std::lock_guard<std::mutex> lock (mutex);
        title.clear ();
        detail.clear ();
        progress = -1.0f;
        events.clear ();
        latest.reset ();
        consumed.reset ();
        completed_world.reset ();
      }
      report ("Waking the world builder",
              "Preparing terrain storage and compute");
    }

    float elapsed () const {
      return static_cast<float> (platform::now () - clock_start);
    }

    void report (std::string new_title,
                 std::string new_detail,
                 float new_progress = -1.0f) {
      const double now = elapsed ();
      const std::lock_guard<std::mutex> lock (mutex);
      if (events.empty () || events.back ().title != new_title)
        events.push_back ({ new_title, now });
      title = std::move (new_title);
      detail = std::move (new_detail);
      progress = new_progress;
    }

    void publish_terrain (const map::RandomHeightMap& terrain) {
      const int width = preview_map.width ();
      const int height = preview_map.height ();
      auto heights = std::make_shared<std::vector<float>> (
        static_cast<std::size_t> (width) * height);
      for (int y = 0; y < height; ++y) {
        const int source_y =
          y * (terrain.height () - 1) / std::max (1, height - 1);
        for (int x = 0; x < width; ++x) {
          const int source_x =
            x * (terrain.width () - 1) / std::max (1, width - 1);
          (*heights)[static_cast<std::size_t> (y) * width + x] =
            terrain.get (source_x, source_y);
        }
      }
      const std::lock_guard<std::mutex> lock (mutex);
      if (!latest || *latest != *heights)
        latest = std::move (heights);
    }

    void publish_completed (std::unique_ptr<GeneratedWorld> world) {
      const std::lock_guard<std::mutex> lock (mutex);
      if (completed_world)
        throw std::logic_error (
          "a completed world is already awaiting activation");
      completed_world = std::move (world);
    }

    WorldParams preview_world;
    std::atomic<std::uint32_t> seed;
    map::RandomHeightMap preview_map;
    double clock_start;
    bool capture_done = false;
    bool terrain_visible = false;
    std::atomic<bool> generation_complete = false;
    std::atomic<bool> in_flight = false;

    std::mutex mutex;
    std::string title;
    std::string detail;
    float progress = -1.0f;
    std::vector<LoadingEvent> events;
    std::shared_ptr<const std::vector<float>> latest;
    std::shared_ptr<const std::vector<float>> consumed;
    std::unique_ptr<GeneratedWorld> completed_world;
  };

  namespace {
    struct GenerationJob {
      std::shared_ptr<WorldLoadingState> state;
      WorldParams params;
      terrain::WorldRecipe recipe;
    };

    void generate_world_inner (GenerationJob& job) {
      MOPPE_PROFILE_ZONE ("WorldLoading::generate_world_inner");
      WorldLoadingState& state = *job.state;
      auto completed =
        std::make_unique<GeneratedWorld> (job.params, job.recipe);
      GeneratedWorld::Builder build = completed->build ();
      map::RandomHeightMap& terrain = build.terrain ();
      std::vector<std::vector<float>>& history = build.terrain_history ();
      std::optional<terrain::TrailNetwork> generated_trails;
      const terrain::WorldRecipe& recipe = completed->recipe ();
      std::unique_ptr<terrain::FieldEvaluator> field_evaluator =
        platform::create_field_evaluator ();
      std::unique_ptr<terrain::StreamPowerEvolutionBackend> evolution_backend =
        platform::create_stream_power_evolution_backend ();

      const char* cache_override = ::getenv ("MOPPE_MAPCACHE");
      const std::string automatic_cache = terrain_cache_path (recipe);
      const char* cache =
        cache_override ? cache_override : automatic_cache.c_str ();
      state.report ("Looking for saved terrain",
                    "Checking this build, profile, and seed");
      const bool cache_loaded = cache && terrain.try_load_cache (cache);

      if (cache_loaded) {
        state.report ("Reading saved terrain",
                      "Reusing the finished heightfield");
        const std::size_t count =
          static_cast<std::size_t> (terrain.width ()) * terrain.height ();
        load_terrain_history (cache, count, history);
        state.publish_terrain (terrain);
      } else {
        state.report ("Drawing the continents",
                      "Materializing the geological field");
        map::TerrainEvaluator evaluator (
          terrain, field_evaluator.get (), evolution_backend.get ());
        history.clear ();
        std::atomic<int> row_watermark { 0 };
        evaluator.evaluate (
          recipe.terrain_program (),
          // A transform finished: snapshot it for the history scrubber and
          // show the newest terrain.
          [&state, &history, &terrain] (
            std::size_t, const terrain::TerrainTransform& transform) {
            const std::size_t count =
              static_cast<std::size_t> (terrain.width ()) * terrain.height ();
            history.emplace_back (terrain.raw_heights (),
                                  terrain.raw_heights () + count);
            state.publish_terrain (terrain);
            if (std::holds_alternative<terrain::OrogenyEvolution> (transform))
              state.report ("Refining the terrain",
                            "Shaping coasts, channels, and the overland "
                            "route");
          },
          // Orogeny progress: geological time is the real measure here.
          [&state] (std::size_t,
                    const terrain::TerrainTransform& transform,
                    int completed_steps,
                    int total_steps) {
            if (!std::holds_alternative<terrain::OrogenyEvolution> (transform))
              return;
            const auto& orogeny =
              std::get<terrain::OrogenyEvolution> (transform);
            const float duration =
              julian_years_value (orogeny.evolution.duration);
            const float step = julian_years_value (orogeny.evolution.time_step);
            const int years_done = static_cast<int> (
              std::lround (std::min (duration, completed_steps * step)));
            const int years_total = static_cast<int> (std::lround (duration));
            std::ostringstream detail;
            detail << "Geological time  " << grouped_number (years_done)
                   << " / " << grouped_number (years_total)
                   << " years  /  step " << completed_steps << " of "
                   << total_steps;
            state.report ("Running geological time",
                          detail.str (),
                          static_cast<float> (completed_steps) /
                            std::max (1, total_steps));
          },
          // Field rows complete out of order across evaluator threads.
          [&state, &row_watermark] (std::size_t completed_rows,
                                    std::size_t total_rows) {
            if (!advance_watermark (row_watermark,
                                    static_cast<int> (completed_rows)))
              return;
            std::ostringstream detail;
            detail << "Field row " << completed_rows << " of " << total_rows;
            state.report ("Drawing the continents",
                          detail.str (),
                          static_cast<float> (completed_rows) /
                            std::max<std::size_t> (1, total_rows));
          });
        generated_trails = evaluator.release_trail_network ();
        const std::size_t count =
          static_cast<std::size_t> (terrain.width ()) * terrain.height ();
        history.emplace_back (terrain.raw_heights (),
                              terrain.raw_heights () + count);
        if (cache) {
          state.report ("Saving the terrain",
                        "Keeping this expensive result for the next launch");
          terrain.save_cache (cache);
          save_terrain_history (cache, history);
        }
      }
      if (history.empty ()) {
        const std::size_t count =
          static_cast<std::size_t> (terrain.width ()) * terrain.height ();
        history.emplace_back (terrain.raw_heights (),
                              terrain.raw_heights () + count);
      }
      state.publish_terrain (terrain);

      state.report ("Calculating slopes",
                    "Rebuilding normals and the sampled surface");
      build.rebuild_surface ();

      build.analyze_hydrology ([&state] (GeneratedWorld::HydrologyStage stage) {
        switch (stage) {
        case GeneratedWorld::HydrologyStage::StandingWater:
          state.report ("Filling seas and lakes",
                        "Finding the connected water surface");
          break;
        case GeneratedWorld::HydrologyStage::Lakes:
          state.report ("Cataloguing lakes",
                        "Measuring every separate body of water");
          break;
        case GeneratedWorld::HydrologyStage::Drainage:
          state.report ("Tracing the drainage",
                        "Following every wet cell downhill");
          break;
        case GeneratedWorld::HydrologyStage::Waterways:
          state.report ("Connecting the waterways",
                        "Joining lakes, outlets, and the sea");
          break;
        case GeneratedWorld::HydrologyStage::Channels:
        case GeneratedWorld::HydrologyStage::Rivers:
          state.report ("Extracting the rivers",
                        "Selecting the channels visible in the world");
          break;
        }
      });
      const auto& hydrology = completed->hydrology ();
      if (!hydrology)
        throw std::logic_error ("completed world has no hydrology");
      std::size_t wet = 0;
      for (const terrain::WaterBody& body : hydrology->lakes ().bodies)
        wet += terrain::count_value (body.cells);
      std::cerr << "standing water: " << hydrology->lakes ().bodies.size ()
                << " bodies, " << wet << " wet cells\n";

      state.report ("Assembling the world",
                    "Painting water, moisture, materials, and the opening "
                    "route");
      build.materialize_analyses (std::move (generated_trails));
      state.publish_completed (std::move (completed));
    }

    void generate_world (void* context) {
      GenerationJob& job = *static_cast<GenerationJob*> (context);
      MOPPE_PROFILE_THREAD ("World generation");
      MOPPE_PROFILE_ZONE ("WorldLoading::generate_world");
      try {
        generate_world_inner (job);
      } catch (const std::exception& error) {
        std::cerr << "world generation failed: " << error.what () << std::endl;
        std::_Exit (-1);
      }
    }

    void finish_world (void* context) {
      GenerationJob& job = *static_cast<GenerationJob*> (context);
      job.state->generation_complete = true;
      job.state->in_flight = false;
    }
  }

  WorldLoading::WorldLoading (const WorldParams& world,
                              const terrain::WorldRecipe& recipe)
      : m_state (std::make_shared<WorldLoadingState> (world, recipe)) {}

  void WorldLoading::start (const WorldParams& world,
                            terrain::WorldRecipe recipe) {
    if (m_state->in_flight.exchange (true))
      throw std::logic_error ("world generation is already in flight");
    {
      const std::lock_guard<std::mutex> lock (m_state->mutex);
      if (m_state->completed_world) {
        m_state->in_flight = false;
        throw std::logic_error ("a completed world has not activated");
      }
    }
    m_state->reset (world, recipe);
    auto job = std::make_shared<GenerationJob> (
      GenerationJob { m_state, world, std::move (recipe) });
    platform::async (generate_world, finish_world, std::move (job));
  }

  void
  WorldLoading::report (std::string title, std::string detail, float progress) {
    m_state->report (std::move (title), std::move (detail), progress);
  }

  bool WorldLoading::generation_complete () const noexcept {
    return m_state->generation_complete.load ();
  }

  std::unique_ptr<GeneratedWorld> WorldLoading::take_completed_world () {
    if (!generation_complete ())
      return {};
    const std::lock_guard<std::mutex> lock (m_state->mutex);
    return std::move (m_state->completed_world);
  }

  LoadingStatus WorldLoading::status () {
    WorldLoadingState& state = *m_state;
    const std::lock_guard<std::mutex> lock (state.mutex);
    return {
      .title = state.title,
      .detail = state.detail,
      .progress = state.progress,
      .elapsed = state.elapsed (),
      .terrain_visible = state.terrain_visible,
      .seed = state.seed.load (),
      .events = state.events,
    };
  }

  bool WorldLoading::refresh_preview () {
    WorldLoadingState& state = *m_state;
    std::shared_ptr<const std::vector<float>> heights;
    {
      const std::lock_guard<std::mutex> lock (state.mutex);
      if (state.latest == state.consumed)
        return false;
      heights = state.latest;
      state.consumed = state.latest;
    }
    std::copy (
      heights->begin (), heights->end (), state.preview_map.raw_heights ());
    state.terrain_visible = true;
    return true;
  }

  const WorldParams& WorldLoading::preview_world () const noexcept {
    return m_state->preview_world;
  }

  const map::RandomHeightMap& WorldLoading::preview_map () const noexcept {
    return m_state->preview_map;
  }

  bool WorldLoading::claim_loading_capture (bool last_chance) noexcept {
    WorldLoadingState& state = *m_state;
    if (state.capture_done || !state.terrain_visible)
      return false;
    if (!last_chance && state.elapsed () < 1.0f)
      return false;
    state.capture_done = true;
    return true;
  }
}
