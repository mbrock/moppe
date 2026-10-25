#ifndef MOPPE_GAME_FRAME_VIEW_HH
#define MOPPE_GAME_FRAME_VIEW_HH

#include <moppe/color.hh>
#include <moppe/game/daylight.hh>
#include <moppe/game/fishing_state.hh>
#include <moppe/game/game_session.hh>
#include <moppe/game/graphics_settings.hh>
#include <moppe/gfx/mat4.hh>

#include <array>
#include <cstdint>
#include <optional>

namespace moppe::game {
  // The one selected presentation mode for a finished world frame. These are
  // deliberately concrete application modes, not a generic scene hierarchy.
  enum class FrameSceneMode { Gameplay, Cinematic, WaterInspection, Gazetteer };

  // The following cameras' vertical field of view, at rest; speed widens
  // it a little.
  inline constexpr degrees_t CHASE_FIELD_OF_VIEW = 78.0f * u::deg;

  // A camera has already been selected by the application before composing a
  // frame.  Keeping its view matrix here preserves cinematics' banked camera
  // while still letting FrameView apply riding-only shake afterward.
  struct FrameCameraReading {
    Vec3 position {};
    Vec3 forward { 0, 0, 1 };
    Mat4 view {};
    degrees_t field_of_view = 70.0f * u::deg;
  };

  // Renderer-facing snapshots of the parts of a vehicle that actually affect
  // its visible pose.  They intentionally omit controls and physical state.
  struct VehiclePose {
    // The drawn frame's origin, about a metre above the ground at rest.
    Vec3 position {};
    Vec3 render_orientation { 0, 0, 1 };
    Vec3 render_normal { 0, 1, 0 };
    // Metres each wheel hangs below its rest position along the frame's
    // down axis; negative is compressed.
    float rear_wheel_drop = 0.0f;
    float front_wheel_drop = 0.0f;
    float lean_radians = 0.0f;
    float wheel_spin_radians = 0.0f;
    // Fork angle about the steering head; positive turns right.
    float fork_radians = 0.0f;
    float thrust = 0.0f;
    float boost_level = 0.0f;
    float boost_drive = 0.0f;
    // Drawn at this share of its size.
    float scale = 1.0f;
    // A pedalled bike's crank, radians round from the right arm down.
    float crank_radians = 0.0f;
  };

  // The jeep as drawn: its origin (midway between the axles, at their
  // standing height) and frame, how far each wheel hangs below where it
  // stands (front left, front right, rear left, rear right; negative is
  // pressed), the front wheels' turn to the right, and the wheels' roll.
  struct JeepPose {
    Vec3 position {};
    Vec3 forward { 0, 0, 1 };
    Vec3 up { 0, 1, 0 };
    std::array<float, 4> wheel_drop {};
    float steer_radians = 0.0f;
    float wheel_spin_radians = 0.0f;
    // Drawn at this share of its size.
    float scale = 1.0f;
  };

  // The helicopter's line and its load's bridle as drawn
  // (game/sling.hh): the hook, the ring the legs meet at, where each leg
  // is made fast, and whether the line is taut.
  struct SlingPose {
    Vec3 hook;
    Vec3 ring;
    std::array<Vec3, 4> legs {};
    bool taut = false;
  };

  // The helicopter as drawn: its origin (under the mast, level with the
  // skids' bottoms) and frame, and how far round each rotor has turned.
  struct HelicopterPose {
    Vec3 position {};
    Vec3 forward { 0, 0, 1 };
    Vec3 up { 0, 1, 0 };
    float rotor_radians = 0.0f;
    float tail_radians = 0.0f;
  };

  // The Caddy as drawn: the same readings as the jeep's.
  using CaddyPose = JeepPose;

  // The tractor as drawn: its origin (midway between the axles, on the
  // ground it stands on) and frame, how far each wheel hangs below where
  // it stands (front left, front right, rear left, rear right; negative
  // is pressed), the front wheels' turn to the right, and how far round
  // each pair has rolled.
  struct TractorPose {
    Vec3 position {};
    Vec3 forward { 0, 0, 1 };
    Vec3 up { 0, 1, 0 };
    std::array<float, 4> wheel_drop {};
    float steer_radians = 0.0f;
    float front_spin_radians = 0.0f;
    float rear_spin_radians = 0.0f;
  };

  // The boat as drawn: its origin (on the keel a quarter of the way
  // along) and frame, how far its long-tail is swung and tipped up, how
  // far round the propeller has turned, and the engine's speed.
  struct BoatPose {
    Vec3 position {};
    Vec3 forward { 0, 0, 1 };
    Vec3 up { 0, 1, 0 };
    float swing_radians = 0.0f;
    float tilt_radians = 0.0f;
    float propeller_radians = 0.0f;
    float rpm = 0.0f;
  };

