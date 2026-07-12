#ifndef MOPPE_GAME_GAME_STATE_HH
#define MOPPE_GAME_GAME_STATE_HH

#include <moppe/game/chase_camera.hh>
#include <moppe/game/dust.hh>
#include <moppe/game/stars.hh>
#include <moppe/game/walker.hh>
#include <moppe/gfx/math.hh>
#include <moppe/mov/vehicle.hh>

#include <random>

namespace moppe::game {
  enum Mode { M_BIKE, M_FOOT, M_CAR };
  enum CamMode { CAM_CHASE, CAM_FRONT, CAM_HELMET };

  // The directly copyable logical state owned by MoppeGame.  Generated world
  // data, renderer resources, platform state, and asynchronous loading state
  // deliberately live outside this value.
  struct GameLogicState {
    // double avoids losing tick precision during long-running sessions.
    double m_total_time = 0.0;
    float m_frame_time = 1.0f / 60.0f;
    float m_cloudiness = 0.5f;
    float m_flare = 0.0f;
    DisplayColor m_fog;
    float m_shake = 0.0f;
    float m_shake_time = 0.0f;
    float m_health = 100.0f;
    float m_fov_k = 0.0f;
    int m_lives = 10;
    bool m_game_over = false;
    float m_fuel = 100.0f;
    double m_odometer = 0.0;
    float m_turn_input = 0.0f;
    float m_go_input = 0.0f;
    float m_boost_input = 0.0f;
    Mode m_mode = M_BIKE;
    CamMode m_cam_mode = CAM_CHASE;
    Vector3D m_fp_eye;
    bool m_car_exists = false;
    int m_combo = 0;
    int m_score = 0;
    float m_jump_airtime = 0.0f;
    float m_landed_airtime = 0.0f;
    int m_landed_points = 0;
    float m_landed_age = 10.0f;
    std::mt19937 m_fx_rng { 7 };
  };

  // First replayable slice of the running game.  Immutable world/resource
  // references remain attached to their live systems when this value is
  // restored. Mutable city actors are deliberately outside the snapshot.
  struct GameState {
    GameLogicState logic;
    mov::Vehicle::State vehicle;
    mov::Vehicle::State car;
    Walker::State walker;
    ChaseCamera::State camera;
    Stars::State stars;
    Dust::State dust;
  };
}

#endif
