#include <moppe/mov/jeep.hh>

#include <algorithm>
#include <cmath>

namespace moppe::mov {
  namespace {
    namespace jeep {
      constexpr float mass = 1200.0f;
      // Moments about the body's right, up, and forward axes.
      constexpr float inertia[3] = { 1500.0f, 1650.0f, 620.0f };
      constexpr float gravity = 9.81f;

      // The struts: where each hangs from above its wheel, and its
      // length standing, hanging free, and pressed home.
      constexpr float strut_top = 0.30f;
      constexpr float standing = 0.30f;
      constexpr float hanging = 0.42f;
      constexpr float pressed = 0.14f;
      constexpr float spring = mass * gravity / 4.0f / (hanging - standing);
      constexpr float damper = 3300.0f;
      constexpr float bump_stop = 160000.0f;

      constexpr float grip = 1.05f;
      constexpr float engine_force = 9500.0f;
      constexpr float engine_power = 80000.0f;
      constexpr float brake_force = 14000.0f;
      constexpr float reverse_speed = 7.0f;
      constexpr float full_lock = 0.55f;
      constexpr float steer_rate = 2.4f;

      constexpr float longest_step = 1.0f / 240.0f;

      // The corners of the tub and of the roll cage, which meet the
      // ground when the wheels do not.
      constexpr float shell[8][3] = {
        { -0.72f, 0.14f, 1.85f },  { 0.72f, 0.14f, 1.85f },
        { -0.72f, 0.14f, -1.85f }, { 0.72f, 0.14f, -1.85f },
        { -0.57f, 1.43f, 0.40f },  { 0.57f, 1.43f, 0.40f },
        { -0.57f, 1.42f, -1.45f }, { 0.57f, 1.42f, -1.45f },
      };

      float height (const map::SurfaceGeometry& surface, float x, float z) {
        return terrain::surface_elevation_value (
          spatial::sample<terrain::surface_elevation> (
            surface, moppe::position (Vec3 (x, 0.0f, z))));
      }

      Vec3 slope (const map::SurfaceGeometry& surface, float x, float z) {
        constexpr float e = 0.3f;
        return normalized (
          Vec3 (height (surface, x - e, z) - height (surface, x + e, z),
                2.0f * e,
                height (surface, x, z - e) - height (surface, x, z + e)));
      }

      Vec3 wheel_place (int wheel) {
        return Vec3 (wheel % 2 == 0 ? -Jeep::track : Jeep::track,
                     0.0f,
                     wheel < 2 ? Jeep::axle : -Jeep::axle);
      }
    }
  }

  Vec3 Jeep::point (const Vec3& local) const {
    return m_state.position + m_state.right * static_cast<float> (local[0]) +
           m_state.up * static_cast<float> (local[1]) +
           m_state.forward * static_cast<float> (local[2]);
  }

  bool Jeep::grounded () const {
    return std::any_of (m_state.touching.begin (),
                        m_state.touching.end (),
                        [] (bool touching) { return touching; });
  }

  void Jeep::set_parked (bool parked) {
    if (m_state.parked && !parked)
      m_state.asleep = false;
    m_state.parked = parked;
  }

  void Jeep::place (const Vec3& at,
                    const Vec3& heading,
                    const map::SurfaceGeometry& surface) {
    State placed;
    placed.parked = m_state.parked;
    Vec3 forward = heading;
    forward[1] = 0.0f;
    placed.forward =
      length2 (forward) > 1e-6f ? normalized (forward) : Vec3 (0, 0, 1);
    placed.up = Vec3 (0, 1, 0);
    placed.right = cross (placed.up, placed.forward);
    placed.position = at;
    // Level with the highest ground under any wheel, so none starts
    // inside it; the springs settle the rest.
    float ground = -1e9f;
    for (int wheel = 0; wheel < wheel_count; ++wheel) {
      const Vec3 local = jeep::wheel_place (wheel);
      const Vec3 under = at + placed.right * static_cast<float> (local[0]) +
                         placed.forward * static_cast<float> (local[2]);
      ground = std::max (ground, jeep::height (surface, under[0], under[2]));
    }
    placed.position[1] = ground + wheel_radius + 0.05f;
    m_state = placed;
  }

