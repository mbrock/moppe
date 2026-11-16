// The views that are not the following camera's: the opening's shots and
// the capture cameras (side, animal, ride, and the boat's film cameras).

#include <moppe/game/game.hh>

namespace moppe {
  namespace game {
    void MoppeGame::rest_camera () {
      const Camp& camp = logic ().m_camp;
      const float load = hammock_load (camp, logic ().m_total_time);
      if (!camp.hung || load <= 0.001f || logic ().m_mode != M_FOOT)
        return;
      ChaseCamera& camera = session ().camera ();
      const Vec3 from = camera.position ();
      const Vec3 looking = camera.forward ();
      Vec3 eye, toward;
      if (logic ().m_cam_mode == CAM_HELMET) {
        eye = resting_eye (camp.hammock, camp.swing);
        toward = resting_gaze (camp.hammock, camp.gaze_yaw, camp.gaze_pitch);
      } else {
        const Vec3 subject = hammock_middle (camp.hammock) - Vec3 (0, 0.45f, 0);
        const float around = camp.gaze_yaw + 0.45f;
        eye = subject +
              (hammock_across (camp.hammock) * std::cos (around) +
               hammock_along (camp.hammock) * std::sin (around)) *
                3.7f +
              Vec3 (0, 0.75f + 1.2f * (1.1f - camp.gaze_pitch), 0);
        eye[1] =
          std::max (static_cast<float> (eye[1]), ground_height (eye) + 0.4f);
        toward = normalized (subject - eye);
      }
      const Vec3 at = from + (eye - from) * load;
      const Vec3 gaze = normalized (looking + (toward - looking) * load);
      camera.place (at, at + gaze * 10.0f);
    }

    void MoppeGame::plan_opening_journey () {
      MOPPE_PROFILE_ZONE ("startup.plan_opening");
      m_opening_shots.clear ();
      const char* choice = moppe::environment ("MOPPE_OPENING");
      if (choice && std::string_view (choice) == "none")
        return;
      const bool flight = choice && std::string_view (choice) == "flight";
      if (choice && std::string_view (choice) == "river") {
        const char* seconds = moppe::environment ("MOPPE_FLYOVER_SECONDS");
        std::vector<Vec3> trees;
        trees.reserve (generated_world ().forest ().sites.size ());
        for (const ForestSite& site : generated_world ().forest ().sites) {
          Vec3 tree = position_value (site.position);
          tree[1] = tree_height (site).numerical_value_in (u::m);
          trees.push_back (tree);
        }
        m_cinematic_plan = plan_river_flight (
          surface (),
          trees,
          generated_world ().water_surface (),
          lake_census (),
          drainage (),
          rivers (),
          (seconds ? std::strtof (seconds, nullptr) : 0.0f) * u::s);
        return;
      }
      if (!flight) {
        plan_opening_reel (choice);
        return;
      }
      m_cinematic_plan = plan_cinematic_flight (surface (),
                                                standing_water (),
                                                lake_census (),
                                                drainage (),
                                                rivers (),
                                                m_spawn_position,
                                                &trail_network ());
      if (m_cinematic_plan.empty ())
        return;
      std::cerr << "cinematic flight: " << m_cinematic_plan.waypoints.size ()
                << " gates through ";
      for (std::size_t i = 0; i < m_cinematic_plan.landmarks.size (); ++i) {
        if (i)
          std::cerr << ", ";
        std::cerr << cinematic_landmark_name (
          m_cinematic_plan.landmarks[i].kind);
      }
      std::cerr << '\n';
    }

    std::optional<OpeningReel>
    MoppeGame::authored_opening (const char* choice) const {
      if (choice && std::string_view (choice) == "generated")
        return std::nullopt;
      const std::string path = choice
                                 ? std::string (choice)
                                 : platform::asset_path ("data/opening.txt");
      std::ifstream input (path);
      if (!input) {
        std::cerr << "moppe: opening: no shot list at " << path << '\n';
        return std::nullopt;
      }
      std::string error;
      std::optional<OpeningReel> reel = parse_opening_reel (input, error);
      if (!reel) {
        std::cerr << "moppe: opening: " << path << ": " << error << '\n';
        return std::nullopt;
      }
      if (!reel->matches (
            static_cast<int> (recipe ().seed ().value),
            recipe ().resolution (),
            terrain::profile_id (recipe ().generation_profile ())) ||
          extent_value (recipe ().extent ())[0] !=
            (reel->km ? *reel->km * 1000.0f
                      : extent_value (WorldParams ().map_size)[0])) {
        std::cerr << "moppe: opening: " << path
                  << " was composed for another world\n";
        return std::nullopt;
      }
      for (const OpeningShot& shot : reel->shots) {
        if (shot.eye[1] < ground_height (shot.eye) + 0.2f) {
          std::cerr << "moppe: opening: shot " << shot.name
                    << " stands underground in this world\n";
          return std::nullopt;
        }
        if (shot.sun && std::fabs (*shot.sun - m_sky.sun_height) > 0.01f)
          std::cerr << "moppe: opening: shot " << shot.name
                    << " was composed under sun " << *shot.sun
                    << ", the world is lit at " << m_sky.sun_height << '\n';
      }
      return reel;
    }

