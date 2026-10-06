#include <moppe/game/core_probe.hh>

#include <moppe/environment.hh>
#include <moppe/game/boulders.hh>
#include <moppe/game/forest.hh>
#include <moppe/game/game_session.hh>
#include <moppe/game/launch_options.hh>
#include <moppe/game/moppe_game.hh>
#include <moppe/game/simulation_clock.hh>
#include <moppe/game/world_loading.hh>
#include <moppe/mov/trunk_field.hh>
#include <moppe/platform/platform.hh>
#include <moppe/terrain/readings.hh>

#include <tests/recording_renderer.hh>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

namespace moppe::game {
  namespace {
    using clock = std::chrono::steady_clock;

    double seconds_since (clock::time_point start) {
      return std::chrono::duration<double> (clock::now () - start).count ();
    }

    double mebibytes (std::uint64_t bytes) {
      return static_cast<double> (bytes) / (1024.0 * 1024.0);
    }

    // The report grows phase by phase and is rewritten whole each time, so
    // a crash leaves everything measured before it.
    class Report {
    public:
      explicit Report (const CoreProbeHost& host) : m_host (host) {}

      template <typename T>
      Report& operator<< (const T& value) {
        m_text << value;
        return *this;
      }

      void memory (const char* when) {
        if (!m_host.memory)
          return;
        const CoreProbeMemory reading = m_host.memory ();
        m_text << std::fixed << std::setprecision (1) << "memory " << when
               << ": " << mebibytes (reading.current) << " MiB now";
        if (reading.peak)
          m_text << ", " << mebibytes (reading.peak) << " MiB peak";
        if (reading.limit)
          m_text << ", " << mebibytes (reading.limit) << " MiB limit";
        m_text << '\n';
      }

      void flush () {
        if (m_host.write_report)
          m_host.write_report (m_text.str ());
      }

    private:
      const CoreProbeHost& m_host;
      std::ostringstream m_text;
    };

    void
    progress (const CoreProbeHost& host, const std::string& what, float t) {
      if (host.progress)
        host.progress (what, t);
    }

    void pump (const CoreProbeHost& host) {
      if (host.pump)
        host.pump ();
      else
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }

    struct StepTimes {
      std::vector<double> micros;

      void write (Report& report) {
        if (micros.empty ())
          return;
        std::vector<double> sorted = micros;
        std::sort (sorted.begin (), sorted.end ());
        const auto at = [&] (double q) {
          return sorted[static_cast<std::size_t> (q * (sorted.size () - 1))];
        };
        double sum = 0.0;
        for (double value : sorted)
          sum += value;
        report << std::fixed << std::setprecision (1) << "  steps "
               << sorted.size () << ", mean " << sum / sorted.size ()
               << " us, p50 " << at (0.5) << " us, p95 " << at (0.95)
               << " us, p99 " << at (0.99) << " us, max " << sorted.back ()
               << " us\n";
      }
    };

    // Builds one world through the game's own loader and narrates its
    // stages into the report.
    std::unique_ptr<GeneratedWorld>
    load_world (const CoreProbeHost& host,
                Report& report,
                const char* name,
                const WorldParams& params,
                const terrain::WorldRecipe& recipe,
                WorldCacheConfig cache) {
      progress (host, name, 0.0f);
      const clock::time_point start = clock::now ();
      WorldLoading loading (recipe, cache);
      loading.start (params, recipe);
      std::unique_ptr<GeneratedWorld> world;
      while (!(world = loading.take_completed_world ())) {
        pump (host);
        const LoadingStatus status = loading.status ();
        progress (host,
                  status.title + (status.detail.empty () ? "" : "\n") +
                    status.detail,
                  status.progress);
      }
      const double total = seconds_since (start);
      const LoadingStatus status = loading.status ();
      report << "\n== " << name << " ==\n";
      report << std::fixed << std::setprecision (2);
      for (std::size_t i = 0; i < status.events.size (); ++i) {
        const double begin = status.events[i].elapsed;
        const double end =
          i + 1 < status.events.size () ? status.events[i + 1].elapsed : total;
        report << "  " << std::setw (8) << end - begin << " s  "
               << status.events[i].title << '\n';
      }
      report << "  " << std::setw (8) << total << " s  total\n";
      report.memory ("after");
      report.flush ();
      return world;
    }

