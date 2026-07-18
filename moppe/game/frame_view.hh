#ifndef MOPPE_GAME_FRAME_VIEW_HH
#define MOPPE_GAME_FRAME_VIEW_HH

#include <moppe/color.hh>
#include <moppe/game/game_session.hh>
#include <moppe/game/graphics_settings.hh>
#include <moppe/gfx/mat4.hh>

#include <cstdint>
#include <optional>

namespace moppe::game {
  // The one selected presentation mode for a finished world frame. These are
  // deliberately concrete application modes, not a generic scene hierarchy.
  enum class FrameSceneMode {
    Gameplay,
    Cinematic,
    TerrainLab,
    WaterInspection,
    TreeDemo
  };

  // A camera has already been selected by the application before composing a
  // frame.  Keeping its view matrix here preserves cinematics' banked camera
  // while still letting FrameView apply riding-only shake afterward.
  struct FrameCameraReading {
    Vec3 position {};
    Vec3 forward { 0, 0, 1 };
    Mat4 view {};
    float field_of_view = 70.0f;
  };

  // Renderer-facing snapshots of the parts of a vehicle that actually affect
  // its visible pose.  They intentionally omit controls and physical state.
  struct VehiclePose {
    Vec3 position {};
    Vec3 render_orientation { 0, 0, 1 };
    Vec3 render_normal { 0, 1, 0 };
    float suspension = 0.0f;
    float lean_radians = 0.0f;
    float wheel_spin_radians = 0.0f;
    float yaw_radians = 0.0f;
    float thrust = 0.0f;
    float boost_level = 0.0f;
    float boost_drive = 0.0f;
    int body_kind = 0;
    DisplayColor body_color {};
  };

  struct GliderPose {
    Vec3 position {};
    Vec3 heading { 0, 0, 1 };
    float bank_radians = 0.0f;
  };

  struct WalkerPose {
    Vec3 position {};
    Vec3 heading { 0, 0, 1 };
    float walk = 0.0f;
    float animation_distance = 0.0f;
  };

  struct FrameActors {
    VehiclePose bike {};
    std::optional<VehiclePose> car;
    std::optional<GliderPose> glider;
    std::optional<WalkerPose> walker;
    Vec3 visual_scale { 1, 1, 1 };
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
    float landed_airtime_s = 0.0f;
    int landed_points = 0;
    float landed_age_s = 10.0f;
    bool on_foot = false;
    bool gliding = false;
    bool can_deploy_glider = false;
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
    bool terrain_lab = false;
    bool cinematic = false;
    bool water_inspection = false;
    bool tree_demo = false;
    bool terrain_lab_torus = false;
    bool terrain_lab_pristine = true;
    bool sky_before_terrain = false;
    bool sky_after_terrain = true;
    bool forest = true;
    bool tree_stand = true;
    bool actors = true;
    bool ocean = true;
    bool terrain_lab_rivers = false;
    bool river_ribbons = true;
    bool dust = true;
    bool underwater = false;
    bool motion_blur = false;
    bool vehicle_effects = true;
    bool star_effects = true;
    bool game_hud = true;
    bool cinematic_hud = false;
    bool terrain_lab_hud = false;
    bool terrain_topology_hint = false;
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
    float field_of_view = 70.0f;
  };

  struct FrameLighting {
    DisplayColor fog_color {};
    DisplayColor clear_color {};
    attenuation_t fog_scale = 0.0f / u::m;
    Vec3 sun_direction {};
    DisplayColor sun_diffuse {};
    DisplayColor sun_specular {};
    DisplayColor ambient {};
    float sun_height = 0.56f;
    float time = 0.0f;
    float cloudiness = 0.0f;
    float sun_visibility = 0.0f;
    float exposure_bias = 1.0f;
  };

  struct FrameGraphics {
    float scene_scale = 1.0f;
    float render_scale_override = 0.0f;
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
    const map::HeightMap& terrain;
    const GameSession& session;
    const GraphicsSettings& graphics;
    FrameCameraReading selected_camera {};
    FrameSceneMode scene = FrameSceneMode::Gameplay;
    // Terrain Lab supplies its own inspection fog and view controls. All
    // other modes use the finished world's normal atmosphere.
    attenuation_t terrain_lab_fog = 0.0f / u::m;
    bool terrain_lab_torus = false;
    bool terrain_lab_pristine = true;
    float aspect = 1.0f;
    float landscape_scale_x = 1.0f;
    float landscape_scale_y = 1.0f;
    float cinematic_motion_blur = 0.0f;
    float cinematic_elapsed = 0.0f;
    FrameBenchmarkTag benchmark {};
  };

  // Shared pure art-direction readings.  The application can update its
  // weather before composition without rendering needing to mutate it.
  Vec3 sun_direction_for (float sun_height);
  void sun_light_colors_for (float sun_height,
                             DisplayColor& diffuse,
                             DisplayColor& specular);
  DisplayColor horizon_color_for (float sun_height);

  VehiclePose vehicle_pose (const mov::Vehicle& vehicle);
  GliderPose glider_pose (const mov::Glider& glider);
  WalkerPose walker_pose (const Walker& walker);
  FrameView compose_frame_view (const FrameViewInput& input);

  // The raw, unsmoothed sight line toward the sun.  The game updates its
  // small temporal accumulator in tick(); this remains a pure reading so
  // composing or rendering a FrameView cannot advance simulation state.
  float sun_visibility_target (const FrameView& view,
                               const WorldParams& world,
                               const map::HeightMap& terrain);
}

#endif