    std::vector<OpeningShot> MoppeGame::generated_opening () const {
      const Vec3 along = trail_direction_from_home ();
      Vec3 eye = m_home_base_position + along * 20.0f;
      eye[1] = ground_height (eye) + 3.6f;
      Vec3 look = m_spawn_position - eye;
      look[1] = 0.0f;
      OpeningShot still;
      still.name = "trailhead";
      still.eye = eye;
      still.heading_deg = heading_degrees (look);
      still.pitch_deg = -3.0f;
      still.fov_deg = 46.0f;
      still.hold = 11.0f;
      still.push = 3.0f;
      still.fade = 2.5f;
      still.captions.push_back (
        { OpeningCaption::Style::Title, "moppe", 1.6f, 4.4f, 0.28f });
      still.captions.push_back ({ OpeningCaption::Style::Credit,
                                  "a game by Mikael Brockman",
                                  6.2f,
                                  3.8f });
      return { still };
    }

    void MoppeGame::plan_opening_reel (const char* choice) {
      const std::optional<OpeningReel> reel = authored_opening (choice);
      m_opening_shots = reel ? reel->shots : generated_opening ();
      m_opening_arrival =
        reel && reel->arrival ? *reel->arrival : OpeningArrival {};
      const OpeningShot last = arrival ();
      m_opening_shots.push_back (last);
      const Vec3 start = session ().camera ().position ();
      std::cerr << "moppe: opening: the player starts at "
                << format_opening_shot ("start",
                                        start,
                                        session ().camera ().forward (),
                                        last.fov_deg,
                                        logic ().m_total_time,
                                        m_sky.sun_height)
                << '\n';
      float duration = 0.0f;
      for (const OpeningShot& shot : m_opening_shots)
        duration += shot.hold;
      std::cerr << "moppe: opening: " << m_opening_shots.size ()
                << (reel ? " authored" : " generated") << " shots, " << duration
                << " s\n";
    }

    void MoppeGame::start_opening () {
      if (!m_cinematic_plan.empty ()) {
        m_cinematic.start (m_cinematic_plan, surface ());
        return;
      }
      if (m_opening_shots.empty ())
        return;
      m_opening.start (m_opening_shots);
      m_opening_shot_seen = SIZE_MAX;
      m_opening_rendered_shot = SIZE_MAX;
      follow_opening_clock ();
    }

    void MoppeGame::follow_opening_clock () {
      if (!m_opening.active () ||
          m_opening.shot_index () == m_opening_shot_seen)
        return;
      m_opening_shot_seen = m_opening.shot_index ();
      if (const std::optional<float> clock = m_opening.shot ().clock) {
        logic ().m_total_time = *clock + m_opening.shot_time ();
        update_world_atmosphere (logic ().m_total_time);
      }
    }

    bool MoppeGame::keeping_pace () const {
      return !m_video_plan && m_screenshot_path.empty ();
    }

    void MoppeGame::walk_side_camera () {
      static const bool side = [] {
        const char* view = moppe::environment ("MOPPE_WALK_CAMERA");
        return view && std::string_view (view) == "side";
      }();
      if (!side || logic ().m_mode != M_FOOT)
        return;
      const Walker& walker = session ().walker ();
      const Vec3 heading = walker.heading ();
      const Vec3 right (heading[2], 0.0f, -heading[0]);
      const Vec3 at = walker.position () + Vec3 (0, 0.9f, 0);
      session ().camera ().place (at + right * 4.0f + Vec3 (0, 0.25f, 0), at);
    }

