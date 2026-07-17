#include <moppe/game/frame_view.hh>

#include <algorithm>
#include <cmath>
#include <limits>

namespace moppe::game {
  namespace {
    constexpr float SUN_AZIMUTH = 0.8f;
    // Keep the historical art-direction calculations bit-for-bit aligned
    // with the game loop while moving them behind the presentation seam.
    constexpr float ART_PI = 3.14159f;

    float smooth_curve (float edge0, float edge1, float value) {
      const float t =
        std::clamp ((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t);
    }

    float sun_elevation_for (float sun_height) {
      return std::sin ((sun_height - 0.5f) * ART_PI);
    }

    float daylight_for (float sun_height) {
      return smooth_curve (-0.08f, 0.18f, sun_elevation_for (sun_height));
    }

    float golden_light_for (float sun_height) {
      const float elevation = sun_elevation_for (sun_height);
      return daylight_for (sun_height) *
             (1.0f - smooth_curve (0.15f, 0.65f, elevation));
    }

    FrameVisibility visibility_for (const FrameViewInput& input,
                                    const FrameView& view) {
      const bool terrain_lab = input.scene == FrameSceneMode::TerrainLab;
      const bool cinematic = input.scene == FrameSceneMode::Cinematic;
      const bool water = input.scene == FrameSceneMode::WaterInspection;
      const bool tree = input.scene == FrameSceneMode::TreeDemo;

      FrameVisibility visibility;
      visibility.terrain_lab = terrain_lab;
      visibility.cinematic = cinematic;
      visibility.water_inspection = water;
      visibility.tree_demo = tree;
      visibility.terrain_lab_torus = terrain_lab && input.terrain_lab_torus;
      visibility.terrain_lab_pristine = input.terrain_lab_pristine;
      visibility.sky_before_terrain = terrain_lab && !input.terrain_lab_torus;
      visibility.sky_after_terrain = !terrain_lab;
      visibility.forest = !terrain_lab && !water && !tree;
      visibility.tree_stand = !terrain_lab && !water;
      visibility.actors = !terrain_lab && !water && !tree;
      visibility.ocean =
        input.graphics.ocean && (!terrain_lab || (!input.terrain_lab_torus &&
                                                  input.terrain_lab_pristine));
      visibility.terrain_lab_rivers = terrain_lab;
      visibility.river_ribbons = !terrain_lab && input.graphics.river_ribbons;
      visibility.dust = !terrain_lab && input.graphics.particles;
      visibility.underwater =
        !terrain_lab &&
        view.camera.position[1] < meters_value (input.world.water_level);
      visibility.motion_blur = !terrain_lab && input.graphics.motion_blur &&
                               view.motion_blur_amount > 0.01f;
      visibility.vehicle_effects =
        visibility.actors && input.graphics.vehicle_effects;
      visibility.star_effects =
        visibility.actors && input.graphics.star_effects;
      visibility.game_hud = !terrain_lab && !cinematic && !water && !tree;
      visibility.cinematic_hud = cinematic;
      visibility.terrain_lab_hud = terrain_lab;
      visibility.terrain_topology_hint = input.graphics.terrain_topology;
      return visibility;
    }
  }

  Vec3 sun_direction_for (float sun_height) {
    const float elevation = (sun_height - 0.5f) * ART_PI;
    return Vec3 (std::cos (elevation) * std::sin (SUN_AZIMUTH),
                 std::sin (elevation),
                 std::cos (elevation) * std::cos (SUN_AZIMUTH));
  }

  void sun_light_colors_for (float sun_height,
                             DisplayColor& diffuse,
                             DisplayColor& specular) {
    const float daylight = daylight_for (sun_height);
    const float golden = golden_light_for (sun_height);
    const DisplayColor warm (1.0f, 0.60f, 0.30f);
    const DisplayColor ivory (1.0f, 0.96f, 0.84f);
    const DisplayColor sun_color = mix_display (ivory, warm, golden);

    diffuse = scale_display (sun_color, 0.10f + 0.98f * daylight);
    specular = scale_display (mix_display (DisplayColor (0.92f, 0.95f, 1.0f),
                                           DisplayColor (1.0f, 0.86f, 0.70f),
                                           golden),
                              0.15f + 0.85f * daylight);
  }

  DisplayColor horizon_color_for (float sun_height) {
    const float daylight = daylight_for (sun_height);
    const float warmth = golden_light_for (sun_height) * 0.16f;
    const DisplayColor day_horizon (0.55f, 0.68f, 0.84f);
    const DisplayColor night_horizon (0.035f, 0.045f, 0.09f);
    const DisplayColor warm_horizon (0.92f, 0.58f, 0.32f);
    return mix_display (
      mix_display (night_horizon, day_horizon, daylight), warm_horizon, warmth);
  }

