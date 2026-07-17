#ifndef MOPPE_GAME_GAME_SESSION_HH
#define MOPPE_GAME_GAME_SESSION_HH

#include <moppe/game/game_state.hh>
#include <moppe/game/world.hh>
#include <moppe/map/generate.hh>
#include <moppe/map/surface.hh>

namespace moppe::game {
  // The mutable state of one playable session on a completed world.  The
  // world retains the map and surface; vehicles and the glider borrow them
  // for their physical readings.  A checkpoint is therefore meaningful only
  // on the same completed world.
  class GameSession {
  public:
    using State = GameState;

    GameSession (const WorldParams& world,
                 const map::RandomHeightMap& terrain,
                 const map::Surface& surface);
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
}

#endif
