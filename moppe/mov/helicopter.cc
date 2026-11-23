#include <moppe/mov/box3d_vec.hh>
#include <moppe/mov/ground_patch.hh>
#include <moppe/mov/helicopter.hh>

#include <algorithm>
#include <cmath>

#include <box3d/box3d.h>

namespace moppe::mov {
  namespace {
    // Kept in a namespace of its own: the engine is a unity build.
    namespace heli_rig {
      constexpr int substeps = 4;

      constexpr kilograms_t mass = 1100.0f * u::kg;
      // Pitch, yaw, and roll about the centre of mass, under the mast.
      constexpr kilogram_square_metres_t inertia[3] = { 2400.0f * u::kg * u::m2,
                                                        2600.0f * u::kg* u::m2,
                                                        700.0f * u::kg* u::m2 };
      const displacement_t mass_centre =
        displacement (Vec3 (0.0f, 1.15f, 0.05f));

      // The rotor at flying speed lifts twice the helicopter's weight;
      // it takes a few seconds to get there and longer to run down.
      constexpr scale_factor_t thrust_to_weight = 2.0f * scale_factor[one];
      constexpr seconds_t spool_up = 3.5f * u::s;
      constexpr seconds_t spool_down = 10.0f * u::s;
      // Revolutions a second at flying speed, main and tail, as drawn:
      // slower than a real rotor's six or seven, which a frame at a time
      // would only smear into nothing.
      constexpr frequency_t rotor_rate = 2.2f * u::Hz;
      constexpr frequency_t tail_rate = 5.0f * u::Hz;

      // What the flight computer asks of it.
      constexpr speed_t top_speed = 45.0f * u::m / u::s;
      constexpr speed_t back_speed = 9.0f * u::m / u::s;
      constexpr speed_t climb_rate = 7.5f * u::m / u::s;
      constexpr speed_t descent_rate = 6.0f * u::m / u::s;
      constexpr radians_t max_tilt = 0.52f * u::rad; // 30 degrees
      // The speed and climb loops, the acceleration asked for a speed
      // short of the one wanted.
      constexpr damping_t speed_gain = 0.7f / u::s;
      constexpr damping_t climb_gain = 2.2f / u::s;
      // The heading loop turns at most this many radians a second.
      constexpr radians_per_second_t yaw_rate = 1.6f * u::rad / u::s;
      // The attitude loop, as a natural frequency and damping.
      constexpr frequency_t attitude_hertz = 0.85f * u::Hz;
      constexpr damping_ratio_t attitude_damping = 0.85f * damping_ratio[one];
      constexpr per_second_t yaw_gain = 4.0f / u::s;
      // Kept above the ground, metres: the hover taxi's height, and more
      // the faster it goes.
      constexpr meters_t taxi_height = 1.6f * u::m;
      constexpr seconds_t clearance_per_speed = 0.12f * u::s;

      // Half the air's density times the fuselage's drag area, and the
      // water's hold on it, per kilogram of it, once it is in up to the
      // cabin's floor.
      constexpr auto air_drag = 0.85f * u::kg / u::m;
      constexpr damping_t water_drag = 1.6f / u::s;

      constexpr int patch_samples = 97;
      constexpr float patch_recentre_distance = 24.0f;
      constexpr float trunk_reach = 72.0f;

      constexpr std::uint64_t ground_bit = 1;
      constexpr std::uint64_t trunk_bit = 2;
      constexpr std::uint64_t body_bit = 8;

      constexpr proportion_t none = 0.0f * proportion[one];
      constexpr proportion_t whole = 1.0f * proportion[one];

      // The skids' ends, where it meets the ground.
      const float skid_x = meters_value (Helicopter::skid);
      const displacement_t skid_ends[4] = {
        displacement (Vec3 (skid_x, 0.0f, 1.35f)),
        displacement (Vec3 (-skid_x, 0.0f, 1.35f)),
        displacement (Vec3 (skid_x, 0.0f, -1.35f)),
        displacement (Vec3 (-skid_x, 0.0f, -1.35f))
      };

