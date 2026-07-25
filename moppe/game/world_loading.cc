// Builds one world on a background thread while the loading screen shows
// honest progress.  The two threads share a narrow channel: the worker
// reports what it is doing, the main thread reads the status once per
// frame.  The build itself is the linear story in build_world ();
// everything before it is the channel it narrates into, and everything
// after it is thread plumbing and the main-thread facade.

#include <moppe/game/world_loading.hh>

#include <moppe/platform/platform.hh>
#include <moppe/profile.hh>

#include <moppe/map/surface.hh>
#include <moppe/terrain/river.hh>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace moppe::game {
  namespace {
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
           << bits (meters_value (recipe.water_datum ())) << ".arrows";
      return platform::cache_path (name.str ());
    }
  }

  // -- the channel between the worker and the loading screen -------------

  class WorldLoadingState {
  public:
    explicit WorldLoadingState (const terrain::WorldRecipe& initial_recipe)
        : seed (initial_recipe.seed ().value), clock_start (platform::now ()) {}

    // Everything the mutex guards, resettable in one assignment.
    struct Shared {
      std::string title;
      std::string detail;
      float progress = -1.0f;
      std::vector<LoadingEvent> events;
      std::unique_ptr<GeneratedWorld> completed_world;
    };

    void reset (const terrain::WorldRecipe& recipe) {
      seed = recipe.seed ().value;
      clock_start = platform::now ();
      capture_done = false;
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

    void publish_completed (std::unique_ptr<GeneratedWorld> world) {
      const std::lock_guard<std::mutex> lock (mutex);
      if (shared.completed_world)
        throw std::logic_error (
          "a completed world is already awaiting activation");
      shared.completed_world = std::move (world);
    }

    // Written at reset, read from both threads.
    std::atomic<std::uint32_t> seed;
    double clock_start;
    std::atomic<bool> generation_complete = false;
    std::atomic<bool> in_flight = false;

    // Main-thread only.
    bool capture_done = false;

    std::mutex mutex;
    Shared shared;
  };

  // -- the build ---------------------------------------------------------

  namespace {
    // Geology reports row completion from several worker threads at once;
    // only the furthest row should reach the status line.
    bool advance_watermark (std::atomic<int>& watermark, int row) {
      int observed = watermark.load ();
      while (observed < row)
        if (watermark.compare_exchange_weak (observed, row))
          return true;
      return false;
    }

    // Build the surface in its one real order: geology, erosion, then trails.
    std::optional<terrain::TrailNetwork>
    evolve_terrain (WorldLoadingState& state,
                    const terrain::WorldRecipe& recipe,
                    map::SurfaceGeometry& terrain) {
      std::unique_ptr<terrain::StreamPowerEvolutionBackend> evolution =
        platform::create_stream_power_evolution_backend ();

      const auto report_geological_time =
        [&state, &recipe] (
          int completed_steps, int total_steps, std::span<const float>) {
          const float duration =
            julian_years_value (recipe.evolution ().duration);
          const float step = julian_years_value (recipe.evolution ().time_step);
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
        };

      std::atomic<int> row_watermark { 0 };
      const auto report_field_rows = [&] (std::size_t completed_rows,
                                          std::size_t total_rows) {
        if (!advance_watermark (row_watermark,
                                static_cast<int> (completed_rows)))
          return;
        std::ostringstream detail;
        detail << "Geology row " << completed_rows << " of " << total_rows;
        state.report ("Drawing the continents",
                      detail.str (),
                      static_cast<float> (completed_rows) /
                        std::max<std::size_t> (1, total_rows));
      };

      std::vector<meters_per_julian_year_t> uplift = map::initialize_terrain (
        terrain, recipe.seed (), recipe.water_datum (), report_field_rows);
      map::evolve_terrain (terrain,
                           uplift,
                           recipe.evolution (),
                           evolution.get (),
                           report_geological_time);
      state.report ("Refining the terrain",
                    "Shaping coasts, channels, and the overland route");
      return map::form_terrain_trails (terrain, recipe.trail_formation ());
    }

    std::pair<const char*, const char*>
    hydrology_report (HydrologyStage stage) {
      using Stage = HydrologyStage;
      switch (stage) {
      case Stage::StandingWater:
        return { "Filling seas and lakes",
                 "Finding the connected water surface" };
      case Stage::Lakes:
        return { "Cataloguing lakes",
                 "Measuring every separate body of water" };
      case Stage::Drainage:
        return { "Tracing the drainage", "Following every wet cell downhill" };
      case Stage::Channels:
      case Stage::Rivers:
        return { "Extracting the rivers",
                 "Selecting the channels visible in the world" };
      }
      return { "Analyzing the water", "" };
    }

    void log_standing_water (const Hydrology& hydrology) {
      const terrain::LakeCensus& census =
        std::get<terrain::LakeCensus> (hydrology);
      std::size_t wet = 0;
      for (const terrain::WaterBody& body : census.bodies)
        wet += terrain::count_value (body.cells);
      std::cerr << "standing water: " << census.bodies.size () << " bodies, "
                << wet << " wet cells\n";
    }

    struct GenerationJob {
      std::shared_ptr<WorldLoadingState> state;
      WorldParams params;
      terrain::WorldRecipe recipe;
    };

    // The whole build, in order. The surface comes from the cache or is
    // evolved fresh; then water analysis and final assembly follow.
    void build_world (GenerationJob& job) {
      MOPPE_PROFILE_ZONE ("WorldLoading::build_world");
      WorldLoadingState& state = *job.state;
      const terrain::WorldRecipe& recipe = job.recipe;
      map::SurfaceGeometry surface =
        map::SurfaceGeometry (terrain::TerrainDomain (
          recipe.resolution (), recipe.resolution (), recipe.extent ()));
      std::optional<terrain::TrailNetwork> evolved_trails;

      const char* cache_override = ::getenv ("MOPPE_MAPCACHE");
      const std::string cache =
        cache_override ? cache_override : terrain_cache_path (recipe);

      state.report ("Looking for saved terrain",
                    "Checking this build, profile, and seed");
      if (map::try_load_cache (surface, cache)) {
        state.report ("Reading saved terrain",
                      "Reusing the finished heightfield");
      } else {
        state.report ("Drawing the continents",
                      "Materializing the geological field");
        evolved_trails = evolve_terrain (state, recipe, surface);
        state.report ("Saving the terrain",
                      "Keeping this expensive result for the next launch");
        map::save_cache (surface, cache);
      }

      state.report ("Calculating slopes",
                    "Rebuilding normals and broad surface readings");
      map::rebuild_geometry (surface);

      Hydrology hydrology =
        analyze_hydrology (surface, recipe, [&state] (HydrologyStage stage) {
          const auto [title, detail] = hydrology_report (stage);
          state.report (title, detail);
        });
      log_standing_water (hydrology);

      state.report ("Assembling the world",
                    "Painting water, moisture, materials, and the opening "
                    "route");
      terrain::TrailNetwork trails =
        evolved_trails
          ? std::move (*evolved_trails)
          : terrain::analyze_trail_network (surface, recipe.trail_formation ());
      auto [water, readings] =
        analyze_surface (surface, recipe, hydrology, trails.use);

      state.publish_completed (
        std::make_unique<GeneratedWorld> (job.params,
                                          recipe,
                                          std::move (surface),
                                          std::move (hydrology),
                                          std::move (water),
                                          std::move (trails),
                                          std::move (readings)));
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

  WorldLoading::WorldLoading (const terrain::WorldRecipe& recipe)
      : m_state (std::make_shared<WorldLoadingState> (recipe)) {}

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
    m_state->reset (recipe);
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
      .seed = state.seed.load (),
      .events = state.shared.events,
    };
  }

  bool WorldLoading::claim_loading_capture (bool last_chance) noexcept {
    WorldLoadingState& state = *m_state;
    if (state.capture_done)
      return false;
    if (!last_chance && state.elapsed () < 1.0f)
      return false;
    state.capture_done = true;
    return true;
  }
}
