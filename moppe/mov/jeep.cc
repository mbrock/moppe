#include <moppe/mov/box3d_vec.hh>
#include <moppe/mov/ground_patch.hh>
#include <moppe/mov/jeep.hh>

#include <algorithm>
#include <cmath>
#include <vector>

#include <box3d/box3d.h>

namespace moppe::mov {
  namespace {
    // Everything of the jeep's is kept in one namespace: the engine is a
    // unity build, and the bike's rig next door has helpers of its own.
    namespace jeep_rig {
      constexpr float gravity = 9.81f;
      constexpr int substeps = 4;

      constexpr float chassis_mass = 1000.0f;
      // Pitch, yaw, and roll about the centre of mass, which lies low
      // between the axles.
      constexpr float inertia[3] = { 1400.0f, 1500.0f, 560.0f };
      constexpr float mass_centre[3] = { 0.0f, 0.15f, 0.05f };
      constexpr float wheel_mass = 30.0f;
      constexpr float wheel_inertia = 2.4f;
      constexpr float total_mass = chassis_mass + 4.0f * wheel_mass;

      // The springs are linear from top-out, so the sag at rest sets
      // their rate; beyond the sag there is this much more before the
      // stops.
      constexpr float sag = 0.12f;
      constexpr float bump = 0.17f;
      constexpr float sprung_damping_ratio = 0.5f;

      // Box3D mixes two shapes' friction as a geometric mean, so the
      // tyre carries the square of the wanted coefficient against unit
      // ground friction.
      constexpr float tyre_friction = 1.25f;
      constexpr float ground_friction = 1.0f;

      constexpr float engine_force = 9000.0f;
      constexpr float engine_power = 80000.0f;
      constexpr float brake_force = 13000.0f;
      constexpr float reverse_speed = 7.0f;
      constexpr float reverse_share = 0.6f;
      constexpr float engine_brake_torque = 45.0f;
      constexpr float parking_torque = 5000.0f;
      constexpr float free_rev_torque = 40.0f;
      constexpr float traction_share = 0.9f;

      constexpr float full_lock = 0.55f;
      constexpr float lock_fade_speed = 13.0f;

      constexpr float rolling_drag = 0.03f;
      constexpr float air_drag = 0.0012f;
      constexpr float water_drag = 1.6f;

      // The ground patch, as the bike's: samples at half the terrain
      // lattice spacing, recentred long before a wheel nears its edge.
      constexpr int patch_samples = 97;
      constexpr float patch_recentre_distance = 24.0f;
      constexpr float trunk_reach = 72.0f;

      constexpr std::uint64_t ground_bit = 1;
      constexpr std::uint64_t trunk_bit = 2;
      constexpr std::uint64_t wheel_bit = 4;
      constexpr std::uint64_t chassis_bit = 8;

      // The jeep's frame for a level heading.
      b3Quat level_rotation (const Vec3& heading) {
        Vec3 f = heading;
        f[1] = 0.0f;
        f = length2 (f) > 1e-8f ? normalized (f) : Vec3 (0, 0, 1);
        const Vec3 up (0, 1, 0);
        const Vec3 left = cross (up, f);
        const b3Matrix3 m { to_b3 (left), to_b3 (up), to_b3 (f) };
        return b3NormalizeQuat (b3MakeQuatFromMatrix (&m));
      }

      Vec3 axle_of (int wheel) {
        return Vec3 (wheel % 2 == 0 ? Jeep::track : -Jeep::track,
                     0.0f,
                     wheel < 2 ? Jeep::axle : -Jeep::axle);
      }
    }
  }

  struct Jeep::Impl {
    const map::SurfaceGeometry& surface;
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

