#include <moppe/game/frame_view.hh>

#include <moppe/game/camp.hh>
#include <moppe/game/fishing.hh>
#include <moppe/game/weather.hh>

#include <algorithm>
#include <cmath>
#include <limits>

namespace moppe::game {
  namespace {
    constexpr degrees_t CHASE_SPEED_WIDENING = 8.0f * u::deg;
    // Keep the historical art-direction calculations bit-for-bit aligned
    // with the game loop while moving them behind the presentation seam.
    constexpr float ART_PI = 3.14159f;

    float sun_elevation_for (float sun_height) {
      return std::sin ((sun_height - 0.5f) * ART_PI);
    }

    float daylight_for (float sun_height) {
      return daylight_for_elevation (sun_elevation_for (sun_height));
    }

    float golden_light_for (float sun_height) {
      return golden_light_for_elevation (sun_elevation_for (sun_height));
    }

    FrameVisibility visibility_for (const FrameViewInput& input,
                                    const FrameView& view) {
      const bool cinematic = input.scene == FrameSceneMode::Cinematic;
      const bool water = input.scene == FrameSceneMode::WaterInspection;
      const bool gazetteer = input.scene == FrameSceneMode::Gazetteer;

      FrameVisibility visibility;
      visibility.cinematic = cinematic;
      visibility.water_inspection = water;
      visibility.forest = !water && input.graphics.forest;
      visibility.boulders = !water && input.graphics.boulders;
      visibility.mushrooms = !water && input.graphics.forest;
      visibility.undergrowth = !water && input.graphics.undergrowth;
      visibility.actors = !water;
      visibility.ocean = input.graphics.ocean;
      visibility.waterfall_curtains = input.graphics.waterfall_curtains;
      visibility.dust = !gazetteer && input.graphics.particles;
      visibility.underwater =
        view.camera.position[1] <
        (input.world.water_level).numerical_value_in (moppe::u::m);
      visibility.motion_blur =
        input.graphics.motion_blur && view.motion_blur_amount > 0.01f;
      visibility.vehicle_effects =
        visibility.actors && input.graphics.vehicle_effects;
      visibility.star_effects =
        visibility.actors && input.graphics.star_effects;
      visibility.game_hud = !cinematic && !water && !gazetteer;
      visibility.cinematic_hud = cinematic;
      return visibility;
    }

    degrees_t gameplay_field_of_view (const FrameViewInput& input) {
      const GameLogicState& logic = input.session.logic ();
      if (logic.m_cam_mode == CAM_HELMET)
        return input.selected_camera.field_of_view;
      return CHASE_FIELD_OF_VIEW + CHASE_SPEED_WIDENING * logic.m_fov_k;
    }
  }