    Vec3 trail_position (const GeneratedWorld& world, float x, float z) {
      return Vec3 (x,
                   terrain::surface_elevation_value (
                     spatial::sample<terrain::surface_elevation> (
                       world.surface (), moppe::position (Vec3 (x, 0.0f, z)))),
                   z);
    }

    // Where the game puts a new rider: a few metres back from the home base
    // along the trail, facing up it.
    std::pair<Vec3, Vec3> spawn_of (const GeneratedWorld& world) {
      const terrain::TrailNetwork& trails = world.trails ();
      Vec3 heading (0, 0, 1);
      if (trails.alignment.points.size () >= 2) {
        const auto& a = trails.alignment.points[0];
        const auto& b = trails.alignment.points[1];
        Vec3 d = trail_position (world, b.x_m, b.z_m) -
                 trail_position (world, a.x_m, a.z_m);
        d[1] = 0.0f;
        if (length2 (d) > 1e-5f)
          heading = normalized (d);
      }
      Vec3 home;
      if (trails.plan.home_base != terrain::no_cell) {
        const std::size_t width = trails.domain.width ();
        const std::size_t cell = trails.plan.home_base.value;
        home = trail_position (
          world,
          (cell % width) * trails.domain.spacing_x ().numerical_value_in (u::m),
          (cell / width) *
            trails.domain.spacing_z ().numerical_value_in (u::m));
      }
      Vec3 spawn = home - heading * 8.0f;
      spawn = trail_position (world, spawn[0], spawn[2]);
      spawn[1] += 1.2f;
      return { spawn, heading };
    }

    struct Course {
      std::uint64_t steps = 0;
      double path = 0.0;
      Vec3 start, end;
    };

    void write_course (Report& report, const Course& course, double wall) {
      const double simulated = course.steps * FIXED_SIMULATION_STEP_SECONDS;
      report << std::fixed << std::setprecision (2) << "  simulated "
             << simulated << " s in " << std::setprecision (3) << wall
             << " s wall (" << std::setprecision (0)
             << simulated / std::max (wall, 1e-9) << "x real time)\n"
             << std::setprecision (1) << "  travelled " << course.path
             << " m, from (" << course.start[0] << ", " << course.start[1]
             << ", " << course.start[2] << ") to (" << course.end[0] << ", "
             << course.end[1] << ", " << course.end[2] << ")\n";
    }

