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
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

namespace moppe::game {
  namespace {
    using clock = std::chrono::steady_clock;
    using seconds_f64_t = quantity<si::second, double>;
    using microseconds_t = quantity<si::micro<si::second>, double>;

    seconds_f64_t seconds_since (clock::time_point start) {
      return std::chrono::duration<double> (clock::now () - start).count () *
             si::second;
    }

    microseconds_t microseconds_since (clock::time_point start) {
      return std::chrono::duration<double, std::micro> (clock::now () - start)
               .count () *
             si::micro<si::second>;
    }

    // A wall-clock time for the report, in its unit.
    double reported (seconds_f64_t time) {
      return time.numerical_value_in (si::second);
    }
    double reported (microseconds_t time) {
      return time.numerical_value_in (si::micro<si::second>);
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
      std::vector<microseconds_t> micros;

      void write (Report& report) {
        if (micros.empty ())
          return;
        std::vector<microseconds_t> sorted = micros;
        std::sort (sorted.begin (), sorted.end ());
        // The time QUANTILE of the steps took or less.
        const auto at = [&] (quantity<probability[one], double> quantile) {
          return reported (sorted[static_cast<std::size_t> (
            quantile.numerical_value_in (one) * (sorted.size () - 1))]);
        };
        microseconds_t sum = 0.0 * si::micro<si::second>;
        for (const microseconds_t value : sorted)
          sum += value;
        report << std::fixed << std::setprecision (1) << "  steps "
               << sorted.size () << ", mean "
               << reported (sum / static_cast<double> (sorted.size ()))
               << " us, p50 " << at (0.5) << " us, p95 " << at (0.95)
               << " us, p99 " << at (0.99) << " us, max "
               << reported (sorted.back ()) << " us\n";
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
        progress (
          host,
          status.title + (status.detail.empty () ? "" : "\n") + status.detail,
          status.progress ? status.progress->numerical_value_in (one) : -1.0f);
      }
      const seconds_f64_t total = seconds_since (start);
      const LoadingStatus status = loading.status ();
      report << "\n== " << name << " ==\n";
      report << std::fixed << std::setprecision (2);
      for (std::size_t i = 0; i < status.events.size (); ++i) {
        const seconds_f64_t begin = status.events[i].elapsed;
        const seconds_f64_t end =
          i + 1 < status.events.size () ? status.events[i + 1].elapsed : total;
        report << "  " << std::setw (8) << reported (end - begin) << " s  "
               << status.events[i].title << '\n';
      }
      report << "  " << std::setw (8) << reported (total) << " s  total\n";
      report.memory ("after");
      report.flush ();
      return world;
    }

    // The ground at a place on the map, as the session stands on it.
    position_t trail_position (const GeneratedWorld& world,
                               plan_position_t at) {
      return terrain::at_elevation (
        at,
        spatial::sample<terrain::surface_elevation> (
          world.surface (), at_height (at, 0.0f * u::m)));
    }

    // Where the game puts a new rider: a few metres back from the home base
    // along the trail, facing up it.
    std::pair<position_t, direction_t> spawn_of (const GeneratedWorld& world) {
      const terrain::TrailNetwork& trails = world.trails ();
      direction_t heading = unit_direction (Vec3 (0, 0, 1));
      if (trails.alignment.points.size () >= 2) {
        const plan_position_t& a = trails.alignment.points[0];
        const plan_position_t& b = trails.alignment.points[1];
        displacement_t d =
          trail_position (world, b) - trail_position (world, a);
        d.numerical_value_ref_in (si::metre)[1] = 0.0f;
        if (length2 (d.numerical_value_in (si::metre)) > 1e-5f)
          heading = direction_of (d);
      }
      position_t home = position (Vec3 ());
      if (trails.plan.home_base != terrain::no_cell) {
        const std::size_t width = trails.domain.width ();
        const std::size_t cell = trails.plan.home_base.value;
        home = trail_position (
          world,
          plan_position (
            static_cast<float> (cell % width) * trails.domain.spacing_x (),
            static_cast<float> (cell / width) * trails.domain.spacing_z ()));
      }
      position_t spawn =
        trail_position (world,
                        plan (home - quantity_cast<isq::position_vector> (
                                       along (heading, 8.0f * u::m))));
      position_value (spawn)[1] += 1.2f;
      return { spawn, heading };
    }