      // A vector quantity's length and the way it points.
      template <class Q>
      auto magnitude_of (const Q& q) {
        return length (q.numerical_value_ref_in (Q::unit)) * Q::reference;
      }
      template <class Q>
      direction_t way_of (const Q& q) {
        return unit_direction (normalized (q.numerical_value_ref_in (Q::unit)));
      }
    }
  }

  struct Helicopter::Impl {
    const map::SurfaceGeometry& surface;
    GroundPatch patch;
    const TrunkField* trunks = nullptr;
    b3WorldId world = b3_nullWorldId;
    b3BodyId body = b3_nullBodyId;
    State kept;

    explicit Impl (const map::SurfaceGeometry& surface)
        : surface (surface),
          patch (
            surface,
            { .ground_friction = 1.0f,
              .obstacle_friction = 0.3f,
              .obstacle_restitution = 0.2f,
              .ground_bit = heli_rig::ground_bit,
              .ground_mask = heli_rig::body_bit,
              .obstacle_bit = heli_rig::trunk_bit,
              .obstacle_mask = heli_rig::body_bit,
              .samples = heli_rig::patch_samples,
              .recentre_distance = heli_rig::patch_recentre_distance * u::m,
              .trunk_reach = heli_rig::trunk_reach * u::m }) {}

    ~Impl () {
      destroy ();
    }

    void destroy () {
      if (B3_IS_NON_NULL (world))
        b3DestroyWorld (world);
      world = b3_nullWorldId;
      body = b3_nullBodyId;
      patch.forget ();
    }

    void create_body (const RigidBodyState& s) {
      b3BodyDef def = b3DefaultBodyDef ();
      def.type = b3_dynamicBody;
      def.position = to_b3 (s.position);
      def.rotation = quat_of (s.rotation);
      def.linearVelocity = to_b3 (s.linear_velocity);
      def.angularVelocity = to_b3 (s.angular_velocity);
      def.enableSleep = false;
      body = b3CreateBody (world, &def);

      // The cabin and the tail boom meet trunks and the ground; the skids
      // are what it stands on.
      b3ShapeDef shape = b3DefaultShapeDef ();
      shape.updateBodyMass = false;
      shape.baseMaterial.friction = 0.5f;
      shape.filter.categoryBits = heli_rig::body_bit;
      shape.filter.maskBits = heli_rig::ground_bit | heli_rig::trunk_bit;
      const b3BoxHull cabin =
        b3MakeOffsetBoxHull (0.75f, 0.70f, 1.30f, { 0.0f, 1.38f, 0.30f });
      b3CreateHullShape (body, &shape, &cabin.base);
      const b3BoxHull boom =
        b3MakeOffsetBoxHull (0.20f, 0.22f, 2.30f, { 0.0f, 1.75f, -3.60f });
      b3CreateHullShape (body, &shape, &boom.base);
      shape.baseMaterial.friction = 0.7f;
      for (const float x : { heli_rig::skid_x, -heli_rig::skid_x }) {
        const b3Capsule runner { { x, 0.05f, -1.40f },
                                 { x, 0.05f, 1.45f },
                                 0.05f };
        b3CreateCapsuleShape (body, &shape, &runner);
      }
      b3Body_SetMassData (body,
                          body_mass (heli_rig::mass,
                                     heli_rig::mass_centre,
                                     { heli_rig::inertia[0],
                                       heli_rig::inertia[1],
                                       heli_rig::inertia[2] }));
    }

    void build (const State& s) {
      destroy ();
      b3WorldDef def = b3DefaultWorldDef ();
      def.gravity = {
        0.0f, -standard_gravity.numerical_value_in (u::m / pow<2> (u::s)), 0.0f
      };
      def.enableSleep = false;
      def.workerCount = 1;
      world = b3CreateWorld (&def);
      patch.build (world, trunks, s.ground_column, s.ground_row);
      create_body (s.body);
      kept = s;
    }