    void simulate (const CoreProbeHost& host,
                   Report& report,
                   const GeneratedWorld& world,
                   const CoreProbeOptions& options) {
      const clock::time_point setup_start = clock::now ();
      test::RecordingRenderer renderer;
      ForestLandscape forest;
      forest.rebuild (renderer, world.forest ());
      const meters_t sea = world.params ().water_level;
      const float highest =
        terrain::measure_height_range (world.surface ()).maximum;
      const BoulderPlan boulder_plan = plan_boulders (
        world.surface (),
        world.readings (),
        world.water_surface (),
        world.recipe ().seed ().value ^ 0x6b0d1e55U,
        sea,
        std::max (highest - sea.numerical_value_in (u::m), 1.0f) * u::m);
      BoulderLandscape boulders;
      boulders.rebuild (renderer, boulder_plan);
      std::vector<mov::Trunk> obstacles = forest.trunks ();
      obstacles.insert (obstacles.end (),
                        boulders.colliders ().begin (),
                        boulders.colliders ().end ());
      const Vec3 period = extent_value (world.forest ().period);
      mov::TrunkField trunks;
      trunks.set_trunks (std::move (obstacles), period[0], period[2]);
      const auto [spawn, heading] = spawn_of (world);
      report << "\n== simulation setup ==\n"
             << "  " << forest.tree_count () << " trees, "
             << boulders.boulder_count () << " boulders, "
             << trunks.trunk_count () << " colliders, in " << std::fixed
             << std::setprecision (2) << seconds_since (setup_start) << " s\n";
      report.flush ();

      const float dt = static_cast<float> (FIXED_SIMULATION_STEP_SECONDS);

      // On foot, as the game starts: walk, run, and jump, turning gently.
      {
        GameSession session (
          world.params (), world.surface (), mov::BikePhysics::rigid);
        session.bike ().set_water_level (sea);
        session.bike ().reset (spawn);
        session.bike ().set_heading (heading);
        session.start_on_foot ();
        StepTimes times;
        Course course { .start = session.subject_position () };
        Vec3 last = course.start;
        const std::uint64_t steps =
          static_cast<std::uint64_t> (options.walk_seconds / dt);
        const clock::time_point start = clock::now ();
        for (std::uint64_t step = 0; step < steps; ++step) {
          const float t = step * dt;
          InputFrame input;
          input.drive = t > 0.5f ? 1.0f : 0.0f;
          input.run = t > 3.0f;
          input.boost = std::fmod (t, 2.5f) < 0.1f && t > 4.0f ? 1.0f : 0.0f;
          input.look_yaw = 0.25f * std::sin (t * 0.35f) * dt;
          const clock::time_point before = clock::now ();
          trunks.focus (session.subject_position ());
          advance_game_session (world.params (),
                                world.surface (),
                                session,
                                input,
                                seconds (dt),
                                &trunks);
          times.micros.push_back (
            std::chrono::duration<double, std::micro> (clock::now () - before)
              .count ());
          const Vec3 now = session.subject_position ();
          course.path += std::sqrt (length2 (now - last));
          last = now;
          if (step % 120 == 0)
            progress (host, "Walking", static_cast<float> (step) / steps);
        }
        course.steps = steps;
        course.end = last;
        report << "\n== walk (Box3D character, 120 Hz) ==\n";
        write_course (report, course, seconds_since (start));
        times.write (report);
        report.flush ();
      }

      // Riding the rigid bike with the demo autopilot, which weaves away
      // from the trunks ahead and boosts into periodic leaps.
      {
        GameSession session (
          world.params (), world.surface (), mov::BikePhysics::rigid);
        session.bike ().set_water_level (sea);
        session.bike ().reset (spawn);
        session.bike ().set_heading (heading);
        StepTimes times;
        Course course { .start = session.subject_position () };
        Vec3 last = course.start;
        const std::uint64_t steps =
          static_cast<std::uint64_t> (options.ride_seconds / dt);
        const clock::time_point start = clock::now ();
        for (std::uint64_t step = 0; step < steps; ++step) {
          const float t = step * dt;
          InputFrame input {
            .turn = 0.35f * std::sin (t * 0.25f),
            .drive = 1.0f,
            .boost = std::fmod (t, 11.0f) < 1.35f ? 1.0f : 0.0f,
          };
          const clock::time_point before = clock::now ();
          const Vec3 at = session.subject_position ();
          const Vec3 ahead_dir = session.subject_heading ();
          const Vec3 right = normalized (cross (ahead_dir, Vec3 (0, 1, 0)));
          trunks.focus (at);
          for (const float ahead : { 4.0f, 9.0f }) {
            const Vec3 probe = at + ahead_dir * ahead;
            const mov::TrunkContact contact =
              trunks.collide (probe, probe + Vec3 (0, 1.5f, 0), 1.8f);
            if (contact.hit) {
              input.turn = dot (contact.normal, right) >= 0.0f ? 1.0f : -1.0f;
              break;
            }
          }
          advance_game_session (world.params (),
                                world.surface (),
                                session,
                                input,
                                seconds (dt),
                                &trunks);
          times.micros.push_back (
            std::chrono::duration<double, std::micro> (clock::now () - before)
              .count ());
          const Vec3 now = session.subject_position ();
          course.path += std::sqrt (length2 (now - last));
          last = now;
          if (step % 120 == 0)
            progress (host, "Riding", static_cast<float> (step) / steps);
        }
        course.steps = steps;
        course.end = last;
        report << "\n== ride (Box3D rigid bike, demo autopilot, 120 Hz) ==\n";
        write_course (report, course, seconds_since (start));
        report << "  speed at the end: " << session.subject_speed_kmh ()
               << " km/h\n";
        times.write (report);
        report.memory ("after simulation");
        report.flush ();
      }
    }

