#include <moppe/environment.hh>
#include <moppe/game/forest.hh>
#include <moppe/game/game_session.hh>
#include <moppe/game/generated_world.hh>
#include <moppe/game/hud.hh>
#include <moppe/game/opening_director.hh>
#include <moppe/game/sky_clock.hh>
#include <moppe/game/world_ground.hh>
#include <moppe/platform/platform.hh>
#include <moppe/profile.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/text.hh>

#include <cmath>
#include <fstream>
#include <iostream>

namespace moppe::game {
  void OpeningDirector::plan (const GeneratedWorld& world,
                              const WorldGround& ground,
                              const SkyReading& sky,
                              GameSession& session,
                              const Vec3& home_base,
                              const Vec3& spawn) {
    MOPPE_PROFILE_ZONE ("startup.plan_opening");
    m_shots.clear ();
    const char* choice = moppe::environment ("MOPPE_OPENING");
    if (choice && std::string_view (choice) == "none")
      return;
    const bool flight = choice && std::string_view (choice) == "flight";
    if (choice && std::string_view (choice) == "river") {
      const char* seconds = moppe::environment ("MOPPE_FLYOVER_SECONDS");
      std::vector<Vec3> trees;
      trees.reserve (world.forest ().sites.size ());
      for (const ForestSite& site : world.forest ().sites) {
        Vec3 tree = position_value (site.position);
        tree[1] = tree_height (site).numerical_value_in (u::m);
        trees.push_back (tree);
      }
      m_flight_plan = plan_river_flight (
        world.surface (),
        trees,
        world.water_surface (),
        world.lake_census (),
        world.drainage (),
        world.rivers (),
        (seconds ? std::strtof (seconds, nullptr) : 0.0f) * u::s);
      return;
    }
    if (!flight) {
      plan_reel (choice, world, ground, sky, session, home_base, spawn);
      return;
    }
    m_flight_plan = plan_cinematic_flight (world.surface (),
                                           world.standing_water (),
                                           world.lake_census (),
                                           world.drainage (),
                                           world.rivers (),
                                           spawn,
                                           &world.trails ());
    if (m_flight_plan.empty ())
      return;
    std::cerr << "cinematic flight: " << m_flight_plan.waypoints.size ()
              << " gates through ";
    for (std::size_t i = 0; i < m_flight_plan.landmarks.size (); ++i) {
      if (i)
        std::cerr << ", ";
      std::cerr << cinematic_landmark_name (m_flight_plan.landmarks[i].kind);
    }
    std::cerr << '\n';
  }

  std::optional<OpeningReel>
  OpeningDirector::authored (const char* choice,
                             const GeneratedWorld& world,
                             const WorldGround& ground,
                             const SkyReading& sky) {
    if (choice && std::string_view (choice) == "generated")
      return std::nullopt;
    const std::string path =
      choice ? std::string (choice) : platform::asset_path ("data/opening.txt");
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
    const terrain::WorldRecipe& recipe = world.recipe ();
    if (!reel->matches (static_cast<int> (recipe.seed ().value),
                        recipe.resolution (),
                        terrain::profile_id (recipe.generation_profile ())) ||
        extent_value (recipe.extent ())[0] !=
          (reel->km ? *reel->km * 1000.0f
                    : extent_value (WorldParams ().map_size)[0])) {
      std::cerr << "moppe: opening: " << path
                << " was composed for another world\n";
      return std::nullopt;
    }
    for (const OpeningShot& shot : reel->shots) {
      if (shot.eye[1] < ground.height (shot.eye) + 0.2f) {
        std::cerr << "moppe: opening: shot " << shot.name
                  << " stands underground in this world\n";
        return std::nullopt;
      }
      if (shot.sun && std::fabs (*shot.sun - sky.sun_height) > 0.01f)
        std::cerr << "moppe: opening: shot " << shot.name
                  << " was composed under sun " << *shot.sun
                  << ", the world is lit at " << sky.sun_height << '\n';
    }
    return reel;
  }

