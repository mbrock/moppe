#ifndef MOPPE_GAME_GAME_STATE_HH
#define MOPPE_GAME_GAME_STATE_HH

#include <moppe/game/animals.hh>
#include <moppe/game/basket.hh>
#include <moppe/game/camp_state.hh>
#include <moppe/game/chase_camera.hh>
#include <moppe/game/doing.hh>
#include <moppe/game/dust.hh>
#include <moppe/game/fishing_state.hh>
#include <moppe/game/hens.hh>
#include <moppe/game/machine_controls.hh>
#include <moppe/game/rescue.hh>
#include <moppe/game/sling.hh>
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
    float m_day_haste = 1.0f;
    float m_frame_time = 1.0f / 60.0f;
    cloud_cover_t m_cloudiness = 0.5f * cloud_cover[mp_units::one];
    // The weather this moment (game/weather.hh), passing from spell to
    // spell with the hours.
    Weather m_weather;
    float m_flare = 0.0f;
    DisplayColor m_fog;
    float m_shake = 0.0f;
    float m_shake_time = 0.0f;
    float m_health = 100.0f;
    float m_fov_k = 0.0f;
    int m_lives = 10;
    bool m_game_over = false;
    double m_odometer = 0.0;
    // What the player's hands last asked (game/machine_controls.hh).
    Controls m_controls;
    Doing m_doing;
    CamMode m_cam_mode = CAM_CHASE;
    // Each way of getting about keeps the view last chosen for it, from
    // behind until Tab chooses another. The one not in use waits here.
    CamMode m_foot_cam = CAM_CHASE;
    CamMode m_ride_cam = CAM_CHASE;
    // Gaze above (positive) or below the walker's horizon, in radians;
    // from outside, looking up lowers the camera and looking down lifts it.
    float m_look_pitch = 0.0f;
    // Riding or gliding, the player's look away from straight ahead:
    // radians around to the right, and seconds since they last moved it
    // (the view comes back round ahead once it has been let be; see
    // settle_look in game_session.cc).
    float m_look_yaw = 0.0f;
    float m_look_idle = 0.0f;
    Vec3 m_fp_eye;
    // Riding along: the seat in the other player's jeep,
    // how it faces, and how it moves, as last heard.
    Vec3 m_seat_position;
    Vec3 m_seat_heading { 0, 0, 1 };
    Vec3 m_seat_up { 0, 1, 0 };
    Vec3 m_seat_velocity;
    // Whether that seat is the fore seat of their boat.
    bool m_seat_boat = false;
    int m_score = 0;
    float m_jump_airtime = 0.0f;
    float m_jump_spin_radians = 0.0f;
    float m_jump_peak_spin_radians = 0.0f;
    Vec3 m_jump_last_heading { 0, 0, 1 };
    float m_landed_airtime = 0.0f;
    float m_landed_spin_degrees = 0.0f;
    int m_landed_points = 0;
    bool m_landed_clean = false;
    float m_landed_age = 10.0f;
    // A wheelie: the ground covered on the back wheel so far (0 while both
    // are down), how long it has been off it, and the last one's length,
    // its points, and the longest; and seconds since the last one ended.
    float m_wheelie_m = 0.0f;
    float m_wheelie_gap = 0.0f;
    float m_last_wheelie_m = 0.0f;
    int m_last_wheelie_points = 0;
    float m_best_wheelie_m = 0.0f;
    float m_wheelie_age = 10.0f;
    std::mt19937 m_fx_rng { 7 };
    Basket m_basket;
    Camp m_camp;
    // The trees the walker stands between that would take the hammock,
    // and where the walker stood when that was worked out.
    std::optional<HammockSite> m_hammock_at_hand;
    std::optional<Vec3> m_hammock_sought_from;
    // One for each of the world's sanctuaries; the other player's as last
    // heard; and how fiercely each burns this step.
    std::vector<Beacon> m_beacons;
    std::vector<Beacon> m_their_beacons;
    std::vector<float> m_beacon_burn;
    // Where the subject stood a step ago, for how fast the animals find
    // it going.
    std::optional<Vec3> m_last_subject;
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
    mov::Vehicle::State vehicle;
    mov::Glider::State glider;
    Walker::State walker;
    ChaseCamera::State camera;
    Stars::State stars;
    Dust::State dust;
    mov::Car::State jeep;
    mov::Helicopter::State helicopter;
    mov::Tractor::State tractor;
    mov::Car::State caddy;
    mov::Vehicle::State mountain_bike;
    mov::Boat::State boat;
    mov::Vehicle::State long_john;
    mov::JetSki::State jet_ski;
    Animals::State animals;
    Hens::State hens;
  };
}

#endif
