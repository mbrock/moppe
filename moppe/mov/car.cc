#include <moppe/mov/box3d_vec.hh>
#include <moppe/mov/car.hh>
#include <moppe/mov/ground_patch.hh>

#include <algorithm>
#include <cmath>
#include <vector>

#include <box3d/box3d.h>

namespace moppe::mov {
  namespace {
    // Kept in a namespace of its own: the engine is a unity build.
    namespace car_rig {
      constexpr int substeps = 4;

      // Box3D mixes two shapes' friction as a geometric mean, so the tyre
      // carries the square of the wanted coefficient against unit ground
      // friction.
      constexpr float ground_friction = 1.0f;
      constexpr float traction_share = 0.9f;

      // The ground patch, as the bike's: samples at half the terrain
      // lattice spacing, recentred long before a wheel nears its edge.
      constexpr int patch_samples = 97;
      constexpr float patch_recentre_distance = 24.0f;
      constexpr float trunk_reach = 72.0f;

      constexpr std::uint64_t ground_bit = 1;
      constexpr std::uint64_t trunk_bit = 2;
      constexpr std::uint64_t wheel_bit = 4;
      constexpr std::uint64_t chassis_bit = 8;

      Vec3 axle_of (const CarRig& rig, int wheel) {
        const float track =
          meters_value (wheel < 2 ? rig.front_track : rig.rear_track);
        const float axle = meters_value (rig.axle);
        return Vec3 (
          wheel % 2 == 0 ? track : -track, 0.0f, wheel < 2 ? axle : -axle);
      }
    }
  }

  struct Car::Impl {
    const map::SurfaceGeometry& surface;
    const CarRig& rig;
    GroundPatch patch;
    float hertz = 0.0f, damping = 0.0f;

    const TrunkField* trunks = nullptr;
    b3WorldId world = b3_nullWorldId;
    b3BodyId chassis = b3_nullBodyId;
    std::array<b3BodyId, wheel_count> wheels {};
    std::array<b3JointId, wheel_count> joints {};

    // What is not in the bodies: the controls and the last step's
    // readings.
    State kept;

    Impl (const map::SurfaceGeometry& surface, const CarRig& rig)
        : surface (surface), rig (rig),
          patch (surface,
                 { .ground_friction = car_rig::ground_friction,
                   .obstacle_friction = 0.3f,
                   .obstacle_restitution = 0.15f,
                   .ground_bit = car_rig::ground_bit,
                   .ground_mask = car_rig::wheel_bit | car_rig::chassis_bit,
                   .obstacle_bit = car_rig::trunk_bit,
                   .obstacle_mask = car_rig::chassis_bit,
                   .samples = car_rig::patch_samples,
                   .recentre_distance = car_rig::patch_recentre_distance,
                   .trunk_reach = car_rig::trunk_reach }) {
      wheels.fill (b3_nullBodyId);
      joints.fill (b3_nullJointId);
      // The joint springs are soft constraints tuned in hertz against
      // the joint's effective mass, which the light wheel dominates.
      // Convert the rate and damping wanted for the sprung chassis into
      // those terms.
      const kilograms_t effective =
        1.0f / (1.0f / rig.wheel_mass + 1.0f / rig.chassis_mass +
                rig.axle * rig.axle / rig.inertia[0] +
                rig.front_track * rig.front_track / rig.inertia[2]);
      const kilograms_t sprung = 0.25f * rig.chassis_mass;
      spring_tuning (sprung * standard_gravity / rig.sag,
                     sprung,
                     effective,
                     rig.sprung_damping_ratio,
                     hertz,
                     damping);
    }

    ~Impl () {
      destroy ();
    }

    void destroy () {
      if (B3_IS_NON_NULL (world))
        b3DestroyWorld (world);
      world = b3_nullWorldId;
      chassis = b3_nullBodyId;
      wheels.fill (b3_nullBodyId);
      joints.fill (b3_nullJointId);
      patch.forget ();
    }

    b3BodyId create_body (const RigidBodyState& s, bool spins = false) {
      b3BodyDef def = b3DefaultBodyDef ();
      def.type = b3_dynamicBody;
      def.position = to_b3 (s.position);
      def.rotation = quat_of (s.rotation);
      def.linearVelocity = to_b3 (s.linear_velocity);
      def.angularVelocity = to_b3 (s.angular_velocity);
      def.enableSleep = false;
      def.allowFastRotation = spins;
      return b3CreateBody (world, &def);
    }

