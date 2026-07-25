#ifndef MOPPE_GAME_GAME_SESSION_HH
#define MOPPE_GAME_GAME_SESSION_HH

#include <moppe/game/game_state.hh>
#include <moppe/game/input_frame.hh>
#include <moppe/game/world.hh>
#include <moppe/map/surface.hh>

#include <vector>

namespace moppe::game {
  // Observable application-side effects of an ordinary simulation step.
  // The application decides how to realize these, keeping platform services
  // outside the simulation seam.
  struct GameSessionAdvanceResult {
    bool say_ouchies = false;
  };

  // The mutable state of one playable session on a completed world.  The
  // world retains the surface; vehicles and the glider borrow it for their
  // physical readings. A checkpoint is therefore meaningful only on the same
  // completed world.
  class GameSession {
  public:
    using State = GameState;

    GameSession (const WorldParams& world, const map::Surface& surface);
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

    mov::Vehicle& car () noexcept {
      return m_car;
    }
    const mov::Vehicle& car () const noexcept {
      return m_car;
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

    mov::Vehicle& active_vehicle () noexcept;
    const mov::Vehicle& active_vehicle () const noexcept;
    Vec3 subject_position () const;
    Vec3 subject_heading () const;
    float subject_speed_kmh () const;
    bool can_deploy_glider (const map::Surface& terrain) const;
    bool can_drop_bike () const;
    void clear_controls ();

    State state () const;
    void restore (const State& state);

  private:
    GameLogicState m_logic;
    mov::Vehicle m_bike;
    mov::Vehicle m_car;
    mov::Glider m_glider;
    Walker m_walker;
    ChaseCamera m_camera;
    Stars m_stars;
    Dust m_dust;
  };

  GameSessionAdvanceResult
  advance_game_session (const WorldParams& world,
                        const map::Surface& surface,
                        const std::vector<mov::Box>& obstacles,
                        GameSession& session,
                        const InputFrame& input,
                        seconds_t dt);
}

#endif
