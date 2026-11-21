#include <moppe/game/walker.hh>

#include <algorithm>
#include <cmath>

namespace moppe {
  namespace game {
    meters_t still_water_at (const WorldParams& world,
                             const map::SurfaceGeometry& surface,
                             const terrain::WaterSheets* water,
                             const position_t& at) {
      if (!water)
        return world.water_level;
      const position_t where = at_height (plan (at), 0.0f * u::m);
      const meters_t level =
        terrain::surface_elevation_value (
          spatial::sample<terrain::surface_elevation> (*water, where)) *
        u::m;
      const meters_t ground =
        terrain::surface_elevation_value (
          spatial::sample<terrain::surface_elevation> (surface, where)) *
        u::m;
      return level > ground ? std::max (level, world.water_level)
                            : world.water_level;
    }

    terrain::SeaWaves water_waves_at (const map::SurfaceGeometry& surface,
                                      const terrain::WaterSheets* water,
                                      const position_t& at,
                                      double time) {
      if (!water)
        return {};
      const position_t where = at_height (plan (at), 0.0f * u::m);
      const terrain::ElevationChange standing =
        spatial::sample<terrain::surface_elevation> (*water, where) -
        spatial::sample<terrain::surface_elevation> (surface, where);
      return terrain::sea_waves_at (
        plan (at),
        time,
        terrain::sea_wave_scale (
          spatial::sample<terrain::wave_amplitude> (*water, where),
          standing.numerical_value_in (u::m) * u::m));
    }

    meters_t water_surface_at (const WorldParams& world,
                               const map::SurfaceGeometry& surface,
                               const terrain::WaterSheets* water,
                               const position_t& at,
                               double time) {
      return still_water_at (world, surface, water, at) +
             water_waves_at (surface, water, at, time).height;
    }

    speed_t wading_speed (meters_t depth) {
      // Through the ankles, the shins, the knees, the waist, and the chest.
      constexpr int stages = 5;
      constexpr meters_t depths[stages] = {
        0.1f * u::m, 0.3f * u::m, 0.5f * u::m, 1.0f * u::m, 1.5f * u::m
      };
      constexpr speed_t speeds[stages] = { Walker::run_speed,
                                           3.0f * u::m / u::s,
                                           Walker::wade_speed,
                                           1.2f * u::m / u::s,
                                           0.8f * u::m / u::s };
      if (depth <= depths[0])
        return speeds[0];
      for (int k = 1; k < stages; ++k)
        if (depth < depths[k])
          return std::lerp (
                   speed_value (speeds[k - 1]),
                   speed_value (speeds[k]),
                   ((depth - depths[k - 1]) / (depths[k] - depths[k - 1]))
                     .numerical_value_in (one)) *
                 u::m / u::s;
      return speeds[stages - 1];
    }

    Walker::Walker ()
        : m_heading (unit_direction (Vec3 (0, 0, 1))), m_turn {}, m_walk {},
          m_strafe {}, m_run (false), m_jump_request (false), m_phase {},
          m_stride_speed (0 * u::m / u::s),
          m_facing (unit_direction (Vec3 (0, 0, 1))) {}

    Walker::State Walker::state () const {
      return { m_body.state (), m_heading, m_turn,         m_walk,
               m_strafe,        m_run,     m_jump_request, m_phase,
               m_stride_speed,  m_facing,  m_turn_speed,   m_turning,
               m_lag,           m_swim };
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
      m_swim = state.swim;
    }

    void Walker::turn_by (radians_t angle) {
      Vec3 heading = Quaternion::rotate (
        direction_value (m_heading), Vec3 (0, 1, 0), -angle);
      heading[1] = 0.0f;
      m_heading = unit_direction (normalized (heading));
    }

    void Walker::spawn (position_t pos, const direction_t& heading) {
      m_body.spawn (pos);
      const Vec3& way = direction_value (heading);
      Vec3 level (way[0], 0, way[2]);
      if (length2 (level) < 0.01f)
        level = Vec3 (0, 0, 1);
      m_heading = unit_direction (normalized (level));
      m_facing = m_heading;
      m_turn_speed = {};
      m_turning = false;
      m_lag = {};
      m_turn = {};
      m_walk = {};
      m_strafe = {};
      m_jump_request = false;
      m_swim = Swim::none;
      m_phase = {};
      m_stride_speed = 0 * u::m / u::s;
    }

    void Walker::spawn (position_t pos,
                        const direction_t& heading,
                        velocity_t velocity) {
      spawn (pos, heading);
      m_body.spawn (pos, velocity);
    }

