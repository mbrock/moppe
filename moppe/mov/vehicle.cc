#include <moppe/mov/box3d_vec.hh>
#include <moppe/mov/vehicle.hh>

#include <algorithm>
#include <cmath>

namespace moppe {
  namespace mov {
    namespace {
      namespace vehicle_rig {
        // How deep the wheels of a bike whose reference point stands
        // `ride_height` over the ground at `height` run in water standing
        // at `level`: 0 dry, 1 from 0.6 m, a little over the axles.
        proportion_t
        wading_depth (meters_t height, meters_t level, meters_t ride_height) {
          return std::clamp (quantity_cast<proportion> (
                               (level - height + ride_height) / (0.6f * u::m)),
                             0.0f * proportion[one],
                             1.0f * proportion[one]);
        }

        constexpr acceleration_component_t boost_acceleration =
          26.0f * isq::acceleration[u::m / pow<2> (u::s)];
        constexpr radians_t boost_max_tilt = 60.0f * u::deg;
        constexpr seconds_t boost_full_burn_time = 3.0f * u::s;
        constexpr seconds_t boost_recharge_time = 5.0f * u::s;
        constexpr seconds_t boost_recharge_pause = 0.65f * u::s;
        constexpr proportion_t boost_reserve_charge = 0.06f * proportion[one];
        constexpr proportion_t boost_emergency_level = 0.18f * proportion[one];

        constexpr per_second_t steering_smoothing = 9.0f / u::s;
        constexpr radians_t full_turn = 2.0f * 3.14159265f * u::rad;

        // Keeps an angle in [0, 2pi).
        radians_t wrapped (radians_t angle) {
          radians_t a = fmod (angle, full_turn);
          if (a < 0.0f * u::rad)
            a += full_turn;
          return a;
        }
      }
    }

    Vehicle::Vehicle (position_t position,
                      degrees_t orientation,
                      const SurfaceGeometry& surface,
                      newtons_t max_thrust,
                      watts_t power,
                      kilograms_t mass,
                      const BikeRig& rig)
        : m_map (surface), m_mass (mass) {
      RigidBike::Params params;
      params.max_drive_force = max_thrust;
      params.power = power;
      params.mass = mass;
      params.rig = rig;
      Vec3 at = position_value (position);
      at[1] = ground_height (surface, at[0], at[2]) +
              meters_value (rig.ride_height ());
      m_rigid = std::make_unique<RigidBike> (
        surface, params, at, Vec3 (sin (orientation), 0.0f, cos (orientation)));
      m_fall_top = static_cast<float> (at[1]) * u::m;
    }

    Vehicle::~Vehicle () = default;

    Vehicle::State Vehicle::state () const {
      State state;
      state.rigid = m_rigid->state ();
      // The coarse pose is derived from the snapshot itself, so restoring
      // an unedited state finds them equal bit for bit.
      state.position = moppe::position (m_rigid->reference_of (state.rigid));
      state.velocity = moppe::velocity (state.rigid.chassis.linear_velocity);
      state.heading = unit_direction (RigidBike::forward_of (state.rigid));
      state.yaw = m_yaw;
      state.yaw_target = m_yaw_target;
      state.wheel_spin = m_wheel_spin;
      state.boost_flight = m_boost_flight;
      state.thrust = m_thrust;
      state.boost_input = m_boost_input;
      state.boost_drive = m_boost_drive;
      state.boost_level = m_boost_level;
      state.boost_charge = m_boost_charge;
      state.boost_recharge_delay = m_boost_recharge_delay;
      state.water_level = m_water_level;
      state.airborne_time = m_airborne_time;
      state.impact = m_impact;
      state.fall_top = m_fall_top;
      state.fall_drop = m_fall_drop;
      state.parked = m_parked;
      state.skid = m_skid;
      state.wheelie = m_wheelie;
      state.crank = m_crank;
      return state;
    }

