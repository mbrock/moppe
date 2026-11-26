#ifndef MOPPE_GAME_FRAME_VIEW_HH
#define MOPPE_GAME_FRAME_VIEW_HH

#include <moppe/color.hh>
#include <moppe/game/daylight.hh>
#include <moppe/game/fishing_state.hh>
#include <moppe/game/game_session.hh>
#include <moppe/game/graphics_settings.hh>
#include <moppe/gfx/mat4.hh>
#include <moppe/terrain/water_view.hh>
#include <moppe/terrain/watercourse.hh>

#include <array>
#include <cstdint>
#include <optional>

namespace moppe::game {
  // The one selected presentation mode for a finished world frame. These are
  // deliberately concrete application modes, not a generic scene hierarchy.
  enum class FrameSceneMode { Gameplay, Cinematic };

  // The following cameras' vertical field of view, at rest; speed widens
  // it a little.
  inline constexpr degrees_t CHASE_FIELD_OF_VIEW = 78.0f * u::deg;

  // A camera has already been selected by the application before composing a
  // frame.  Keeping its view matrix here preserves cinematics' banked camera
  // while still letting FrameView apply riding-only shake afterward.
  struct FrameCameraReading {
    position_t position {};
    direction_t forward = unit_direction (Vec3 (0, 0, 1));
    Mat4 view {};
    degrees_t field_of_view = 70.0f * u::deg;
  };

  // Renderer-facing snapshots of the parts of a vehicle that actually affect
  // its visible pose.  They intentionally omit controls and physical state.
  struct VehiclePose {
    // The drawn frame's origin, about a metre above the ground at rest.
    position_t position {};
    direction_t render_orientation = unit_direction (Vec3 (0, 0, 1));
    direction_t render_normal = unit_direction (Vec3 (0, 1, 0));
    // How far each wheel hangs below its rest position along the frame's
    // down axis; negative is compressed.
    meters_t rear_wheel_drop = 0.0f * u::m;
    meters_t front_wheel_drop = 0.0f * u::m;
    radians_t lean = 0.0f * u::rad;
    radians_t wheel_spin = 0.0f * u::rad;
    // Fork angle about the steering head; positive turns right.
    radians_t fork = 0.0f * u::rad;
    control_signal_t thrust = 0.0f * control_signal[one];
    // How full the jets' charge is, 0..1, and how they are tilted by the
    // drive, -1 (reverse) to 1 (ahead).
    proportion_t boost_level = 0.0f * proportion[one];
    control_signal_t boost_drive = 0.0f * control_signal[one];
    // Drawn at this share of its size.
    scale_factor_t scale = 1.0f * scale_factor[one];
    // A pedalled bike's crank, round from the right arm down.
    radians_t crank = 0.0f * u::rad;
    // How far back the rider leans for a wheelie, -1..1 of the way
    // (negative forward; mov::Vehicle::rider_lean).
    control_signal_t rider_lean = 0.0f * control_signal[one];
  };

  // A car (the jeep, the Caddy) as drawn: its origin (midway between the axles,
  // at their standing height) and frame, how far each wheel hangs below where
  // it stands (front left, front right, rear left, rear right; negative is
  // pressed), the front wheels' turn to the right, and the wheels' roll.
  struct CarPose {
    position_t position {};
    direction_t forward = unit_direction (Vec3 (0, 0, 1));
    direction_t up = unit_direction (Vec3 (0, 1, 0));
    std::array<meters_t, 4> wheel_drop {};
    radians_t steer = 0.0f * u::rad;
    radians_t wheel_spin = 0.0f * u::rad;
    // Drawn at this share of its size.
    scale_factor_t scale = 1.0f * scale_factor[one];
    // How it moves: someone riding along moves with it.
    velocity_t velocity {};
  };

