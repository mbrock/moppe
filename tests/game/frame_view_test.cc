#include <moppe/game/frame_view.hh>
#include <moppe/map/generate.hh>
#include <moppe/map/surface.hh>

#include <tests/test.hh>

#include <algorithm>
#include <cmath>
#include <memory>
#include <type_traits>

using namespace moppe;

namespace {
  void frame_view_check_vector (const Vec3& actual, const Vec3& expected) {
    MOPPE_CHECK_NEAR (actual[0], expected[0], 1e-6f);
    MOPPE_CHECK_NEAR (actual[1], expected[1], 1e-6f);
    MOPPE_CHECK_NEAR (actual[2], expected[2], 1e-6f);
  }

  void frame_view_check_color (DisplayColor actual, DisplayColor expected) {
    MOPPE_CHECK_NEAR (actual.red, expected.red, 1e-6f);
    MOPPE_CHECK_NEAR (actual.green, expected.green, 1e-6f);
    MOPPE_CHECK_NEAR (actual.blue, expected.blue, 1e-6f);
  }

  bool frame_view_same_matrix (const Mat4& left, const Mat4& right) {
    for (int i = 0; i < 16; ++i)
      if (left.m[i] != right.m[i])
        return false;
    return true;
  }

  struct FrameFixture {
    map::RandomHeightMap map {
      17, 17, Vec3 (160, 40, 160), 7, terrain::Topology::Bounded
    };
    game::WorldParams world;
    map::Surface surface;
    std::unique_ptr<game::GameSession> session;
    game::GraphicsSettings graphics = game::high_graphics_settings ();

    FrameFixture () {
      std::fill (map.raw_heights (),
                 map.raw_heights () + map.width () * map.height (),
                 0.25f);
      map.recompute_normals ();
      surface.refresh (map);
      world.map_size = spatial_extent_in_metres (map.size ());
      world.resolution = map.width ();
      world.water_level = 4.0f * u::m;
      world.fog_scale = 0.0004f / u::m;
      world.terrain_topology = terrain::Topology::Bounded;
      session = std::make_unique<game::GameSession> (world, map, surface);
      session->bike ().reset (Vec3 (48, 11.2f, 48));
      session->bike ().set_heading (Vec3 (0, 0, 1));
      session->camera ().place (Vec3 (48, 20, 32), Vec3 (48, 12, 48));
      session->logic ().m_total_time = 12.5;
      session->logic ().m_cloudiness = 0.42f;
      session->logic ().m_flare = 0.37f;
      session->logic ().m_fog = DisplayColor (0.4f, 0.5f, 0.6f);
      session->logic ().m_fov_k = 0.25f;
    }

    game::GameSession& running () {
      return *session;
    }
    const game::GameSession& running () const {
      return *session;
    }
  };

  game::FrameCameraReading camera_reading (const game::GameSession& session,
                                           float fov = 58.0f) {
    return {
      .position = session.camera ().position (),
      .forward = session.camera ().forward (),
      .view = session.camera ().view_matrix (),
      .field_of_view = fov,
    };
  }

  game::FrameViewInput gameplay_input (const FrameFixture& fixture) {
    return {
      .world = fixture.world,
      .terrain = fixture.map,
      .session = fixture.running (),
      .graphics = fixture.graphics,
      .selected_camera = camera_reading (fixture.running ()),
      .scene = game::FrameSceneMode::Gameplay,
      .aspect = 16.0f / 9.0f,
      .landscape_scale_x = 1.0f,
      .landscape_scale_y = 1.0f,
    };
  }

  game::FrameView clear_sun_view () {
    game::FrameView view;
    view.camera.position = Vec3 (10, 30, 80);
    view.lighting.sun_direction = Vec3 (1, 0, 0);
    return view;
  }

