#include <moppe/mov/box3d_vec.hh>
#include <moppe/mov/ground_patch.hh>
#include <moppe/mov/tractor.hh>

#include <algorithm>
#include <cmath>
#include <vector>

#include <box3d/box3d.h>

namespace moppe::mov {
  namespace {
    // Everything of the tractor's is kept in one namespace: the engine is
    // a unity build, and the jeep's and the bike's rigs have helpers of
    // their own.
    namespace tractor_rig {
      constexpr float gravity = 9.81f;
      constexpr int substeps = 4;

      constexpr float chassis_mass = 1700.0f;
      // Pitch, yaw, and roll about the centre of mass, which lies over
      // the gearbox, back toward the rear axle and high; the weights
      // hung on the nose keep it from lying further back.
      constexpr float inertia[3] = { 1350.0f, 1450.0f, 520.0f };
      constexpr float mass_centre[3] = { 0.0f, 0.78f, -0.15f };
      constexpr float front_wheel_mass = 35.0f;
      constexpr float front_wheel_inertia = 2.0f;
      constexpr float rear_wheel_mass = 130.0f;
      constexpr float rear_wheel_inertia = 28.0f;
      constexpr float total_mass =
        chassis_mass + 2.0f * (front_wheel_mass + rear_wheel_mass);

      // There are no springs but the tyres', which give a little and
      // bounce: a short sag and a short way beyond it to the stops.
      constexpr float sag = 0.05f;
      constexpr float bump = 0.08f;
      constexpr float sprung_damping_ratio = 0.45f;

      // Box3D mixes two shapes' friction as a geometric mean, so the
      // tyre carries the square of the wanted coefficient against unit
      // ground friction. The rear bars bite.
      constexpr float front_friction = 1.0f;
      constexpr float rear_friction = 1.35f;
      constexpr float ground_friction = 1.0f;

      // The rear wheels pull hard and the engine is governed: the
      // throttle asks for a road speed, which the wheels are driven to
      // and held at.
      constexpr float engine_force = 9000.0f;
      constexpr float engine_power = 38000.0f;
      constexpr float top_speed = 8.5f;
      constexpr float reverse_speed = 3.2f;
      constexpr float brake_force = 18000.0f;
      // Off the throttle the gearbox holds it back.
      constexpr float engine_brake_torque = 350.0f;
      constexpr float parking_torque = 9000.0f;
      constexpr float free_rev_torque = 60.0f;
      constexpr float traction_share = 0.95f;
      // With its nose off the ground the driver eases off, or it would
      // rear over onto its back.
      constexpr float rearing_share = 0.35f;

      constexpr float full_lock = 0.62f;
      constexpr float lock_fade_speed = 9.0f;

      constexpr float rolling_resistance = 0.04f;
      constexpr float air_drag = 0.004f;
      constexpr float water_drag = 1.2f;

      // The ground patch, as the jeep's.
      constexpr int patch_samples = 97;
      constexpr float patch_recentre_distance = 24.0f;
      constexpr float trunk_reach = 72.0f;

      constexpr std::uint64_t ground_bit = 1;
      constexpr std::uint64_t trunk_bit = 2;
      constexpr std::uint64_t wheel_bit = 4;
      constexpr std::uint64_t chassis_bit = 8;

      // The tractor's frame for a level heading.
      b3Quat level_rotation (const Vec3& heading) {
        Vec3 f = heading;
        f[1] = 0.0f;
        f = length2 (f) > 1e-8f ? normalized (f) : Vec3 (0, 0, 1);
        const Vec3 up (0, 1, 0);
        const Vec3 left = cross (up, f);
        const b3Matrix3 m { to_b3 (left), to_b3 (up), to_b3 (f) };
        return b3NormalizeQuat (b3MakeQuatFromMatrix (&m));
      }

      bool front (int wheel) {
        return wheel < 2;
      }

      Vec3 axle_of (int wheel) {
        const float track =
          front (wheel) ? Tractor::front_track : Tractor::rear_track;
        return Vec3 (wheel % 2 == 0 ? track : -track,
                     Tractor::wheel_radius (wheel),
                     front (wheel) ? Tractor::front_axle : Tractor::rear_axle);
      }