    State snapshot () const {
      State s = kept;
      s.body = read_body (body);
      s.ground_column = patch.column ();
      s.ground_row = patch.row ();
      return s;
    }

    Vec3 axis (const Vec3& local) const {
      return from_b3 (
        b3RotateVector (b3Body_GetRotation (body), to_b3 (local)));
    }

    direction_t facing (const direction_t& local) const {
      return turned (b3Body_GetRotation (body), local);
    }

    position_t where (const displacement_t& local) const {
      return body_position (body) +
             quantity_cast<isq::position_vector> (
               turned (b3Body_GetRotation (body), local));
    }

    meters_t altitude () const {
      meters_t least = 1e30f * u::m;
      for (const displacement_t& end : heli_rig::skid_ends) {
        const position_t p = where (end);
        least = std::min (least, height_of (p) - ground_under (surface, p));
      }
      return least;
    }

    State standing (const position_t& at, const Vec3& heading) const {
      const b3Quat q = level_rotation (heading);
      const std::array<float, 4> rotation = array_of (q);
      meters_t highest = -1e30f * u::m;
      for (const displacement_t& end : heli_rig::skid_ends) {
        const position_t under =
          at + quantity_cast<isq::position_vector> (
                 displacement (rotated (rotation, displacement_value (end))));
        highest = std::max (highest, ground_under (surface, under));
      }
      Vec3 centre = position_value (at);
      centre[1] = meters_value (highest + 0.03f * u::m);
      State s = kept;
      s.body = { centre, rotation, Vec3 (), Vec3 () };
      patch.origin_for (centre, s.ground_column, s.ground_row);
      s.grounded = true;
      s.still = s.overturned = seconds_t {};
      s.asleep = false;
      return s;
    }

    void place (const position_t& at, const Vec3& heading) {
      const State s = standing (at, heading);
      if (B3_IS_NULL (world)) {
        build (s);
        return;
      }
      b3Body_SetTransform (
        body, to_b3 (s.body.position), quat_of (s.body.rotation));
      b3Body_SetLinearVelocity (body, { 0.0f, 0.0f, 0.0f });
      b3Body_SetAngularVelocity (body, { 0.0f, 0.0f, 0.0f });
      kept = s;
      patch.recentre (world, trunks, s.body.position);
    }