  void check_logic_reading (const game::GameLogicState& actual,
                            const game::GameLogicState& expected) {
    MOPPE_CHECK_NEAR (actual.m_total_time, expected.m_total_time, 1e-6f);
    MOPPE_CHECK_NEAR (actual.m_frame_time, expected.m_frame_time, 1e-6f);
    MOPPE_CHECK_NEAR (actual.m_cloudiness, expected.m_cloudiness, 1e-6f);
    MOPPE_CHECK_NEAR (actual.m_flare, expected.m_flare, 1e-6f);
    frame_view_check_color (actual.m_fog, expected.m_fog);
    MOPPE_CHECK_NEAR (actual.m_shake, expected.m_shake, 1e-6f);
    MOPPE_CHECK_NEAR (actual.m_shake_time, expected.m_shake_time, 1e-6f);
    MOPPE_CHECK_NEAR (actual.m_fov_k, expected.m_fov_k, 1e-6f);
    MOPPE_CHECK (actual.m_mode == expected.m_mode);
    MOPPE_CHECK (actual.m_cam_mode == expected.m_cam_mode);
    frame_view_check_vector (actual.m_fp_eye, expected.m_fp_eye);
    MOPPE_CHECK (actual.m_car_exists == expected.m_car_exists);
    MOPPE_CHECK (actual.m_score == expected.m_score);
    MOPPE_CHECK (actual.m_fx_rng == expected.m_fx_rng);
  }
}

MOPPE_TEST (frame_view_is_a_read_only_deterministic_value) {
  using ComposeFrameView = game::FrameView (*) (const game::FrameViewInput&);
  static_assert (
    std::is_same_v<decltype (&game::compose_frame_view), ComposeFrameView>);
  static_assert (std::is_copy_constructible_v<game::FrameView>);
  static_assert (std::is_copy_assignable_v<game::FrameView>);

  FrameFixture fixture;
  const game::GameState before = fixture.running ().state ();
  const game::FrameViewInput input = gameplay_input (fixture);

  const game::FrameView first = game::compose_frame_view (input);
  const game::FrameView second = game::compose_frame_view (input);
  const game::GameState after = fixture.running ().state ();

  check_logic_reading (after.logic, before.logic);
  frame_view_check_vector (position_value (after.vehicle.position),
                           position_value (before.vehicle.position));
  frame_view_check_vector (after.vehicle.render_heading,
                           before.vehicle.render_heading);
  frame_view_check_vector (position_value (after.camera.position),
                           position_value (before.camera.position));
  frame_view_check_vector (position_value (after.camera.target),
                           position_value (before.camera.target));
  MOPPE_CHECK (after.dust.emissions.size () == before.dust.emissions.size ());
  MOPPE_CHECK (after.stars.count == before.stars.count);

  MOPPE_CHECK (first.scene == game::FrameSceneMode::Gameplay);
  MOPPE_CHECK (frame_view_same_matrix (first.camera.view, second.camera.view));
  MOPPE_CHECK (
    frame_view_same_matrix (first.camera.projection, second.camera.projection));
  frame_view_check_vector (first.camera.position, second.camera.position);
  frame_view_check_vector (first.camera.right, second.camera.right);
  frame_view_check_vector (first.camera.up, second.camera.up);
  frame_view_check_vector (first.camera.frame_forward,
                           second.camera.frame_forward);
  frame_view_check_color (first.lighting.clear_color,
                          second.lighting.clear_color);
  MOPPE_CHECK_NEAR (
    first.lighting.sun_visibility, fixture.running ().logic ().m_flare, 1e-6f);
  frame_view_check_vector (position_value (first.environment.camera_pos),
                           first.camera.position);
  frame_view_check_vector (first.environment.cam_right, first.camera.right);
  frame_view_check_vector (first.environment.cam_up, first.camera.up);
  frame_view_check_vector (first.environment.cam_forward,
                           first.camera.frame_forward);
}

