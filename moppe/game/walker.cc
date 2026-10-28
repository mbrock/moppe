#include <moppe/game/walker.hh>

#include <algorithm>
#include <cmath>

namespace moppe {
  namespace game {
    meters_t still_water_at (const WorldParams& world,
                             const map::SurfaceGeometry& surface,
                             const terrain::WaterSheets* water,
                             const Vec3& at) {
      if (!water)
        return world.water_level;
      const position_t where = moppe::position (Vec3 (at[0], 0.0f, at[2]));
      const float level = terrain::surface_elevation_value (
        spatial::sample<terrain::surface_elevation> (*water, where));
      const float ground = terrain::surface_elevation_value (
        spatial::sample<terrain::surface_elevation> (surface, where));
      return level > ground ? std::max (level * u::m, world.water_level)
                            : world.water_level;
    }

    terrain::SeaWaves water_waves_at (const map::SurfaceGeometry& surface,
                                      const terrain::WaterSheets* water,
                                      const Vec3& at,
                                      double time) {
      if (!water)
        return {};
      const position_t where = moppe::position (Vec3 (at[0], 0.0f, at[2]));
      const float level = terrain::surface_elevation_value (
        spatial::sample<terrain::surface_elevation> (*water, where));
      const float ground = terrain::surface_elevation_value (
        spatial::sample<terrain::surface_elevation> (surface, where));
      const float amplitude =
        spatial::sample<terrain::wave_amplitude> (*water, where)
          .numerical_value_in (mp_units::one);
      return terrain::sea_waves_at (
        at[0],
        at[2],
        time,
        terrain::sea_wave_scale (amplitude, level - ground));
    }

    meters_t water_surface_at (const WorldParams& world,
                               const map::SurfaceGeometry& surface,
                               const terrain::WaterSheets* water,
                               const Vec3& at,
                               double time) {
      return still_water_at (world, surface, water, at) +
             water_waves_at (surface, water, at, time).height * u::m;
    }

    float wading_speed (float depth) {
      // Through the ankles, the shins, the knees, the waist, and the chest.
      constexpr int stages = 5;
      constexpr float depths[stages] = { 0.1f, 0.3f, 0.5f, 1.0f, 1.5f };
      constexpr float speeds[stages] = {
        Walker::run_speed, 3.0f, Walker::wade_speed, 1.2f, 0.8f
      };
      if (depth <= depths[0])
        return speeds[0];
      for (int k = 1; k < stages; ++k)
        if (depth < depths[k])
          return std::lerp (speeds[k - 1],
                            speeds[k],
                            (depth - depths[k - 1]) /
                              (depths[k] - depths[k - 1]));
      return speeds[stages - 1];
    }

    Walker::Walker ()
        : m_heading (0, 0, 1), m_turn (0), m_walk (0), m_strafe (0),
          m_run (false), m_jump_request (false), m_phase (0),
          m_stride_speed (0 * u::m / u::s), m_facing (0, 0, 1) {}

    Walker::State Walker::state () const {
      return {
        m_body.state (), m_heading,      m_turn,  m_walk,         m_strafe,
        m_run,           m_jump_request, m_phase, m_stride_speed, m_facing,
        m_turn_speed,    m_turning,      m_lag
      };
    }

    void Walker::restore (const State& state) {
      m_body.restore (state.body);
      m_heading = state.heading;
      m_turn = state.turn;
      m_walk = state.walk;
      m_strafe = state.strafe;
      m_run = state.run;
      m_jump_request = state.jump_request;
      m_phase = state.stride_phase;
      m_stride_speed = state.stride_speed;
      m_facing = state.facing;
      m_turn_speed = state.turn_speed;
      m_turning = state.turning;
      m_lag = state.lag;
    }

    void Walker::turn_by (float radians) {
      m_heading =
        Quaternion::rotate (m_heading, Vec3 (0, 1, 0), -radians * u::rad);
      m_heading[1] = 0.0f;
      normalize (m_heading);
    }

    void Walker::spawn (position_t pos, const Vec3& heading) {
      m_body.spawn (pos);
      m_heading = Vec3 (heading[0], 0, heading[2]);
      if (length2 (m_heading) < 0.01f)
        m_heading = Vec3 (0, 0, 1);
      normalize (m_heading);
      m_facing = m_heading;
      m_turn_speed = 0.0f;
      m_turning = false;
      m_lag = 0.0f;
      m_turn = 0;
      m_walk = 0;
      m_strafe = 0;
      m_jump_request = false;
      m_phase = 0;
      m_stride_speed = 0 * u::m / u::s;
    }

    void
    Walker::spawn (position_t pos, const Vec3& heading, velocity_t velocity) {
      spawn (pos, heading);
      m_body.spawn (pos, velocity);
    }