  // An impulse at a point of the body.
  void Jeep::push (const Vec3& impulse, const Vec3& at) {
    State& s = m_state;
    s.velocity = s.velocity + impulse * (1.0f / jeep::mass);
    const Vec3 turning = cross (at - s.position, impulse);
    s.spin = s.spin + s.right * (dot (turning, s.right) / jeep::inertia[0]) +
             s.up * (dot (turning, s.up) / jeep::inertia[1]) +
             s.forward * (dot (turning, s.forward) / jeep::inertia[2]);
  }

  void Jeep::update (float dt,
                     const map::SurfaceGeometry& surface,
                     float water_level,
                     const TrunkField* trunks) {
    if (m_state.asleep || dt <= 0.0f)
      return;
    const int steps =
      std::max (1, static_cast<int> (std::ceil (dt / jeep::longest_step)));
    for (int i = 0; i < steps; ++i)
      step (dt / steps, surface, water_level, trunks);
  }

  void Jeep::step (float dt,
                   const map::SurfaceGeometry& surface,
                   float water_level,
                   const TrunkField* trunks) {
    State& s = m_state;
    const float forward_speed = dot (s.velocity, s.forward);
    const float speed = length (s.velocity);

    // The wheel turns toward what is asked, less far the faster it goes.
    const float lock =
      jeep::full_lock / (1.0f + (speed / 13.0f) * (speed / 13.0f));
    const float wanted = std::clamp (s.turn, -1.0f, 1.0f) * lock;
    s.steer += std::clamp (
      wanted - s.steer, -jeep::steer_rate * dt, jeep::steer_rate * dt);
    s.wheel_spin = std::remainder (
      s.wheel_spin + forward_speed / wheel_radius * dt, 6.2831853f);

    const float drive = s.parked ? 0.0f : std::clamp (s.drive, -1.0f, 1.0f);
    // Asking for the other way while still rolling is braking.
    const bool against =
      drive * forward_speed < 0.0f && std::fabs (forward_speed) > 1.5f;
    const bool braking = s.parked || s.brake || against;

    push (Vec3 (0, -jeep::mass * jeep::gravity * dt, 0), s.position);

    const Vec3 down = s.up * -1.0f;
    for (int wheel = 0; wheel < wheel_count; ++wheel) {
      const Vec3 place = jeep::wheel_place (wheel);
      const Vec3 top = point (place + Vec3 (0, jeep::strut_top, 0));
      const Vec3 below = top + down * (jeep::standing + wheel_radius);
      const Vec3 normal = jeep::slope (surface, below[0], below[2]);
      const Vec3 ground (
        below[0], jeep::height (surface, below[0], below[2]), below[2]);
      const float facing = dot (down, normal);
      float reach = jeep::hanging;
      if (facing < -0.2f)
        reach = dot (ground - top, normal) / facing + wheel_radius / facing;
      const float was = jeep::standing + s.drop[wheel];
      s.touching[wheel] = reach < jeep::hanging;
      if (!s.touching[wheel]) {
        // The wheel falls to the end of its strut.
        s.drop[wheel] =
          std::min (jeep::hanging - jeep::standing, s.drop[wheel] + 2.0f * dt);
        continue;
      }
      const float length = std::max (reach, jeep::pressed);
      s.drop[wheel] = length - jeep::standing;
      float load =
        jeep::spring * (jeep::hanging - length) +
        jeep::damper * (was - length) / dt +
        jeep::bump_stop * (jeep::pressed - std::min (reach, jeep::pressed));
      load = std::clamp (load, 0.0f, 6.0f * jeep::mass * jeep::gravity);
      const Vec3 contact = top + down * (length + wheel_radius);
      push (normal * (load * dt), contact);

      // The tyre: its heading and its side, lying in the ground.
      Vec3 heading = s.forward;
      if (wheel < 2)
        heading = s.forward * std::cos (s.steer) + s.right * std::sin (s.steer);
      heading = heading - normal * dot (heading, normal);
      if (length2 (heading) < 1e-6f)
        continue;
      heading = normalized (heading);
      const Vec3 side = cross (normal, heading);
      const Vec3 moving = s.velocity + cross (s.spin, contact - s.position);
      const float rolling = dot (moving, heading);
      const float sliding = dot (moving, side);
      const float share = jeep::mass / wheel_count;

      float along = 0.0f;
      if (braking)
        along =
          -std::copysign (std::min (jeep::brake_force / wheel_count,
                                    0.5f * std::fabs (rolling) * share / dt),
                          rolling);
      else if (drive != 0.0f &&
               !(drive < 0.0f && rolling < -jeep::reverse_speed))
        along =
          drive *
          std::min (jeep::engine_force,
                    jeep::engine_power / std::max (std::fabs (rolling), 2.0f)) /
          wheel_count;
      // The drag of rolling, and of an engine left to idle.
      along -= rolling * (drive == 0.0f ? 70.0f : 22.0f);
      float across = -0.35f * sliding * share / dt;
      const float most = jeep::grip * load;
      const float asked = std::hypot (along, across);
      if (asked > most) {
        along *= most / asked;
        across *= most / asked;
      }
      push ((heading * along + side * across) * dt, contact);
    }

    // The shell on the ground.
    for (const auto& corner : jeep::shell) {
      const Vec3 at = point (Vec3 (corner[0], corner[1], corner[2]));
      const float depth = jeep::height (surface, at[0], at[2]) - at[1];
      if (depth <= 0.0f)
        continue;
      const Vec3 normal = jeep::slope (surface, at[0], at[2]);
      const Vec3 moving = s.velocity + cross (s.spin, at - s.position);
      const float closing = dot (moving, normal);
      const float load = std::clamp (90000.0f * depth - 7000.0f * closing,
                                     0.0f,
                                     8.0f * jeep::mass * jeep::gravity);
      const Vec3 scraping = moving - normal * closing;
      const float scrape = length (scraping);
      Vec3 force = normal * load;
      if (scrape > 1e-3f)
        force = force -
                scraping * (std::min (0.7f * load,
                                      0.3f * scrape * jeep::mass / 8.0f / dt) /
                            scrape);
      push (force * dt, at);
    }

    // Trunks stop it at the nose and the tail.
    if (trunks)
      for (const float along : { 1.15f, -1.15f }) {
        const Vec3 low = point (Vec3 (0, 0.25f, along));
        const TrunkContact contact =
          trunks->collide (low, low + Vec3 (0, 0.6f, 0), 0.8f);
        if (!contact.hit)
          continue;
        s.position = s.position + contact.push;
        const Vec3 at = low + contact.push;
        const Vec3 moving = s.velocity + cross (s.spin, at - s.position);
        const float closing = dot (moving, contact.normal);
        if (closing < 0.0f)
          push (contact.normal * (-1.15f * closing * jeep::mass * 0.5f), at);
      }

    // Air, and water up to the sills.
    const float wet =
      std::clamp ((water_level - (s.position[1] - 0.3f)) / 0.8f, 0.0f, 1.0f);
    s.velocity = s.velocity * std::exp (-(0.0006f * speed + 2.2f * wet) * dt);
    s.spin = s.spin * std::exp (-(0.5f + 2.0f * wet) * dt);

    s.position = s.position + s.velocity * dt;
    const Vec3 turned = s.spin * dt;
    s.forward = normalized (s.forward + cross (turned, s.forward));
    s.up = s.up + cross (turned, s.up);
    s.right = normalized (cross (s.up, s.forward));
    s.up = cross (s.forward, s.right);

    // Left on its side or back, it is set on its wheels again.
    const bool resting = length2 (s.velocity) < 2.0f * 2.0f;
    s.overturned = static_cast<float> (s.up[1]) < 0.25f && resting
                     ? s.overturned + dt
                     : 0.0f;
    if (s.overturned > 2.5f) {
      const bool parked = s.parked;
      const Vec3 heading = s.forward;
      place (s.position, heading, surface);
      m_state.parked = parked;
      return;
    }

    // Parked and still, it stays where it is.
    const bool quiet = length2 (s.velocity) < 0.08f * 0.08f &&
                       length2 (s.spin) < 0.08f * 0.08f && grounded ();
    s.still = s.parked && quiet ? s.still + dt : 0.0f;
    if (s.still > 1.0f) {
      s.asleep = true;
      s.velocity = Vec3 ();
      s.spin = Vec3 ();
    }
  }
}
