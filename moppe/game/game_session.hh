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
    void clear_controls ();
    // Begin standing beside the parked bike, seeing through your own eyes.
    void start_on_foot ();
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
                        const GroundCover& ground = {});
}

#endif