    void Walker::update (seconds_t dt,
                         const map::SurfaceGeometry& surface,
                         const WorldParams& world,
                         const mov::TrunkField* trunks,
                         const terrain::WaterSheets* water,
                         double time) {
      const float turn = scalar_value (m_turn);
      if (std::abs (turn) > 0.01f)
        m_heading = Quaternion::rotate (
          m_heading, Vec3 (0, 1, 0), -turn * 2.4f * u::rad / u::s * dt);

      // A brisk walk, or a run with Shift held; wading slows either.
      const Vec3 feet = position ();
      const float depth =
        (water_surface_at (world, surface, water, feet, time) - feet[1] * u::m)
          .numerical_value_in (u::m);
      const float speed =
        std::min (m_run ? run_speed : walk_speed, wading_speed (depth));

      const Vec3 right (m_heading[2], 0.0f, -m_heading[0]);
      Vec3 wish =
        m_heading * scalar_value (m_walk) - right * scalar_value (m_strafe);
      const float amount = std::min (1.0f, length (wish));
      if (amount > 0.0f)
        wish = normalized (wish) * (amount * speed);
      // A current over the knees pushes the legs downstream, and more the
      // deeper they stand in it.
      if (water && depth > 0.15f) {
        const Vec3 current =
          spatial::sample<terrain::water_velocity> (
            *water, moppe::position (Vec3 (feet[0], 0.0f, feet[2])))
            .numerical_value_in (u::m / u::s);
        wish += Vec3 (current[0], 0.0f, current[2]) *
                (0.6f * std::min (1.0f, depth - 0.15f));
      }

      // On the rope, what would take the walker further out, or in
      // nearer, only leans on it; beyond either, it sets them back.
      if (m_tethered) {
        Vec3 out = feet - m_tether;
        out[1] = 0.0f;
        const float distance = length (out);
        if (distance > 1e-4f) {
          out *= 1.0f / distance;
          const float away = dot (wish, out);
          if (distance > m_tether_longest) {
            if (away > 0.0f)
              wish -= out * away;
            wish -= out * std::min (2.0f, 4.0f * (distance - m_tether_longest));
          } else if (distance < m_tether_shortest) {
            if (away < 0.0f)
              wish -= out * away;
            wish +=
              out * std::min (2.0f, 4.0f * (m_tether_shortest - distance));
          }
        }
        m_tethered = false;
      }

      m_body.step (dt, { wish, m_jump_request }, surface, trunks);
      m_jump_request = false;

      // The stride counts only ground the feet actually covered: pressed
      // against a trunk the legs stop rather than run on the spot.
      const Vec3 moved = position () - feet;
      const float covered =
        std::sqrt (moved[0] * moved[0] + moved[2] * moved[2]);
      const float step_speed = covered / seconds_value (dt);

      // The body comes round after the heading. Under way it turns
      // toward it briskly but no faster than a body can, so a quick
      // look round has it lean through the turn rather than snap; a
      // runner's momentum turns slower than a walker. Standing, the head
      // and chest look round first and the body stays, until the look
      // goes further aside than they can twist: then the feet step round
      // on the spot until it faces the look again.
      const float step = seconds_value (dt);
      // The look's way round from the body, counted on from the last
      // step's so that a look swung round faster than the body can
      // follow passes behind it and the body keeps turning the way the
      // look went, rather than turning back to meet it the short way.
      float aside = yaw_between (m_facing, m_heading);
      aside += PI2 * std::round ((m_lag - aside) / PI2);
      aside = std::clamp (aside, -1.1f * PI, 1.1f * PI);
      const bool under_way =
        !grounded () || step_speed > 0.5f || amount > 0.05f;
      float turned = 0.0f;
      if (under_way) {
        m_turning = false;
        const float most = (m_run ? 4.5f : 6.0f) * step;
        turned = std::clamp (aside * std::min (1.0f, 9.0f * step), -most, most);
      } else {
        if (std::abs (aside) > 1.25f)
          m_turning = true;
        if (m_turning) {
          const float pace =
            std::clamp (4.0f * std::abs (aside), 1.4f, 3.4f) * step;
          turned = std::copysign (std::min (std::abs (aside), pace), aside);
          if (std::abs (aside - turned) < 0.03f)
            m_turning = false;
        }
      }
      m_facing = yawed (m_facing, turned);
      m_facing[1] = 0.0f;
      normalize (m_facing);
      m_turn_speed = turned / step;
      m_lag = aside - turned;

      if (grounded ()) {
        m_stride_speed = step_speed * u::m / u::s;
        m_phase += step * gait_rate (step_speed, m_turn_speed);
        m_phase -= std::floor (m_phase);
      }
    }

    float Walker::landing_dip () const {
      // Knees take the landing over a tenth of a second and give it back
      // over the next few: t e^(1 - t) peaks at one when t is one.
      const mov::Character::State body = m_body.state ();
      if (!body.grounded)
        return 0.0f;
      const float impact = body.landing_speed.numerical_value_in (u::m / u::s);
      const float depth = std::min (0.3f, 0.022f * impact);
      const float t = seconds_value (body.since_landing) / 0.09f;
      return t > 8.0f ? 0.0f : depth * t * std::exp (1.0f - t);
    }

    Vec3 Walker::eye_position () const {
      const mov::Character::State body = m_body.state ();
      const float speed = m_stride_speed.numerical_value_in (u::m / u::s);
      // A subtle bob, lowest as each foot strikes: a centimetre or two at
      // a walk, a little more at a run, nothing standing or in the air.
      float bob = 0.0f;
      if (body.grounded) {
        const float walking = std::min (1.0f, speed / walk_speed);
        const float running = std::clamp (
          (speed - walk_speed) / (run_speed - walk_speed), 0.0f, 1.0f);
        const float amplitude = 0.018f * walking + 0.014f * running;
        bob = -amplitude * std::cos (2.0f * PI2 * m_phase);
      }
      return position () + Vec3 (0.0f,
                                 body.step_offset.numerical_value_in (u::m) +
                                   eye_height + bob - landing_dip (),
                                 0.0f);
    }
  }
}