    explicit Impl (const map::SurfaceGeometry& surface)
        : surface (surface),
          patch (surface,
                 { .ground_friction = jeep_rig::ground_friction,
                   .obstacle_friction = 0.3f,
                   .obstacle_restitution = 0.15f,
                   .ground_bit = jeep_rig::ground_bit,
                   .ground_mask = jeep_rig::wheel_bit | jeep_rig::chassis_bit,
                   .obstacle_bit = jeep_rig::trunk_bit,
                   .obstacle_mask = jeep_rig::chassis_bit,
                   .samples = jeep_rig::patch_samples,
                   .recentre_distance = jeep_rig::patch_recentre_distance,
                   .trunk_reach = jeep_rig::trunk_reach }) {
      wheels.fill (b3_nullBodyId);
      joints.fill (b3_nullJointId);
      // The joint springs are soft constraints tuned in hertz against
      // the joint's effective mass, which the light wheel dominates.
      // Convert the rate and damping wanted for the sprung chassis into
      // those terms.
      const float effective =
        1.0f / (1.0f / jeep_rig::wheel_mass + 1.0f / jeep_rig::chassis_mass +
                axle * axle / jeep_rig::inertia[0] +
                track * track / jeep_rig::inertia[2]);
      const float sprung = 0.25f * jeep_rig::chassis_mass;
      const float stiffness = sprung * jeep_rig::gravity / jeep_rig::sag;
      const float c =
        2.0f * jeep_rig::sprung_damping_ratio * std::sqrt (stiffness * sprung);
      hertz = std::sqrt (stiffness / effective) / (2.0f * 3.14159265f);
      damping = c / (2.0f * std::sqrt (stiffness * effective));
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

    b3BodyId create_body (const RigidBodyState& s) {
      b3BodyDef def = b3DefaultBodyDef ();
      def.type = b3_dynamicBody;
      def.position = to_b3 (s.position);
      def.rotation = quat_of (s.rotation);
      def.linearVelocity = to_b3 (s.linear_velocity);
      def.angularVelocity = to_b3 (s.angular_velocity);
      def.enableSleep = false;
      return b3CreateBody (world, &def);
    }

    b3BodyId create_wheel (const RigidBodyState& s) {
      const b3BodyId wheel = create_body (s);
      b3ShapeDef shape = b3DefaultShapeDef ();
      shape.updateBodyMass = false;
      shape.baseMaterial.friction = jeep_rig::tyre_friction *
                                    jeep_rig::tyre_friction /
                                    jeep_rig::ground_friction;
      shape.filter.categoryBits = jeep_rig::wheel_bit;
      shape.filter.maskBits = jeep_rig::ground_bit;
      const b3Sphere sphere { { 0.0f, 0.0f, 0.0f }, wheel_radius };
      b3CreateSphereShape (wheel, &shape, &sphere);
      b3MassData mass {};
      mass.mass = jeep_rig::wheel_mass;
      mass.inertia.cx = { jeep_rig::wheel_inertia, 0.0f, 0.0f };
      mass.inertia.cy = { 0.0f, jeep_rig::wheel_inertia, 0.0f };
      mass.inertia.cz = { 0.0f, 0.0f, jeep_rig::wheel_inertia };
      b3Body_SetMassData (wheel, mass);
      return wheel;
    }

    void create_chassis (const RigidBodyState& s) {
      chassis = create_body (s);
      // The tub, wide enough to shield the wheels (which meet only the
      // ground), and the roll cage over the seats. Both meet trunks and,
      // when the springs are spent or the jeep goes over, the ground.
      b3ShapeDef shape = b3DefaultShapeDef ();
      shape.updateBodyMass = false;
      shape.baseMaterial.friction = 0.45f;
      shape.filter.categoryBits = jeep_rig::chassis_bit;
      shape.filter.maskBits = jeep_rig::ground_bit | jeep_rig::trunk_bit;
      const b3BoxHull tub =
        b3MakeOffsetBoxHull (0.92f, 0.36f, 1.9f, { 0.0f, 0.42f, 0.0f });
      b3CreateHullShape (chassis, &shape, &tub.base);
      const b3BoxHull cage =
        b3MakeOffsetBoxHull (0.6f, 0.33f, 0.98f, { 0.0f, 1.1f, -0.55f });
      b3CreateHullShape (chassis, &shape, &cage.base);
      b3MassData mass {};
      mass.mass = jeep_rig::chassis_mass;
      mass.center = { jeep_rig::mass_centre[0],
                      jeep_rig::mass_centre[1],
                      jeep_rig::mass_centre[2] };
      mass.inertia.cx = { jeep_rig::inertia[0], 0.0f, 0.0f };
      mass.inertia.cy = { 0.0f, jeep_rig::inertia[1], 0.0f };
      mass.inertia.cz = { 0.0f, 0.0f, jeep_rig::inertia[2] };
      b3Body_SetMassData (chassis, mass);
    }

    b3JointId create_suspension (int wheel) {
      const b3Quat frame = suspension_rotation ();
      b3WheelJointDef def = b3DefaultWheelJointDef ();
      def.base.bodyIdA = chassis;
      def.base.bodyIdB = wheels[wheel];
      // Spring zero is top-out, a sag below the standing axle.
      def.base.localFrameA = { to_b3 (jeep_rig::axle_of (wheel) -
                                      Vec3 (0.0f, jeep_rig::sag, 0.0f)),
                               frame };
      def.base.localFrameB = { { 0.0f, 0.0f, 0.0f }, frame };
      def.base.collideConnected = false;
      def.enableSuspensionSpring = true;
      def.suspensionHertz = hertz;
      def.suspensionDampingRatio = damping;
      def.enableSuspensionLimit = true;
      def.lowerSuspensionLimit = -(jeep_rig::sag + jeep_rig::bump);
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
        def.lowerSteeringLimit = -(jeep_rig::full_lock + 0.1f);
        def.upperSteeringLimit = jeep_rig::full_lock + 0.1f;
      }
      return b3CreateWheelJoint (world, &def);
    }