MOPPE_TEST (frame_view_snapshots_hud_and_overlay_readings) {
  FrameFixture fixture;
  game::GameSession& session = fixture.running ();
  game::GameLogicState& logic = session.logic ();
  logic.m_fuel = 47.0f;
  logic.m_health = 83.0f;
  logic.m_odometer = 1234.0;
  logic.m_lives = 6;
  logic.m_score = 420;
  logic.m_jump_airtime = 2.75f;
  logic.m_landed_age = 0.35f;
  logic.m_frame_time = 0.0125f;

  const game::FrameView frozen =
    game::compose_frame_view (gameplay_input (fixture));
  MOPPE_CHECK_NEAR (frozen.hud.fuel, 47.0f, 1e-6f);
  MOPPE_CHECK_NEAR (frozen.hud.health01, 0.83f, 1e-6f);
  MOPPE_CHECK_NEAR (frozen.hud.odometer_m, 1234.0f, 1e-6f);
  MOPPE_CHECK (frozen.hud.lives == 6);
  MOPPE_CHECK (frozen.hud.score == 420);
  MOPPE_CHECK_NEAR (frozen.hud.airtime_s, 2.75f, 1e-6f);
  MOPPE_CHECK_NEAR (frozen.hud.landed_age_s, 0.35f, 1e-6f);
  MOPPE_CHECK_NEAR (frozen.hud.frame_time_s, 0.0125f, 1e-6f);
  MOPPE_CHECK_NEAR (frozen.hud.heading_radians, 0.0f, 1e-6f);

  logic.m_fuel = 9.0f;
  logic.m_score = 7;
  session.bike ().reset (Vec3 (96, 11.2f, 48));
  session.bike ().set_heading (Vec3 (1, 0, 0));
  const game::FrameView changed =
    game::compose_frame_view (gameplay_input (fixture));
  MOPPE_CHECK_NEAR (frozen.hud.fuel, 47.0f, 1e-6f);
  MOPPE_CHECK (frozen.hud.score == 420);
  frame_view_check_vector (frozen.hud.subject_position, Vec3 (48, 11.2f, 48));
  MOPPE_CHECK_NEAR (frozen.hud.heading_radians, 0.0f, 1e-6f);
  MOPPE_CHECK_NEAR (changed.hud.fuel, 9.0f, 1e-6f);
  MOPPE_CHECK (changed.hud.score == 7);
  MOPPE_CHECK_NEAR (changed.hud.heading_radians, 0.5f * PI, 1e-6f);

  game::FrameViewInput cinematic = gameplay_input (fixture);
  cinematic.scene = game::FrameSceneMode::Cinematic;
  cinematic.cinematic_elapsed = 5.0f;
  const game::FrameView prompt = game::compose_frame_view (cinematic);
  MOPPE_CHECK (prompt.visibility.cinematic_hud);
  MOPPE_CHECK_NEAR (prompt.overlay.cinematic_prompt_alpha, 0.5f, 1e-6f);
}

MOPPE_TEST (frame_view_actor_snapshots_survive_session_mutation) {
  FrameFixture fixture;
  game::GameSession& session = fixture.running ();
  session.logic ().m_mode = game::M_FOOT;
  session.walker ().spawn (position (Vec3 (60, 10, 60)), Vec3 (1, 0, 0));

  const game::FrameView frozen =
    game::compose_frame_view (gameplay_input (fixture));
  MOPPE_CHECK (frozen.actors.walker.has_value ());
  const Vec3 bike_position = frozen.actors.bike.position;
  const Vec3 walker_position = frozen.actors.walker->position;

  session.bike ().reset (Vec3 (112, 11.2f, 48));
  session.walker ().spawn (position (Vec3 (96, 10, 40)), Vec3 (-1, 0, 0));
  const game::FrameView changed =
    game::compose_frame_view (gameplay_input (fixture));

  frame_view_check_vector (frozen.actors.bike.position, bike_position);
  frame_view_check_vector (frozen.actors.walker->position, walker_position);
  MOPPE_CHECK (length2 (changed.actors.bike.position - bike_position) > 1.0f);
  MOPPE_CHECK (length2 (changed.actors.walker->position - walker_position) >
               1.0f);

  session.logic ().m_mode = game::M_GLIDER;
  session.glider ().launch (
    position (Vec3 (72, 42, 72)), velocity (Vec3 (0, 1, 14)), Vec3 (0, 0, 1));
  const game::FrameView gliding =
    game::compose_frame_view (gameplay_input (fixture));
  MOPPE_CHECK (gliding.actors.glider.has_value ());
  const Vec3 glider_position = gliding.actors.glider->position;
  session.glider ().launch (
    position (Vec3 (108, 55, 72)), velocity (Vec3 (0, 1, 14)), Vec3 (0, 0, 1));
  const game::FrameView relaunched =
    game::compose_frame_view (gameplay_input (fixture));
  frame_view_check_vector (gliding.actors.glider->position, glider_position);
  MOPPE_CHECK (length2 (relaunched.actors.glider->position - glider_position) >
               1.0f);
}