    struct Course {
      std::uint64_t steps = 0;
      meters_f64_t path = 0.0 * u::m;
      position_t start, end;
    };

    void
    write_course (Report& report, const Course& course, seconds_f64_t wall) {
      const seconds_f64_t simulated =
        static_cast<double> (course.steps) *
        FIXED_SIMULATION_STEP.numerical_value_in (si::second) * si::second;
      const Vec3& from = position_value (course.start);
      const Vec3& to = position_value (course.end);
      report << std::fixed << std::setprecision (2) << "  simulated "
             << reported (simulated) << " s in " << std::setprecision (3)
             << reported (wall) << " s wall (" << std::setprecision (0)
             << reported (simulated) / std::max (reported (wall), 1e-9)
             << "x real time)\n"
             << std::setprecision (1) << "  travelled "
             << course.path.numerical_value_in (u::m) << " m, from (" << from[0]
             << ", " << from[1] << ", " << from[2] << ") to (" << to[0] << ", "
             << to[1] << ", " << to[2] << ")\n";
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
      const terrain::SurfaceElevation highest =
        terrain::measure_elevation_range (world.surface ()).maximum;
      const BoulderPlan boulder_plan = plan_boulders (
        world.surface (),
        world.readings (),
        world.water_surface (),
        std::get<terrain::RiverNetwork> (world.hydrology ()),
        world.recipe ().seed ().value ^ 0x6b0d1e55U,
        terrain::surface_elevation_point (sea),
        std::max (meters_t (highest - terrain::surface_elevation_point (sea)),
                  1.0f * u::m));
      BoulderLandscape boulders;
      boulders.rebuild (renderer, boulder_plan);
      std::vector<mov::Trunk> obstacles = forest.trunks ();
      obstacles.insert (obstacles.end (),
                        boulders.colliders ().begin (),
                        boulders.colliders ().end ());
      const spatial_extent_t period = world.forest ().period;
      mov::TrunkField trunks;
      trunks.set_trunks (std::move (obstacles),
                         extent_value (period)[0],
                         extent_value (period)[2]);
      const auto [spawn, heading] = spawn_of (world);
      report << "\n== simulation setup ==\n"
             << "  " << forest.tree_count () << " trees, "
             << boulders.boulder_count () << " boulders, "
             << trunks.trunk_count () << " colliders, in " << std::fixed
             << std::setprecision (2) << reported (seconds_since (setup_start))
             << " s\n";
      report.flush ();

      const seconds_t dt =
        static_cast<float> (
          FIXED_SIMULATION_STEP.numerical_value_in (si::second)) *
        si::second;
      // Where the session's subject stands, and how far it went.
      const auto subject_at = [] (const GameSession& session) {
        return session.subject_position ();
      };
      const auto travelled = [] (position_t from, position_t to) {
        return std::sqrt (
                 length2 (position_value (to) - position_value (from))) *
               u::m;
      };

      // On foot, as the game starts: walk, run, and jump, turning gently.
      {
        GameSession session (world.params (), world.surface ());
        session.bike ().set_water_level (sea);
        session.bike ().reset (position_value (spawn));
        session.bike ().set_heading (direction_value (heading));
        session.start_on_foot ();
        StepTimes times;
        Course course { .start = subject_at (session) };
        position_t last = course.start;
        const std::uint64_t steps =
          static_cast<std::uint64_t> (static_cast<double> (options.walk / dt));
        const clock::time_point start = clock::now ();
        for (std::uint64_t step = 0; step < steps; ++step) {
          const seconds_t t = static_cast<float> (step) * dt;
          const float clock_s = seconds_value (t);
          InputFrame input;
          input.drive = t > 0.5f * si::second ? 1.0f : 0.0f;
          input.run = t > 3.0f * si::second;
          input.boost =
            std::fmod (clock_s, 2.5f) < 0.1f && t > 4.0f * si::second ? 1.0f
                                                                      : 0.0f;
          // A gentle swing of the head, in radians this tick.
          const radians_per_second_t yaw_rate =
            0.25f * std::sin (clock_s * 0.35f) * u::rad / u::s;
          input.look_yaw = yaw_rate * dt;
          const clock::time_point before = clock::now ();
          trunks.focus (position_value (session.subject_position ()));
          advance_game_session (world.params (),
                                world.surface (),
                                session,
                                input,
                                dt,
                                &trunks,
                                {},
                                &world.water_surface ());
          times.micros.push_back (microseconds_since (before));
          const position_t now = subject_at (session);
          course.path += travelled (last, now);
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
        GameSession session (world.params (), world.surface ());
        session.bike ().set_water_level (sea);
        session.bike ().reset (position_value (spawn));
        session.bike ().set_heading (direction_value (heading));
        StepTimes times;
        Course course { .start = subject_at (session) };
        position_t last = course.start;
        const std::uint64_t steps =
          static_cast<std::uint64_t> (static_cast<double> (options.ride / dt));
        const clock::time_point start = clock::now ();
        for (std::uint64_t step = 0; step < steps; ++step) {
          const float clock_s = seconds_value (static_cast<float> (step) * dt);
          InputFrame input {
            .turn = 0.35f * std::sin (clock_s * 0.25f),
            .drive = 1.0f,
            .boost = std::fmod (clock_s, 11.0f) < 1.35f ? 1.0f : 0.0f,
          };
          const clock::time_point before = clock::now ();
          const position_t at = subject_at (session);
          const direction_t ahead_dir = session.subject_heading ();
          const direction_t right = unit_direction (
            normalized (cross (direction_value (ahead_dir), Vec3 (0, 1, 0))));
          trunks.focus (position_value (at));
          // A bike's height of trunk, its reach to either side.
          const meters_t height = 1.5f * u::m;
          const meters_t reach = 1.8f * u::m;
          for (const meters_t ahead : { 4.0f * u::m, 9.0f * u::m }) {
            const Vec3 probe = position_value (at) +
                               displacement_value (along (ahead_dir, ahead));
            const mov::TrunkContact contact =
              trunks.collide (probe,
                              probe + Vec3 (0, meters_value (height), 0),
                              meters_value (reach));
            if (contact.hit) {
              input.turn = dot (contact.normal, direction_value (right)) >= 0.0f
                             ? 1.0f
                             : -1.0f;
              break;
            }
          }
          advance_game_session (world.params (),
                                world.surface (),
                                session,
                                input,
                                dt,
                                &trunks,
                                {},
                                &world.water_surface ());
          times.micros.push_back (microseconds_since (before));
          const position_t now = subject_at (session);
          course.path += travelled (last, now);
          last = now;
          if (step % 120 == 0)
            progress (host, "Riding", static_cast<float> (step) / steps);
        }
        course.steps = steps;
        course.end = last;
        report << "\n== ride (Box3D rigid bike, demo autopilot, 120 Hz) ==\n";
        write_course (report, course, seconds_since (start));
        report << "  speed at the end: "
               << session.subject_speed_kmh ().numerical_value_in (u::km / u::h)
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
      const seconds_t dt = 1.0f / 60.0f * si::second;
      // When the forest was first drawn, or not yet.
      std::optional<seconds_f64_t> ready_at;
      StepTimes frames;
      std::uint64_t loading_frames = 0;
      const std::uint64_t frame_count =
        static_cast<std::uint64_t> (static_cast<double> (options.game / dt));
      while (frames.micros.size () < frame_count) {
        pump (host);
        const clock::time_point before = clock::now ();
        game->tick (dt);
        game->render (renderer);
        const microseconds_t micros = microseconds_since (before);
        if (!ready_at && renderer.forest_draws > 0)
          ready_at = seconds_since (start);
        if (!ready_at) {
          ++loading_frames;
          progress (host, "Starting the game", 0.0f);
          if (seconds_since (start) > 600.0 * si::second)
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
             << reported (*ready_at) << " s (" << loading_frames
             << " loading frames)\n"
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
           << std::setprecision (1) << reported (seconds_since (start))
           << " s\n";
    report.memory ("at the end");
    report.flush ();
    progress (host, ok ? "Done" : "Failed", 1.0f);
    return ok;
  }
}