    void build (const State& s) {
      destroy ();
      b3WorldDef def = b3DefaultWorldDef ();
      def.gravity = { 0.0f, -jeep_rig::gravity, 0.0f };
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

    RigidBodyState read (b3BodyId body) const {
      RigidBodyState s;
      s.position = from_b3 (b3Body_GetPosition (body));
      s.rotation = array_of (b3Body_GetRotation (body));
      s.linear_velocity = from_b3 (b3Body_GetLinearVelocity (body));
      s.angular_velocity = from_b3 (b3Body_GetAngularVelocity (body));
      return s;
    }

    State snapshot () const {
      State s = kept;
      s.chassis = read (chassis);
      for (int wheel = 0; wheel < wheel_count; ++wheel)
        s.wheels[wheel] = read (wheels[wheel]);
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
      const b3Quat q = jeep_rig::level_rotation (heading);
      const std::array<float, 4> rotation = array_of (q);
      Vec3 centre = at;
      float highest = -1e30f;
      for (int wheel = 0; wheel < wheel_count; ++wheel) {
        const Vec3 under = at + rotated (rotation, jeep_rig::axle_of (wheel));
        highest = std::max (highest,
                            surface_elevation_at (surface, under[0], under[2]));
      }
      centre[1] = highest + wheel_radius + 0.04f;
      State s = kept;
      s.chassis = { centre, rotation, Vec3 (), Vec3 () };
      for (int wheel = 0; wheel < wheel_count; ++wheel)
        s.wheels[wheel] = { centre +
                              rotated (rotation, jeep_rig::axle_of (wheel)),
                            rotation,
                            Vec3 (),
                            Vec3 () };
      patch.origin_for (centre, s.ground_column, s.ground_row);
      s.touching.fill (false);
      s.load.fill (0.0f);
      s.still = s.overturned = 0.0f;
      s.asleep = false;
      return s;
    }

    void place (const Vec3& at, const Vec3& heading) {
      const State s = standing (at, heading);
      // A jeep already standing in a world is moved in it; only a patch
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

    void step (float dt, float water_level) {
      recentre_ground ();
      const Vec3 forward = axis (Vec3 (0, 0, 1));
      const Vec3 v = from_b3 (b3Body_GetLinearVelocity (chassis));
      const float vf = dot (v, forward);
      const float speed = length (v);
      const float r = wheel_radius;

      // The wheel is turned less far the faster the jeep goes. The
      // joint steers about its axis down the chassis, so a positive
      // angle turns to the right.
      const float lock =
        jeep_rig::full_lock / (1.0f + (speed / jeep_rig::lock_fade_speed) *
                                        (speed / jeep_rig::lock_fade_speed));
      const float steer = std::clamp (kept.turn, -1.0f, 1.0f) * lock;
      b3WheelJoint_SetTargetSteeringAngle (joints[0], steer);
      b3WheelJoint_SetTargetSteeringAngle (joints[1], steer);

      // Engine and brakes through the wheel joints' spin motors: all
      // four wheels drive, each within what its load lets it put down.
      const float drive =
        kept.parked ? 0.0f : std::clamp (kept.drive, -1.0f, 1.0f);
      const float pull =
        std::min (jeep_rig::engine_force,
                  jeep_rig::engine_power / std::max (std::abs (vf), 2.0f));
      for (int wheel = 0; wheel < wheel_count; ++wheel) {
        float spin = 0.0f;
        float torque = jeep_rig::engine_brake_torque;
        const float grip = jeep_rig::traction_share * jeep_rig::tyre_friction *
                           kept.load[wheel] * r;
        if (kept.parked) {
          torque = jeep_rig::parking_torque;
        } else if (kept.brake || (drive * vf < 0.0f && std::abs (vf) > 1.5f)) {
          // Asking for the other way while still rolling is braking.
          const float effort = kept.brake ? 1.0f : std::abs (drive);
          torque = effort * jeep_rig::brake_force * r / wheel_count;
        } else if (std::abs (drive) > 0.02f) {
          const float share = drive > 0.0f ? 1.0f : jeep_rig::reverse_share;
          const float wanted =
            std::abs (drive) * share * pull * r / wheel_count;
          if (kept.touching[wheel]) {
            spin = drive > 0.0f ? (std::max (vf, 0.0f) + 20.0f) / r
                                : -jeep_rig::reverse_speed / r;
            torque = std::min (wanted, grip);
          } else {
            // In the air a wheel only revs up to road speed.
            spin = (vf + std::copysign (1.0f, drive)) / r;
            torque = jeep_rig::free_rev_torque;
          }
        }
        b3WheelJoint_SetSpinMotorSpeed (joints[wheel], spin);
        b3WheelJoint_SetMaxSpinTorque (joints[wheel], torque);
      }

      // Drag, and water up to the sills, on the whole jeep's mass.
      const float wet = std::clamp (
        (water_level - (point (Vec3 (0, -0.3f, 0))[1])) / 0.8f, 0.0f, 1.0f);
      const Vec3 force =
        v * (-(jeep_rig::rolling_drag + jeep_rig::air_drag * speed +
               jeep_rig::water_drag * wet) *
             jeep_rig::total_mass);
      b3Body_ApplyForceToCenter (chassis, to_b3 (force), true);

      b3World_Step (world, dt, jeep_rig::substeps);

      for (int wheel = 0; wheel < wheel_count; ++wheel)
        kept.touching[wheel] =
          wheel_touches_ground (wheels[wheel], dt, kept.load[wheel]);
      kept.wheel_spin = std::remainder (
        kept.wheel_spin + b3WheelJoint_GetSpinSpeed (joints[2]) * dt,
        6.2831853f);

      // Left on its side or back, it is set on its wheels again.
      const Vec3 moving = from_b3 (b3Body_GetLinearVelocity (chassis));
      const Vec3 turning = from_b3 (b3Body_GetAngularVelocity (chassis));
      const bool resting = length2 (moving) < 2.0f * 2.0f;
      const float upright = axis (Vec3 (0, 1, 0))[1];
      kept.overturned =
        upright < 0.25f && resting ? kept.overturned + dt : 0.0f;
      if (kept.overturned > 2.5f) {
        place (point (Vec3 ()), axis (Vec3 (0, 0, 1)));
        return;
      }

      // Parked and still, it stays where it is.
      const bool quiet = length2 (moving) < 0.08f * 0.08f &&
                         length2 (turning) < 0.08f * 0.08f &&
                         std::any_of (
                           kept.touching.begin (),
                           kept.touching.end (),
                           [] (bool touching) { return touching; });
      kept.still = kept.parked && quiet ? kept.still + dt : 0.0f;
      if (kept.still > 1.0f)
        kept.asleep = true;
    }
  };

  Jeep::Jeep (const map::SurfaceGeometry& surface)
      : m_impl (std::make_unique<Impl> (surface)) {
    m_impl->place (Vec3 (), Vec3 (0, 0, 1));
  }

  Jeep::~Jeep () = default;

  Jeep::State Jeep::state () const {
    return m_impl->snapshot ();
  }

  void Jeep::restore (const State& state) {
    m_impl->build (state);
  }

  void Jeep::place (const Vec3& at, const Vec3& heading) {
    m_impl->place (at, heading);
  }

  void Jeep::set_trunks (const TrunkField* trunks) {
    Impl& m = *m_impl;
    if (trunks == m.trunks)
      return;
    const State s = m.snapshot ();
    m.trunks = trunks;
    m.build (s);
  }

  void Jeep::set_drive (float drive) {
    m_impl->kept.drive = drive;
  }

  void Jeep::set_turn (float turn) {
    m_impl->kept.turn = turn;
  }

  void Jeep::set_brake (bool brake) {
    m_impl->kept.brake = brake;
  }

  void Jeep::set_parked (bool parked) {
    State& kept = m_impl->kept;
    if (kept.parked && !parked)
      kept.asleep = false;
    kept.parked = parked;
  }

  void Jeep::update (float dt, float water_level) {
    if (m_impl->kept.asleep || dt <= 0.0f)
      return;
    m_impl->step (dt, water_level);
  }

  Vec3 Jeep::position () const {
    return m_impl->point (Vec3 ());
  }

  Vec3 Jeep::forward () const {
    return m_impl->axis (Vec3 (0, 0, 1));
  }

  Vec3 Jeep::up () const {
    return m_impl->axis (Vec3 (0, 1, 0));
  }

  Vec3 Jeep::left () const {
    return m_impl->axis (Vec3 (1, 0, 0));
  }

  Vec3 Jeep::velocity () const {
    return from_b3 (b3Body_GetLinearVelocity (m_impl->chassis));
  }

  bool Jeep::grounded () const {
    const auto& touching = m_impl->kept.touching;
    return std::any_of (
      touching.begin (), touching.end (), [] (bool wheel) { return wheel; });
  }

  bool Jeep::asleep () const {
    return m_impl->kept.asleep;
  }

  Vec3 Jeep::point (const Vec3& local) const {
    return m_impl->point (local);
  }

  float Jeep::wheel_drop (int wheel) const {
    const Impl& m = *m_impl;
    const Vec3 at = from_b3 (b3Body_GetPosition (m.wheels[wheel]));
    return dot (at - m.point (jeep_rig::axle_of (wheel)),
                m.axis (Vec3 (0, -1, 0)));
  }

  float Jeep::steer () const {
    return b3WheelJoint_GetSteeringAngle (m_impl->joints[0]);
  }

  float Jeep::wheel_spin () const {
    return m_impl->kept.wheel_spin;
  }
}