    void step (seconds_t dt, meters_t water_level) {
      using namespace heli_rig;
      using angular_acceleration_t =
        quantity<isq::angular_acceleration[u::rad / pow<2> (u::s)], Vec3>;
      patch.recentre (world, trunks, from_b3 (b3Body_GetPosition (body)));

      const seconds_t step = dt;
      const proportion_t was = kept.rotor;
      const proportion_t rotor =
        kept.parked
          ? std::max (none, was - quantity_cast<proportion> (step / spool_down))
          : std::min (whole, was + quantity_cast<proportion> (step / spool_up));
      kept.rotor = rotor;
      kept.rotor_angle =
        std::remainder (
          radians_value (kept.rotor_angle) +
            (rotor * 2.0f * PI * rotor_rate * step).numerical_value_in (one),
          2.0f * PI) *
        u::rad;
      kept.tail_angle =
        std::remainder (
          radians_value (kept.tail_angle) +
            (rotor * 2.0f * PI * tail_rate * step).numerical_value_in (one),
          2.0f * PI) *
        u::rad;

      const direction_t local_up = unit_direction (Vec3 (0, 1, 0));
      const direction_t up = facing (local_up);
      const velocity_t v = linear_velocity (body);
      const angular_velocity_t w = angular_velocity (body);
      const meters_t height = altitude ();
      kept.grounded = height < 0.12f * u::m;

      constexpr control_signal_t let_go = 0.0f * control_signal[one];
      constexpr control_signal_t full = 1.0f * control_signal[one];
      constexpr control_signal_t nudge = 0.05f * control_signal[one];
      const control_signal_t drive =
        kept.parked ? let_go : std::clamp (kept.drive, -full, full);
      const control_signal_t turn =
        kept.parked ? let_go : std::clamp (kept.turn, -full, full);
      const control_signal_t lift =
        kept.parked ? let_go : std::clamp (kept.lift, -full, full);
      const proportion_t authority = quantity_cast<proportion> (rotor * rotor);

      // The way it heads, level.
      Vec3 heading = direction_value (facing (unit_direction (Vec3 (0, 0, 1))));
      heading[1] = 0.0f;
      const direction_t ahead = unit_direction (
        length2 (heading) > 1e-6f ? normalized (heading) : Vec3 (0, 0, 1));
      velocity_t across = v;
      velocity_value (across)[1] = 0.0f;
      const speed_t speed = magnitude_of (across);
      const speed_t ground_speed = speed;

      // Sitting on its skids with nothing asked of it, it stays sat.
      const bool sitting = kept.grounded && lift <= nudge &&
                           abs (drive) < nudge && abs (turn) < nudge;
      const newtons_t weight = heli_rig::mass * standard_gravity;
      force_t thrust_force;
      angular_acceleration_t angular;
      if (sitting || kept.parked) {
        thrust_force = quantity_cast<isq::force> (
          up *
          newtons_t (quantity_cast<isq::force> (0.25f * weight * authority)));
      } else {
        // Climb or descend as asked, or hold the height; slow for the
        // touchdown; keep clear of the ground ahead when not descending.
        speed_t climb =
          lift > let_go
            ? speed_t (quantity_cast<isq::speed> (lift * climb_rate))
            : speed_t (quantity_cast<isq::speed> (lift * descent_rate));
        if (lift < let_go)
          climb = std::max (
            climb, speed_t (-(0.6f * u::m / u::s + 0.5f / u::s * height)));
        if (lift >= let_go) {
          const position_t at = body_position (body);
          meters_t terrain = ground_under (surface, at);
          for (const seconds_t ahead_by :
               { 1.0f * u::s, 2.0f * u::s, 3.5f * u::s })
            terrain =
              std::max (terrain,
                        ground_under (surface,
                                      at + quantity_cast<isq::position_vector> (
                                             across * ahead_by)));
          const meters_t wanted =
            taxi_height + clearance_per_speed * ground_speed +
            meters_t (quantity_cast<isq::length> (
              kept.load_hang * std::clamp (quantity_cast<proportion> (
                                             speed / (6.0f * u::m / u::s)),
                                           none,
                                           whole)));
          const meters_t clear = height_of (at) - terrain;
          if (clear < wanted)
            climb = std::max (climb, speed_t ((wanted - clear) * 1.5f / u::s));
        }
        const speed_t climbing = velocity_value (v)[1] * u::m / u::s;
        const metres_per_second_squared_t rise = std::clamp (
          metres_per_second_squared_t (climb_gain * (climb - climbing)),
          -5.0f * u::m / pow<2> (u::s),
          7.0f * u::m / pow<2> (u::s));

        // Lean toward the speed asked for: a coordinated turn falls out
        // of the heading turning away from the way it is going.
        const speed_t target =
          drive > let_go
            ? speed_t (quantity_cast<isq::speed> (drive * top_speed))
            : speed_t (quantity_cast<isq::speed> (drive * back_speed));
        // The level acceleration asked for.
        acceleration_t push = quantity_cast<isq::acceleration> (
          (quantity_cast<isq::velocity> (ahead * target) - across) *
          speed_gain);
        const metres_per_second_squared_t most =
          standard_gravity * libm::tan (max_tilt);
        if (magnitude_of (push) > most)
          push = quantity_cast<isq::acceleration> (way_of (push) * most);
        // The rotor gives its own weight and the climb asked for, and
        // leans for the speed asked for, against what hangs under it.
        const force_t& pull = kept.load_pull;
        const newtons_t hold = std::max (weight - force_value (pull)[1] * u::N,
                                         newtons_t (0.0f * u::N));
        force_t level =
          quantity_cast<isq::force> (push * heli_rig::mass) - pull;
        level.numerical_value_ref_in (u::N)[1] = 0.0f;
        const newtons_t reach = libm::tan (max_tilt * 1.3f) * hold;
        if (magnitude_of (level) > reach)
          level = quantity_cast<isq::force> (way_of (level) * reach);
        const newtons_t thrust =
          std::clamp (newtons_t ((hold + heli_rig::mass * rise) /
                                 std::max (direction_value (up)[1], 0.5f)),
                      0.0f * u::N,
                      newtons_t (quantity_cast<isq::force> (
                        (quantity_cast<isq::force> (thrust_to_weight * weight) +
                         magnitude_of (pull)) *
                        authority)));
        thrust_force = quantity_cast<isq::force> (up * thrust);
        const Vec3& levelled = force_value (level);
        const direction_t wanted_up =
          hold > 1.0f * u::N
            ? unit_direction (normalized (
                Vec3 (levelled[0], newtons_value (hold), levelled[2])))
            : local_up;
        const per_second_t omega = 2.0f * PI * attitude_hertz;
        const angular_velocity_t spin_about_up =
          quantity_cast<isq::angular_velocity> (up * dot (w, up));
        const angular_velocity_t tilt_rate = w - spin_about_up;
        angular =
          angular_acceleration_t (quantity_cast<isq::angular_acceleration> (
            cross (up, wanted_up) * (omega * omega) * u::rad)) -
          angular_acceleration_t (quantity_cast<isq::angular_acceleration> (
            tilt_rate * (2.0f * attitude_damping * omega)));
        // Turns quickest at a hover, and no quicker at speed than the
        // tilt can carry it round.
        const radians_per_second_t rate = std::clamp (
          radians_per_second_t (1.2f * most /
                                std::max (speed, 1.0f * u::m / u::s) * u::rad),
          0.35f * u::rad / u::s,
          yaw_rate);
        const radians_per_second_t yawing =
          quantity_cast<isq::angular_velocity> (dot (w, up));
        angular =
          angular +
          quantity_cast<isq::angular_acceleration> (
            up * (yaw_gain *
                  (radians_per_second_t (
                     quantity_cast<isq::angular_velocity> (-turn * rate)) -
                   yawing)));
        angular =
          quantity_cast<isq::angular_acceleration> (angular * authority);
      }

      // The air's drag, and the water's up to the cabin's floor.
      const proportion_t wet = std::clamp (
        quantity_cast<proportion> (
          (water_level - height_of (where (displacement (Vec3 (0, 0.6f, 0))))) /
          (0.8f * u::m)),
        none,
        whole);
      const quantity<u::kg / u::s, float> resistance =
        air_drag * magnitude_of (v) +
        quantity<u::kg / u::s, float> (
          water_drag * quantity_cast<mp_units::dimensionless> (wet) *
          heli_rig::mass);
      const force_t drag = quantity_cast<isq::force> (v * -resistance);
      apply_force_to_centre (body, thrust_force + drag);
      const Vec3& spin =
        angular.numerical_value_ref_in (u::rad / pow<2> (u::s));
      if (length2 (spin) > 0.0f)
        b3Body_ApplyTorque (
          body,
          torque_for (b3Body_GetRotation (body), spin, heli_rig::inertia),
          true);

      b3World_Step (world, seconds_value (dt), substeps);

      // Left on its side or back, it is set on its skids again.
      const velocity_t moving = linear_velocity (body);
      const angular_velocity_t turning = angular_velocity (body);
      const auto squared = [] (auto q) { return q * q; };
      kept.overturned = direction_value (facing (local_up))[1] < 0.3f &&
                            dot (moving, moving) < squared (2.0f * u::m / u::s)
                          ? kept.overturned + dt
                          : seconds_t {};
      if (kept.overturned > 2.5f * u::s) {
        place (where (displacement (Vec3 ())), axis (Vec3 (0, 0, 1)));
        return;
      }

      // Parked, its rotor stopped, and still: it stays where it is.
      const bool quiet =
        dot (moving, moving) < squared (0.08f * u::m / u::s) &&
        dot (turning, turning) < squared (0.08f * u::rad / u::s) &&
        kept.grounded;
      kept.still = kept.parked && kept.rotor <= proportion_t {} && quiet
                     ? kept.still + dt
                     : seconds_t {};
      if (kept.still > 1.0f * u::s)
        kept.asleep = true;
    }
  };