  VehiclePose vehicle_pose (const mov::Vehicle& vehicle) {
    return {
      .position = vehicle.position (),
      .render_orientation = vehicle.render_orientation (),
      .render_normal = vehicle.render_normal (),
      .suspension = vehicle.susp (),
      .lean_radians = radians_value (vehicle.lean ()),
      .wheel_spin_radians = radians_value (vehicle.wheel_spin ()),
      .yaw_radians = radians_value (vehicle.yaw ()),
      .thrust = vehicle.thrust ().numerical_value_in (one),
      .boost_level = vehicle.boost_level (),
      .boost_drive = vehicle.boost_drive (),
      .body_kind = vehicle.body_kind (),
      .body_color = vehicle.body_color (),
    };
  }

  GliderPose glider_pose (const mov::Glider& glider) {
    return {
      .position = glider.position (),
      .heading = glider.heading (),
      .bank_radians = radians_value (glider.bank ()),
    };
  }

  WalkerPose walker_pose (const Walker& walker) {
    const Walker::State state = walker.state ();
    return {
      .position = position_value (state.position),
      .heading = state.heading,
      .walk = state.walk.numerical_value_in (one),
      .animation_distance = meters_value (state.animation_distance),
    };
  }

  FrameView compose_frame_view (const FrameViewInput& input) {
    const GameLogicState& logic = input.session.logic ();
    const bool terrain_lab = input.scene == FrameSceneMode::TerrainLab;
    const bool cinematic = input.scene == FrameSceneMode::Cinematic;
    const bool water = input.scene == FrameSceneMode::WaterInspection;
    const bool tree = input.scene == FrameSceneMode::TreeDemo;

    FrameView result;
    result.scene = input.scene;
    result.benchmark = input.benchmark;

    result.camera.position = input.selected_camera.position;
    result.camera.forward = input.selected_camera.forward;
    result.camera.field_of_view =
      cinematic                      ? input.selected_camera.field_of_view
      : terrain_lab || water || tree ? 70.0f
                                     : 100.0f + 9.0f * logic.m_fov_k;
    result.camera.view = input.selected_camera.view;

    // Hard landings rotate only the reading of the riding camera.  The camera
    // body stays where simulation placed it, and cinematics/inspection views
    // are explicitly stable reference views.
    if (input.scene == FrameSceneMode::Gameplay && logic.m_shake > 0.005f) {
      const Vec3& camera = result.camera.position;
      const float ground =
        input.terrain.interpolated_height (camera[0], camera[2]);
      const float clearance = camera[1] - ground;
      const float room =
        std::min (1.0f, std::max (0.0f, (clearance - 2.0f) / 8.0f));
      const float pulse = logic.m_shake * room;
      const float roll =
        pulse * std::sin (2.0f * PI * 15.0f * logic.m_shake_time);
      const float pitch =
        pulse * 0.55f *
        std::sin (2.0f * PI * 19.0f * logic.m_shake_time + 0.7f);
      result.camera.view = result.camera.view *
                           Mat4::rotation (roll * u::deg, Vec3 (0, 0, 1)) *
                           Mat4::rotation (pitch * u::deg, Vec3 (1, 0, 0));
    }

    result.camera.right = Vec3 (result.camera.view.m[0],
                                result.camera.view.m[4],
                                result.camera.view.m[8]);
    result.camera.up = Vec3 (result.camera.view.m[1],
                             result.camera.view.m[5],
                             result.camera.view.m[9]);
    result.camera.frame_forward = Vec3 (-result.camera.view.m[2],
                                        -result.camera.view.m[6],
                                        -result.camera.view.m[10]);

    const float scale = std::max (
      1e-6f, std::max (input.landscape_scale_x, input.landscape_scale_y));
    const float near = std::clamp (0.5f / scale, 0.02f, 0.5f);
    result.camera.projection =
      Mat4::perspective_reversed (result.camera.field_of_view * u::deg,
                                  std::max (0.01f, input.aspect),
                                  near,
                                  terrain_lab ? 30000.0f : 9000.0f);

    result.lighting.fog_color = logic.m_fog;
    result.lighting.clear_color = terrain_lab && input.terrain_lab_torus
                                    ? DisplayColor (0.012f, 0.016f, 0.022f)
                                    : logic.m_fog;
    result.lighting.fog_scale =
      terrain_lab ? input.terrain_lab_fog : input.world.fog_scale;
    result.lighting.sun_direction =
      sun_direction_for (input.graphics.sun_height);
    sun_light_colors_for (input.graphics.sun_height,
                          result.lighting.sun_diffuse,
                          result.lighting.sun_specular);
    result.lighting.sun_specular =
      scale_display (result.lighting.sun_specular, 0.5f);
    result.lighting.ambient =
      scale_display (DisplayColor (0.39f, 0.43f, 0.49f),
                     0.35f + 0.65f * daylight_for (input.graphics.sun_height));
    if (terrain_lab) {
      result.lighting.ambient = scale_display (result.lighting.ambient, 1.15f);
      result.lighting.exposure_bias = 0.88f;
    }
    result.lighting.time = static_cast<float> (logic.m_total_time);
    result.lighting.sun_height = input.graphics.sun_height;
    result.lighting.cloudiness = logic.m_cloudiness;
    result.lighting.sun_visibility = terrain_lab ? 0.0f : logic.m_flare;

    result.environment.fog_color = result.lighting.fog_color;
    result.environment.fog_scale = result.lighting.fog_scale;
    result.environment.sun_dir = result.lighting.sun_direction;
    result.environment.camera_pos = position (result.camera.position);
    result.environment.cam_right = result.camera.right;
    result.environment.cam_up = result.camera.up;
    result.environment.cam_forward = result.camera.frame_forward;
    result.environment.time = seconds (result.lighting.time);

    result.graphics = {
      .scene_scale = input.graphics.scene_scale,
      .render_scale_override = input.graphics.render_scale_override,
      .bloom = input.graphics.bloom,
      .auto_exposure = input.graphics.auto_exposure,
      .lens_flare = input.graphics.lens_flare,
    };

    result.actors.bike = vehicle_pose (input.session.bike ());
    if (logic.m_car_exists)
      result.actors.car = vehicle_pose (input.session.car ());
    if (logic.m_mode == M_GLIDER)
      result.actors.glider = glider_pose (input.session.glider ());
    if (logic.m_mode == M_FOOT)
      result.actors.walker = walker_pose (input.session.walker ());
    result.actors.visual_scale = Vec3 (1.0f / input.landscape_scale_x,
                                       1.0f / input.landscape_scale_y,
                                       1.0f / input.landscape_scale_x);
    result.actors.active_mode = logic.m_mode;
    result.actors.camera_mode = logic.m_cam_mode;
    result.actors.helmet_camera = logic.m_cam_mode == CAM_HELMET;

    FrameHud& hud = result.hud;
    hud.speed_kmh = input.session.subject_speed_kmh ();
    hud.fuel = logic.m_fuel;
    if (logic.m_mode == M_GLIDER) {
      const float lift =
        input.session.glider ().air_mass_lift ().numerical_value_in (u::m /
                                                                     u::s);
      hud.boost_ready01 = std::clamp (lift / 4.0f, 0.0f, 1.0f);
      hud.vertical_speed_mps =
        input.session.glider ().vertical_speed ().numerical_value_in (u::m /
                                                                      u::s);
    } else {
      hud.boost_ready01 = logic.m_mode == M_FOOT
                            ? 1.0f
                            : input.session.active_vehicle ().boost_charge ();
    }
    hud.health01 = logic.m_health / 100.0f;
    hud.odometer_m = static_cast<float> (logic.m_odometer);
    hud.lives = logic.m_lives;
    hud.stars = input.session.stars ().collected ();
    hud.score = logic.m_score;
    hud.airtime_s = logic.m_jump_airtime;
    hud.landed_airtime_s = logic.m_landed_airtime;
    hud.landed_points = logic.m_landed_points;
    hud.landed_age_s = logic.m_landed_age;
    hud.on_foot = logic.m_mode == M_FOOT;
    hud.gliding = logic.m_mode == M_GLIDER;
    hud.can_deploy_glider = input.session.can_deploy_glider (input.terrain);
    hud.frame_time_s = logic.m_frame_time;
    hud.subject_position = input.session.subject_position ();
    hud.subject_heading = input.session.subject_heading ();
    hud.heading_radians =
      std::atan2 (hud.subject_heading[0], hud.subject_heading[2]);

    if (cinematic)
      result.motion_blur_amount = input.cinematic_motion_blur;
    else if (!terrain_lab) {
      const float kmh = input.session.subject_speed_kmh ();
      result.motion_blur_amount =
        std::clamp ((kmh - 90.0f) / 160.0f, 0.0f, 1.0f);
    }

    if (cinematic) {
      result.overlay.cinematic_prompt_alpha = std::clamp (
        1.0f - std::max (0.0f, input.cinematic_elapsed - 4.0f) / 2.0f,
        0.0f,
        0.72f);
    }

    const float fog = attenuation_value (input.world.fog_scale);
    result.terrain_distance = terrain_lab ? 30000.0f
                              : fog > 0.0f
                                ? 3.0f / fog
                                : std::numeric_limits<float>::infinity ();
    result.visibility = visibility_for (input, result);
    return result;
  }

  float sun_visibility_target (const FrameView& view,
                               const WorldParams& world,
                               const map::HeightMap& terrain) {
    const Vec3& camera = view.camera.position;
    float visibility = 1.0f;
    if (camera[1] < meters_value (world.water_level))
      visibility = 0.0f;
    else {
      for (int i = 1; i <= 40; ++i) {
        const float distance = 90.0f * i;
        const Vec3 sample = camera + view.lighting.sun_direction * distance;
        if (!terrain.in_bounds (sample[0], sample[2]))
          break;
        if (terrain.interpolated_height (sample[0], sample[2]) > sample[1]) {
          visibility = 0.0f;
          break;
        }
      }
    }
    return visibility * (1.0f - 0.65f * view.lighting.cloudiness);
  }
}
