#include <moppe/game/dust.hh>

#include <algorithm>

namespace moppe::game {
  Dust::Dust () = default;

  Dust::State Dust::state () const {
    return { m_emissions, m_next_id, m_logical_time };
  }

  void Dust::restore (const State& state) {
    m_emissions = state.emissions;
    m_next_id = state.next_id;
    m_logical_time = state.logical_time;
  }

  void
  Dust::emit (position_t pos, velocity_t vel, int count, DisplayColor color) {
    emit (pos, vel, count, color, Style ());
  }

  void Dust::emit (position_t pos,
                   velocity_t vel,
                   int count,
                   DisplayColor color,
                   const Style& style) {
    std::size_t live_particles = 0;
    for (const Emission& emission : m_emissions)
      live_particles += emission.particle_count;
    const int available = std::max (0, 500 - static_cast<int> (live_particles));
    const int accepted = std::clamp (count, 0, available);
    if (accepted == 0)
      return;
    for (int remaining = accepted; remaining > 0;) {
      Emission emission;
      emission.id = m_next_id++;
      emission.birth_time = m_logical_time;
      emission.position = pos;
      emission.velocity = vel;
      emission.color = color;
      emission.style = style;
      emission.particle_count =
        static_cast<uint32_t> (std::min (remaining, 64));
      m_emissions.push_back (emission);
      remaining -= emission.particle_count;
    }
  }

  void Dust::update (seconds_t dt) {
    m_logical_time += dt;
    std::erase_if (m_emissions, [this] (const Emission& emission) {
      return m_logical_time - emission.birth_time >
             emission.style.lifetime * 0.9f;
    });
  }

  void Dust::render (render::Renderer& renderer) const {
    std::vector<render::DustEmission> payload;
    payload.reserve (m_emissions.size ());
    for (const Emission& emission : m_emissions) {
      payload.push_back (
        { .id = emission.id,
          .birth_time = seconds_value (emission.birth_time),
          .position = position_value (emission.position),
          .velocity = velocity_value (emission.velocity),
          .color = emission.color,
          .size = meters_value (emission.style.size),
          .life = seconds_value (emission.style.lifetime),
          .gravity = emission.style.downward_acceleration.numerical_value_in (
            u::m / pow<2> (u::s)),
          .spread = scalar_value (emission.style.spread),
          .particle_count = emission.particle_count,
          .additive = emission.style.additive });
    }
    renderer.draw_dust (payload, seconds_value (m_logical_time));
  }
}