    // The whole application shell, as a platform's run loop would drive it,
    // with a renderer that only records: loading, world activation, the
    // presentations' uploads, and the demo ride with its HUD and draw lists.
    void run_game (const CoreProbeHost& host,
                   Report& report,
                   const LaunchOptions& launch,
                   const terrain::WorldRecipe& recipe,
                   const CoreProbeOptions& options) {
      moppe::set_environment ("MOPPE_DEMO", "1");
      test::RecordingRenderer renderer;
      std::unique_ptr<platform::Game> game = make_moppe_game (launch, recipe);
      const clock::time_point start = clock::now ();
      game->setup (renderer, 1920, 1080);
      const float dt = 1.0f / 60.0f;
      double ready_at = -1.0;
      StepTimes frames;
      std::uint64_t loading_frames = 0;
      const std::uint64_t frame_count =
        static_cast<std::uint64_t> (options.game_seconds / dt);
      while (frames.micros.size () < frame_count) {
        pump (host);
        const clock::time_point before = clock::now ();
        game->tick (dt);
        game->render (renderer);
        const double micros =
          std::chrono::duration<double, std::micro> (clock::now () - before)
            .count ();
        if (ready_at < 0.0 && renderer.forest_draws > 0)
          ready_at = seconds_since (start);
        if (ready_at < 0.0) {
          ++loading_frames;
          progress (host, "Starting the game", 0.0f);
          if (seconds_since (start) > 600.0)
            throw std::runtime_error ("the game did not become ready");
          std::this_thread::sleep_for (std::chrono::milliseconds (10));
        } else {
          frames.micros.push_back (micros);
          if (frames.micros.size () % 60 == 0)
            progress (host,
                      "Playing the demo ride",
                      static_cast<float> (frames.micros.size ()) / frame_count);
        }
      }
      moppe::set_environment ("MOPPE_DEMO", nullptr);
      report << "\n== application shell (game.cc, recording renderer) ==\n"
             << std::fixed << std::setprecision (2) << "  ready after "
             << ready_at << " s (" << loading_frames << " loading frames)\n"
             << "  frames: tick(1/60 s) + render, CPU only\n";
      frames.write (report);
      report << "  meshes baked " << renderer.baked_vertex_counts.size ()
             << ", forest instances " << renderer.forest_instances.size ()
             << ", boulder instances " << renderer.boulder_instances.size ()
             << '\n';
      report.memory ("after the game");
      report.flush ();
      (void)recipe;
    }
  }

  bool run_core_probe (const CoreProbeHost& host,
                       const CoreProbeOptions& options) {
    Report report (host);
    const clock::time_point start = clock::now ();
    report << "moppe core probe on " << host.platform_name << '\n'
           << "build " << platform::executable_build_id () << ", "
           << std::thread::hardware_concurrency () << " hardware threads\n"
           << "world: seed " << options.seed << ", " << options.resolution
           << "x" << options.resolution << " cells, Play profile\n";
    report.memory ("at start");
    report.flush ();
    bool ok = true;
    try {
      LaunchOptions launch;
      launch.seed = options.seed;
      launch.world.resolution = options.resolution;
      const terrain::WorldRecipe recipe = make_launch_recipe (launch);
      const WorldParams params = bind_world_params (launch.world, recipe);

      // The probe keeps its own caches, so it neither reuses nor disturbs
      // the player's.
      const std::string terrain_cache =
        platform::cache_path ("core-probe-terrain.arrows");
      moppe::set_environment ("MOPPE_MAPCACHE", terrain_cache.c_str ());
      const WorldCacheConfig probe_cache { WorldCacheMode::Reuse,
                                           "core-probe" };
      if (options.clear_caches) {
        std::error_code ignored;
        std::filesystem::remove_all (terrain_cache, ignored);
      }

      std::unique_ptr<GeneratedWorld> world =
        load_world (host,
                    report,
                    "world generation",
                    params,
                    recipe,
                    { options.clear_caches ? WorldCacheMode::Refresh
                                           : WorldCacheMode::Reuse,
                      probe_cache.key });
      world.reset ();
      report.memory ("after releasing the world");

      world = load_world (host,
                          report,
                          "world from the finished-world cache",
                          params,
                          recipe,
                          probe_cache);
      simulate (host, report, *world, options);
      world.reset ();

      launch.world_cache = probe_cache;
      run_game (host, report, launch, recipe, options);
      moppe::set_environment ("MOPPE_MAPCACHE", nullptr);
    } catch (const std::exception& error) {
      report << "\nERROR: " << error.what () << '\n';
      ok = false;
    }
    report << "\n"
           << (ok ? "PASS" : "FAIL") << " after " << std::fixed
           << std::setprecision (1) << seconds_since (start) << " s\n";
    report.memory ("at the end");
    report.flush ();
    progress (host, ok ? "Done" : "Failed", 1.0f);
    return ok;
  }
}