    b3BodyId create_wheel (const RigidBodyState& s) {
      const b3BodyId wheel = create_body (s, rig.fast_wheels);
      b3ShapeDef shape = b3DefaultShapeDef ();
      shape.updateBodyMass = false;
      shape.baseMaterial.friction =
        rig.tyre_friction * rig.tyre_friction / car_rig::ground_friction;
      shape.baseMaterial.rollingResistance = rig.rolling_resistance;
      shape.filter.categoryBits = car_rig::wheel_bit;
      shape.filter.maskBits = car_rig::ground_bit;
      const b3Sphere sphere { { 0.0f, 0.0f, 0.0f },
                              meters_value (rig.wheel_radius) };
      b3CreateSphereShape (wheel, &shape, &sphere);
      b3Body_SetMassData (
        wheel,
        body_mass (
          rig.wheel_mass,
          Vec3 (),
          { rig.wheel_inertia, rig.wheel_inertia, rig.wheel_inertia }));
      return wheel;
    }

    void create_chassis (const RigidBodyState& s) {
      chassis = create_body (s);
      b3ShapeDef shape = b3DefaultShapeDef ();
      shape.updateBodyMass = false;
      shape.baseMaterial.friction = 0.45f;
      shape.filter.categoryBits = car_rig::chassis_bit;
      shape.filter.maskBits = car_rig::ground_bit | car_rig::trunk_bit;
      for (int i = 0; i < rig.box_count; ++i) {
        const CarRig::Box& box = rig.boxes[i];
        const b3BoxHull hull =
          b3MakeOffsetBoxHull (box.half[0],
                               box.half[1],
                               box.half[2],
                               { box.centre[0], box.centre[1], box.centre[2] });
        b3CreateHullShape (chassis, &shape, &hull.base);
      }
      b3Body_SetMassData (
        chassis,
        body_mass (
          rig.chassis_mass,
          Vec3 (rig.mass_centre[0], rig.mass_centre[1], rig.mass_centre[2]),
          { rig.inertia[0], rig.inertia[1], rig.inertia[2] }));
    }

    b3JointId create_suspension (int wheel) {
      const b3Quat frame = suspension_rotation ();
      b3WheelJointDef def = b3DefaultWheelJointDef ();
      def.base.bodyIdA = chassis;
      def.base.bodyIdB = wheels[wheel];
      // Spring zero is top-out, a sag below the standing axle.
      def.base.localFrameA = { to_b3 (
                                 car_rig::axle_of (rig, wheel) -
                                 Vec3 (0.0f, meters_value (rig.sag), 0.0f)),
                               frame };
      def.base.localFrameB = { { 0.0f, 0.0f, 0.0f }, frame };
      def.base.collideConnected = false;
      def.enableSuspensionSpring = true;
      def.suspensionHertz = hertz;
      def.suspensionDampingRatio = damping;
      def.enableSuspensionLimit = true;
      def.lowerSuspensionLimit = -meters_value (rig.sag + rig.bump);
      def.upperSuspensionLimit = 0.0f;
      def.enableSpinMotor = true;
      def.maxSpinTorque = 0.0f;
      def.spinSpeed = 0.0f;
      if (wheel < 2) {
        def.enableSteering = true;
        def.steeringHertz = 6.0f;
        def.steeringDampingRatio = 1.0f;
        def.maxSteeringTorque = 6000.0f;
        def.enableSteeringLimit = true;
        def.lowerSteeringLimit = -(radians_value (rig.full_lock) + 0.1f);
        def.upperSteeringLimit = radians_value (rig.full_lock) + 0.1f;
      }
      return b3CreateWheelJoint (world, &def);
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
      create_chassis (s.chassis);
      for (int wheel = 0; wheel < wheel_count; ++wheel)
        wheels[wheel] = create_wheel (s.wheels[wheel]);
      for (int wheel = 0; wheel < wheel_count; ++wheel)
        joints[wheel] = create_suspension (wheel);
      kept = s;
    }

    State snapshot () const {
      State s = kept;
      s.chassis = read_body (chassis);
      for (int wheel = 0; wheel < wheel_count; ++wheel)
        s.wheels[wheel] = read_body (wheels[wheel]);
      s.ground_column = patch.column ();
      s.ground_row = patch.row ();
      return s;
    }