  // The helicopter's line and its load's bridle as drawn
  // (game/sling.hh): the hook, the ring the legs meet at, where each leg
  // is made fast, and whether the line is taut.
  struct SlingPose {
    position_t hook;
    position_t ring;
    std::array<position_t, 4> legs {};
    bool taut = false;
  };

  // The helicopter as drawn: its origin (under the mast, level with the
  // skids' bottoms) and frame, and how far round each rotor has turned.
  struct HelicopterPose {
    position_t position {};
    direction_t forward = unit_direction (Vec3 (0, 0, 1));
    direction_t up = unit_direction (Vec3 (0, 1, 0));
    radians_t rotor = 0.0f * u::rad;
    radians_t tail = 0.0f * u::rad;
  };

  // The tractor as drawn: its origin (midway between the axles, on the
  // ground it stands on) and frame, how far each wheel hangs below where
  // it stands (front left, front right, rear left, rear right; negative
  // is pressed), the front wheels' turn to the right, and how far round
  // each pair has rolled; and its trailer's origin (on the ground under
  // its axle), frame, wheels' drop (left, right), and roll.
  struct TractorPose {
    position_t position {};
    direction_t forward = unit_direction (Vec3 (0, 0, 1));
    direction_t up = unit_direction (Vec3 (0, 1, 0));
    std::array<meters_t, 4> wheel_drop {};
    radians_t steer = 0.0f * u::rad;
    radians_t front_spin = 0.0f * u::rad;
    radians_t rear_spin = 0.0f * u::rad;
    position_t trailer_position = moppe::position (Vec3 (0, 0, -3.72f));
    direction_t trailer_forward = unit_direction (Vec3 (0, 0, 1));
    direction_t trailer_up = unit_direction (Vec3 (0, 1, 0));
    std::array<meters_t, 2> trailer_wheel_drop {};
    radians_t trailer_spin = 0.0f * u::rad;
  };

  // The boat as drawn: its origin (on the keel a quarter of the way
  // along) and frame, how far its long-tail is swung and tipped up, how
  // far round the propeller has turned, and the engine's speed.
  struct BoatPose {
    position_t position {};
    direction_t forward = unit_direction (Vec3 (0, 0, 1));
    direction_t up = unit_direction (Vec3 (0, 1, 0));
    radians_t swing = 0.0f * u::rad;
    radians_t tilt = 0.0f * u::rad;
    radians_t propeller = 0.0f * u::rad;
    proportion_t rpm = 0.0f * proportion[one];
    // How it moves: someone riding along moves with it.
    velocity_t velocity {};
  };

  // The jet ski as drawn: its origin (on the keel under the front of
  // the saddle) and frame, how far its nozzle is swung (positive turns
  // the bow to the right), how far its rider leans (positive to the
  // right), and the engine's speed.
  struct JetSkiPose {
    position_t position {};
    direction_t forward = unit_direction (Vec3 (0, 0, 1));
    direction_t up = unit_direction (Vec3 (0, 1, 0));
    radians_t steer = 0.0f * u::rad;
    radians_t lean = 0.0f * u::rad;
    proportion_t rpm = 0.0f * proportion[one];
    // How it moves.
    velocity_t velocity {};
  };

  // What a frame shows of each kind of machine, and of the fleet
  // (game/fleet.hh).
  template <class Body>
  struct pose_of;
  template <>
  struct pose_of<mov::Vehicle> {
    using type = VehiclePose;
  };
  template <>
  struct pose_of<mov::Car> {
    using type = CarPose;
  };
  template <>
  struct pose_of<mov::Helicopter> {
    using type = HelicopterPose;
  };
  template <>
  struct pose_of<mov::Tractor> {
    using type = TractorPose;
  };
  template <>
  struct pose_of<mov::Boat> {
    using type = BoatPose;
  };
  template <>
  struct pose_of<mov::JetSki> {
    using type = JetSkiPose;
  };
  template <class Body>
  using PoseOf = typename pose_of<Body>::type;
  using FleetPoses = FleetOf<PoseOf>;

