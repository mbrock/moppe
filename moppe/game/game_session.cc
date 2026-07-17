#include <moppe/game/game_session.hh>

namespace moppe::game {
  GameSession::GameSession (const WorldParams& world,
                            const map::RandomHeightMap& terrain,
                            const map::Surface& surface)
      : m_bike (world.spawn_position (),
                45 * u::deg,
                terrain,
                2600 * u::N,
                30 * u::kW,
                150 * u::kg),
        m_car (world.spawn_position (),
               45 * u::deg,
               terrain,
               14 * u::kN,
               100 * u::kW,
               900 * u::kg),
        m_glider (surface), m_camera (18 * u::deg, 6.5f * u::m) {}

  GameSession::State GameSession::state () const {
    return { m_logic,           m_bike.state (),   m_car.state (),
             m_glider.state (), m_walker.state (), m_camera.state (),
             m_stars.state (),  m_dust.state () };
  }

  void GameSession::restore (const State& state) {
    m_logic = state.logic;
    m_bike.restore (state.vehicle);
    m_car.restore (state.car);
    m_glider.restore (state.glider);
    m_walker.restore (state.walker);
    m_camera.restore (state.camera);
    m_stars.restore (state.stars);
    m_dust.restore (state.dust);
  }
}