  Helicopter::Helicopter (const map::SurfaceGeometry& surface)
      : m_impl (std::make_unique<Impl> (surface)) {
    m_impl->place (moppe::position (Vec3 ()), Vec3 (0, 0, 1));
  }

  Helicopter::~Helicopter () = default;

  Helicopter::State Helicopter::state () const {
    return m_impl->snapshot ();
  }

  void Helicopter::restore (const State& state) {
    m_impl->build (state);
  }

  void Helicopter::place (position_t at, direction_t heading) {
    m_impl->place (at, direction_value (heading));
  }

  void Helicopter::shift (plan_displacement_t delta) {
    Impl& m = *m_impl;
    State s = m.snapshot ();
    s.body.position = s.body.position + lift (plan_value (delta));
    m.patch.origin_for (s.body.position, s.ground_column, s.ground_row);
    m.build (s);
  }

  void Helicopter::set_trunks (const TrunkField* trunks) {
    Impl& m = *m_impl;
    if (trunks == m.trunks)
      return;
    const State s = m.snapshot ();
    m.trunks = trunks;
    m.build (s);
  }

  void Helicopter::set_drive (control_signal_t drive) {
    m_impl->kept.drive = drive;
  }

  void Helicopter::set_turn (control_signal_t turn) {
    m_impl->kept.turn = turn;
  }