  struct GliderPose {
    Vec3 position {};
    Vec3 heading { 0, 0, 1 };
    float bank_radians = 0.0f;
    bool bike_attached = false;
  };

  // Everything the avatar's procedural animation reads: where the feet
  // are and how the body is moving over the ground, not its controls.
  struct WalkerPose {
    Vec3 position {}; // the feet, eased over steps
    Vec3 heading { 0, 0, 1 };
    Vec3 velocity {};
    Vec3 ground_normal { 0, 1, 0 };
    float stride_phase = 0.0f;
    float stride_speed = 0.0f; // m/s
    bool grounded = true;
    float airborne_seconds = 0.0f;
    float vertical_speed = 0.0f; // m/s, up positive
    float landing_dip = 0.0f;    // metres
    float look_pitch = 0.0f;     // radians, up positive
    // Hauling: where the rope in the right hand is tied.
    std::optional<Vec3> rope_end;
    // Fishing: the rod in the right hand and its line.
    std::optional<RodPose> rod;
  };

  struct FrameActors {
    VehiclePose bike {};
    VehiclePose mountain_bike {};
    JeepPose jeep {};
    HelicopterPose helicopter {};
    TractorPose tractor {};
    CaddyPose caddy {};
    BoatPose boat {};
    // Fishing from the boat: the rod in the helmsman's right hand.
    std::optional<RodPose> helm_rod;
    // The bike and the jeep ride in the Caddy, its hardtop off and its
    // tailgate down, and the Caddy may hang on the helicopter's line.
    std::optional<SlingPose> sling;
    std::optional<GliderPose> glider;
    std::optional<WalkerPose> walker;
    Mode active_mode = M_BIKE;
    CamMode camera_mode = CAM_CHASE;
    bool helmet_camera = false;
  };

  // Renderer-free readings for the compact driving HUD. They deliberately
  // mirror the HUD's value inputs without importing its DrawList dependency
  // into FrameView. The trail map needs the unprojected subject reading too.
  struct FrameHud {
    float speed_kmh = 0.0f;
    float boost_ready01 = 1.0f;
    float health01 = 1.0f;
    float odometer_m = 0.0f;
    int lives = 10;
    int stars = 0;
    int score = 0;
    float airtime_s = 0.0f;
    float spin_degrees = 0.0f;
    float landed_airtime_s = 0.0f;
    float landed_spin_degrees = 0.0f;
    int landed_points = 0;
    bool landed_clean = false;
    float landed_age_s = 10.0f;
    bool on_foot = false;
    bool gliding = false;
    bool can_deploy_glider = false;
    bool can_drop_bike = false;
    bool can_mount = false;
    // On foot beside the jeep, which the mount key would drive.
    bool can_drive = false;
    // On foot beside the helicopter, which the mount key would fly.
    bool can_fly = false;
    // On foot beside the tractor, which the mount key would drive.
    bool can_drive_tractor = false;
    // On foot beside the Caddy, which the mount key would drive.
    bool can_drive_caddy = false;
    // On foot beside the mountainbike, which the mount key would ride.
    bool can_ride_mountain_bike = false;
    // On foot beside the boat, which the mount key would take out.
    bool can_take_boat = false;
    // On foot beside a stuck machine, which the mount key would haul
    // out; hauling one; or driving one that is stuck (game/rescue.hh).
    const char* haul = nullptr;
    const char* hauling = nullptr;
    bool stuck = false;
    float vertical_speed_mps = 0.0f;
    float frame_time_s = 1.0f / 60.0f;
    float heading_radians = 0.0f;
    Vec3 subject_position {};
    Vec3 subject_heading { 0, 0, 1 };
  };

  struct FrameOverlay {
    float cinematic_prompt_alpha = 0.0f;
  };

  // These flags retain the existing render paragraphs' selection decisions.
  // Their values are plain frame data, so a later presenter need not query
  // mutable application mode while encoding commands.
  struct FrameVisibility {
    bool cinematic = false;
    bool water_inspection = false;
    bool sky_before_terrain = false;
    bool sky_after_terrain = true;
    bool forest = true;
    bool boulders = true;
    bool mushrooms = true;
    bool undergrowth = true;
    bool actors = true;
    bool ocean = true;
    bool waterfall_curtains = true;
    bool dust = true;
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
    Vec3 position {};
    Vec3 forward { 0, 0, 1 };
    Vec3 right { 1, 0, 0 };
    Vec3 up { 0, 1, 0 };
    Vec3 frame_forward { 0, 0, 1 };
    Mat4 view {};
    Mat4 projection {};
    degrees_t field_of_view = 70.0f * u::deg;
    float aspect = 1.0f;
  };