  struct GliderPose {
    position_t position {};
    direction_t heading = unit_direction (Vec3 (0, 0, 1));
    radians_t bank = 0.0f * u::rad;
    bool bike_attached = false;
  };

  // Everything the avatar's procedural animation reads: where the feet
  // are and how the body is moving over the ground, not its controls.
  struct WalkerPose {
    position_t position {}; // the feet, eased over steps
    // The way the body faces.
    direction_t heading = unit_direction (Vec3 (0, 0, 1));
    // How far round to the right the look is from the body, and how fast
    // the body turns after it.
    radians_t look_aside = 0.0f * u::rad;
    radians_per_second_t turn_speed = 0.0f * u::rad / u::s;
    velocity_t velocity {};
    direction_t ground_normal = unit_direction (Vec3 (0, 1, 0));
    // How far through the gait cycle; the left foot strikes at zero and
    // the right at one half.
    proportion_t stride_phase = 0.0f * proportion[one];
    speed_t stride_speed = 0.0f * u::m / u::s;
    bool grounded = true;
    bool swimming = false;
    seconds_t airborne = 0.0f * u::s;
    // Up positive.
    speed_t vertical_speed = 0.0f * u::m / u::s;
    meters_t landing_dip = 0.0f * u::m;
    inclination_t look_pitch = 0.0f * inclination[u::rad];
    // Hauling: where the rope in the right hand is tied.
    std::optional<position_t> rope_end;
    // Fishing: the rod in the right hand and its line.
    std::optional<RodPose> rod;
  };

  struct FrameActors {
    FleetPoses fleet {};
    // Fishing from the boat: the rod in the helmsman's right hand.
    std::optional<RodPose> helm_rod;
    // The bike and the jeep ride in the Caddy, its hardtop off and its
    // tailgate down, and the Caddy may hang on the helicopter's line.
    std::optional<SlingPose> sling;
    std::optional<GliderPose> glider;
    std::optional<WalkerPose> walker;
    Doing active_mode = Doing::driving (Machine::bike);
    CamMode camera_mode = CAM_CHASE;
    bool helmet_camera = false;
  };

  // Renderer-free readings for the compact driving HUD. They deliberately
  // mirror the HUD's value inputs without importing its DrawList dependency
  // into FrameView. The trail map needs the unprojected subject reading too.

  struct FrameHud {
    kilometres_per_hour_t speed = 0.0f * si::kilo<si::metre> / si::hour;
    proportion_t boost_ready = 1.0f * proportion[one];
    proportion_t health = 1.0f * proportion[one];
    meters_t odometer = 0.0f * u::m;
    int lives = 10;
    int stars = 0;
    int score = 0;
    seconds_t airtime = 0.0f * u::s;
    degrees_t spin = 0.0f * u::deg;
    seconds_t landed_airtime = 0.0f * u::s;
    degrees_t landed_spin = 0.0f * u::deg;
    int landed_points = 0;
    bool landed_clean = false;
    seconds_t landed_age = 10.0f * u::s;
    // GameLogicState's wheelie readings.
    meters_t wheelie = 0.0f * u::m;
    meters_t last_wheelie = 0.0f * u::m;
    int last_wheelie_points = 0;
    meters_t best_wheelie = 0.0f * u::m;
    seconds_t wheelie_age = 10.0f * u::s;
    bool on_foot = false;
    bool gliding = false;
    // Up positive.
    speed_t vertical_speed = 0.0f * u::m / u::s;
    seconds_t frame_time = (1.0f / 60.0f) * u::s;
    heading_t heading = 0.0f * moppe::heading[u::rad];
    position_t subject_position {};
    direction_t subject_heading = unit_direction (Vec3 (0, 0, 1));
  };

  struct FrameOverlay {
    proportion_t cinematic_prompt_alpha = 0.0f * proportion[one];
  };

