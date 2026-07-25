// Builds one world on a background thread while the loading screen shows
// honest progress.  The two threads share a narrow channel: the worker
// writes status text and its newest terrain snapshot, the main thread
// reads them once per frame.  The build itself is the linear story in
// build_world (); everything before it is the channel it narrates into,
// and everything after it is thread plumbing and the main-thread facade.

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
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <variant>

namespace moppe::game {
  namespace {
    // The preview never needs more vertices than the screen has pixels for.
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

    // Cache files are keyed by everything that determines their contents,
    // so a stale cache is impossible by construction.  Extents encode as
    // hex float bits because a rounded decimal would blur that key.
    std::string terrain_cache_path (const terrain::WorldRecipe& recipe) {
      const Vec3 extent = extent_value (recipe.extent ());
      const auto bits = [] (float value) {
        return std::bit_cast<std::uint32_t> (value);
      };
      std::ostringstream name;
      name << "terrain-" << platform::executable_build_id () << '-'
           << terrain::profile_id (recipe.generation_profile ()) << '-'
           << recipe.resolution () << '-' << recipe.seed ().value << std::hex
           << "-extent-" << bits (extent[0]) << '-' << bits (extent[1]) << '-'
           << bits (extent[2]) << "-water-"
           << bits (meters_value (recipe.water_datum ()))
           << (recipe.topology () == terrain::Topology::Torus ? "-torus.map"
                                                              : "-bounded.map");
      return platform::cache_path (name.str ());
    }
  }

  // -- the channel between the worker and the loading screen -------------

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

    // Everything the mutex guards, resettable in one assignment.
    struct Shared {
      std::string title;
      std::string detail;
      float progress = -1.0f;
      std::vector<LoadingEvent> events;
      std::shared_ptr<const std::vector<float>> latest;
      std::shared_ptr<const std::vector<float>> consumed;
      std::unique_ptr<GeneratedWorld> completed_world;
    };

    void reset (const WorldParams& world, const terrain::WorldRecipe& recipe) {
      preview_world = world;
      seed = recipe.seed ().value;
      clock_start = platform::now ();
      capture_done = false;
      terrain_visible = false;
      generation_complete = false;
      {
        const std::lock_guard<std::mutex> lock (mutex);
        shared = Shared {};
      }
      report ("Waking the world builder",
              "Preparing terrain storage and compute");
    }

    float elapsed () const {
      return static_cast<float> (platform::now () - clock_start);
    }

    void
    report (std::string title, std::string detail, float progress = -1.0f) {
      const double now = elapsed ();
      const std::lock_guard<std::mutex> lock (mutex);
      if (shared.events.empty () || shared.events.back ().title != title)
        shared.events.push_back ({ title, now });
      shared.title = std::move (title);
      shared.detail = std::move (detail);
      shared.progress = progress;
    }

    void publish_terrain (const map::RandomHeightMap& terrain) {
      auto heights = downsample_for_preview (terrain);
      const std::lock_guard<std::mutex> lock (mutex);
      if (!shared.latest || *shared.latest != *heights)
        shared.latest = std::move (heights);
    }

    void publish_completed (std::unique_ptr<GeneratedWorld> world) {
      const std::lock_guard<std::mutex> lock (mutex);
      if (shared.completed_world)
        throw std::logic_error (
          "a completed world is already awaiting activation");
      shared.completed_world = std::move (world);
    }

    // Written at reset, read from both threads.
    WorldParams preview_world;
    std::atomic<std::uint32_t> seed;
    double clock_start;
    std::atomic<bool> generation_complete = false;
    std::atomic<bool> in_flight = false;

    // Main-thread only: the materialized preview and its bookkeeping.
    map::RandomHeightMap preview_map;
    bool capture_done = false;
    bool terrain_visible = false;

    std::mutex mutex;
    Shared shared;

