// The opening's shots: planning them, playing them, and the clock they set.
// The capture cameras are capture_scripts.hh's.

#include <moppe/game/game.hh>

namespace moppe {
  namespace game {
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
          generated_world ().lake_census (),
          generated_world ().drainage (),
          generated_world ().rivers (),
          (seconds ? std::strtof (seconds, nullptr) : 0.0f) * u::s);
        return;
      }
      if (!flight) {
        plan_opening_reel (choice);
        return;
      }
      m_cinematic_plan =
        plan_cinematic_flight (surface (),
                               generated_world ().standing_water (),
                               generated_world ().lake_census (),
                               generated_world ().drainage (),
                               generated_world ().rivers (),
                               m_spawn_position,
                               &generated_world ().trails ());
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
        if (shot.eye[1] < world_ground ().height (shot.eye) + 0.2f) {
          std::cerr << "moppe: opening: shot " << shot.name
                    << " stands underground in this world\n";
          return std::nullopt;
        }
        if (shot.sun && std::fabs (*shot.sun - m_sky.sky ().sun_height) > 0.01f)
          std::cerr << "moppe: opening: shot " << shot.name
                    << " was composed under sun " << *shot.sun
                    << ", the world is lit at " << m_sky.sky ().sun_height
                    << '\n';
      }
      return reel;
    }

    std::vector<OpeningShot> MoppeGame::generated_opening () const {
      const Vec3 along = world_ground ().trail_heading ();
      Vec3 eye = m_home_base_position + along * 20.0f;
      eye[1] = world_ground ().height (eye) + 3.6f;
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
                                        static_cast<float> (seconds_into_play (
                                          logic ().m_total_time)),
                                        m_sky.sky ().sun_height)
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
        logic ().m_total_time = play_time (*clock + m_opening.shot_time ());
        m_sky.update (
          logic (),
          static_cast<float> (seconds_into_play (logic ().m_total_time)));
      }
    }

    bool MoppeGame::keeping_pace () const {
      return m_capture.keeping_pace ();
    }

  }
}