  std::vector<OpeningShot> OpeningDirector::generated (
    const WorldGround& ground, const Vec3& home_base, const Vec3& spawn) {
    const Vec3 along = direction_value (ground.trail_heading ());
    Vec3 eye = home_base + along * 20.0f;
    eye[1] = ground.height (eye) + 3.6f;
    Vec3 look = spawn - eye;
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

  void OpeningDirector::plan_reel (const char* choice,
                                   const GeneratedWorld& world,
                                   const WorldGround& ground,
                                   const SkyReading& sky,
                                   GameSession& session,
                                   const Vec3& home_base,
                                   const Vec3& spawn) {
    const std::optional<OpeningReel> reel =
      authored (choice, world, ground, sky);
    m_shots = reel ? reel->shots : generated (ground, home_base, spawn);
    m_arrival = reel && reel->arrival ? *reel->arrival : OpeningArrival {};
    const OpeningShot last = arrival (session, world.surface ());
    m_shots.push_back (last);
    const Vec3 start = session.camera ().position ();
    std::cerr << "moppe: opening: the player starts at "
              << format_opening_shot ("start",
                                      start,
                                      session.camera ().forward (),
                                      last.fov_deg,
                                      static_cast<float> (seconds_into_play (
                                        session.logic ().m_total_time)),
                                      sky.sun_height)
              << '\n';
    float duration = 0.0f;
    for (const OpeningShot& shot : m_shots)
      duration += shot.hold;
    std::cerr << "moppe: opening: " << m_shots.size ()
              << (reel ? " authored" : " generated") << " shots, " << duration
              << " s\n";
  }

  OpeningShot
  OpeningDirector::arrival (GameSession& session,
                            const map::SurfaceGeometry& surface) const {
    session.settle_camera (surface);
    const ChaseCamera& camera = session.camera ();
    const float fov = session.logic ().m_cam_mode == CAM_HELMET
                        ? 70.0f
                        : CHASE_FIELD_OF_VIEW.numerical_value_in (u::deg);
    return arrival_shot (m_arrival, camera.position (), camera.forward (), fov);
  }

  void OpeningDirector::start (GameSession& session,
                               const map::SurfaceGeometry& surface,
                               SkyClock& sky) {
    if (!m_flight_plan.empty ()) {
      m_flight.start (m_flight_plan, surface);
      return;
    }
    if (m_shots.empty ())
      return;
    m_reel.start (m_shots);
    m_shot_seen = SIZE_MAX;
    m_drawn_shot = SIZE_MAX;
    follow_clock (session, sky);
  }

  void OpeningDirector::forget () {
    stop ();
    m_flight_plan = {};
    m_shots.clear ();
  }

  void OpeningDirector::follow_clock (GameSession& session, SkyClock& sky) {
    if (!m_reel.active () || m_reel.shot_index () == m_shot_seen)
      return;
    m_shot_seen = m_reel.shot_index ();
    if (const std::optional<float> clock = m_reel.shot ().clock) {
      GameLogicState& logic = session.logic ();
      logic.m_total_time = play_time (*clock + m_reel.shot_time ());
      sky.update (logic,
                  static_cast<float> (seconds_into_play (logic.m_total_time)));
    }
  }

  OpeningDirector::Step
  OpeningDirector::step (InputFrame& input,
                         seconds_t interval,
                         GameSession& session,
                         const map::SurfaceGeometry& surface,
                         SkyClock& sky) {
    if (m_flight.active ()) {
      if (input.leave_cinematic) {
        input = {};
        return { .left = true };
      }
      const CinematicFlightControls controls {
        .lateral = input_value (input.turn),
        .lift = input_value (input.boost),
        .pace = input_value (input.drive),
      };
      m_flight.tick (interval, surface, controls);
      return { .left = !m_flight.active (), .played = true };
    }
    if (m_reel.active ()) {
      if (input.leave_cinematic) {
        input = {};
        return { .left = true };
      }
      m_reel.tick (interval);
      follow_clock (session, sky);
      return { .left = !m_reel.active (), .played = true };
    }
    return {};
  }

  std::optional<FrameCameraReading> OpeningDirector::camera () const {
    if (m_reel.active ())
      return FrameCameraReading {
        .position = position (m_reel.position ()),
        .forward = unit_direction (m_reel.forward ()),
        .view = m_reel.view_matrix (),
        .field_of_view = m_reel.field_of_view () * u::deg,
      };
    if (m_flight.active ())
      return FrameCameraReading {
        .position = position (m_flight.position ()),
        .forward = unit_direction (m_flight.forward ()),
        .view = m_flight.view_matrix (),
        .field_of_view = m_flight.field_of_view (),
      };
    return std::nullopt;
  }

  bool OpeningDirector::take_cut () {
    if (!m_reel.active () || m_reel.shot_index () == m_drawn_shot)
      return false;
    const bool cut = m_drawn_shot != SIZE_MAX;
    m_drawn_shot = m_reel.shot_index ();
    return cut;
  }

  void OpeningDirector::reaim (GameSession& session,
                               const map::SurfaceGeometry& surface) {
    if (m_reel.active ())
      m_reel.replace_arrival (arrival (session, surface));
  }

  void OpeningDirector::draw_titles (const Hud& hud,
                                     render::DrawList& dl,
                                     render::TextList& text,
                                     int width_pts,
                                     int height_pts) const {
    if (!m_reel.active ())
      return;
    if (const float veil = m_reel.veil (); veil > 0.002f) {
      render::DrawState over;
      over.blend = true;
      over.depth_test = false;
      over.depth_write = false;
      over.cull = false;
      dl.state (over);
      dl.lit (false);
      dl.fogged (false);
      dl.color (0.0f, 0.0f, 0.0f, veil);
      dl.begin (render::Prim::Quads);
      dl.vertex (-64.0f, -64.0f);
      dl.vertex (width_pts + 64.0f, -64.0f);
      dl.vertex (width_pts + 64.0f, height_pts + 64.0f);
      dl.vertex (-64.0f, height_pts + 64.0f);
      dl.end ();
      dl.state (render::DrawState ());
    }
    for (const OpeningCaptionView& caption : m_reel.captions ())
      hud.draw_title_card (text,
                           caption.style == OpeningCaption::Style::Title,
                           *caption.text,
                           caption.alpha * opacity[one],
                           caption.y * proportion[one],
                           static_cast<float> (width_pts) * u::pt,
                           static_cast<float> (height_pts) * u::pt);
  }
}