    void Vehicle::restore (const State& state) {
      m_yaw = state.yaw;
      m_yaw_target = state.yaw_target;
      m_wheel_spin = state.wheel_spin;
      m_boost_flight = state.boost_flight;
      m_thrust = state.thrust;
      m_boost_input = state.boost_input;
      m_boost_drive = state.boost_drive;
      m_boost_level = state.boost_level;
      m_boost_charge = state.boost_charge;
      m_boost_recharge_delay = state.boost_recharge_delay;
      m_water_level = state.water_level;
      m_airborne_time = state.airborne_time;
      m_impact = state.impact;
      m_fall_top = state.fall_top;
      m_fall_drop = state.fall_drop;
      m_parked = state.parked;
      m_skid = state.skid;
      m_wheelie = state.wheelie;
      m_crank = state.crank;
      RigidBikeState rigid = state.rigid;
      const Vec3& position = position_value (state.position);
      const Vec3& velocity = velocity_value (state.velocity);
      if (!(position == m_rigid->reference_of (rigid) &&
            velocity == rigid.chassis.linear_velocity &&
            direction_value (state.heading) == RigidBike::forward_of (rigid)))
        rigid = m_rigid->repose (
          rigid, position, direction_value (state.heading), velocity);
      m_rigid->restore (rigid);
    }

    void Vehicle::reset (const Vec3& position) {
      m_boost_input = {};
      m_boost_drive = {};
      m_boost_level = {};
      m_boost_charge = 1.0f * proportion[one];
      m_boost_recharge_delay = {};
      m_boost_flight = false;
      m_impact = {};
      m_rigid->place (
        position, direction_value (forward ()), Vec3 (0, 1, 0), Vec3 ());
    }

    void Vehicle::set_heading (const Vec3& h) {
      Vec3 v (h[0], 0, h[2]);
      if (length2 (v) <= 0.0001f)
        return;
      m_rigid->place (m_rigid->reference_position (),
                      normalized (v),
                      Vec3 (0, 1, 0),
                      m_rigid->velocity ());
    }

    // The trigger meters a finite reserve.  Recharging pauses after a
    // burn and is deliberately slower in the air, so feathering the
    // jets cannot produce permanent flight.
    void Vehicle::update_jets (seconds_t dt, bool grounded) {
      // The share of a full charge a full burn spends over the step.
      const proportion_t burn =
        quantity_cast<proportion> (dt / vehicle_rig::boost_full_burn_time);
      if (m_boost_input > 0.001f * proportion[one]) {
        if (m_boost_charge > vehicle_rig::boost_reserve_charge) {
          const proportion_t available = quantity_cast<proportion> (
            (m_boost_charge - vehicle_rig::boost_reserve_charge) / burn);
          m_boost_level = std::min (m_boost_input, available);
          m_boost_charge = std::max (
            vehicle_rig::boost_reserve_charge,
            m_boost_charge - quantity_cast<proportion> (m_boost_level * burn));
          m_boost_flight = true;
        } else
          // The reserve cannot sustain flight, but it always supplies enough
          // thrust to take the cruelty out of a long fall.
          m_boost_level = quantity_cast<proportion> (
            vehicle_rig::boost_emergency_level * m_boost_input);
        // The trigger must be released before recharge starts; this avoids
        // alternating reserve thrust and tiny rechargeable impulses.
        m_boost_recharge_delay = vehicle_rig::boost_recharge_pause;
      } else {
        m_boost_level = {};
        if (m_boost_recharge_delay > seconds_t {})
          m_boost_recharge_delay -= dt;
        else {
          // In the air the jets recharge at a share of the ground's rate.
          const proportion_t recharge_scale =
            (grounded ? 1.0f : 0.35f) * proportion[one];
          m_boost_charge = std::min (
            1.0f * proportion[one],
            m_boost_charge +
              quantity_cast<proportion> (
                recharge_scale * (dt / vehicle_rig::boost_recharge_time)));
        }
      }
    }

    // A landing's impact is the speed into the ground just before the
    // wheels touched, and collisions of the chassis itself report their
    // approach speed: an obstacle (a trunk, a railing) glancingly, the
    // frame slamming the ground in full.
    void Vehicle::update (seconds_t dt) {
      bool first = true;
      in_substeps (dt, [&] (seconds_t h) {
        substep (h, first);
        first = false;
      });
      m_hop = {};
    }