    void recentre_ground () {
      patch.recentre (world, trunks, from_b3 (b3Body_GetPosition (chassis)));
    }

    Vec3 axis (const Vec3& local) const {
      return from_b3 (
        b3RotateVector (b3Body_GetRotation (chassis), to_b3 (local)));
    }

    Vec3 point (const Vec3& local) const {
      return from_b3 (b3Body_GetPosition (chassis)) + axis (local);
    }

    // The standing pose at `at`, level with the highest ground under any
    // wheel so that none starts inside it; the springs settle the rest.
    State standing (const Vec3& at, const Vec3& heading) const {
      const std::array<float, 4> rotation = array_of (level_rotation (heading));
      Vec3 centre = at;
      float highest = -1e30f;
      for (int wheel = 0; wheel < wheel_count; ++wheel) {
        const Vec3 under =
          at + rotated (rotation, car_rig::axle_of (rig, wheel));
        highest =
          std::max (highest, ground_height (surface, under[0], under[2]));
      }
      centre[1] = highest + meters_value (rig.wheel_radius) + 0.04f;
      State s = kept;
      s.chassis = { centre, rotation, Vec3 (), Vec3 () };
      for (int wheel = 0; wheel < wheel_count; ++wheel)
        s.wheels[wheel] = { centre +
                              rotated (rotation, car_rig::axle_of (rig, wheel)),
                            rotation,
                            {},
                            {} };
      patch.origin_for (centre, s.ground_column, s.ground_row);
      s.touching.fill (false);
      s.load.fill (newtons_t {});
      s.still = s.overturned = seconds_t {};
      s.asleep = false;
      return s;
    }

    void place (const Vec3& at, const Vec3& heading) {
      const State s = standing (at, heading);
      // A car already standing in a world is moved in it; only a patch
      // left far behind costs a rebuild.
      if (B3_IS_NULL (world)) {
        build (s);
        return;
      }
      const auto move = [] (b3BodyId body, const RigidBodyState& b) {
        b3Body_SetTransform (body, to_b3 (b.position), quat_of (b.rotation));
        b3Body_SetLinearVelocity (body, to_b3 (b.linear_velocity));
        b3Body_SetAngularVelocity (body, to_b3 (b.angular_velocity));
      };
      move (chassis, s.chassis);
      for (int wheel = 0; wheel < wheel_count; ++wheel)
        move (wheels[wheel], s.wheels[wheel]);
      kept = s;
      recentre_ground ();
    }