  private:
    std::shared_ptr<std::vector<float>>
    downsample_for_preview (const map::RandomHeightMap& terrain) const {
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
      return heights;
    }
  };

  // -- the build ---------------------------------------------------------

  namespace {
    void snapshot_history (const map::RandomHeightMap& terrain,
                           std::vector<std::vector<float>>& history) {
      const std::size_t count =
        static_cast<std::size_t> (terrain.width ()) * terrain.height ();
      history.emplace_back (terrain.raw_heights (),
                            terrain.raw_heights () + count);
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

    // Materializes the recipe's terrain program, narrating the two long
    // phases with real measurements: field rows while the continents
    // materialize, then geological time while orogeny runs.  Every
    // finished transform is snapshotted for the Terrain Lab scrubber and
    // published to the preview.
    std::optional<terrain::TrailNetwork>
    evolve_terrain (WorldLoadingState& state,
                    const terrain::WorldRecipe& recipe,
                    map::RandomHeightMap& terrain,
                    std::vector<std::vector<float>>& history) {
      std::unique_ptr<terrain::FieldEvaluator> field_evaluator =
        platform::create_field_evaluator ();
      std::unique_ptr<terrain::StreamPowerEvolutionBackend> evolution =
        platform::create_stream_power_evolution_backend ();
      map::TerrainEvaluator evaluator (
        terrain, field_evaluator.get (), evolution.get ());
      history.clear ();

      const auto after_transform =
        [&] (std::size_t, const terrain::TerrainTransform& transform) {
          snapshot_history (terrain, history);
          state.publish_terrain (terrain);
          if (std::holds_alternative<terrain::OrogenyEvolution> (transform))
            state.report ("Refining the terrain",
                          "Shaping coasts, channels, and the overland route");
        };

      const auto report_geological_time =
        [&] (std::size_t,
             const terrain::TerrainTransform& transform,
             int completed_steps,
             int total_steps) {
          if (!std::holds_alternative<terrain::OrogenyEvolution> (transform))
            return;
          const auto& orogeny = std::get<terrain::OrogenyEvolution> (transform);
          const float duration =
            julian_years_value (orogeny.evolution.duration);
          const float step = julian_years_value (orogeny.evolution.time_step);
          const int years_done = static_cast<int> (
            std::lround (std::min (duration, completed_steps * step)));
          const int years_total = static_cast<int> (std::lround (duration));
          std::ostringstream detail;
          detail << "Geological time  " << grouped_number (years_done) << " / "
                 << grouped_number (years_total) << " years  /  step "
                 << completed_steps << " of " << total_steps;
          state.report ("Running geological time",
                        detail.str (),
                        static_cast<float> (completed_steps) /
                          std::max (1, total_steps));
          state.publish_terrain (terrain);
        };

      std::atomic<int> row_watermark { 0 };
      const auto report_field_rows = [&] (std::size_t completed_rows,
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
      };

      evaluator.evaluate (recipe.terrain_program (),
                          after_transform,
                          report_geological_time,
                          report_field_rows);
      snapshot_history (terrain, history);
      return evaluator.release_trail_network ();
    }

    std::pair<const char*, const char*>
    hydrology_report (GeneratedWorld::HydrologyStage stage) {
      using Stage = GeneratedWorld::HydrologyStage;
      switch (stage) {
      case Stage::StandingWater:
        return { "Filling seas and lakes",
                 "Finding the connected water surface" };
      case Stage::Lakes:
        return { "Cataloguing lakes",
                 "Measuring every separate body of water" };
      case Stage::Drainage:
        return { "Tracing the drainage", "Following every wet cell downhill" };
      case Stage::Waterways:
        return { "Connecting the waterways",
                 "Joining lakes, outlets, and the sea" };
      case Stage::Channels:
      case Stage::Rivers:
        return { "Extracting the rivers",
                 "Selecting the channels visible in the world" };
      }
      return { "Analyzing the water", "" };
    }

    void log_standing_water (const GeneratedWorld::Hydrology& hydrology) {
      std::size_t wet = 0;
      for (const terrain::WaterBody& body : hydrology.lakes ().bodies)
        wet += terrain::count_value (body.cells);
      std::cerr << "standing water: " << hydrology.lakes ().bodies.size ()
                << " bodies, " << wet << " wet cells\n";
    }

    struct GenerationJob {
      std::shared_ptr<WorldLoadingState> state;
      WorldParams params;
      terrain::WorldRecipe recipe;
    };

    // The whole build, in order.  Terrain comes from the cache or is
    // evolved fresh; then the surface, the water analysis, and the final
    // assembly, each announcing itself as it starts.
    void build_world (GenerationJob& job) {
      MOPPE_PROFILE_ZONE ("WorldLoading::build_world");
      WorldLoadingState& state = *job.state;
      auto completed =
        std::make_unique<GeneratedWorld> (job.params, job.recipe);
      GeneratedWorld::Builder build = completed->build ();
      map::RandomHeightMap& terrain = build.terrain ();
      std::vector<std::vector<float>>& history = build.terrain_history ();
      const terrain::WorldRecipe& recipe = completed->recipe ();
      std::optional<terrain::TrailNetwork> trails;

      const char* cache_override = ::getenv ("MOPPE_MAPCACHE");
      const std::string cache =
        cache_override ? cache_override : terrain_cache_path (recipe);

      state.report ("Looking for saved terrain",
                    "Checking this build, profile, and seed");
      if (terrain.try_load_cache (cache)) {
        state.report ("Reading saved terrain",
                      "Reusing the finished heightfield");
        terrain.try_load_cached_history (cache, history);
      } else {
        state.report ("Drawing the continents",
                      "Materializing the geological field");
        trails = evolve_terrain (state, recipe, terrain, history);
        state.report ("Saving the terrain",
                      "Keeping this expensive result for the next launch");
        terrain.save_cache (cache);
        terrain.append_cached_history (cache, history);
      }
      if (history.empty ())
        snapshot_history (terrain, history);
      state.publish_terrain (terrain);

      state.report ("Calculating slopes",
                    "Rebuilding normals and the sampled surface");
      build.rebuild_surface ();

      build.analyze_hydrology ([&state] (GeneratedWorld::HydrologyStage stage) {
        const auto [title, detail] = hydrology_report (stage);
        state.report (title, detail);
      });
      if (!completed->hydrology ())
        throw std::logic_error ("completed world has no hydrology");
      log_standing_water (*completed->hydrology ());

      state.report ("Assembling the world",
                    "Painting water, moisture, materials, and the opening "
                    "route");
      build.materialize_analyses (std::move (trails));
      state.publish_completed (std::move (completed));
    }

    // -- thread plumbing -------------------------------------------------

    void run_generation_job (void* context) {
      GenerationJob& job = *static_cast<GenerationJob*> (context);
      MOPPE_PROFILE_THREAD ("World generation");
      try {
        build_world (job);
      } catch (const std::exception& error) {
        std::cerr << "world generation failed: " << error.what () << std::endl;
        std::_Exit (-1);
      }
    }

    void finish_generation_job (void* context) {
      GenerationJob& job = *static_cast<GenerationJob*> (context);
      job.state->generation_complete = true;
      job.state->in_flight = false;
    }
  }

  // -- the main-thread facade --------------------------------------------

  WorldLoading::WorldLoading (const WorldParams& world,
                              const terrain::WorldRecipe& recipe)
      : m_state (std::make_shared<WorldLoadingState> (world, recipe)) {}

  void WorldLoading::start (const WorldParams& world,
                            terrain::WorldRecipe recipe) {
    if (m_state->in_flight.exchange (true))
      throw std::logic_error ("world generation is already in flight");
    {
      const std::lock_guard<std::mutex> lock (m_state->mutex);
      if (m_state->shared.completed_world) {
        m_state->in_flight = false;
        throw std::logic_error ("a completed world has not activated");
      }
    }
    m_state->reset (world, recipe);
    auto job = std::make_shared<GenerationJob> (
      GenerationJob { m_state, world, std::move (recipe) });
    platform::async (
      run_generation_job, finish_generation_job, std::move (job));
  }

  void
  WorldLoading::report (std::string title, std::string detail, float progress) {
    m_state->report (std::move (title), std::move (detail), progress);
  }

  std::unique_ptr<GeneratedWorld> WorldLoading::take_completed_world () {
    if (!m_state->generation_complete.load ())
      return {};
    const std::lock_guard<std::mutex> lock (m_state->mutex);
    return std::move (m_state->shared.completed_world);
  }

  LoadingStatus WorldLoading::status () {
    WorldLoadingState& state = *m_state;
    const std::lock_guard<std::mutex> lock (state.mutex);
    return {
      .title = state.shared.title,
      .detail = state.shared.detail,
      .progress = state.shared.progress,
      .elapsed = state.elapsed (),
      .terrain_visible = state.terrain_visible,
      .seed = state.seed.load (),
      .events = state.shared.events,
    };
  }

  bool WorldLoading::refresh_preview () {
    WorldLoadingState& state = *m_state;
    std::shared_ptr<const std::vector<float>> heights;
    {
      const std::lock_guard<std::mutex> lock (state.mutex);
      if (state.shared.latest == state.shared.consumed)
        return false;
      heights = state.shared.latest;
      state.shared.consumed = state.shared.latest;
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