    void MoppeGame::animal_camera (seconds_t interval) {
      const float dt = seconds_value (interval);
      static const float distance = [] {
        const char* value = moppe::environment ("MOPPE_ANIMAL_CAMERA");
        return value ? static_cast<float> (::atof (value)) : 0.0f;
      }();
      if (distance <= 0.0f || !m_watched ||
          *m_watched >= session ().animals ().animals ().size ())
        return;
      const Animal& a = session ().animals ().animals ()[*m_watched];
      const float height = 0.7f * meters_value (traits (a.species).length);
      const Vec3 target = a.position + Vec3 (0.0f, height, 0.0f);
      // Eased, so that the view glides after it.
      const float ease = 1.0f - std::exp (-1.5f * dt);
      m_watch_target = m_watch_target
                         ? *m_watch_target + (target - *m_watch_target) * ease
                         : target;
      Vec3 eye = *m_watch_target + m_watch_side * distance +
                 Vec3 (0.0f, 0.25f * distance, 0.0f);
      eye[1] = std::max<float> (eye[1], ground_height (eye) + 1.2f);
      session ().camera ().place (eye, *m_watch_target);
    }

    void MoppeGame::ride_capture_camera (seconds_t interval) {
      const float dt = seconds_value (interval);
      static const std::string_view view = [] {
        const char* name = moppe::environment ("MOPPE_RIDE_CAMERA");
        return std::string_view (name ? name : "");
      }();
      m_film_fov.reset ();
      if (logic ().m_mode == M_BOAT && boat_film_camera (view, interval))
        return;
      // Flying, it is the machine on the line it stands beside.
      const bool slung =
        logic ().m_mode == M_HELICOPTER && logic ().m_sling.hooked;
      if (view.empty () || logic ().m_mode == M_FOOT ||
          (logic ().m_mode == M_HELICOPTER && !slung))
        return;
      if (slung) {
        const MachineBody load =
          session ().machine_body (logic ().m_sling.load);
        Vec3 heading = load.forward;
        heading[1] = 0.0f;
        heading =
          length2 (heading) > 1e-6f ? normalized (heading) : Vec3 (0, 0, 1);
        const Vec3 right (heading[2], 0.0f, -heading[0]);
        const Vec3 at = load.position + Vec3 (0, 0.5f, 0);
        const Vec3 from = view == "front" ? heading * 7.5f + right * 2.0f
                                          : right * 7.5f + heading * 0.6f;
        session ().camera ().place (at + from + Vec3 (0, 0.5f, 0), at);
        return;
      }
      const bool gliding = logic ().m_mode == M_GLIDER;
      const bool tractor = logic ().m_mode == M_TRACTOR;
      const bool caddy = logic ().m_mode == M_CADDY;
      const bool boat = logic ().m_mode == M_BOAT;
      const bool ski = logic ().m_mode == M_JET_SKI;
      const bool driving =
        logic ().m_mode == M_JEEP || tractor || caddy || boat || ski;
      const bool pedalled = game::pedalled (logic ().m_mode);
      const auto& bike = session ().ridden_bike ();
      // The tractor with its trailer, from their middle.
      const Vec3 car = tractor ? (session ().tractor ().position () +
                                  session ().tractor ().trailer_position ()) *
                                   0.5f
                       : caddy ? session ().caddy ().position ()
                       : boat  ? session ().boat ().position ()
                       : ski   ? session ().jet_ski ().position ()
                               : session ().jeep ().position ();
      Vec3 heading = gliding   ? session ().glider ().heading ()
                     : tractor ? session ().tractor ().forward ()
                     : caddy   ? session ().caddy ().forward ()
                     : boat    ? session ().boat ().forward ()
                     : ski     ? session ().jet_ski ().forward ()
                     : driving ? session ().jeep ().forward ()
                               : bike.render_orientation ();
      heading[1] = 0.0f;
      heading = normalized (heading);
      const Vec3 right (heading[2], 0.0f, -heading[0]);
      const Vec3 at = gliding
                        ? session ().glider ().position () - Vec3 (0, 0.9f, 0)
                      : driving  ? car + Vec3 (0, tractor ? 0.9f : 0.5f, 0)
                      : pedalled ? bike.render_position () + Vec3 (0, 0.55f, 0)
                                 : bike.render_position () + Vec3 (0, 0.4f, 0);
      const float away = gliding    ? 7.0f
                         : caddy    ? 7.5f
                         : boat     ? 7.0f
                         : ski      ? 5.5f
                         : tractor  ? 9.0f
                         : driving  ? 6.0f
                         : pedalled ? 3.4f
                                    : 4.5f;
      const Vec3 from = view == "front"
                          ? heading * away + right * (away * 0.27f)
                          : right * away + heading * 0.6f;
      session ().camera ().place (at + from + Vec3 (0, 0.5f, 0), at);
    }