    void Walker::update (seconds_t dt,
                         const map::SurfaceGeometry& surface,
                         const WorldParams& world,
                         const mov::TrunkField* trunks,
                         const terrain::WaterSheets* water,
                         double time,
                         control_signal_t swim_lift) {
      if (abs (m_turn) > 0.01f * control_signal[one]) {
        const radians_per_second_t rate =
          -m_turn.numerical_value_in (one) * 2.4f * u::rad / u::s;
        m_heading = unit_direction (Quaternion::rotate (
          direction_value (m_heading), Vec3 (0, 1, 0), rate * dt));
      }

      // A brisk walk, or a run with Shift held; wading slows either.
      const position_t feet = position ();
      const meters_t depth =
        water_surface_at (world, surface, water, feet, time) -
        position_value (feet)[1] * u::m;
      // Strike out at chest depth; stand again when the bed takes the
      // feet in shallower water. A passing trough is not a shore.
      constexpr meters_t waterline = 1.28f * u::m;
      if (!swimming () && depth >= 1.38f * u::m)
        m_swim = Swim::surface;
      else if (depth <= 0.0f * u::m ||
               (m_body.grounded () && depth < 1.16f * u::m))
        m_swim = Swim::none;
      const speed_t speed =
        swimming ()
          ? 1.85f * u::m / u::s
          : std::min (m_run ? run_speed : walk_speed, wading_speed (depth));

      const Vec3& ahead = direction_value (m_heading);
      const direction_t right =
        unit_direction (Vec3 (ahead[2], 0.0f, -ahead[0]));
      // The sticks' way and how far over they are pushed.
      const Vec3 stick =
        ahead * m_walk.numerical_value_in (one) -
        direction_value (right) * m_strafe.numerical_value_in (one);
      const proportion_t amount =
        std::min (1.0f, length (stick)) * proportion[one];
      velocity_t wish = moppe::velocity (stick);
      if (amount > 0.0f * proportion[one])
        wish = moppe::velocity (
          normalized (stick) *
          (amount.numerical_value_in (one) * speed_value (speed)));
      // A current over the knees pushes the legs downstream, and more the
      // deeper they stand in it.
      if (water && depth > 0.15f * u::m) {
        const velocity_t current =
          moppe::velocity (spatial::sample<terrain::water_velocity> (
                             *water, at_height (plan (feet), 0.0f * u::m))
                             .numerical_value_in (u::m / u::s));
        const Vec3& flow = velocity_value (current);
        wish += moppe::velocity (
          Vec3 (flow[0], 0.0f, flow[2]) *
          (swimming () ? 1.0f
                       : 0.6f * std::min (1.0f, meters_value (depth) - 0.15f)));
      }

      // On the rope, what would take the walker further out, or in
      // nearer, only leans on it; beyond either, it sets them back.
      if (m_tethered) {
        Vec3 out = (feet - m_tether).numerical_value_in (si::metre);
        out[1] = 0.0f;
        const meters_t distance = length (out) * u::m;
        if (distance > 1e-4f * u::m) {
          out *= 1.0f / meters_value (distance);
          const speed_t away = dot (velocity_value (wish), out) * (u::m / u::s);
          constexpr speed_t still = 0.0f * (u::m / u::s);
          if (distance > m_tether_longest) {
            if (away > still)
              wish -= moppe::velocity (out * speed_value (away));
            wish -= moppe::velocity (
              out * std::min (
                      2.0f, 4.0f * meters_value (distance - m_tether_longest)));
          } else if (distance < m_tether_shortest) {
            if (away < still)
              wish -= moppe::velocity (out * speed_value (away));
            wish += moppe::velocity (
              out *
              std::min (2.0f,
                        4.0f * meters_value (m_tether_shortest - distance)));
          }
        }
        m_tethered = false;
      }

      if (swimming ()) {
        if (swim_lift < -0.1f * control_signal[one])
          m_swim = Swim::submerged;
        else if (depth <= waterline + 0.08f * u::m)
          m_swim = Swim::surface;
        // Tread at the moving waterline. Once deliberately submerged,
        // neutral holds depth and the rise control brings the head up.
        const speed_t lift =
          m_swim == Swim::surface
            ? std::clamp ((depth - waterline) * 7.0f / u::s,
                          -2.0f * u::m / u::s,
                          2.0f * u::m / u::s)
            : swim_lift.numerical_value_in (one) * 1.4f * u::m / u::s;
        velocity_value (wish)[1] = speed_value (lift);
      }
      m_body.step (dt, { wish, m_jump_request, swimming () }, surface, trunks);
      m_jump_request = false;

      // The stride counts only ground the feet actually covered: pressed
      // against a trunk the legs stop rather than run on the spot.
      const meters_t covered = length (plan (position () - feet));
      const speed_t step_speed = covered / dt;

      // The body comes round after the heading. Under way it turns
      // toward it briskly but no faster than a body can, so a quick
      // look round has it lean through the turn rather than snap; a
      // runner's momentum turns slower than a walker. Standing, the head
      // and chest look round first and the body stays, until the look
      // goes further aside than they can twist: then the feet step round
      // on the spot until it faces the look again.
      constexpr radians_t full_turn = PI2 * u::rad;
      // The look's way round from the body, counted on from the last
      // step's so that a look swung round faster than the body can
      // follow passes behind it and the body keeps turning the way the
      // look went, rather than turning back to meet it the short way.
      radians_t aside = yaw_between (m_facing, m_heading);
      aside +=
        PI2 *
        std::round (((m_lag - aside) / full_turn).numerical_value_in (one)) *
        u::rad;
      aside = std::clamp (aside, -1.1f * PI * u::rad, 1.1f * PI * u::rad);
      const bool under_way = !grounded () ||
                             step_speed > 0.5f * (u::m / u::s) ||
                             amount > 0.05f * proportion[one];
      radians_t turned = 0.0f * u::rad;
      if (under_way) {
        m_turning = false;
        const radians_t most = (m_run ? 4.5f : 6.0f) * u::rad / u::s * dt;
        turned = std::clamp (
          aside * std::min (1.0f, 9.0f * seconds_value (dt)), -most, most);
      } else {
        if (abs (aside) > 1.25f * u::rad)
          m_turning = true;
        if (m_turning) {
          const radians_t pace =
            std::clamp (4.0f * abs (aside), 1.4f * u::rad, 3.4f * u::rad) /
            u::s * dt;
          turned = std::copysign (radians_value (std::min (abs (aside), pace)),
                                  radians_value (aside)) *
                   u::rad;
          if (abs (aside - turned) < 0.03f * u::rad)
            m_turning = false;
        }
      }
      Vec3 facing = yawed (direction_value (m_facing), turned);
      facing[1] = 0.0f;
      m_facing = unit_direction (normalized (facing));
      m_turn_speed = turned / dt;
      m_lag = aside - turned;

      if (grounded () || swimming ()) {
        if (swimming ()) {
          // Ease from sculling upright to a full stroke, independent of
          // the current: a drifting swimmer need not stroke at all.
          const speed_t change = 5.0f * u::m / pow<2> (u::s) * dt;
          m_stride_speed += std::clamp (
            amount.numerical_value_in (one) * speed - m_stride_speed,
            -change,
            change);
        } else
          m_stride_speed = step_speed;
        const per_second_t rate =
          swimming () ? (0.46f + 0.46f * amount.numerical_value_in (one)) / u::s
                      : gait_rate (step_speed, m_turn_speed);
        m_phase += (dt * rate).numerical_value_in (one) * proportion[one];
        m_phase -=
          std::floor (m_phase.numerical_value_in (one)) * proportion[one];
      }
    }