  void Helicopter::set_lift (control_signal_t lift) {
    m_impl->kept.lift = lift;
  }

  void Helicopter::set_load (const force_t& pull, meters_t hang) {
    m_impl->kept.load_pull = pull;
    m_impl->kept.load_hang = hang;
  }

  void Helicopter::set_parked (bool parked) {
    State& kept = m_impl->kept;
    if (kept.parked && !parked)
      kept.asleep = false;
    kept.parked = parked;
  }

  void Helicopter::update (seconds_t dt, meters_t water_level) {
    in_substeps (dt, [&] (seconds_t h) {
      if (!m_impl->kept.asleep)
        m_impl->step (h, water_level);
    });
  }

  position_t Helicopter::position () const {
    return m_impl->where (displacement (Vec3 ()));
  }

  direction_t Helicopter::forward () const {
    return m_impl->facing (unit_direction (Vec3 (0, 0, 1)));
  }

  direction_t Helicopter::up () const {
    return m_impl->facing (unit_direction (Vec3 (0, 1, 0)));
  }

  direction_t Helicopter::left () const {
    return m_impl->facing (unit_direction (Vec3 (1, 0, 0)));
  }

  velocity_t Helicopter::velocity () const {
    return moppe::velocity (from_b3 (b3Body_GetLinearVelocity (m_impl->body)));
  }

  bool Helicopter::grounded () const {
    return m_impl->kept.grounded;
  }

  bool Helicopter::asleep () const {
    return m_impl->kept.asleep;
  }

  kilograms_t Helicopter::mass () const {
    return heli_rig::mass;
  }

  void Helicopter::push (position_t at, const impulse_t& impulse) {
    Impl& m = *m_impl;
    m.kept.asleep = false;
    m.kept.still = seconds_t {};
    b3Body_ApplyLinearImpulse (
      m.body, to_b3 (impulse_value (impulse)), to_b3 (at), true);
  }

  position_t Helicopter::point (const displacement_t& local) const {
    return m_impl->where (local);
  }

  meters_t Helicopter::altitude () const {
    return m_impl->altitude ();
  }

  proportion_t Helicopter::rotor () const {
    return m_impl->kept.rotor;
  }

  radians_t Helicopter::rotor_angle () const {
    return m_impl->kept.rotor_angle;
  }

  radians_t Helicopter::tail_angle () const {
    return m_impl->kept.tail_angle;
  }
}