    void step (seconds_t dt, meters_t water_level) {
      recentre_ground ();
      const Vec3 forward = axis (Vec3 (0, 0, 1));
      const Vec3 v = from_b3 (b3Body_GetLinearVelocity (chassis));
      const speed_t vf = dot (v, forward) * u::m / u::s;
      const speed_t speed = length (v) * u::m / u::s;
      const meters_t radius = rig.wheel_radius;
      const float r = meters_value (radius);

      // The wheel is turned less far the faster it goes. The joint steers
      // about its axis down the chassis, so a positive angle turns to the
      // right.
      const float fade = (speed / rig.lock_fade_speed).numerical_value_in (one);
      const radians_t lock = rig.full_lock / (1.0f + fade * fade);
      const float steer = radians_value (
        std::clamp (scalar_value (kept.turn), -1.0f, 1.0f) * lock);
      b3WheelJoint_SetTargetSteeringAngle (joints[0], steer);
      b3WheelJoint_SetTargetSteeringAngle (joints[1], steer);

      // Engine and brakes through the wheel joints' spin motors: the
      // driven wheels pull, each within what its load lets it put down,
      // and the rest only roll, and brake.
      const float drive =
        kept.parked ? 0.0f
                    : std::clamp (scalar_value (kept.drive), -1.0f, 1.0f);
      const newtons_t pull = std::min (
        rig.engine_force,
        newtons_t (rig.engine_power / std::max (abs (vf), 2.0f * u::m / u::s)));
      kept.throttle = std::abs (drive);
      const float driven = rig.front_drive ? 2.0f : 4.0f;
      for (int wheel = 0; wheel < wheel_count; ++wheel) {
        const bool front = wheel < 2;
        const bool drives = front || !rig.front_drive;
        float spin = 0.0f;
        newton_metres_t torque =
          drives ? rig.engine_brake_torque : newton_metres_t {};
        const newton_metres_t grip = car_rig::traction_share *
                                     rig.tyre_friction * kept.load[wheel] *
                                     radius;
        const float brake_share =
          front ? rig.front_brake_share : 1.0f - rig.front_brake_share;
        if (kept.parked) {
          torque = rig.parking_torque;
        } else if (kept.brake ||
                   (drive * vf < speed_t {} && abs (vf) > 1.5f * u::m / u::s)) {
          // Asking for the other way while still rolling is braking.
          const float effort = kept.brake ? 1.0f : std::abs (drive);
          torque = effort * rig.brake_force * brake_share * radius / 2.0f;
        } else if (std::abs (drive) > 0.02f && drives) {
          const float share = drive > 0.0f ? 1.0f : rig.reverse_share;
          const newton_metres_t wanted =
            std::abs (drive) * share * pull * radius / driven;
          const float ahead = speed_value (vf);
          if (kept.touching[wheel]) {
            spin = drive > 0.0f ? (std::max (ahead, 0.0f) + 20.0f) / r
                                : -speed_value (rig.reverse_speed) / r;
            torque = std::min (wanted, grip);
          } else {
            // In the air a wheel only revs up to road speed.
            spin = (ahead + std::copysign (1.0f, drive)) / r;
            torque = rig.free_rev_torque;
          }
        }
        b3WheelJoint_SetSpinMotorSpeed (joints[wheel], spin);
        b3WheelJoint_SetMaxSpinTorque (joints[wheel],
                                       newton_metres_value (torque));
      }

      // The air's drag, and water's up to the sills, on the whole mass.
      const float wet = std::clamp (
        (meters_value (water_level) - (point (Vec3 (0, -0.3f, 0))[1])) / 0.8f,
        0.0f,
        1.0f);
      apply_drag (chassis,
                  v,
                  (rig.air_drag * speed + rig.water_drag * wet) *
                    rig.total_mass ());

      // Hung from the helicopter's line and off the ground, the air on
      // its flanks steadies its swinging and turns its nose the way it
      // is carried.
      const bool aloft = std::none_of (kept.touching.begin (),
                                       kept.touching.end (),
                                       [] (bool touching) { return touching; });
      if (kept.slung && aloft && rig.slung_damping > per_second_t {}) {
        const Vec3 w = from_b3 (b3Body_GetAngularVelocity (chassis));
        Vec3 wanted = w * -per_second_value (rig.slung_damping);
        Vec3 level = v;
        level[1] = 0.0f;
        const float carried = length (level);
        Vec3 nose = forward;
        nose[1] = 0.0f;
        if (carried > 2.0f && length2 (nose) > 1e-4f) {
          level = level * (1.0f / carried);
          nose = normalized (nose);
          const float off =
            std::atan2 (cross (nose, level)[1], dot (nose, level));
          wanted[1] += off *
                       rig.slung_vane.numerical_value_in (one / pow<2> (u::s)) *
                       std::min (1.0f, carried / 10.0f);
        }
        b3Body_ApplyTorque (
          chassis,
          torque_for (b3Body_GetRotation (chassis), wanted, rig.inertia),
          true);
      }

      b3World_Step (world, seconds_value (dt), car_rig::substeps);

      for (int wheel = 0; wheel < wheel_count; ++wheel)
        kept.touching[wheel] =
          wheel_touches_ground (wheels[wheel], dt, kept.load[wheel]);
      kept.wheel_spin = rolled (kept.wheel_spin, joints[2], dt);

      // Left on its side or back, it is set on its wheels again.
      const Vec3 moving = from_b3 (b3Body_GetLinearVelocity (chassis));
      const Vec3 turning = from_b3 (b3Body_GetAngularVelocity (chassis));
      const bool resting = length2 (moving) < 2.0f * 2.0f;
      const float upright = axis (Vec3 (0, 1, 0))[1];
      kept.overturned =
        upright < 0.25f && resting ? kept.overturned + dt : seconds_t {};
      if (kept.overturned > 2.5f * u::s) {
        place (point (Vec3 ()), axis (Vec3 (0, 0, 1)));
        return;
      }

      // Parked and still, it stays where it is.
      const bool quiet = length2 (moving) < 0.08f * 0.08f &&
                         length2 (turning) < 0.08f * 0.08f && !aloft;
      kept.still = kept.parked && quiet ? kept.still + dt : seconds_t {};
      if (kept.still > 1.0f * u::s)
        kept.asleep = true;
    }
  };

