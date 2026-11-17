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

    // The sine of the sun's elevation, which the light's art direction
    // reads.
    float sun_elevation_for (proportion_t sun_height) {
      return std::sin ((sun_height.numerical_value_in (one) - 0.5f) * ART_PI);
    }

    float daylight_for (proportion_t sun_height) {
      return daylight_for_elevation (sun_elevation_for (sun_height))
        .numerical_value_in (one);
    }

    float golden_light_for (proportion_t sun_height) {
      return golden_light_for_elevation (sun_elevation_for (sun_height))
        .numerical_value_in (one);
    }

    FrameVisibility visibility_for (const FrameViewInput& input,
                                    const FrameView& view) {
      const bool cinematic = input.scene == FrameSceneMode::Cinematic;

      FrameVisibility visibility;
      visibility.cinematic = cinematic;
      visibility.undergrowth = input.graphics.undergrowth;
      visibility.ocean = input.graphics.ocean;
      visibility.underwater =
        visibility.ocean && view.camera_water_level &&
        position_value (view.camera.position)[1] <
          terrain::surface_elevation_value (*view.camera_water_level);
      visibility.motion_blur =
        input.graphics.motion_blur &&
        view.motion_blur_amount > 0.01f * proportion[one];
      visibility.vehicle_effects = input.graphics.vehicle_effects;
      visibility.star_effects = input.graphics.star_effects;
      visibility.game_hud = !cinematic;
      visibility.cinematic_hud = cinematic;
      return visibility;
    }

    degrees_t gameplay_field_of_view (const FrameViewInput& input) {
      const GameLogicState& logic = input.session.logic ();
      if (input.capture_field_of_view)
        return *input.capture_field_of_view;
      if (logic.m_cam_mode == CAM_HELMET)
        return input.selected_camera.field_of_view;
      return CHASE_FIELD_OF_VIEW +
             CHASE_SPEED_WIDENING * logic.m_fov_k.numerical_value_in (one);
    }
  }

  direction_t sun_direction_for (proportion_t sun_height) {
    return authored_sun_direction (sun_height);
  }

  void sun_light_colors_for (proportion_t sun_height,
                             DisplayColor& diffuse,
                             DisplayColor& specular) {
    const float daylight = daylight_for (sun_height);
    const float golden = golden_light_for (sun_height);
    const DisplayColor warm (1.0f, 0.60f, 0.30f);
    const DisplayColor ivory (1.0f, 0.96f, 0.84f);
    const DisplayColor sun_color =
      mix_display (ivory, warm, golden * blend_weight[one]);

    diffuse =
      scale_display (sun_color, (0.10f + 0.98f * daylight) * scale_factor[one]);
    specular = scale_display (mix_display (DisplayColor (0.92f, 0.95f, 1.0f),
                                           DisplayColor (1.0f, 0.86f, 0.70f),
                                           golden * blend_weight[one]),
                              (0.15f + 0.85f * daylight) * scale_factor[one]);
  }

  DisplayColor horizon_color_for (proportion_t sun_height) {
    const float daylight = daylight_for (sun_height);
    const float warmth = golden_light_for (sun_height) * 0.16f;
    const DisplayColor day_horizon (0.55f, 0.68f, 0.84f);
    const DisplayColor night_horizon (0.035f, 0.045f, 0.09f);
    const DisplayColor warm_horizon (0.92f, 0.58f, 0.32f);
    return mix_display (
      mix_display (night_horizon, day_horizon, daylight * blend_weight[one]),
      warm_horizon,
      warmth * blend_weight[one]);
  }

  VehiclePose pose (const mov::Vehicle& vehicle) {
    return {
      .position = vehicle.render_position (),
      .render_orientation = vehicle.render_orientation (),
      .render_normal = vehicle.render_normal (),
      .rear_wheel_drop = vehicle.rear_wheel_drop (),
      .front_wheel_drop = vehicle.front_wheel_drop (),
      .wheel_spin = vehicle.wheel_spin (),
      .fork = vehicle.fork_angle (),
      .thrust = vehicle.thrust (),
      .boost_level = vehicle.boost_level (),
      .boost_drive = quantity_cast<proportion> (vehicle.boost_drive ()),
      .crank = vehicle.crank (),
      .rider_lean = vehicle.rider_lean (),
    };
  }

  CarPose pose (const mov::Car& car) {
    return {
      .position = car.position (),
      .forward = car.forward (),
      .up = car.up (),
      .wheel_drop = { car.wheel_drop (0),
                      car.wheel_drop (1),
                      car.wheel_drop (2),
                      car.wheel_drop (3) },
      .steer = car.steer (),
      .wheel_spin = car.wheel_spin (),
      .velocity = car.velocity (),
    };
  }

  BoatPose pose (const mov::Boat& boat) {
    return {
      .position = boat.position (),
      .forward = boat.forward (),
      .up = boat.up (),
      .swing = boat.swing (),
      .tilt = boat.tilt (),
      .propeller = boat.propeller_angle (),
      .rpm = boat.rpm (),
      .velocity = boat.velocity (),
    };
  }

  JetSkiPose pose (const mov::JetSki& ski) {
    return {
      .position = ski.position (),
      .forward = ski.forward (),
      .up = ski.up (),
      .steer = ski.steer (),
      .lean = ski.lean (),
      .rpm = ski.rpm (),
      .velocity = ski.velocity (),
    };
  }

  TractorPose pose (const mov::Tractor& tractor) {
    return {
      .position = tractor.position (),
      .forward = tractor.forward (),
      .up = tractor.up (),
      .wheel_drop = { tractor.wheel_drop (0),
                      tractor.wheel_drop (1),
                      tractor.wheel_drop (2),
                      tractor.wheel_drop (3) },
      .steer = tractor.steer (),
      .front_spin = tractor.front_spin (),
      .rear_spin = tractor.rear_spin (),
      .trailer_position = position (tractor.trailer_position ()),
      .trailer_forward = unit_direction (tractor.trailer_forward ()),
      .trailer_up = unit_direction (tractor.trailer_up ()),
      .trailer_wheel_drop = { tractor.trailer_wheel_drop (0),
                              tractor.trailer_wheel_drop (1) },
      .trailer_spin = tractor.trailer_spin (),
    };
  }

  HelicopterPose pose (const mov::Helicopter& helicopter) {
    return {
      .position = helicopter.position (),
      .forward = helicopter.forward (),
      .up = helicopter.up (),
      .rotor = helicopter.rotor_angle (),
      .tail = helicopter.tail_angle (),
    };
  }

  FleetPoses poses (const Fleet& fleet) {
    FleetPoses result;
    for_each_machine_across (
      [] (Machine, auto& out, const auto& body) { out = pose (body); },
      result,
      fleet);
    return result;
  }

  GliderPose glider_pose (const mov::Glider& glider) {
    return {
      .position = glider.position (),
      .heading = unit_direction (glider.nose ()),
      .bank = glider.bank (),
      .bike_attached = glider.bike_attached (),
    };
  }

  WalkerPose walker_pose (const Walker& walker, inclination_t look_pitch) {
    const Walker::State state = walker.state ();
    const mov::Character::State& body = state.body;
    const Vec3 velocity = velocity_value (body.velocity);
    return {
      .position = position (position_value (body.position) +
                            Vec3 (0, meters_value (body.step_offset), 0)),
      .heading = state.facing,
      .look_aside = state.lag,
      .turn_speed = state.turn_speed,
      .velocity = body.velocity,
      .ground_normal = body.ground_normal,
      .stride_phase = state.stride_phase,
      .stride_speed = state.stride_speed,
      .grounded = body.grounded,
      .airborne = body.airborne_time,
      .vertical_speed = velocity[1] * u::m / u::s,
      .landing_dip = walker.landing_dip (),
      .look_pitch = look_pitch,
    };
  }

  FrameView compose_frame_view (const FrameViewInput& input) {
    const GameLogicState& logic = input.session.logic ();
    const bool cinematic = input.scene == FrameSceneMode::Cinematic;

    FrameView result;
    result.scene = input.scene;

    result.camera.position = input.selected_camera.position;
    result.camera.forward = input.selected_camera.forward;
    result.camera.field_of_view = cinematic
                                    ? input.selected_camera.field_of_view
                                    : gameplay_field_of_view (input);
    result.camera.aspect = std::max (0.01f * aspect_ratio[one], input.aspect);
    result.camera.view = input.selected_camera.view;

    // Hard landings rotate only the reading of the riding camera.  The camera
    // body stays where simulation placed it, and cinematics/inspection views
    // are explicitly stable reference views.
    if (input.scene == FrameSceneMode::Gameplay &&
        logic.m_shake > 0.005f * u::deg) {
      const position_t& camera = result.camera.position;
      const meters_t ground =
        terrain::surface_elevation_value (
          spatial::sample<terrain::surface_elevation> (
            input.surface, at_height (plan (camera), 0.0f * u::m))) *
        u::m;
      const meters_t clearance = position_value (camera)[1] * u::m - ground;
      const proportion_t room =
        std::min (
          1.0f,
          std::max (0.0f, meters_value (clearance - 2.0f * u::m) / 8.0f)) *
        proportion[one];
      // Degrees of roll and pitch the landing shakes the view by.
      const float pulse = logic.m_shake.numerical_value_in (u::deg) *
                          room.numerical_value_in (one);
      const float shaken = seconds_value (logic.m_shake_time);
      const degrees_t roll =
        pulse * std::sin (2.0f * PI * 15.0f * shaken) * u::deg;
      const degrees_t pitch =
        pulse * 0.55f * std::sin (2.0f * PI * 19.0f * shaken + 0.7f) * u::deg;
      result.camera.view = result.camera.view *
                           Mat4::rotation (roll, Vec3 (0, 0, 1)) *
                           Mat4::rotation (pitch, Vec3 (1, 0, 0));
    }

    const Mat4& view = result.camera.view;
    result.camera.right = unit_direction (
      Vec3 (view.element (0), view.element (4), view.element (8)));
    result.camera.up = unit_direction (
      Vec3 (view.element (1), view.element (5), view.element (9)));
    result.camera.frame_forward = unit_direction (
      Vec3 (-view.element (2), -view.element (6), -view.element (10)));

    result.camera.projection =
      Mat4::perspective_reversed (result.camera.field_of_view,
                                  result.camera.aspect.numerical_value_in (one),
                                  render::camera_near_m,
                                  render::camera_far_m);

    const Weather& weather = logic.m_weather;
    // Lightning lights the cloud and the haze, and everything under it.
    const float flash = lightning_flash (weather.lightning, logic.m_total_time)
                          .numerical_value_in (one);
    const DisplayColor flashed_fog =
      mix_display (logic.m_fog,
                   DisplayColor (0.62f, 0.64f, 0.76f),
                   0.75f * flash * blend_weight[one]);
    FrameLighting& lighting = result.lighting;
    lighting.fog_color = flashed_fog;
    lighting.clear_color = flashed_fog;
    lighting.fog_scale =
      input.world.fog_scale * weather.fog_density.numerical_value_in (one);
    lighting.mist = weather.mist;
    lighting.rain = weather.rain;
    const SkyReading sky =
      input.sky ? *input.sky : Almanac ().held (input.graphics.sun_height);
    lighting.sun_direction = sky.light;
    lighting.sun_specular =
      scale_display (sky.light_specular, 0.5f * weather.sunlight);
    lighting.sun_diffuse = scale_display (sky.light_diffuse, weather.sunlight);
    lighting.ambient =
      mix_display (scale_display (sky.ambient, weather.skylight),
                   DisplayColor (0.85f, 0.88f, 1.05f),
                   0.7f * flash * blend_weight[one]);
    lighting.sky_sun = sky.sun;
    lighting.sky_moon = sky.moon;
    lighting.sky_pole = sky.pole;
    lighting.sky_turn = sky.turn;
    lighting.moonlight = sky.moonlight;
    lighting.daylight = sky.daylight;
    const float time =
      static_cast<float> (seconds_into_play (logic.m_total_time));
    lighting.time = time * u::s;
    // The campfire lights what stands round it, most of all at night.
    if (const proportion_t burn = fire_burn (logic.m_camp, logic.m_total_time);
        burn > 0.01f * proportion[one]) {
      lighting.lamp_position = fire_light_position (logic.m_camp);
      lighting.lamp_reach = fire_light_reach;
      lighting.lamp_color =
        scale_display (fire_light_color (burn, time * u::s),
                       (1.0f - 0.25f * sky.daylight.numerical_value_in (one)) *
                         scale_factor[one]);
    }
    lighting.sun_height = sky.sun_height;
    lighting.cloudiness = logic.m_cloudiness;
    lighting.sun_visibility = logic.m_flare;

    result.environment.fog_color = lighting.fog_color;
    result.environment.fog_scale = lighting.fog_scale;
    result.environment.sun_dir = lighting.sun_direction;
    result.environment.camera_pos = result.camera.position;
    result.environment.cam_right = result.camera.right;
    result.environment.cam_up = result.camera.up;
    result.environment.cam_forward = result.camera.frame_forward;
    result.environment.time = lighting.time;

    // Dry sheet cells sit at or below the ground; they must not fall back
    // to the sea. Waves use the same time as the frame being drawn.
    const position_t where =
      at_height (plan (result.camera.position), 0.0f * u::m);
    const terrain::SurfaceElevation ground =
      spatial::sample<terrain::surface_elevation> (input.surface, where);
    const float still_water =
      input.water
        ? terrain::surface_elevation_value (
            spatial::sample<terrain::surface_elevation> (*input.water, where))
        : input.world.water_level.numerical_value_in (u::m);
    const float local_water =
      still_water +
      meters_value (water_waves_at (
                      input.surface, input.water, result.camera.position, time)
                      .height);
    if (local_water > terrain::surface_elevation_value (ground))
      result.camera_water_level =
        terrain::surface_elevation_point (local_water * u::m);

    result.graphics = {
      .scene_scale = input.graphics.scene_scale,
      .render_scale_override = input.graphics.render_scale_override,
      .scene_megapixel_budget = input.graphics.scene_megapixel_budget,
      .target_frame_rate = input.graphics.target_frame_rate,
      .bloom = input.graphics.bloom,
      .auto_exposure = input.graphics.auto_exposure,
      .lens_flare = input.graphics.lens_flare,
    };

    result.actors.fleet = poses (input.session.fleet ());
    if (logic.m_sling.hooked) {
      const Machine load = logic.m_sling.load;
      const MachineBody body = input.session.machine_body (load);
      SlingPose sling;
      sling.hook = input.session.helicopter ().point (sling_hook ());
      sling.ring = body.point (machine_kind (load).ring);
      const std::array<displacement_t, 4> legs = machine_kind (load).legs;
      for (std::size_t i = 0; i < legs.size (); ++i)
        sling.legs[i] = body.point (legs[i]);
      sling.taut = logic.m_sling.stretch > -0.05f * u::m;
      result.actors.sling = sling;
    }
    if (logic.m_doing == Doing::gliding ())
      result.actors.glider = glider_pose (input.session.glider ());
    if (logic.m_doing == Doing::on_foot ())
      result.actors.walker =
        walker_pose (input.session.walker (), logic.m_look_pitch);
    if (result.actors.walker && input.session.hauling ())
      result.actors.walker->rope_end = input.session.rope_end ();
    if (result.actors.walker)
      result.actors.walker->rod =
        rod_pose (logic.m_fishing.line, logic.m_total_time, lighting.time);
    if (logic.m_doing == Doing::driving (Machine::boat))
      result.actors.helm_rod =
        rod_pose (logic.m_fishing.line, logic.m_total_time, lighting.time);
    result.actors.active_mode = logic.m_doing;
    result.actors.camera_mode = logic.m_cam_mode;
    result.actors.helmet_camera =
      logic.m_cam_mode == CAM_HELMET && !input.reveal_player;

    const kilometres_per_hour_t speed = input.session.subject_speed_kmh ();
    FrameHud& hud = result.hud;
    hud.speed = speed;
    if (logic.m_doing == Doing::gliding ()) {
      const float lift =
        input.session.glider ().air_mass_lift ().numerical_value_in (u::m /
                                                                     u::s);
      hud.boost_ready = std::clamp (lift / 4.0f, 0.0f, 1.0f) * proportion[one];
      hud.vertical_speed = input.session.glider ().vertical_speed ();
    } else {
      hud.boost_ready = logic.m_doing == Doing::driving (Machine::bike)
                          ? input.session.bike ().boost_charge ()
                          : 1.0f * proportion[one];
    }
    hud.health =
      logic.m_health.numerical_value_in (one) / 100.0f * proportion[one];
    hud.odometer =
      static_cast<float> (logic.m_odometer.numerical_value_in (u::m)) * u::m;
    hud.lives = logic.m_lives;
    hud.stars = input.session.stars ().collected ();
    hud.score = logic.m_score;
    hud.airtime = logic.m_jump_airtime;
    hud.spin =
      radians_value (logic.m_jump_peak_spin) * 180.0f / 3.14159265f * u::deg;
    hud.landed_airtime = logic.m_landed_airtime;
    hud.landed_spin = logic.m_landed_spin;
    hud.landed_points = logic.m_landed_points;
    hud.landed_clean = logic.m_landed_clean;
    hud.landed_age = logic.m_landed_age;
    hud.wheelie = logic.m_wheelie_m;
    hud.last_wheelie = logic.m_last_wheelie_m;
    hud.last_wheelie_points = logic.m_last_wheelie_points;
    hud.best_wheelie = logic.m_best_wheelie_m;
    hud.wheelie_age = logic.m_wheelie_age;
    hud.on_foot = logic.m_doing == Doing::on_foot ();
    hud.gliding = logic.m_doing == Doing::gliding ();
    hud.frame_time = logic.m_frame_time;
    hud.subject_position = input.session.subject_position ();
    hud.subject_heading = input.session.subject_heading ();
    const Vec3& facing = direction_value (hud.subject_heading);
    hud.heading = std::atan2 (facing[0], facing[2]) * moppe::heading[u::rad];

    result.motion_blur_amount = cinematic
                                  ? input.cinematic_motion_blur
                                  : motion_blur_for (logic.m_doing, speed);

    if (cinematic) {
      result.overlay.cinematic_prompt_alpha =
        std::clamp (
          1.0f -
            std::max (0.0f, seconds_value (input.cinematic_elapsed) - 4.0f) /
              2.0f,
          0.0f,
          0.72f) *
        proportion[one];
    }

    const float fog = attenuation_value (input.world.fog_scale);
    result.terrain_distance =
      (fog > 0.0f ? 3.0f / fog : std::numeric_limits<float>::infinity ()) *
      u::m;
    result.visibility = visibility_for (input, result);
    return result;
  }

  proportion_t motion_blur_for (Doing mode, kilometres_per_hour_t speed) {
    const float kmh = speed.numerical_value_in (si::kilo<si::metre> / si::hour);
    // From the speed it begins at to the speed at which it is whole, and
    // how long it is then.
    struct Ramp {
      float from, to, share;
    };
    Ramp ramp {};
    switch (mode.way) {
    case Doing::Way::foot:
    case Doing::Way::glider:
      return 0.0f * proportion[one];
    case Doing::Way::passenger:
    case Doing::Way::bus:
      ramp = { 80.0f, 140.0f, 0.6f };
      break;
    case Doing::Way::driving:
      switch (mode.machine) {
      case Machine::boat:
        // It planes at some 26 km/h, flat out on still water.
        ramp = { 15.0f, 26.0f, 1.0f };
        break;
      case Machine::jet_ski:
        // Some 60 km/h flat out on still water, low over it.
        ramp = { 35.0f, 60.0f, 0.9f };
        break;
      case Machine::bike:
        // 60 to 105 km/h unboosted; a boost carries it to 150.
        ramp = { 110.0f, 150.0f, 0.8f };
        break;
      case Machine::mountain_bike:
      case Machine::long_john:
        ramp = { 40.0f, 70.0f, 0.6f };
        break;
      case Machine::jeep:
      case Machine::caddy:
        ramp = { 80.0f, 140.0f, 0.6f };
        break;
      case Machine::helicopter:
      case Machine::tractor:
        return 0.0f * proportion[one];
      }
      break;
    }
    return ramp.share *
           std::clamp ((kmh - ramp.from) / (ramp.to - ramp.from), 0.0f, 1.0f) *
           proportion[one];
  }

  proportion_t sun_visibility_target (const FrameView& view,
                                      const WorldParams&,
                                      const map::SurfaceGeometry& surface) {
    const Vec3& camera = position_value (view.camera.position);
    const Vec3& sun = direction_value (view.lighting.sun_direction);
    float visibility = 1.0f;
    if (view.visibility.underwater)
      visibility = 0.0f;
    else {
      for (int i = 1; i <= 40; ++i) {
        const float distance = 90.0f * i;
        const Vec3 sample = camera + sun * distance;
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
    return visibility * view.lighting.daylight.numerical_value_in (one) *
           (1.0f - 0.65f * view.lighting.cloudiness.numerical_value_in (one)) *
           proportion[one];
  }
}