      float wheel_mass (int wheel) {
        return front (wheel) ? front_wheel_mass : rear_wheel_mass;
      }
    }
  }

  struct Tractor::Impl {
    const map::SurfaceGeometry& surface;
    GroundPatch patch;
    std::array<float, wheel_count> hertz {}, damping {};

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
          patch (
            surface,
            { .ground_friction = tractor_rig::ground_friction,
              .obstacle_friction = 0.3f,
              .obstacle_restitution = 0.1f,
              .ground_bit = tractor_rig::ground_bit,
              .ground_mask = tractor_rig::wheel_bit | tractor_rig::chassis_bit,
              .obstacle_bit = tractor_rig::trunk_bit,
              .obstacle_mask = tractor_rig::chassis_bit,
              .samples = tractor_rig::patch_samples,
              .recentre_distance = tractor_rig::patch_recentre_distance,
              .trunk_reach = tractor_rig::trunk_reach }) {
      wheels.fill (b3_nullBodyId);
      joints.fill (b3_nullJointId);
      // Each wheel carries its share of the chassis as it stands, most
      // of it on the rear pair; the joint springs are soft constraints
      // tuned in hertz against the joint's effective mass, so convert
      // the rate and damping wanted for that share into those terms.
      const float base = Tractor::front_axle - Tractor::rear_axle;
      const float rear_share =
        (Tractor::front_axle - tractor_rig::mass_centre[2]) / base;
      for (int wheel = 0; wheel < wheel_count; ++wheel) {
        const Vec3 axle = tractor_rig::axle_of (wheel);
        const float along = axle[2] - tractor_rig::mass_centre[2];
        const float effective =
          1.0f / (1.0f / tractor_rig::wheel_mass (wheel) +
                  1.0f / tractor_rig::chassis_mass +
                  along * along / tractor_rig::inertia[0] +
                  axle[0] * axle[0] / tractor_rig::inertia[2]);
        const float sprung =
          0.5f * tractor_rig::chassis_mass *
          (tractor_rig::front (wheel) ? 1.0f - rear_share : rear_share);
        const float stiffness =
          sprung * tractor_rig::gravity / tractor_rig::sag;
        const float c = 2.0f * tractor_rig::sprung_damping_ratio *
                        std::sqrt (stiffness * sprung);
        hertz[wheel] = std::sqrt (stiffness / effective) / (2.0f * 3.14159265f);
        damping[wheel] = c / (2.0f * std::sqrt (stiffness * effective));
      }
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

    b3BodyId create_wheel (int wheel_index, const RigidBodyState& s) {
      const bool front = tractor_rig::front (wheel_index);
      const b3BodyId wheel = create_body (s);
      b3ShapeDef shape = b3DefaultShapeDef ();
      shape.updateBodyMass = false;
      const float grip =
        front ? tractor_rig::front_friction : tractor_rig::rear_friction;
      shape.baseMaterial.friction = grip * grip / tractor_rig::ground_friction;
      shape.baseMaterial.rollingResistance = tractor_rig::rolling_resistance;
      shape.filter.categoryBits = tractor_rig::wheel_bit;
      shape.filter.maskBits = tractor_rig::ground_bit;
      const b3Sphere sphere { { 0.0f, 0.0f, 0.0f },
                              wheel_radius (wheel_index) };
      b3CreateSphereShape (wheel, &shape, &sphere);
      const float inertia = front ? tractor_rig::front_wheel_inertia
                                  : tractor_rig::rear_wheel_inertia;
      b3MassData mass {};
      mass.mass = tractor_rig::wheel_mass (wheel_index);
      mass.inertia.cx = { inertia, 0.0f, 0.0f };
      mass.inertia.cy = { 0.0f, inertia, 0.0f };
      mass.inertia.cz = { 0.0f, 0.0f, inertia };
      b3Body_SetMassData (wheel, mass);
      return wheel;
    }

    void create_chassis (const RigidBodyState& s) {
      chassis = create_body (s);
      // The bonnet and front axle, wide enough to shield the front
      // wheels; the gearbox and seat; and the mudguards over the rear
      // wheels. The wheels meet only the ground, so these meet trunks
      // and, when the tractor goes over, the ground.
      b3ShapeDef shape = b3DefaultShapeDef ();
      shape.updateBodyMass = false;
      shape.baseMaterial.friction = 0.45f;
      shape.filter.categoryBits = tractor_rig::chassis_bit;
      shape.filter.maskBits = tractor_rig::ground_bit | tractor_rig::trunk_bit;
      const b3BoxHull nose =
        b3MakeOffsetBoxHull (0.76f, 0.34f, 0.55f, { 0.0f, 0.82f, 0.95f });
      b3CreateHullShape (chassis, &shape, &nose.base);
      const b3BoxHull middle =
        b3MakeOffsetBoxHull (0.32f, 0.42f, 0.70f, { 0.0f, 0.95f, -0.25f });
      b3CreateHullShape (chassis, &shape, &middle.base);
      const b3BoxHull rear = b3MakeOffsetBoxHull (
        0.96f, 0.36f, 0.62f, { 0.0f, 1.02f, Tractor::rear_axle - 0.05f });
      b3CreateHullShape (chassis, &shape, &rear.base);
      b3MassData mass {};
      mass.mass = tractor_rig::chassis_mass;
      mass.center = { tractor_rig::mass_centre[0],
                      tractor_rig::mass_centre[1],
                      tractor_rig::mass_centre[2] };
      mass.inertia.cx = { tractor_rig::inertia[0], 0.0f, 0.0f };
      mass.inertia.cy = { 0.0f, tractor_rig::inertia[1], 0.0f };
      mass.inertia.cz = { 0.0f, 0.0f, tractor_rig::inertia[2] };
      b3Body_SetMassData (chassis, mass);
    }

    b3JointId create_suspension (int wheel) {
      const b3Quat frame = suspension_rotation ();
      b3WheelJointDef def = b3DefaultWheelJointDef ();
      def.base.bodyIdA = chassis;
      def.base.bodyIdB = wheels[wheel];
      // Spring zero is top-out, a sag below the standing axle.
      def.base.localFrameA = { to_b3 (tractor_rig::axle_of (wheel) -
                                      Vec3 (0.0f, tractor_rig::sag, 0.0f)),
                               frame };
      def.base.localFrameB = { { 0.0f, 0.0f, 0.0f }, frame };
      def.base.collideConnected = false;
      def.enableSuspensionSpring = true;
      def.suspensionHertz = hertz[wheel];
      def.suspensionDampingRatio = damping[wheel];
      def.enableSuspensionLimit = true;
      def.lowerSuspensionLimit = -(tractor_rig::sag + tractor_rig::bump);
      def.upperSuspensionLimit = 0.0f;
      def.enableSpinMotor = true;
      def.maxSpinTorque = 0.0f;
      def.spinSpeed = 0.0f;
      if (tractor_rig::front (wheel)) {
        def.enableSteering = true;
        def.steeringHertz = 5.0f;
        def.steeringDampingRatio = 1.0f;
        def.maxSteeringTorque = 4000.0f;
        def.enableSteeringLimit = true;
        def.lowerSteeringLimit = -(tractor_rig::full_lock + 0.1f);
        def.upperSteeringLimit = tractor_rig::full_lock + 0.1f;
      }
      return b3CreateWheelJoint (world, &def);
    }

    void build (const State& s) {
      destroy ();
      b3WorldDef def = b3DefaultWorldDef ();
      def.gravity = { 0.0f, -tractor_rig::gravity, 0.0f };
      def.enableSleep = false;
      def.workerCount = 1;
      world = b3CreateWorld (&def);
      patch.build (world, trunks, s.ground_column, s.ground_row);
      create_chassis (s.chassis);
      for (int wheel = 0; wheel < wheel_count; ++wheel)
        wheels[wheel] = create_wheel (wheel, s.wheels[wheel]);
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

    // The standing pose at `at`, its origin level with the highest
    // ground under any wheel so that none starts inside it; the springs
    // settle the rest.
    State standing (const Vec3& at, const Vec3& heading) const {
      const b3Quat q = tractor_rig::level_rotation (heading);
      const std::array<float, 4> rotation = array_of (q);
      Vec3 centre = at;
      float highest = -1e30f;
      for (int wheel = 0; wheel < wheel_count; ++wheel) {
        const Vec3 under =
          at + rotated (rotation, tractor_rig::axle_of (wheel));
        highest = std::max (highest,
                            surface_elevation_at (surface, under[0], under[2]));
      }
      centre[1] = highest + 0.04f;
      State s = kept;
      s.chassis = { centre, rotation, Vec3 (), Vec3 () };
      for (int wheel = 0; wheel < wheel_count; ++wheel)
        s.wheels[wheel] = { centre +
                              rotated (rotation, tractor_rig::axle_of (wheel)),
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
      // A tractor already standing in a world is moved in it; only a
      // patch left far behind costs a rebuild.
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

      // The wheel is turned less far the faster the tractor goes. The
      // joint steers about its axis down the chassis, so a positive
      // angle turns to the right.
      const float fade = speed / tractor_rig::lock_fade_speed;
      const float lock = tractor_rig::full_lock / (1.0f + fade * fade);
      const float steer = std::clamp (kept.turn, -1.0f, 1.0f) * lock;
      b3WheelJoint_SetTargetSteeringAngle (joints[0], steer);
      b3WheelJoint_SetTargetSteeringAngle (joints[1], steer);

      // The rear wheels are driven toward the road speed the throttle
      // asks for, each within what its load lets it put down; the brakes
      // act on all four.
      const float drive =
        kept.parked ? 0.0f : std::clamp (kept.drive, -1.0f, 1.0f);
      const float pull =
        std::min (tractor_rig::engine_force,
                  tractor_rig::engine_power / std::max (std::abs (vf), 1.0f));
      const bool braking =
        kept.brake || (drive * vf < 0.0f && std::abs (vf) > 1.0f);
      const bool rearing = !kept.touching[0] && !kept.touching[1] &&
                           (kept.touching[2] || kept.touching[3]);
      for (int wheel = 0; wheel < wheel_count; ++wheel) {
        const float r = wheel_radius (wheel);
        const bool driven = !tractor_rig::front (wheel);
        float spin = 0.0f;
        float torque = driven ? tractor_rig::engine_brake_torque : 0.0f;
        const float grip = tractor_rig::traction_share *
                           tractor_rig::rear_friction * kept.load[wheel] * r;
        if (kept.parked) {
          torque = tractor_rig::parking_torque;
        } else if (braking) {
          // Asking for the other way while still rolling is braking.
          const float effort = kept.brake ? 1.0f : std::abs (drive);
          torque = effort * tractor_rig::brake_force * r / wheel_count;
        } else if (!driven) {
          // The front wheels roll free.
          torque = 0.0f;
        } else if (std::abs (drive) > 0.02f) {
          const float wanted =
            pull * r / 2.0f * (rearing ? tractor_rig::rearing_share : 1.0f);
          if (kept.touching[wheel]) {
            spin = drive > 0.0f ? drive * tractor_rig::top_speed / r
                                : drive * tractor_rig::reverse_speed / r;
            torque = std::min (wanted, grip);
          } else {
            // In the air a wheel only revs up to road speed.
            spin = (vf + std::copysign (1.0f, drive)) / r;
            torque = tractor_rig::free_rev_torque;
          }
        }
        b3WheelJoint_SetSpinMotorSpeed (joints[wheel], spin);
        b3WheelJoint_SetMaxSpinTorque (joints[wheel], torque);
      }

      // The air's drag, and water's up to the axles, on the whole
      // tractor's mass.
      const float wet = std::clamp (
        (water_level - point (Vec3 (0, 0.2f, 0))[1]) / 0.8f, 0.0f, 1.0f);
      const Vec3 force =
        v * (-(tractor_rig::air_drag * speed + tractor_rig::water_drag * wet) *
             tractor_rig::total_mass);
      b3Body_ApplyForceToCenter (chassis, to_b3 (force), true);

      b3World_Step (world, dt, tractor_rig::substeps);

      for (int wheel = 0; wheel < wheel_count; ++wheel)
        kept.touching[wheel] =
          wheel_touches_ground (wheels[wheel], dt, kept.load[wheel]);
      kept.front_spin = std::remainder (
        kept.front_spin + b3WheelJoint_GetSpinSpeed (joints[0]) * dt,
        6.2831853f);
      kept.rear_spin = std::remainder (
        kept.rear_spin + b3WheelJoint_GetSpinSpeed (joints[2]) * dt,
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

  Tractor::Tractor (const map::SurfaceGeometry& surface)
      : m_impl (std::make_unique<Impl> (surface)) {
    m_impl->place (Vec3 (), Vec3 (0, 0, 1));
  }

  Tractor::~Tractor () = default;

  Tractor::State Tractor::state () const {
    return m_impl->snapshot ();
  }

  void Tractor::restore (const State& state) {
    m_impl->build (state);
  }

  void Tractor::place (const Vec3& at, const Vec3& heading) {
    m_impl->place (at, heading);
  }

  void Tractor::set_trunks (const TrunkField* trunks) {
    Impl& m = *m_impl;
    if (trunks == m.trunks)
      return;
    const State s = m.snapshot ();
    m.trunks = trunks;
    m.build (s);
  }

  void Tractor::set_drive (float drive) {
    m_impl->kept.drive = drive;
  }

  void Tractor::set_turn (float turn) {
    m_impl->kept.turn = turn;
  }

  void Tractor::set_brake (bool brake) {
    m_impl->kept.brake = brake;
  }

  void Tractor::set_parked (bool parked) {
    State& kept = m_impl->kept;
    if (kept.parked && !parked)
      kept.asleep = false;
    kept.parked = parked;
  }

  void Tractor::update (float dt, float water_level) {
    if (m_impl->kept.asleep || dt <= 0.0f)
      return;
    m_impl->step (dt, water_level);
  }

  Vec3 Tractor::position () const {
    return m_impl->point (Vec3 ());
  }

  Vec3 Tractor::forward () const {
    return m_impl->axis (Vec3 (0, 0, 1));
  }

  Vec3 Tractor::up () const {
    return m_impl->axis (Vec3 (0, 1, 0));
  }

  Vec3 Tractor::left () const {
    return m_impl->axis (Vec3 (1, 0, 0));
  }

  Vec3 Tractor::velocity () const {
    return from_b3 (b3Body_GetLinearVelocity (m_impl->chassis));
  }

  bool Tractor::grounded () const {
    const auto& touching = m_impl->kept.touching;
    return std::any_of (
      touching.begin (), touching.end (), [] (bool wheel) { return wheel; });
  }

  bool Tractor::asleep () const {
    return m_impl->kept.asleep;
  }

  float Tractor::mass () const {
    return tractor_rig::total_mass;
  }

  void Tractor::push (const Vec3& at, const Vec3& impulse) {
    Impl& m = *m_impl;
    m.kept.asleep = false;
    m.kept.still = 0.0f;
    b3Body_ApplyLinearImpulse (m.chassis, to_b3 (impulse), to_b3 (at), true);
  }

  float Tractor::throttle () const {
    const State& kept = m_impl->kept;
    if (kept.parked)
      return 0.0f;
    return std::max (0.15f, std::min (1.0f, std::abs (kept.drive)));
  }

  Vec3 Tractor::point (const Vec3& local) const {
    return m_impl->point (local);
  }

  float Tractor::wheel_drop (int wheel) const {
    const Impl& m = *m_impl;
    const Vec3 at = from_b3 (b3Body_GetPosition (m.wheels[wheel]));
    return dot (at - m.point (tractor_rig::axle_of (wheel)),
                m.axis (Vec3 (0, -1, 0)));
  }

  float Tractor::steer () const {
    return b3WheelJoint_GetSteeringAngle (m_impl->joints[0]);
  }

  float Tractor::front_spin () const {
    return m_impl->kept.front_spin;
  }

  float Tractor::rear_spin () const {
    return m_impl->kept.rear_spin;
  }
}
