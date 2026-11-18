#ifndef MOPPE_GAME_GAME_STATE_HH
#define MOPPE_GAME_GAME_STATE_HH

#include <moppe/game/animals.hh>
#include <moppe/game/basket.hh>
#include <moppe/game/camp_state.hh>
#include <moppe/game/chase_camera.hh>
#include <moppe/game/doing.hh>
#include <moppe/game/dust.hh>
#include <moppe/game/fishing_state.hh>
#include <moppe/game/fleet.hh>
#include <moppe/game/hens.hh>
#include <moppe/game/machine_controls.hh>
#include <moppe/game/rescue.hh>
#include <moppe/game/sling.hh>
#include <moppe/game/snow.hh>
#include <moppe/game/stars.hh>
#include <moppe/game/walker.hh>
#include <moppe/game/weather.hh>
#include <moppe/gfx/math.hh>
#include <moppe/mov/boat.hh>
#include <moppe/mov/car.hh>
#include <moppe/mov/glider.hh>
#include <moppe/mov/helicopter.hh>
#include <moppe/mov/jet_ski.hh>
#include <moppe/mov/tractor.hh>
#include <moppe/mov/vehicle.hh>

#include <array>
#include <optional>
#include <random>
#include <vector>

namespace moppe::game {
  using cloud_cover_t = mp_units::quantity<cloud_cover[mp_units::one], float>;
  using health_t = mp_units::quantity<health[mp_units::one], float>;

  enum CamMode { CAM_CHASE, CAM_FRONT, CAM_HELMET };