  struct FrameLighting {
    DisplayColor fog_color {};
    DisplayColor clear_color {};
    attenuation_t fog_scale = 0.0f / u::m;
    Vec3 sun_direction {};
    DisplayColor sun_diffuse {};
    DisplayColor sun_specular {};
    DisplayColor ambient {};
    float sun_height = 0.62f;
    float time = 0.0f;
    cloud_cover_t cloudiness = 0.0f * cloud_cover[mp_units::one];
    float sun_visibility = 0.0f;
    float exposure_bias = 1.0f;
    float mist = 0.0f;
    float rain = 0.0f;
    // `sun_direction` is the key light, which at night is the moon; these
    // are the sky itself (game/daylight.hh).
    Vec3 sky_sun { 0, 1, 0 };
    Vec3 sky_moon { 0, -1, 0 };
    Vec3 sky_pole { 0, 1, 0 };
    float sky_turn = 0.0f;
    float moonlight = 0.0f;
    float daylight = 1.0f;
    // A fire's light: where it burns, how far it reaches, and its colour
    // at full strength. No reach, no fire.
    Vec3 lamp_position {};
    float lamp_reach = 0.0f;
    DisplayColor lamp_color {};
  };

  struct FrameGraphics {
    float scene_scale = 1.0f;
    float render_scale_override = 0.0f;
    float scene_megapixel_budget = 0.0f;
    float target_frame_rate = 0.0f;
    bool bloom = true;
    bool auto_exposure = true;
    bool lens_flare = true;
  };

  // This remains a raw value because the Metal benchmark CSV owns its column
  // contract.  FrameView only carries the sample coordinates to the encoder.
  struct FrameBenchmarkTag {
    uint32_t mask = 0;
    uint32_t partition_mask = 0;
    uint32_t epoch = 0;
    uint32_t logical_frame = 0;
    bool measured = false;
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
    float terrain_distance = 0.0f;
    float motion_blur_amount = 0.0f;
    FrameBenchmarkTag benchmark {};
  };

  struct FrameViewInput {
    const WorldParams& world;
    const map::SurfaceGeometry& surface;
    const GameSession& session;
    const GraphicsSettings& graphics;
    FrameCameraReading selected_camera {};
    FrameSceneMode scene = FrameSceneMode::Gameplay;
    float aspect = 1.0f;
    float cinematic_motion_blur = 0.0f;
    float cinematic_elapsed = 0.0f;
    // A cinematic camera away from the player's eyes shows the figure that
    // the first-person view otherwise leaves undrawn.
    bool reveal_player = false;
    FrameBenchmarkTag benchmark {};
    // The sky at this moment of the day; absent, the fixed sun at the
    // graphics settings' height.
    std::optional<SkyReading> sky;
  };

  // Shared pure art-direction readings.  The application can update its
  // weather before composition without rendering needing to mutate it.
  Vec3 sun_direction_for (float sun_height);
  void sun_light_colors_for (float sun_height,
                             DisplayColor& diffuse,
                             DisplayColor& specular);
  DisplayColor horizon_color_for (float sun_height);

  VehiclePose vehicle_pose (const mov::Vehicle& vehicle);
  JeepPose jeep_pose (const mov::Jeep& jeep);
  HelicopterPose helicopter_pose (const mov::Helicopter& helicopter);
  TractorPose tractor_pose (const mov::Tractor& tractor);
  BoatPose boat_pose (const mov::Boat& boat);
  CaddyPose caddy_pose (const mov::Caddy& caddy);
  // The bike and the jeep as they ride in the Caddy (game/sling.hh).
  GliderPose glider_pose (const mov::Glider& glider);
  WalkerPose walker_pose (const Walker& walker, float look_pitch = 0.0f);
  FrameView compose_frame_view (const FrameViewInput& input);

  // How long the motion blur draws the world's streaks, as a share of a
  // frame's motion, for a player getting about in `mode` at `kmh`. Each
  // way of getting about blurs from the speed that feels fast in it: the
  // boat as it comes up onto the plane, the motocross (half again life
  // size) only past what it cruises at unboosted. On foot and in the air
  // there is none: the ground is far off from a wing or a cabin, so what
  // streaks there is the camera's turning, not speed.
  float motion_blur_for (Mode mode, float kmh);

  // The raw, unsmoothed sight line toward the sun.  The game updates its
  // small temporal accumulator in tick(); this remains a pure reading so
  // composing or rendering a FrameView cannot advance simulation state.
  float sun_visibility_target (const FrameView& view,
                               const WorldParams& world,
                               const map::SurfaceGeometry& surface);
}

#endif