  Car::Car (const map::SurfaceGeometry& surface, const CarRig& rig)
      : m_impl (std::make_unique<Impl> (surface, rig)) {
    m_impl->place (Vec3 (), Vec3 (0, 0, 1));
  }

  Car::~Car () = default;

  const CarRig& Car::rig () const {
    return m_impl->rig;
  }

  Car::State Car::state () const {
    return m_impl->snapshot ();
  }

  void Car::restore (const State& state) {
    m_impl->build (state);
  }

  void Car::place (position_t at, direction_t heading) {
    m_impl->place (position_value (at), direction_value (heading));
  }

  void Car::set_trunks (const TrunkField* trunks) {
    Impl& m = *m_impl;
    if (trunks == m.trunks)
      return;
    const State s = m.snapshot ();
    m.trunks = trunks;
    m.build (s);
  }

  void Car::set_drive (control_signal_t drive) {
    m_impl->kept.drive = drive;
  }

  void Car::set_turn (control_signal_t turn) {
    m_impl->kept.turn = turn;
  }

  void Car::set_brake (bool brake) {
    m_impl->kept.brake = brake;
  }

  void Car::set_slung (bool slung) {
    State& kept = m_impl->kept;
    if (slung)
      kept.asleep = false;
    kept.slung = slung;
  }

  void Car::set_parked (bool parked) {
    State& kept = m_impl->kept;
    if (kept.parked && !parked)
      kept.asleep = false;
    kept.parked = parked;
    if (parked)
      kept.throttle = {};
  }

  void Car::update (seconds_t dt, meters_t water_level) {
    in_substeps (dt, [&] (seconds_t h) {
      if (!m_impl->kept.asleep)
        m_impl->step (h, water_level);
    });
  }

  position_t Car::position () const {
    return moppe::position (m_impl->point (Vec3 ()));
  }

  direction_t Car::forward () const {
    return unit_direction (m_impl->axis (Vec3 (0, 0, 1)));
  }

  direction_t Car::up () const {
    return unit_direction (m_impl->axis (Vec3 (0, 1, 0)));
  }

  direction_t Car::left () const {
    return unit_direction (m_impl->axis (Vec3 (1, 0, 0)));
  }

  velocity_t Car::velocity () const {
    return moppe::velocity (
      from_b3 (b3Body_GetLinearVelocity (m_impl->chassis)));
  }

  bool Car::grounded () const {
    const auto& touching = m_impl->kept.touching;
    return std::any_of (
      touching.begin (), touching.end (), [] (bool wheel) { return wheel; });
  }

  bool Car::asleep () const {
    return m_impl->kept.asleep;
  }

  kilograms_t Car::mass () const {
    return m_impl->rig.total_mass ();
  }

  void Car::push (position_t at, const impulse_t& impulse) {
    Impl& m = *m_impl;
    m.kept.asleep = false;
    m.kept.still = seconds_t {};
    b3Body_ApplyLinearImpulse (m.chassis,
                               to_b3 (impulse_value (impulse)),
                               to_b3 (position_value (at)),
                               true);
  }

  position_t Car::point (const displacement_t& local) const {
    return moppe::position (m_impl->point (displacement_value (local)));
  }

  position_t Car::driver_seat () const {
    const float* seat = m_impl->rig.seat;
    return point (displacement (Vec3 (seat[0], seat[1], seat[2])));
  }

  position_t Car::passenger_seat () const {
    const float* seat = m_impl->rig.seat;
    return point (displacement (Vec3 (-seat[0], seat[1], seat[2])));
  }

  meters_t Car::wheel_drop (int wheel) const {
    const Impl& m = *m_impl;
    const Vec3 at = from_b3 (b3Body_GetPosition (m.wheels[wheel]));
    return dot (at - m.point (car_rig::axle_of (m.rig, wheel)),
                m.axis (Vec3 (0, -1, 0))) *
           u::m;
  }

  radians_t Car::steer () const {
    return b3WheelJoint_GetSteeringAngle (m_impl->joints[0]) * u::rad;
  }

  radians_t Car::wheel_spin () const {
    return m_impl->kept.wheel_spin;
  }

  control_signal_t Car::throttle () const {
    return m_impl->kept.throttle;
  }
}
