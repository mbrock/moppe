#ifndef MOPPE_GAME_GAME_SESSION_HH
#define MOPPE_GAME_GAME_SESSION_HH

#include <moppe/game/game_state.hh>
#include <moppe/game/input_frame.hh>
#include <moppe/game/world.hh>
#include <moppe/map/surface.hh>

namespace moppe::game {
  // Observable application-side effects of an ordinary simulation step.
  // The application decides how to realize these, keeping platform services
  // outside the simulation seam.
  struct GameSessionAdvanceResult {
    bool say_ouchies = false;
  };

  // How the view follows whatever the player is doing, the same for every
  // way of getting about: the eyes and the way ahead they look along, and
  // for the following cameras the point on the subject they swing about,
  // how it moves, and how far behind and how high above it they stand.
  struct CameraRig {
    Vec3 eye;
    Vec3 ahead { 0, 0, 1 };
    Vec3 pivot;
    Vec3 velocity;
    meters_t distance = 6.5f * u::m;
    // As near as the ground may pull the camera in.
    meters_t nearest = 2.6f * u::m;
    degrees_t elevation = 18.0f * u::deg;
  };

  // The mutable state of one playable session on a completed world.  The
  // world retains the surface; the bike and the glider borrow it for their
  // physical readings. A checkpoint is therefore meaningful only on the same
  // completed world.
  class GameSession {
  public:
    using State = GameState;

    // The bike physics is chosen once per session; see mov::BikePhysics.
    GameSession (const WorldParams& world,
                 const map::SurfaceGeometry& surface,
                 mov::BikePhysics physics = mov::default_bike_physics);
    GameSession (const GameSession&) = delete;
    GameSession& operator= (const GameSession&) = delete;
    GameSession (GameSession&&) = delete;
    GameSession& operator= (GameSession&&) = delete;

    GameLogicState& logic () noexcept {
      return m_logic;
    }
    const GameLogicState& logic () const noexcept {
      return m_logic;
    }

    mov::Vehicle& bike () noexcept {
      return m_bike;
    }
    const mov::Vehicle& bike () const noexcept {
      return m_bike;
    }

    mov::Jeep& jeep () noexcept {
      return m_jeep;
    }
    const mov::Jeep& jeep () const noexcept {
      return m_jeep;
    }

    mov::Helicopter& helicopter () noexcept {
      return m_helicopter;
    }
    const mov::Helicopter& helicopter () const noexcept {
      return m_helicopter;
    }

    mov::Tractor& tractor () noexcept {
      return m_tractor;
    }
    const mov::Tractor& tractor () const noexcept {
      return m_tractor;
    }

    mov::Caddy& caddy () noexcept {
      return m_caddy;
    }
    const mov::Caddy& caddy () const noexcept {
      return m_caddy;
    }

    mov::Glider& glider () noexcept {
      return m_glider;
    }
    const mov::Glider& glider () const noexcept {
      return m_glider;
    }

    Walker& walker () noexcept {
      return m_walker;
    }
    const Walker& walker () const noexcept {
      return m_walker;
    }

    ChaseCamera& camera () noexcept {
      return m_camera;
    }
    const ChaseCamera& camera () const noexcept {
      return m_camera;
    }

    Stars& stars () noexcept {
      return m_stars;
    }
    const Stars& stars () const noexcept {
      return m_stars;
    }

    Dust& dust () noexcept {
      return m_dust;
    }
    const Dust& dust () const noexcept {
      return m_dust;
    }

    Vec3 subject_position () const;
    Vec3 subject_heading () const;
    float subject_speed_kmh () const;
    bool can_deploy_glider (const map::SurfaceGeometry& terrain) const;
    bool can_drop_bike () const;
    // What the mount key would take, on foot: of the bike, the jeep, the
    // helicopter, the tractor, and the Caddy, the nearest within its
    // reach.
    enum class Mount { none, bike, jeep, helicopter, tractor, caddy };
    Mount mount_within_reach () const;
    // On foot nearest the jeep, within its reach.
    bool can_drive () const;
    // On foot nearest the helicopter, within its reach.
    bool can_fly () const;
    // On foot nearest the tractor, within its reach.
    bool can_drive_tractor () const;
    // On foot nearest the Caddy, within reach of its doors.
    bool can_drive_caddy () const;
    // Flying low and slow enough to step out.
    bool can_step_out_of_helicopter () const;
    void clear_controls ();
    // Begin standing beside the parked bike, seen from behind.
    void start_on_foot ();
    CameraRig camera_rig () const;
    // Moves the camera the way the current view follows the player over
    // `dt`; a following camera reset beforehand stands straight at its
    // place (settle_camera).
    void follow_camera (const map::SurfaceGeometry& surface, seconds_t dt);
    // Puts the camera straight where the current view rests, with no
    // easing: after a cinematic hands the view back.
    void settle_camera (const map::SurfaceGeometry& surface);
    // Changes how the player gets about, and with it the view: mounting
    // takes up the view last used for riding (from behind, unless Tab
    // chose another) and stepping off the one last used on foot.
    void set_mode (Mode mode);

    State state () const;
    void restore (const State& state);

  private:
    GameLogicState m_logic;
    mov::Vehicle m_bike;
    mov::Glider m_glider;
    mov::Jeep m_jeep;
    mov::Helicopter m_helicopter;
    mov::Tractor m_tractor;
    mov::Caddy m_caddy;
    Walker m_walker;
    ChaseCamera m_camera;
    Stars m_stars;
    Dust m_dust;
  };

  // What the wheels roll over, read from the world by the caller: how much
  // loose dust the ground gives up and its colour, and how thickly fallen
  // leaves lie there to be thrown up.
  struct GroundCover {
    float dust = 1.0f;
    DisplayColor dust_color { 0.60f, 0.52f, 0.40f };
    DisplayColor clod_color { 0.42f, 0.34f, 0.24f };
    float leaves = 0.0f;
  };

  GameSessionAdvanceResult
  advance_game_session (const WorldParams& world,
                        const map::SurfaceGeometry& surface,
                        GameSession& session,
                        const InputFrame& input,
                        seconds_t dt,
                        const mov::TrunkField* trunks = nullptr,
                        const GroundCover& ground = {},
                        const terrain::WaterSheets* water = nullptr);
}

#endif