    meters_t Walker::landing_dip () const {
      // Knees take the landing over a tenth of a second and give it back
      // over the next few: t e^(1 - t) peaks at one when t is one.
      const mov::Character::State body = m_body.state ();
      if (!body.grounded)
        return 0.0f * u::m;
      const meters_t depth =
        std::min (0.3f * u::m, 0.022f * u::s * body.landing_speed);
      const auto t = (body.since_landing / (0.09f * u::s)).in (one);
      return t > 8.0f * one ? 0.0f * u::m
                            : depth * t.numerical_value_in (one) *
                                std::exp (1.0f - t.numerical_value_in (one));
    }

    position_t Walker::eye_position () const {
      if (swimming ())
        return moppe::position (position_value (position ()) +
                                direction_value (m_facing) * 0.45f +
                                Vec3 (0, 1.46f, 0));
      const mov::Character::State body = m_body.state ();
      // A subtle bob, lowest as each foot strikes: a centimetre or two at
      // a walk, a little more at a run, nothing standing or in the air.
      meters_t bob = 0.0f * u::m;
      if (body.grounded) {
        const proportion_t walking =
          std::min (1.0f,
                    (m_stride_speed / walk_speed).numerical_value_in (one)) *
          proportion[one];
        const proportion_t running =
          std::clamp (((m_stride_speed - walk_speed) / (run_speed - walk_speed))
                        .numerical_value_in (one),
                      0.0f,
                      1.0f) *
          proportion[one];
        const meters_t amplitude =
          0.018f * u::m * walking.numerical_value_in (one) +
          0.014f * u::m * running.numerical_value_in (one);
        bob =
          -amplitude * std::cos (2.0f * PI2 * m_phase.numerical_value_in (one));
      }
      const meters_t lift =
        body.step_offset + eye_height + bob - landing_dip ();
      return position () + quantity_cast<isq::position_vector> (displacement (
                             Vec3 (0.0f, meters_value (lift), 0.0f)));
    }
  }
}