  // These flags retain the existing render paragraphs' selection decisions.
  // Their values are plain frame data, so a later presenter need not query
  // mutable application mode while encoding commands.
  struct FrameVisibility {
    bool cinematic = false;
    bool sky_before_terrain = false;
    bool sky_after_terrain = true;
    bool undergrowth = true;
    bool ocean = true;
    bool underwater = false;
    bool motion_blur = false;
    bool vehicle_effects = true;
    bool star_effects = true;
    bool game_hud = true;
    bool cinematic_hud = false;
  };

  struct FrameCamera {
    // The selected camera body remains stable when shake rotates only its
    // reading.  `frame_*` is derived from the final shaken view matrix.
    position_t position {};
    direction_t forward = unit_direction (Vec3 (0, 0, 1));
    direction_t right = unit_direction (Vec3 (1, 0, 0));
    direction_t up = unit_direction (Vec3 (0, 1, 0));
    direction_t frame_forward = unit_direction (Vec3 (0, 0, 1));
    Mat4 view {};
    Mat4 projection {};
    meters_t far_plane = render::camera_far_m * u::m;
    degrees_t field_of_view = 70.0f * u::deg;
    aspect_ratio_t aspect = 1.0f * aspect_ratio[one];
  };

  struct FrameLighting {
    DisplayColor fog_color {};
    DisplayColor clear_color {};
    attenuation_t fog_scale = 0.0f / u::m;
    direction_t sun_direction {};
    DisplayColor sun_diffuse {};
    DisplayColor sun_specular {};
    DisplayColor ambient {};
    // How high the sun stands, as the art direction reads it: a half on
    // the horizon (game/daylight.hh).
    proportion_t sun_height = 0.62f * proportion[one];
    // Seconds into play, the clock everything animated reads.
    seconds_t time = 0.0f * u::s;
    cloud_cover_t cloudiness = 0.0f * cloud_cover[mp_units::one];
    // How much of the sun the camera sees.
    proportion_t sun_visibility = 0.0f * proportion[one];
    scale_factor_t exposure_bias = 1.0f * scale_factor[one];
    // The weather (game/weather.hh): mist lying in the valleys, and rain,
    // 1 a drizzle and up to 2.5 a downpour.
    proportion_t mist = 0.0f * proportion[one];
    scale_factor_t rain = 0.0f * scale_factor[one];
    // `sun_direction` is the key light, which at night is the moon; these
    // are the sky itself (game/daylight.hh).
    direction_t sky_sun = unit_direction (Vec3 (0, 1, 0));
    direction_t sky_moon = unit_direction (Vec3 (0, -1, 0));
    direction_t sky_pole = unit_direction (Vec3 (0, 1, 0));
    radians_t sky_turn = 0.0f * u::rad;
    proportion_t moonlight = 0.0f * proportion[one];
    proportion_t daylight = 1.0f * proportion[one];
    // A fire's light: where it burns, how far it reaches, and its colour
    // at full strength. No reach, no fire.
    position_t lamp_position {};
    meters_t lamp_reach = 0.0f * u::m;
    DisplayColor lamp_color {};
  };

  struct FrameGraphics {
    scale_factor_t scene_scale = 1.0f * scale_factor[one];
    scale_factor_t render_scale_override = 0.0f * scale_factor[one];
    // Millions of pixels; zero, no budget.
    float scene_megapixel_budget = 0.0f;
    frequency_t target_frame_rate = 0.0f * u::Hz;
    bool bloom = true;
    bool auto_exposure = true;
    bool lens_flare = true;
  };

