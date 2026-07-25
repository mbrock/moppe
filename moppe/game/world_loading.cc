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
#include <deque>
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
  }

  LoadingStageText loading_stage_text (LoadingStage stage) {
    switch (stage) {
    case LoadingStage::Starting:
      return { "Waking the world builder",
               "Preparing terrain storage and compute",
               0.02f };
    case LoadingStage::LookingForCache:
      return { "Looking for saved terrain",
               "Checking this build, profile, and seed",
               0.04f };
    case LoadingStage::ReadingCache:
      return { "Reading saved terrain",
               "Reusing the finished heightfield",
               0.10f };
    case LoadingStage::BuildingContinents:
      return { "Drawing the continents",
               "Materializing the geological field",
               0.06f };
    case LoadingStage::EvolvingTerrain:
      return { "Running geological time",
               "Uplift and erosion are reshaping the land",
               0.20f };
    case LoadingStage::RefiningTerrain:
      return { "Refining the terrain",
               "Shaping coasts, channels, and the overland route",
               0.68f };
    case LoadingStage::SavingTerrain:
      return { "Saving the terrain",
               "Keeping this expensive result for the next launch",
               0.69f };
    case LoadingStage::RebuildingSurface:
      return { "Calculating slopes",
               "Rebuilding normals and the sampled surface",
               0.72f };
    case LoadingStage::FindingStandingWater:
      return { "Filling seas and lakes",
               "Finding the connected water surface",
               0.75f };
    case LoadingStage::CataloguingLakes:
      return { "Cataloguing lakes",
               "Measuring every separate body of water",
               0.78f };
    case LoadingStage::TracingDrainage:
      return { "Tracing the drainage",
               "Following every wet cell downhill",
               0.81f };
    case LoadingStage::ConnectingWaterways:
      return { "Connecting the waterways",
               "Joining lakes, outlets, and the sea",
               0.84f };
    case LoadingStage::ExtractingRivers:
      return { "Extracting the rivers",
               "Selecting the channels visible in the world",
               0.87f };
    case LoadingStage::AssemblingWorld:
      return { "Assembling the world",
               "Painting water, moisture, materials, and the opening route",
               0.90f };
    case LoadingStage::PreparingWater:
      return { "Setting the water in motion",
               "Building river ribbons and standing-water surfaces",
               0.91f };
    case LoadingStage::PreparingSurface:
      return { "Painting the surface",
               "Preparing moisture and geological materials",
               0.925f };
    case LoadingStage::PlacingStars:
      return { "Placing the stars",
               "Finding bright landmarks across the terrain",
               0.94f };
    case LoadingStage::GrowingForest:
      return { "Growing the forest",
               "Distributing the canopy across the landscape",
               0.95f };
    case LoadingStage::PlantingTrailside:
      return { "Planting the trailside",
               "Growing the first stand around the journey's beginning",
               0.96f };
    case LoadingStage::PlanningJourney:
      return { "Planning the first journey",
               "Choosing a route through the new landscape",
               0.97f };
    case LoadingStage::UploadingTerrain:
      return { "Uploading the landscape",
               "Moving the finished world onto the GPU",
               0.98f };
    case LoadingStage::CastingShadows:
      return { "Casting the first shadows",
               "Precomputing sunlight across the terrain",
               0.99f };
    case LoadingStage::Ready:
      return { "The world is ready", "Setting out", 1.0f };
    }
    return { "Building the world", "Working", 0.0f };
  }

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
      work_done = 0;
      work_total = 1;
      geological_years_done = 0;
      geological_years_total = 0;
      source_done = 0;
      source_total = 1;
      progress_display = 0.0f;
      progress_target = 0.0f;
      last_frame_time = 0.0f;
      evolution_frames = 0;
      evolution_frame_time = 0.0;
      evolution_max_frame_time = 0.0f;
      evolution_frames_over_20ms = 0;
      benchmark_reported = false;
      height_transition_ready_time = 0.0f;
      capture_done = false;
      clock_start = platform::now ();
      terrain_visible = false;
      generation_complete = false;
      {
        const std::lock_guard<std::mutex> lock (mutex);
        events.clear ();
        height_queue.clear ();
        last_published_heights.reset ();
        completed_world.reset ();
      }
      set_stage (LoadingStage::Starting);
    }

    void set_stage (LoadingStage value) {
      stage = value;
      const double elapsed = platform::now () - clock_start;
      const std::lock_guard<std::mutex> lock (mutex);
      if (events.empty () || events.back ().stage != value)
        events.push_back ({ value, elapsed });
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
      if (!last_published_heights || *last_published_heights != *heights) {
        last_published_heights = heights;
        height_queue.push_back (std::move (heights));
      }
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
    std::mutex mutex;
    std::vector<LoadingEvent> events;
    std::deque<std::shared_ptr<const std::vector<float>>> height_queue;
    std::shared_ptr<const std::vector<float>> last_published_heights;
    std::unique_ptr<GeneratedWorld> completed_world;
    std::atomic<int> work_done = 0;
    std::atomic<int> work_total = 1;
    std::atomic<int> geological_years_done = 0;
    std::atomic<int> geological_years_total = 0;
    std::atomic<int> source_done = 0;
    std::atomic<int> source_total = 1;
    float progress_display = 0.0f;
    float progress_target = 0.0f;
    float last_frame_time = 0.0f;
    int evolution_frames = 0;
    double evolution_frame_time = 0.0;
    float evolution_max_frame_time = 0.0f;
    int evolution_frames_over_20ms = 0;
    bool benchmark_reported = false;
    float height_transition_ready_time = 0.0f;
    double clock_start;
    bool capture_done = false;
    bool terrain_visible = false;
    std::atomic<bool> generation_complete = false;
    std::atomic<bool> in_flight = false;
    std::atomic<LoadingStage> stage = LoadingStage::Starting;
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
      std::unique_ptr<terrain::FieldEvaluator> field_evaluator;
      std::unique_ptr<terrain::StreamPowerEvolutionBackend> evolution_backend;
      {
        MOPPE_PROFILE_ZONE ("startup.create_field_evaluator");
        field_evaluator = platform::create_field_evaluator ();
        evolution_backend = platform::create_stream_power_evolution_backend ();
      }
      const char* cache_override = ::getenv ("MOPPE_MAPCACHE");
      const std::string automatic_cache = terrain_cache_path (recipe);
      const char* cache =
        cache_override ? cache_override : automatic_cache.c_str ();
      state.set_stage (LoadingStage::LookingForCache);
      bool cache_loaded = false;
      {
        MOPPE_PROFILE_ZONE ("startup.try_load_terrain_cache");
        cache_loaded = cache && terrain.try_load_cache (cache);
      }
      if (cache_loaded) {
        state.set_stage (LoadingStage::ReadingCache);
        const std::size_t count =
          static_cast<std::size_t> (terrain.width ()) * terrain.height ();
        {
          MOPPE_PROFILE_ZONE ("startup.load_terrain_history");
          load_terrain_history (cache, count, history);
        }
        state.publish_terrain (terrain);
      } else {
        state.set_stage (LoadingStage::BuildingContinents);
        map::TerrainEvaluator evaluator (
          terrain, field_evaluator.get (), evolution_backend.get ());
        history.clear ();
        {
          MOPPE_PROFILE_ZONE ("startup.evaluate_terrain_program");
          evaluator.evaluate (
            recipe.terrain_program (),
            [&state, &history, &terrain] (
              std::size_t, const terrain::TerrainTransform& transform) {
              const std::size_t count =
                static_cast<std::size_t> (terrain.width ()) * terrain.height ();
              history.emplace_back (terrain.raw_heights (),
                                    terrain.raw_heights () + count);
              state.publish_terrain (terrain);
              if (std::holds_alternative<terrain::OrogenyEvolution> (transform))
                state.set_stage (LoadingStage::RefiningTerrain);
            },
            [&state, &terrain] (std::size_t,
                                const terrain::TerrainTransform& transform,
                                int completed_steps,
                                int total_steps) {
              if (!std::holds_alternative<terrain::OrogenyEvolution> (
                    transform))
                return;
              const auto& orogeny =
                std::get<terrain::OrogenyEvolution> (transform);
              state.set_stage (LoadingStage::EvolvingTerrain);
              state.work_done = completed_steps;
              state.work_total = total_steps;
              const float duration =
                julian_years_value (orogeny.evolution.duration);
              const float step =
                julian_years_value (orogeny.evolution.time_step);
              state.geological_years_done = static_cast<int> (
                std::lround (std::min (duration, completed_steps * step)));
              state.geological_years_total =
                static_cast<int> (std::lround (duration));
              state.publish_terrain (terrain);
            },
            [&state] (std::size_t completed_rows, std::size_t total_rows) {
              int observed = state.source_done.load ();
              const int value = static_cast<int> (completed_rows);
              while (
                observed < value &&
                !state.source_done.compare_exchange_weak (observed, value)) {}
              state.source_total = static_cast<int> (total_rows);
            });
        }
        generated_trails = evaluator.release_trail_network ();
        const std::size_t count =
          static_cast<std::size_t> (terrain.width ()) * terrain.height ();
        {
          MOPPE_PROFILE_ZONE ("startup.snapshot_finished_terrain");
          history.emplace_back (terrain.raw_heights (),
                                terrain.raw_heights () + count);
        }
        if (cache) {
          state.set_stage (LoadingStage::SavingTerrain);
          {
            MOPPE_PROFILE_ZONE ("startup.save_terrain_cache");
            terrain.save_cache (cache);
          }
          {
            MOPPE_PROFILE_ZONE ("startup.save_terrain_history");
            save_terrain_history (cache, history);
          }
        }
      }
      if (history.empty ()) {
        const std::size_t count =
          static_cast<std::size_t> (terrain.width ()) * terrain.height ();
        history.emplace_back (terrain.raw_heights (),
                              terrain.raw_heights () + count);
      }
      state.publish_terrain (terrain);
      state.set_stage (LoadingStage::RebuildingSurface);
      {
        MOPPE_PROFILE_ZONE ("startup.recompute_terrain_normals");
        build.rebuild_surface ();
      }

      build.analyze_hydrology ([&state] (GeneratedWorld::HydrologyStage stage) {
        switch (stage) {
        case GeneratedWorld::HydrologyStage::StandingWater:
          state.set_stage (LoadingStage::FindingStandingWater);
          break;
        case GeneratedWorld::HydrologyStage::Lakes:
          state.set_stage (LoadingStage::CataloguingLakes);
          break;
        case GeneratedWorld::HydrologyStage::Drainage:
          state.set_stage (LoadingStage::TracingDrainage);
          break;
        case GeneratedWorld::HydrologyStage::Waterways:
          state.set_stage (LoadingStage::ConnectingWaterways);
          break;
        case GeneratedWorld::HydrologyStage::Channels:
        case GeneratedWorld::HydrologyStage::Rivers:
          state.set_stage (LoadingStage::ExtractingRivers);
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

      state.set_stage (LoadingStage::AssemblingWorld);
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

  void WorldLoading::set_stage (LoadingStage stage) {
    m_state->set_stage (stage);
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

  WorldLoadingFrame WorldLoading::advance (double now,
                                           float transition_seconds) {
    WorldLoadingState& state = *m_state;
    const float elapsed = static_cast<float> (now - state.clock_start);
    const LoadingStage stage = state.stage.load ();
    const LoadingStageText text = loading_stage_text (stage);
    float progress = text.progress;
    float local_progress = -1.0f;
    if (stage == LoadingStage::BuildingContinents) {
      const float source =
        static_cast<float> (state.source_done.load ()) /
        std::max (1.0f, static_cast<float> (state.source_total.load ()));
      local_progress = source;
      progress = 0.06f + 0.13f * source;
    } else if (stage == LoadingStage::EvolvingTerrain) {
      const float erosion =
        static_cast<float> (state.work_done.load ()) /
        std::max (1.0f, static_cast<float> (state.work_total.load ()));
      local_progress = erosion;
      progress = 0.20f + 0.48f * erosion;
    }
    state.progress_target = std::max (state.progress_target, progress);
    const float frame_dt =
      std::clamp (elapsed - state.last_frame_time, 0.0f, 0.1f);
    state.last_frame_time = elapsed;
    if (stage == LoadingStage::EvolvingTerrain &&
        ::getenv ("MOPPE_LOADING_BENCHMARK")) {
      ++state.evolution_frames;
      state.evolution_frame_time += frame_dt;
      state.evolution_max_frame_time =
        std::max (state.evolution_max_frame_time, frame_dt);
      if (frame_dt > 0.020f)
        ++state.evolution_frames_over_20ms;
    } else if (::getenv ("MOPPE_LOADING_BENCHMARK") &&
               state.evolution_frames > 0 && !state.benchmark_reported) {
      const double elapsed_ms = 1000.0 * state.evolution_frame_time;
      const double mean_ms = elapsed_ms / state.evolution_frames;
      std::cerr << "loading benchmark: evolution_elapsed_ms=" << elapsed_ms
                << " evolution_frames=" << state.evolution_frames
                << " evolution_mean_ms=" << mean_ms << " evolution_max_ms="
                << 1000.0 * state.evolution_max_frame_time
                << " evolution_over_20ms=" << state.evolution_frames_over_20ms
                << std::endl;
      state.benchmark_reported = true;
    }
    const float progress_blend = 1.0f - std::exp (-5.0f * frame_dt);
    state.progress_display +=
      (state.progress_target - state.progress_display) * progress_blend;
    if (state.progress_target - state.progress_display < 0.0005f)
      state.progress_display = state.progress_target;

    bool preview_changed = false;
    bool preview_queue_empty = false;
    std::vector<LoadingEvent> events;
    {
      const std::lock_guard<std::mutex> lock (state.mutex);
      if (!state.height_queue.empty () &&
          elapsed >= state.height_transition_ready_time) {
        const auto heights = std::move (state.height_queue.front ());
        state.height_queue.pop_front ();
        std::copy (
          heights->begin (), heights->end (), state.preview_map.raw_heights ());
        preview_changed = true;
      }
      preview_queue_empty = state.height_queue.empty ();
      events = state.events;
    }
    if (preview_changed) {
      const bool transition = state.terrain_visible;
      state.terrain_visible = true;
      state.height_transition_ready_time =
        transition ? elapsed + transition_seconds : elapsed;
    }

    return {
      .stage = stage,
      .elapsed = elapsed,
      .progress = state.progress_display,
      .local_progress = local_progress,
      .preview_changed = preview_changed,
      .preview_sequence_complete =
        preview_queue_empty && elapsed >= state.height_transition_ready_time,
      .terrain_visible = state.terrain_visible,
      .seed = state.seed.load (),
      .work_done = state.work_done.load (),
      .work_total = state.work_total.load (),
      .geological_years_done = state.geological_years_done.load (),
      .geological_years_total = state.geological_years_total.load (),
      .source_done = state.source_done.load (),
      .source_total = state.source_total.load (),
      .events = std::move (events),
    };
  }

  const WorldParams& WorldLoading::preview_world () const noexcept {
    return m_state->preview_world;
  }

  const map::RandomHeightMap& WorldLoading::preview_map () const noexcept {
    return m_state->preview_map;
  }

  bool WorldLoading::claim_loading_capture () noexcept {
    if (m_state->capture_done || m_state->progress_display < 0.20f)
      return false;
    m_state->capture_done = true;
    return true;
  }
}
