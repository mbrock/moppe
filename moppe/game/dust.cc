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

  void Dust::emit (const Vector3D& pos,
                   const Vector3D& vel,
                   int count,
                   const Vector3D& color) {
    emit (pos, vel, count, color, Style ());
  }

  void Dust::emit (const Vector3D& pos,
                   const Vector3D& vel,
                   int count,
                   const Vector3D& color,
                   const Style& style) {
    std::size_t live_particles = 0;
    for (const render::DustEmission& emission : m_emissions)
      live_particles += emission.particle_count;
    const int available = std::max (0, 500 - static_cast<int> (live_particles));
    const int accepted = std::clamp (count, 0, available);
    if (accepted == 0)
      return;
    for (int remaining = accepted; remaining > 0;) {
      render::DustEmission emission;
      emission.id = m_next_id++;
      emission.birth_time = m_logical_time;
      emission.position = pos;
      emission.velocity = vel;
      emission.color = color;
      emission.size = style.size;
      emission.life = style.life;
      emission.gravity = style.gravity;
      emission.spread = style.spread;
      emission.particle_count =
        static_cast<uint32_t> (std::min (remaining, 64));
      emission.additive = style.additive;
      m_emissions.push_back (emission);
      remaining -= emission.particle_count;
    }
  }

  void Dust::update (float dt) {
    m_logical_time += dt;
    std::erase_if (m_emissions, [this] (const render::DustEmission& emission) {
      return m_logical_time - emission.birth_time > emission.life * 0.9f;
    });
  }

  void Dust::render (render::Renderer& renderer) const {
    renderer.draw_dust (m_emissions, m_logical_time);
  }
}
