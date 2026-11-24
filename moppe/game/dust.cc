#include <moppe/game/dust.hh>
#include <moppe/gfx/signal.hh>

#include <algorithm>

namespace moppe::game {
  Dust::Dust () = default;

  void Dust::draw_powder (render::DrawList& dl, const FrameEnv& env) const {
    // A cloudlet is a lit dome in the camera plane. Its optical depth
    // falls to zero at the rim. The chips are smaller, sharp facets and
    // fall much faster. Both move in world space, not with the camera.
    render::DrawState draw;
    draw.blend = true;
    draw.depth_write = false;
    draw.cull = false;
    dl.state (draw);
    dl.begin (render::Prim::Triangles);
    for (const Emission& e : m_emissions) {
      if (!e.style.powder)
        continue;
      const seconds_t age = m_logical_time - e.birth_time;
      const auto phase = std::clamp (
        (age / (0.9f * e.style.lifetime)).numerical_value_in (one), 0.0f, 1.0f);
      const bool chip = e.style.flake;
      for (unsigned i = 0; i < e.particle_count; ++i) {
        const auto n = std::uint32_t (e.id * 67 + i);
        const velocity_t scatter =
          velocity (Vec3 (hash_lane (n, 0) - 0.5f,
                          hash_lane (n, 1) - 0.35f,
                          hash_lane (n, 2) - 0.5f) *
                    (3.0f * e.style.spread.numerical_value_in (one)));
        const displacement_t travel =
          quantity_cast<isq::displacement> ((e.velocity + scatter) * age);
        const meters_t fall = 0.5f * e.style.downward_acceleration * age * age;
        const position_t centre =
          e.position +
          quantity_cast<isq::position_vector> (
            travel - along (unit_direction (Vec3 (0, 1, 0)), fall));
        const meters_t radius = e.style.size *
                                (0.7f + 0.5f * hash_lane (n, 3)) *
                                (chip ? 1.0f : 0.45f + phase * 1.15f);
        const proportion_t alpha =
          (chip ? 0.96f : 0.38f) * (1.0f - phase) * proportion[one];
        const int sides = chip ? 3 : 8;
        const radians_t angle =
          (hash_lane (n, 4) * 6.28f + phase * (chip ? 3.6f : 0.45f)) * u::rad;
        const auto rim = [&] (int j) {
          const radians_t a = angle + (2.0f * PI * j / sides) * u::rad;
          const direction_t ray =
            env.cam_right * libm::cos (a) + env.cam_up * libm::sin (a);
          dl.normal (unit_direction (
            normalized (Vec3 (0, 0.92f, 0) + 0.28f * direction_value (ray) +
                        0.30f * direction_value (env.sun_dir))));
          dl.color (e.color, chip ? alpha : proportion_t {});
          dl.vertex (centre +
                     quantity_cast<isq::position_vector> (along (ray, radius)));
        };
        for (int j = 0; j < sides; ++j) {
          dl.normal (unit_direction (normalized (
            Vec3 (0, 0.95f, 0) + 0.35f * direction_value (env.sun_dir))));
          dl.color (e.color, alpha);
          dl.vertex (centre);
          rim (j);
          rim ((j + 1) % sides);
        }
      }
    }
    dl.end ();
  }

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
}