  Vec3 sun_direction_for (float sun_height) {
    return authored_sun_direction (sun_height);
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
      .position = vehicle.render_position (),
      .render_orientation = vehicle.render_orientation (),
      .render_normal = vehicle.render_normal (),
      .rear_wheel_drop = vehicle.rear_wheel_drop (),
      .front_wheel_drop = vehicle.front_wheel_drop (),
      .lean_radians = radians_value (vehicle.lean ()),
      .wheel_spin_radians = radians_value (vehicle.wheel_spin ()),
      .fork_radians = radians_value (vehicle.fork_angle ()),
      .thrust = vehicle.thrust ().numerical_value_in (one),
      .boost_level = vehicle.boost_level (),
      .boost_drive = vehicle.boost_drive (),
      .crank_radians = radians_value (vehicle.crank ()),
    };
  }

  JeepPose jeep_pose (const mov::Jeep& jeep) {
    return {
      .position = jeep.position (),
      .forward = jeep.forward (),
      .up = jeep.up (),
      .wheel_drop = { jeep.wheel_drop (0),
                      jeep.wheel_drop (1),
                      jeep.wheel_drop (2),
                      jeep.wheel_drop (3) },
      .steer_radians = jeep.steer (),
      .wheel_spin_radians = jeep.wheel_spin (),
    };
  }

  CaddyPose caddy_pose (const mov::Caddy& caddy) {
    return {
      .position = caddy.position (),
      .forward = caddy.forward (),
      .up = caddy.up (),
      .wheel_drop = { caddy.wheel_drop (0),
                      caddy.wheel_drop (1),
                      caddy.wheel_drop (2),
                      caddy.wheel_drop (3) },
      .steer_radians = caddy.steer (),
      .wheel_spin_radians = caddy.wheel_spin (),
    };
  }

  BoatPose boat_pose (const mov::Boat& boat) {
    return {
      .position = boat.position (),
      .forward = boat.forward (),
      .up = boat.up (),
      .swing_radians = boat.swing (),
      .tilt_radians = boat.tilt (),
      .propeller_radians = boat.propeller_angle (),
      .rpm = boat.rpm (),
    };
  }

  TractorPose tractor_pose (const mov::Tractor& tractor) {
    return {
      .position = tractor.position (),
      .forward = tractor.forward (),
      .up = tractor.up (),
      .wheel_drop = { tractor.wheel_drop (0),
                      tractor.wheel_drop (1),
                      tractor.wheel_drop (2),
                      tractor.wheel_drop (3) },
      .steer_radians = tractor.steer (),
      .front_spin_radians = tractor.front_spin (),
      .rear_spin_radians = tractor.rear_spin (),
    };
  }

  HelicopterPose helicopter_pose (const mov::Helicopter& helicopter) {
    return {
      .position = helicopter.position (),
      .forward = helicopter.forward (),
      .up = helicopter.up (),
      .rotor_radians = helicopter.rotor_angle (),
      .tail_radians = helicopter.tail_angle (),
    };
  }

  GliderPose glider_pose (const mov::Glider& glider) {
    return {
      .position = glider.position (),
      .heading = glider.nose (),
      .bank_radians = radians_value (glider.bank ()),
      .bike_attached = glider.bike_attached (),
    };
  }

  WalkerPose walker_pose (const Walker& walker, float look_pitch) {
    const Walker::State state = walker.state ();
    const mov::Character::State& body = state.body;
    const Vec3 velocity = velocity_value (body.velocity);
    return {
      .position = position_value (body.position) +
                  Vec3 (0, body.step_offset.numerical_value_in (u::m), 0),
      .heading = state.facing,
      .look_aside = state.lag,
      .turn_speed = state.turn_speed,
      .velocity = velocity,
      .ground_normal = body.ground_normal,
      .stride_phase = state.stride_phase,
      .stride_speed = state.stride_speed.numerical_value_in (u::m / u::s),
      .grounded = body.grounded,
      .airborne_seconds = seconds_value (body.airborne_time),
      .vertical_speed = velocity[1],
      .landing_dip = walker.landing_dip (),
      .look_pitch = look_pitch,
    };
  }

  FrameView compose_frame_view (const FrameViewInput& input) {
    const GameLogicState& logic = input.session.logic ();
    const bool cinematic = input.scene == FrameSceneMode::Cinematic;
    const bool water = input.scene == FrameSceneMode::WaterInspection;

    FrameView result;
    result.scene = input.scene;
    result.benchmark = input.benchmark;

    result.camera.position = input.selected_camera.position;
    result.camera.forward = input.selected_camera.forward;
    result.camera.field_of_view = cinematic
                                    ? input.selected_camera.field_of_view
                                  : water ? 70.0f * u::deg
                                  : input.scene == FrameSceneMode::Gazetteer
                                    ? input.selected_camera.field_of_view
                                    : gameplay_field_of_view (input);
    result.camera.aspect = std::max (0.01f, input.aspect);
    result.camera.view = input.selected_camera.view;

    // Hard landings rotate only the reading of the riding camera.  The camera
    // body stays where simulation placed it, and cinematics/inspection views
    // are explicitly stable reference views.
    if (input.scene == FrameSceneMode::Gameplay && logic.m_shake > 0.005f) {
      const Vec3& camera = result.camera.position;
      const float ground = terrain::surface_elevation_value (
        spatial::sample<terrain::surface_elevation> (
          input.surface, moppe::position (Vec3 (camera[0], 0.0f, camera[2]))));
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

    result.camera.right = Vec3 (result.camera.view.element (0),
                                result.camera.view.element (4),
                                result.camera.view.element (8));
    result.camera.up = Vec3 (result.camera.view.element (1),
                             result.camera.view.element (5),
                             result.camera.view.element (9));
    result.camera.frame_forward = Vec3 (-result.camera.view.element (2),
                                        -result.camera.view.element (6),
                                        -result.camera.view.element (10));

    result.camera.projection = Mat4::perspective_reversed (
      result.camera.field_of_view, result.camera.aspect, 0.5f, 9000.0f);

    result.lighting.fog_color = logic.m_fog;
    result.lighting.clear_color = logic.m_fog;
    const Weather& weather = current_weather ();
    result.lighting.fog_scale = input.world.fog_scale * weather.fog_density;
    result.lighting.mist = weather.mist;
    result.lighting.rain = weather.rain;
    const SkyReading sky =
      input.sky ? *input.sky : Almanac ().held (input.graphics.sun_height);
    result.lighting.sun_direction = sky.light;
    result.lighting.sun_specular =
      scale_display (sky.light_specular, 0.5f * weather.sunlight);
    result.lighting.sun_diffuse =
      scale_display (sky.light_diffuse, weather.sunlight);
    result.lighting.ambient = scale_display (sky.ambient, weather.skylight);
    result.lighting.sky_sun = sky.sun;
    result.lighting.sky_moon = sky.moon;
    result.lighting.sky_pole = sky.pole;
    result.lighting.sky_turn = sky.turn;
    result.lighting.moonlight = sky.moonlight;
    result.lighting.daylight = sky.daylight;
    result.lighting.time = static_cast<float> (logic.m_total_time);
    // The campfire lights what stands round it, most of all at night.
    if (const float burn = fire_burn (logic.m_camp, logic.m_total_time);
        burn > 0.01f) {
      result.lighting.lamp_position = fire_light_position (logic.m_camp);
      result.lighting.lamp_reach = fire_light_reach;
      result.lighting.lamp_color =
        scale_display (fire_light_color (burn, result.lighting.time),
                       1.0f - 0.25f * sky.daylight);
    }
    result.lighting.sun_height = sky.sun_height;
    result.lighting.cloudiness = logic.m_cloudiness;
    result.lighting.sun_visibility = logic.m_flare;

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
      .scene_megapixel_budget = input.graphics.scene_megapixel_budget,
      .target_frame_rate = input.graphics.target_frame_rate,
      .bloom = input.graphics.bloom,
      .auto_exposure = input.graphics.auto_exposure,
      .lens_flare = input.graphics.lens_flare,
    };

    result.actors.bike = vehicle_pose (input.session.bike ());
    result.actors.mountain_bike =
      vehicle_pose (input.session.mountain_bike ());
    result.actors.jeep = jeep_pose (input.session.jeep ());
    result.actors.helicopter = helicopter_pose (input.session.helicopter ());
    result.actors.tractor = tractor_pose (input.session.tractor ());
    result.actors.boat = boat_pose (input.session.boat ());
    result.actors.caddy = caddy_pose (input.session.caddy ());
    if (logic.m_sling.hooked) {
      const Machine load = logic.m_sling.load;
      const MachineBody body = input.session.machine_body (load);
      SlingPose sling;
      sling.hook = input.session.helicopter ().point (sling_hook ());
      sling.ring = body.point (sling_ring (load));
      const std::array<Vec3, 4> legs = sling_legs (load);
      for (std::size_t i = 0; i < legs.size (); ++i)
        sling.legs[i] = body.point (legs[i]);
      sling.taut = logic.m_sling.stretch > -0.05f;
      result.actors.sling = sling;
    }
    if (logic.m_mode == M_GLIDER)
      result.actors.glider = glider_pose (input.session.glider ());
    if (logic.m_mode == M_FOOT)
      result.actors.walker =
        walker_pose (input.session.walker (), logic.m_look_pitch);
    if (result.actors.walker && input.session.hauling ())
      result.actors.walker->rope_end = input.session.rope_end ();
    if (result.actors.walker)
      result.actors.walker->rod = rod_pose (logic.m_fishing.line,
                                            logic.m_total_time,
                                            result.lighting.time);
    if (logic.m_mode == M_BOAT)
      result.actors.helm_rod = rod_pose (
        logic.m_fishing.line, logic.m_total_time, result.lighting.time);
    result.actors.active_mode = logic.m_mode;
    result.actors.camera_mode = logic.m_cam_mode;
    result.actors.helmet_camera =
      logic.m_cam_mode == CAM_HELMET && !input.reveal_player;

    FrameHud& hud = result.hud;
    hud.speed_kmh = input.session.subject_speed_kmh ();
    if (logic.m_mode == M_GLIDER) {
      const float lift =
        input.session.glider ().air_mass_lift ().numerical_value_in (u::m /
                                                                     u::s);
      hud.boost_ready01 = std::clamp (lift / 4.0f, 0.0f, 1.0f);
      hud.vertical_speed_mps =
        input.session.glider ().vertical_speed ().numerical_value_in (u::m /
                                                                      u::s);
    } else {
      hud.boost_ready01 =
        logic.m_mode == M_BIKE ? input.session.bike ().boost_charge () : 1.0f;
    }
    hud.health01 = logic.m_health / 100.0f;
    hud.odometer_m = static_cast<float> (logic.m_odometer);
    hud.lives = logic.m_lives;
    hud.stars = input.session.stars ().collected ();
    hud.score = logic.m_score;
    hud.airtime_s = logic.m_jump_airtime;
    hud.spin_degrees = logic.m_jump_peak_spin_radians * 180.0f / 3.14159265f;
    hud.landed_airtime_s = logic.m_landed_airtime;
    hud.landed_spin_degrees = logic.m_landed_spin_degrees;
    hud.landed_points = logic.m_landed_points;
    hud.landed_clean = logic.m_landed_clean;
    hud.landed_age_s = logic.m_landed_age;
    hud.on_foot = logic.m_mode == M_FOOT;
    hud.gliding = logic.m_mode == M_GLIDER;
    hud.can_deploy_glider = input.session.can_deploy_glider (input.surface);
    hud.can_drop_bike = input.session.can_drop_bike ();
    // The remount reach of GameSession's toggle_mount, five metres.
    hud.can_mount = logic.m_mode == M_FOOT &&
                    length2 (input.session.walker ().position () -
                             input.session.bike ().position ()) < 5.0f * 5.0f;
    hud.can_drive = input.session.can_drive ();
    hud.can_fly = input.session.can_fly ();
    hud.can_drive_tractor = input.session.can_drive_tractor ();
    hud.can_drive_caddy = input.session.can_drive_caddy ();
    hud.can_ride_mountain_bike = input.session.can_ride_mountain_bike ();
    hud.can_take_boat = input.session.can_take_boat ();
    if (const auto stuck = input.session.haul_within_reach ())
      hud.haul = machine_name (*stuck);
    if (const auto held = input.session.hauling ())
      hud.hauling = machine_name (*held);
    hud.stuck = input.session.stuck ();
    hud.frame_time_s = logic.m_frame_time;
    hud.subject_position = input.session.subject_position ();
    hud.subject_heading = input.session.subject_heading ();
    hud.heading_radians =
      std::atan2 (hud.subject_heading[0], hud.subject_heading[2]);

    result.motion_blur_amount =
      cinematic ? input.cinematic_motion_blur
                : motion_blur_for (logic.m_mode,
                                   input.session.subject_speed_kmh ());

    if (cinematic) {
      result.overlay.cinematic_prompt_alpha = std::clamp (
        1.0f - std::max (0.0f, input.cinematic_elapsed - 4.0f) / 2.0f,
        0.0f,
        0.72f);
    }

    const float fog = attenuation_value (input.world.fog_scale);
    result.terrain_distance =
      fog > 0.0f ? 3.0f / fog : std::numeric_limits<float>::infinity ();
    result.visibility = visibility_for (input, result);
    return result;
  }

  float motion_blur_for (Mode mode, float kmh) {
    // From the speed it begins at to the speed at which it is whole, and
    // how long it is then.
    struct Ramp {
      float from, to, share;
    };
    Ramp ramp {};
    switch (mode) {
    case M_BOAT:
      // It planes at some 26 km/h, flat out on still water.
      ramp = { 15.0f, 26.0f, 1.0f };
      break;
    case M_BIKE:
      // 60 to 105 km/h unboosted; a boost carries it to 150.
      ramp = { 110.0f, 150.0f, 0.8f };
      break;
    case M_MOUNTAIN_BIKE:
      ramp = { 40.0f, 70.0f, 0.6f };
      break;
    case M_JEEP:
    case M_CADDY:
    case M_PASSENGER:
      ramp = { 80.0f, 140.0f, 0.6f };
      break;
    case M_FOOT:
    case M_GLIDER:
    case M_HELICOPTER:
    case M_TRACTOR:
      return 0.0f;
    }
    return ramp.share *
           std::clamp ((kmh - ramp.from) / (ramp.to - ramp.from), 0.0f, 1.0f);
  }

  float sun_visibility_target (const FrameView& view,
                               const WorldParams& world,
                               const map::SurfaceGeometry& surface) {
    const Vec3& camera = view.camera.position;
    float visibility = 1.0f;
    if (camera[1] < (world.water_level).numerical_value_in (moppe::u::m))
      visibility = 0.0f;
    else {
      for (int i = 1; i <= 40; ++i) {
        const float distance = 90.0f * i;
        const Vec3 sample = camera + view.lighting.sun_direction * distance;
        if (!(std::isfinite (sample[0]) && std::isfinite (sample[2])))
          break;
        if (terrain::surface_elevation_value (
              spatial::sample<terrain::surface_elevation> (
                surface, moppe::position (Vec3 (sample[0], 0.0f, sample[2])))) >
            sample[1]) {
          visibility = 0.0f;
          break;
        }
      }
    }
    // The moon dazzles no one.
    return visibility * view.lighting.daylight *
           (1.0f - 0.65f * view.lighting.cloudiness.numerical_value_in (one));
  }
}