MOPPE_TEST (frame_view_applies_shake_to_the_reading_not_the_camera_body) {
  FrameFixture fixture;
  game::GameSession& session = fixture.running ();
  const game::FrameView steady =
    game::compose_frame_view (gameplay_input (fixture));

  session.logic ().m_shake = 0.2f;
  session.logic ().m_shake_time = 0.031f;
  const game::FrameView shaken =
    game::compose_frame_view (gameplay_input (fixture));

  frame_view_check_vector (shaken.camera.position, steady.camera.position);
  frame_view_check_vector (shaken.camera.forward, steady.camera.forward);
  MOPPE_CHECK (
    !frame_view_same_matrix (shaken.camera.view, steady.camera.view));
  MOPPE_CHECK (length2 (shaken.camera.frame_forward -
                        steady.camera.frame_forward) > 1e-7f);
  frame_view_check_vector (shaken.environment.cam_right, shaken.camera.right);
  frame_view_check_vector (shaken.environment.cam_up, shaken.camera.up);
  frame_view_check_vector (shaken.environment.cam_forward,
                           shaken.camera.frame_forward);
}

MOPPE_TEST (frame_view_selects_cinematic_and_terrain_lab_rules) {
  FrameFixture fixture;
  game::GameSession& session = fixture.running ();
  session.logic ().m_shake = 0.2f;
  session.logic ().m_shake_time = 0.031f;

  const game::FrameCameraReading cinematic_camera {
    .position = Vec3 (20, 60, 20),
    .forward = normalized (Vec3 (1, -0.2f, 1)),
    .view =
      Mat4::look_at (Vec3 (20, 60, 20), Vec3 (80, 20, 80), Vec3 (0, 1, 0)),
    .field_of_view = 52.0f,
  };
  game::FrameViewInput cinematic = gameplay_input (fixture);
  cinematic.selected_camera = cinematic_camera;
  cinematic.scene = game::FrameSceneMode::Cinematic;
  cinematic.cinematic_motion_blur = 0.47f;
  const game::FrameView flight = game::compose_frame_view (cinematic);

  MOPPE_CHECK (flight.scene == game::FrameSceneMode::Cinematic);
  MOPPE_CHECK_NEAR (flight.camera.field_of_view, 52.0f, 1e-6f);
  MOPPE_CHECK (
    frame_view_same_matrix (flight.camera.view, cinematic_camera.view));
  MOPPE_CHECK_NEAR (flight.motion_blur_amount, 0.47f, 1e-6f);
  MOPPE_CHECK (flight.visibility.cinematic);
  MOPPE_CHECK (flight.visibility.sky_after_terrain);
  MOPPE_CHECK (flight.visibility.cinematic_hud);
  MOPPE_CHECK (!flight.visibility.game_hud);

  game::FrameViewInput lab = gameplay_input (fixture);
  lab.selected_camera = {
    .position = Vec3 (80, 150, 80),
    .forward = Vec3 (0, -1, 0),
    .view =
      Mat4::look_at (Vec3 (80, 150, 80), Vec3 (80, 10, 80), Vec3 (0, 0, 1)),
    .field_of_view = 35.0f,
  };
  lab.scene = game::FrameSceneMode::TerrainLab;
  lab.terrain_lab_fog = 0.0f / u::m;
  lab.terrain_lab_torus = true;
  lab.terrain_lab_pristine = false;
  const game::FrameView inspection = game::compose_frame_view (lab);

  MOPPE_CHECK (inspection.scene == game::FrameSceneMode::TerrainLab);
  MOPPE_CHECK_NEAR (inspection.camera.field_of_view, 70.0f, 1e-6f);
  MOPPE_CHECK_NEAR (inspection.terrain_distance, 30000.0f, 1e-6f);
  MOPPE_CHECK_NEAR (
    attenuation_value (inspection.lighting.fog_scale), 0.0f, 1e-6f);
  frame_view_check_color (inspection.lighting.clear_color,
                          DisplayColor (0.012f, 0.016f, 0.022f));
  MOPPE_CHECK_NEAR (inspection.lighting.exposure_bias, 0.88f, 1e-6f);
  MOPPE_CHECK_NEAR (inspection.lighting.sun_visibility, 0.0f, 1e-6f);
  MOPPE_CHECK (inspection.visibility.terrain_lab);
  MOPPE_CHECK (inspection.visibility.terrain_lab_torus);
  MOPPE_CHECK (!inspection.visibility.sky_before_terrain);
  MOPPE_CHECK (!inspection.visibility.sky_after_terrain);
  MOPPE_CHECK (!inspection.visibility.ocean);
  MOPPE_CHECK (inspection.visibility.terrain_lab_rivers);
  MOPPE_CHECK (!inspection.visibility.actors);
  MOPPE_CHECK (!inspection.visibility.dust);
  MOPPE_CHECK (!inspection.visibility.motion_blur);
  MOPPE_CHECK (inspection.visibility.terrain_lab_hud);
}