  // The directly copyable logical state owned by GameSession. Generated world
  // data, renderer resources, platform state, and asynchronous loading state
  // deliberately live outside this value.
  struct GameLogicState {
    // Play's clock, and the day's: the hour counted on from the first
    // midnight, and how many times faster than usual it is passing
    // (game_clock.hh).
    play_time_t m_total_time = play_time (0.0);
    clock_time_t m_day_hours = clock_time (9.0);
    scale_factor_t m_day_haste = 1.0f * scale_factor[mp_units::one];
    // How long the last rendered frame took (the HUD's cadence).
    seconds_t m_frame_time = (1.0f / 60.0f) * u::s;
    cloud_cover_t m_cloudiness = 0.5f * cloud_cover[mp_units::one];
    // The weather this moment (game/weather.hh), passing from spell to
    // spell with the hours.
    Weather m_weather;
    // How much of the sun the eye sees, for its flare.
    proportion_t m_flare = 0.0f * proportion[mp_units::one];
    DisplayColor m_fog;
    // A hard landing's shake of the riding camera: how far it rolls, and
    // how long it has been shaking.
    degrees_t m_shake = 0.0f * u::deg;
    seconds_t m_shake_time = 0.0f * u::s;
    health_t m_health = 100.0f * health[mp_units::one];
    // How far speed has widened the chase camera's field of view.
    blend_weight_t m_fov_k = 0.0f * blend_weight[mp_units::one];
    int m_lives = 10;
    bool m_game_over = false;
    meters_f64_t m_odometer = 0.0 * u::m;
    // What the player's hands last asked (game/machine_controls.hh).
    Controls m_controls;
    SnowPack m_snow;
    std::optional<position_t> m_snow_last;
    direction_t m_snow_heading {};
    Doing m_snow_mode;
    meters_t m_snow_distance {};
    bool m_snow_left = false;
    Doing m_doing;
    CamMode m_cam_mode = CAM_CHASE;
    // Each way of getting about keeps the view last chosen for it, from
    // behind until Tab chooses another. The one not in use waits here.
    CamMode m_foot_cam = CAM_CHASE;
    CamMode m_ride_cam = CAM_CHASE;
    // Gaze above (positive) or below the walker's horizon; from
    // outside, looking up lowers the camera and looking down lifts it.
    inclination_t m_look_pitch = 0.0f * inclination[u::rad];
    // Riding or gliding, the player's look away from straight ahead:
    // round to the right, and how long since they last moved it (the
    // view comes back round ahead once it has been let be; see
    // settle_look in game_session.cc).
    radians_t m_look_yaw = 0.0f * u::rad;
    seconds_t m_look_idle = 0.0f * u::s;
    // The helmet camera's eye.
    position_t m_fp_eye = position (Vec3 ());
    // Riding along: the seat in the other player's machine, or in the
    // bus, how it faces, and how it moves, as last heard.
    position_t m_seat_position = position (Vec3 ());
    direction_t m_seat_heading = unit_direction (Vec3 (0, 0, 1));
    direction_t m_seat_up = unit_direction (Vec3 (0, 1, 0));
    velocity_t m_seat_velocity = velocity (Vec3 ());
    // On the bus: which of them (BusLine::where), which seat (0 the
    // front row's by the door, 1 beside it), and whether the rider has
    // asked to get off at the next stop.
    std::uint8_t m_bus = 0;
    std::uint8_t m_bus_seat = 0;
    bool m_bus_alight = false;
    // How far the buses' timetable runs ahead of play's clock: nothing
    // alone, and with the other player as much as keeps the two games'
    // buses together (bus_time, Companionship::keep_time).
    duration_t m_bus_lead = 0.0 * si::second;
    int m_score = 0;
    // A jump: how long the bike has been in the air, how far it has
    // turned about up and the most it has, and which way it faced last
    // step; and the last landing's airtime, spin, points, and age.
    seconds_t m_jump_airtime = 0.0f * u::s;
    radians_t m_jump_spin = 0.0f * u::rad;
    radians_t m_jump_peak_spin = 0.0f * u::rad;
    direction_t m_jump_last_heading = unit_direction (Vec3 (0, 0, 1));
    seconds_t m_landed_airtime = 0.0f * u::s;
    degrees_t m_landed_spin = 0.0f * u::deg;
    int m_landed_points = 0;
    bool m_landed_clean = false;
    seconds_t m_landed_age = 10.0f * u::s;
    // A wheelie: the ground covered on the back wheel so far (0 while both
    // are down), how long it has been off it, and the last one's length,
    // its points, and the longest; and seconds since the last one ended.
    meters_t m_wheelie_m = 0.0f * u::m;
    seconds_t m_wheelie_gap = 0.0f * u::s;
    meters_t m_last_wheelie_m = 0.0f * u::m;
    int m_last_wheelie_points = 0;
    meters_t m_best_wheelie_m = 0.0f * u::m;
    seconds_t m_wheelie_age = 10.0f * u::s;
    std::mt19937 m_fx_rng { 7 };
    Basket m_basket;
    Camp m_camp;
    // The trees the walker stands between that would take the hammock,
    // and where the walker stood when that was worked out.
    std::optional<HammockSite> m_hammock_at_hand;
    std::optional<position_t> m_hammock_sought_from;
    // One for each of the world's sanctuaries; the other player's as last
    // heard; and how fiercely each burns this step.
    std::vector<Beacon> m_beacons;
    std::vector<Beacon> m_their_beacons;
    std::vector<proportion_t> m_beacon_burn;
    // Where the subject stood a step ago, for how fast the animals find
    // it going.
    std::optional<position_t> m_last_subject;
    Fishing m_fishing;
    // The eggs in each barn's nest boxes, four boxes a barn in the order
    // of the places' farms.
    std::vector<NestState> m_nests;
    // How each machine is getting on (game/rescue.hh), indexed by
    // Machine, and the rope the walker hauls one by.
    std::array<Trouble, machine_count> m_trouble {};
    Haul m_haul;
    // The Caddy hung under the helicopter (game/sling.hh).
    Sling m_sling;
  };

  // First replayable slice of a GameSession. Immutable world/resource
  // references remain attached to its live systems when this value is
  // restored.
  struct GameState {
    GameLogicState logic;
    FleetState fleet;
    mov::Glider::State glider;
    Walker::State walker;
    ChaseCamera::State camera;
    Stars::State stars;
    Dust::State dust;
    Animals::State animals;
    Hens::State hens;
  };
}

#endif