    void Vehicle::substep (seconds_t h, bool first) {
      m_yaw += radians_t ((m_yaw_target - m_yaw) *
                          smoothing_alpha (vehicle_rig::steering_smoothing, h));
      update_jets (h, m_rigid->grounded ());

      Vec3 ahead = m_rigid->forward ();
      ahead[1] = 0.0f;
      ahead = length2 (ahead) > 1e-6f ? normalized (ahead) : Vec3 (0, 0, 1);
      const radians_t tilt = vehicle_rig::boost_max_tilt *
                             abs (m_boost_drive).numerical_value_in (one);
      const control_signal_t drive_sign =
        (m_boost_drive < control_signal_t {} ? -1.0f : 1.0f) *
        control_signal[one];
      const direction_t boost_direction = unit_direction (
        Vec3 (0, cos (tilt), 0) +
        ahead * (drive_sign * sin (tilt)).numerical_value_in (one));

      RigidBikeControls controls;
      controls.throttle = m_thrust;
      controls.steer = m_yaw;
      controls.boost_acceleration = quantity_cast<isq::acceleration> (
        boost_direction * (vehicle_rig::boost_acceleration *
                           m_boost_level.numerical_value_in (one)));
      controls.parked = m_parked;
      controls.skid = m_skid;
      controls.wheelie = m_wheelie;
      controls.hop = first ? m_hop : speed_t {};
      if (m_hung) {
        controls.throttle = {};
        controls.steer = radians_t {};
        controls.skid = {};
        controls.wheelie = {};
        controls.boost_acceleration = m_hang.acceleration;
        controls.hung = true;
        controls.hang_forward = m_hang.forward;
        controls.hang_up = m_hang.up;
      }
      controls.wading = vehicle_rig::wading_depth (
        static_cast<float> (m_rigid->reference_position ()[1]) * u::m,
        m_water_level,
        m_rigid->rig ().ride_height ());

      const Vec3 before = m_rigid->velocity ();
      m_rigid->step (controls, h);
      const Vec3 at = m_rigid->reference_position ();
      const meters_t height = static_cast<float> (at[1]) * u::m;

      if (m_rigid->grounded ()) {
        if (m_airborne_time > 0.25f * u::s) {
          const Vec3 n = ground_normal (m_map, at[0], at[2]);
          m_impact = std::max (m_impact,
                               std::max (0.0f, -dot (before, n)) * u::m / u::s);
          // Boost-assisted landings are partly forgiven: the jets flare
          // on touchdown, or so the story goes.
          if (m_boost_flight)
            m_impact *= 0.75f;
          m_fall_drop = m_fall_top - height;
        }
        if (m_boost_level <= 0)
          m_boost_flight = false;
        m_airborne_time = {};
        m_fall_top = height;
      } else {
        m_airborne_time += h;
        // A glide is no fall, however far down it ends.
        m_fall_top = m_hung ? height : std::max (m_fall_top, height);
      }
      m_impact = std::max (m_impact, speed_t (0.4f * m_rigid->trunk_hit ()));
      m_impact = std::max (m_impact, m_rigid->body_hit ());

      const radians_per_second_t wheel_rate = m_rigid->wheel_spin_rate ();
      m_wheel_spin = vehicle_rig::wrapped (m_wheel_spin + wheel_rate * h);

      // Pedalling, the rider keeps to a cadence of some eight radians a
      // second through the gears, from the lowest, which turns the wheel
      // a little slower than the crank, to the highest, three times as
      // fast; a wheel off the ground spins up to it. A single speed's
      // crank goes round with the wheel at its one gear.
      if (m_thrust > 0.02f * control_signal[one] && !m_hung && !m_parked) {
        constexpr radians_per_second_t pedalling = 8.0f * u::rad / u::s;
        const radians_per_second_t wheel = abs (wheel_rate);
        const scale_factor_t gear = m_rigid->rig ().gear_ratio;
        const radians_per_second_t cadence =
          gear > scale_factor_t {} &&
              (m_rigid->rear_contact () || wheel > 1.0f * u::rad / u::s)
            ? wheel / gear.numerical_value_in (one)
          : m_rigid->rear_contact ()
            ? wheel / std::clamp (scalar_value (wheel / pedalling), 0.75f, 3.0f)
            : pedalling * m_thrust.numerical_value_in (one);
        m_crank = vehicle_rig::wrapped (m_crank + cadence * h);
      }
    }

    void Vehicle::set_boost (proportion_t boost, control_signal_t drive) {
      m_boost_input =
        std::clamp (boost, 0.0f * proportion[one], 1.0f * proportion[one]);
      m_boost_drive = std::clamp (
        drive, -1.0f * control_signal[one], 1.0f * control_signal[one]);
    }
  }
}