MOPPE_TEST (frame_view_carries_actor_visual_scale_and_sun_height) {
  FrameFixture fixture;
  fixture.graphics.sun_height = 0.75f;
  game::FrameViewInput input = gameplay_input (fixture);
  input.landscape_scale_x = 2.5f;
  input.landscape_scale_y = 4.0f;

  const game::FrameView view = game::compose_frame_view (input);

  frame_view_check_vector (view.actors.visual_scale, Vec3 (0.4f, 0.25f, 0.4f));
  MOPPE_CHECK_NEAR (view.lighting.sun_height, 0.75f, 1e-6f);
  frame_view_check_vector (view.lighting.sun_direction,
                           game::sun_direction_for (0.75f));
  MOPPE_CHECK_NEAR (
    view.lighting.sun_direction[1], std::sin (0.25f * 3.14159f), 1e-6f);
}

MOPPE_TEST (
  frame_view_sun_visibility_target_reads_clear_water_ridge_and_cloud) {
  FrameFixture clear;
  const game::FrameView view = clear_sun_view ();
  MOPPE_CHECK_NEAR (
    game::sun_visibility_target (view, clear.world, clear.map), 1.0f, 1e-6f);

  FrameFixture underwater;
  game::FrameView submerged = clear_sun_view ();
  submerged.camera.position[1] =
    meters_value (underwater.world.water_level) - 0.1f;
  MOPPE_CHECK_NEAR (
    game::sun_visibility_target (submerged, underwater.world, underwater.map),
    0.0f,
    1e-6f);

  FrameFixture ridge;
  for (int z = 0; z < ridge.map.height (); ++z)
    for (int x = 10; x <= 11; ++x)
      ridge.map.raw_heights ()[z * ridge.map.width () + x] = 4.0f;
  MOPPE_CHECK_NEAR (
    game::sun_visibility_target (view, ridge.world, ridge.map), 0.0f, 1e-6f);

  FrameFixture cloudy;
  game::FrameView overcast = clear_sun_view ();
  overcast.lighting.cloudiness = 0.4f;
  MOPPE_CHECK_NEAR (
    game::sun_visibility_target (overcast, cloudy.world, cloudy.map),
    0.74f,
    1e-6f);
}