    bool MoppeGame::boat_film_camera (std::string_view view,
                                      seconds_t interval) {
      const float dt = seconds_value (interval);
      static const float angle = [] {
        const char* value = moppe::environment ("MOPPE_RIDE_CAMERA_ANGLE");
        return value ? std::strtof (value, nullptr) * PI / 180.0f : 0.0f;
      }();
      const mov::Boat& boat = session ().boat ();
      const Vec3 at = boat.position () + Vec3 (0, 0.6f, 0);
      Vec3 heading = boat.forward ();
      heading[1] = 0.0f;
      heading = normalized (heading);
      // The subject eased, so the waves' heave and the hull's yawing
      // do not shake a camera that is meant to be flown.
      const bool fresh = m_film_time <= 0.0f;
      const auto ease = [dt] (float rate) {
        return 1.0f - std::exp (-rate * dt);
      };
      if (fresh) {
        m_film_at = at;
        m_film_heading = heading;
      }
      m_film_at += (at - m_film_at) * ease (5.0f);
      m_film_heading =
        normalized (m_film_heading + (heading - m_film_heading) * ease (0.9f));
      m_film_time += dt;
      const Vec3 h = m_film_heading;
      const Vec3 r (h[2], 0.0f, -h[0]);
      const auto around = [&] (float a) {
        return -h * std::cos (a) - r * std::sin (a);
      };
      const Vec3 up (0, 1, 0);
      Vec3 eye, look;
      float fov = 55.0f;
      bool clear = true;
      if (view == "drone") {
        eye = m_film_at + around (angle) * 26.0f + up * 13.0f;
        look = m_film_at + h * 8.0f;
        fov = 48.0f;
      } else if (view == "aerial") {
        eye = m_film_at + around (angle) * 75.0f + up * 48.0f;
        look = m_film_at + h * 6.0f;
        fov = 30.0f;
      } else if (view == "orbit") {
        eye =
          m_film_at + around (angle + 0.25f * m_film_time) * 15.0f + up * 3.5f;
        look = m_film_at;
        fov = 50.0f;
      } else if (view == "top") {
        eye = m_film_at + up * 30.0f - h * 3.0f;
        look = m_film_at + h * 2.0f;
        fov = 40.0f;
      } else if (view == "fpv") {
        const float sway = 0.35f * std::sin (0.45f * m_film_time);
        eye = m_film_at + around (angle + sway) * 5.0f + up * 1.0f;
        look = m_film_at + h * 14.0f + up * 0.3f;
        fov = 84.0f;
      } else if (view == "hull") {
        eye = boat.point (Vec3 (-1.35f, 0.45f, 1.3f));
        look = boat.point (Vec3 (0.0f, 0.85f, -1.0f));
        fov = 70.0f;
        clear = false;
      } else if (view == "stern") {
        eye = boat.point (Vec3 (0.4f, 1.45f, -2.9f));
        look = boat.point (Vec3 (0.0f, 0.5f, 9.0f));
        fov = 72.0f;
        clear = false;
      } else if (view == "flyby") {
        // A new stand whenever it has gone by and well off.
        const Vec3 off = m_film_stand - at;
        if (fresh || (length (off) > 65.0f && dot (off, h) < 0.0f)) {
          const float side = std::sin (angle) >= 0.0f ? 1.0f : -1.0f;
          m_film_stand = at + heading * 45.0f + r * (side * 8.0f);
          m_film_stand[1] = std::max (boat_depth_at (m_film_stand) +
                                        ground_height (m_film_stand),
                                      ground_height (m_film_stand)) +
                            1.1f;
        }
        // A long lens that keeps the boat the same size as it nears.
        eye = m_film_stand;
        look = at;
        fov =
          std::clamp (2.0f * std::atan (5.0f / length (at - eye)) * 180.0f / PI,
                      8.0f,
                      60.0f);
      } else if (view == "helm") {
        // The helmsman's eyes, through play's own camera.
        logic ().m_cam_mode = CAM_HELMET;
        m_film_fov = 75.0f;
        return true;
      } else {
        return false;
      }
      session ().camera ().place (eye, look);
      if (clear)
        session ().camera ().keep_above (surface ());
      m_film_fov = fov;
      return true;
    }
  }
}