  // The immutable reading consumed by a presentation frame. It has no
  // Renderer or platform type and contains no borrowed mutable actor state.
  struct FrameView {
    FrameSceneMode scene = FrameSceneMode::Gameplay;
    FrameCamera camera {};
    FrameLighting lighting {};
    // The existing game-side environment contract, materialized from the
    // final camera matrix so billboards observe camera shake consistently.
    FrameEnv environment {};
    FrameGraphics graphics {};
    FrameActors actors {};
    FrameHud hud {};
    FrameOverlay overlay {};
    FrameVisibility visibility {};
    meters_t terrain_distance = 0.0f * u::m;
    // How long the motion blur draws streaks, a share of a frame's motion.
    proportion_t motion_blur_amount = 0.0f * proportion[one];
    // Water at the final camera's x,z, including waves. Present above the
    // surface too, and when water is hidden; absent over dry ground. The
    // camera's height gives the depth below it.
    std::optional<terrain::SurfaceElevation> camera_water_level;
  };

  struct FrameViewInput {
    const WorldParams& world;
    const map::SurfaceGeometry& surface;
    const GameSession& session;
    const GraphicsSettings& graphics;
    FrameCameraReading selected_camera {};
    FrameSceneMode scene = FrameSceneMode::Gameplay;
    aspect_ratio_t aspect = 1.0f * aspect_ratio[one];
    proportion_t cinematic_motion_blur = 0.0f * proportion[one];
    seconds_t cinematic_elapsed = 0.0f * u::s;
    // A cinematic camera away from the player's eyes shows the figure that
    // the first-person view otherwise leaves undrawn.
    bool reveal_player = false;
    // A capture camera's own lens, in place of play's.
    std::optional<degrees_t> capture_field_of_view;
    // The sky at this moment of the day; absent, the fixed sun at the
    // graphics settings' height.
    std::optional<SkyReading> sky;
    // The local water field; absent, only sea above local ground is wet.
    terrain::WaterView water = nullptr;
  };

  // Shared pure art-direction readings.  The application can update its
  // weather before composition without rendering needing to mutate it.
  direction_t sun_direction_for (proportion_t sun_height);
  void sun_light_colors_for (proportion_t sun_height,
                             DisplayColor& diffuse,
                             DisplayColor& specular);
  DisplayColor horizon_color_for (proportion_t sun_height);

  VehiclePose pose (const mov::Vehicle& vehicle);
  CarPose pose (const mov::Car& car);
  HelicopterPose pose (const mov::Helicopter& helicopter);
  TractorPose pose (const mov::Tractor& tractor);
  BoatPose pose (const mov::Boat& boat);
  JetSkiPose pose (const mov::JetSki& ski);
  // Every machine's.
  FleetPoses poses (const Fleet& fleet);
  // The bike and the jeep as they ride in the Caddy (game/sling.hh).
  GliderPose glider_pose (const mov::Glider& glider);
  WalkerPose walker_pose (const Walker& walker,
                          inclination_t look_pitch = 0.0f *
                                                     inclination[u::rad]);
  FrameView compose_frame_view (const FrameViewInput& input);
  // The lens play's own view would draw `input` through: the capture
  // camera's, the eyes', or the following camera's, widening with speed.
  degrees_t gameplay_field_of_view (const FrameViewInput& input);

  // How long the motion blur draws the world's streaks, as a share of a
  // frame's motion, for a player getting about in `mode` at `kmh`. Each
  // way of getting about blurs from the speed that feels fast in it: the
  // boat as it comes up onto the plane, the jet ski past the speed it
  // cruises at, the Trail 90 only past what it reaches unboosted. On foot and
  // in the air there is none: the ground is far off from a wing or a cabin, so
  // what streaks there is the camera's turning, not speed.
  proportion_t motion_blur_for (Doing mode, kilometres_per_hour_t speed);

  // The raw, unsmoothed sight line toward the sun.  The game updates its
  // small temporal accumulator in tick(); this remains a pure reading so
  // composing or rendering a FrameView cannot advance simulation state.
  proportion_t sun_visibility_target (const FrameView& view,
                                      const WorldParams& world,
                                      const map::SurfaceGeometry& surface);
}

#endif
