#include <moppe/mov/box3d_vec.hh>
#include <moppe/mov/caddy.hh>
#include <moppe/mov/ground_patch.hh>

#include <algorithm>
#include <cmath>
#include <vector>

#include <box3d/box3d.h>

namespace moppe::mov {
  namespace {
    // Everything of the Caddy's is kept in one namespace: the engine is a
    // unity build, and the jeep's rig next door has helpers of its own.
    namespace caddy_rig {
      constexpr float gravity = 9.81f;
      constexpr int substeps = 4;

      // Some 900 kg at the kerb with the hardtop on, nose-heavy over the
      // engine set across the front.
      constexpr float chassis_mass = 860.0f;
      // Pitch, yaw, and roll about the centre of mass.
      constexpr float inertia[3] = { 1300.0f, 1450.0f, 380.0f };
      constexpr float mass_centre[3] = { 0.0f, 0.24f, 0.30f };
      constexpr float wheel_mass = 16.0f;
      constexpr float wheel_inertia = 0.75f;
      constexpr float total_mass = chassis_mass + 4.0f * wheel_mass;
      // Slung under the helicopter: how quickly the air damps its turning,
      // a second, and how hard it turns its nose into the way it is
      // carried, an angular acceleration a radian off.
      constexpr float slung_damping = 1.6f;
      constexpr float slung_vane = 2.0f;

      // The springs are linear from top-out, so the sag at rest sets
      // their rate; beyond the sag there is this much more before the
      // stops. A car's: softer and shorter than the jeep's.
      constexpr float sag = 0.085f;
      constexpr float bump = 0.10f;
      constexpr float sprung_damping_ratio = 0.42f;

      // Box3D mixes two shapes' friction as a geometric mean, so the
      // tyre carries the square of the wanted coefficient against unit
      // ground friction.
      constexpr float tyre_friction = 1.08f;
      constexpr float ground_friction = 1.0f;

      // The 1.6 litre's seventy horsepower, through the front wheels.
      constexpr float engine_force = 5600.0f;
      constexpr float engine_power = 52000.0f;
      constexpr float brake_force = 9500.0f;
      // The brakes' share at the front, where the weight goes.
      constexpr float front_brake_share = 0.62f;
      constexpr float reverse_speed = 6.0f;
      constexpr float reverse_share = 0.55f;
      constexpr float engine_brake_torque = 28.0f;
      constexpr float parking_torque = 2600.0f;
      constexpr float free_rev_torque = 18.0f;
      constexpr float traction_share = 0.9f;

      constexpr float full_lock = 0.58f;
      constexpr float lock_fade_speed = 15.0f;

      // What it costs a loaded tyre to roll, as a share of its load: the
      // wheels' own rolling resistance, so there is none in the air.
      constexpr float rolling_resistance = 0.015f;
      // Drag per speed per mass: set so that the engine's power is spent
      // against it a little short of 140 km/h.
      constexpr float air_drag = 0.00105f;
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

      // The Caddy's frame for a level heading.
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
        const float track = wheel < 2 ? Caddy::front_track : Caddy::rear_track;
        return Vec3 (wheel % 2 == 0 ? track : -track,
                     0.0f,
                     wheel < 2 ? Caddy::axle : -Caddy::axle);
      }
    }
  }

  struct Caddy::Impl {
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
    float kept_throttle = 0.0f;

    explicit Impl (const map::SurfaceGeometry& surface)
        : surface (surface),
          patch (surface,
                 { .ground_friction = caddy_rig::ground_friction,
                   .obstacle_friction = 0.3f,
                   .obstacle_restitution = 0.15f,
                   .ground_bit = caddy_rig::ground_bit,
                   .ground_mask = caddy_rig::wheel_bit | caddy_rig::chassis_bit,
                   .obstacle_bit = caddy_rig::trunk_bit,
                   .obstacle_mask = caddy_rig::chassis_bit,
                   .samples = caddy_rig::patch_samples,
                   .recentre_distance = caddy_rig::patch_recentre_distance,
                   .trunk_reach = caddy_rig::trunk_reach }) {
      wheels.fill (b3_nullBodyId);
      joints.fill (b3_nullJointId);
      // The joint springs are soft constraints tuned in hertz against
      // the joint's effective mass, which the light wheel dominates.
      // Convert the rate and damping wanted for the sprung chassis into
      // those terms.
      const float effective =
        1.0f / (1.0f / caddy_rig::wheel_mass + 1.0f / caddy_rig::chassis_mass +
                axle * axle / caddy_rig::inertia[0] +
                front_track * front_track / caddy_rig::inertia[2]);
      const float sprung = 0.25f * caddy_rig::chassis_mass;
      const float stiffness = sprung * caddy_rig::gravity / caddy_rig::sag;
      const float c =
        2.0f * caddy_rig::sprung_damping_ratio * std::sqrt (stiffness * sprung);
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

    b3BodyId create_body (const RigidBodyState& s, bool spins = false) {
      b3BodyDef def = b3DefaultBodyDef ();
      // Box3D holds a body to a quarter turn a step unless told it may
      // spin faster, which would hold these small wheels under 100 km/h.
      def.allowFastRotation = spins;
      def.type = b3_dynamicBody;
      def.position = to_b3 (s.position);
      def.rotation = quat_of (s.rotation);
      def.linearVelocity = to_b3 (s.linear_velocity);
      def.angularVelocity = to_b3 (s.angular_velocity);
      def.enableSleep = false;
      return b3CreateBody (world, &def);
    }

    b3BodyId create_wheel (const RigidBodyState& s) {
      const b3BodyId wheel = create_body (s, true);
      b3ShapeDef shape = b3DefaultShapeDef ();
      shape.updateBodyMass = false;
      shape.baseMaterial.friction = caddy_rig::tyre_friction *
                                    caddy_rig::tyre_friction /
                                    caddy_rig::ground_friction;
      shape.baseMaterial.rollingResistance = caddy_rig::rolling_resistance;
      shape.filter.categoryBits = caddy_rig::wheel_bit;
      shape.filter.maskBits = caddy_rig::ground_bit;
      const b3Sphere sphere { { 0.0f, 0.0f, 0.0f }, wheel_radius };
      b3CreateSphereShape (wheel, &shape, &sphere);
      b3MassData mass {};
      mass.mass = caddy_rig::wheel_mass;
      mass.inertia.cx = { caddy_rig::wheel_inertia, 0.0f, 0.0f };
      mass.inertia.cy = { 0.0f, caddy_rig::wheel_inertia, 0.0f };
      mass.inertia.cz = { 0.0f, 0.0f, caddy_rig::wheel_inertia };
      b3Body_SetMassData (wheel, mass);
      return wheel;
    }

    void create_chassis (const RigidBodyState& s) {
      chassis = create_body (s);
      // The body from the sills to the waist, wide enough to shield the
      // wheels (which meet only the ground); the cab over the front of
      // it; and the hardtop over the bed and the cab's roof. All meet
      // trunks and, when the springs are spent or it goes over, the
      // ground. models/caddy.blend's measures, heights above the axles.
      b3ShapeDef shape = b3DefaultShapeDef ();
      shape.updateBodyMass = false;
      shape.baseMaterial.friction = 0.45f;
      shape.filter.categoryBits = caddy_rig::chassis_bit;
      shape.filter.maskBits = caddy_rig::ground_bit | caddy_rig::trunk_bit;
      const b3BoxHull body =
        b3MakeOffsetBoxHull (0.82f, 0.33f, 2.14f, { 0.0f, 0.35f, -0.08f });
      b3CreateHullShape (chassis, &shape, &body.base);
      const b3BoxHull cab =
        b3MakeOffsetBoxHull (0.66f, 0.25f, 0.42f, { 0.0f, 0.92f, 0.08f });
      b3CreateHullShape (chassis, &shape, &cab.base);
      const b3BoxHull hardtop =
        b3MakeOffsetBoxHull (0.785f, 0.47f, 0.94f, { 0.0f, 1.15f, -1.28f });
      b3CreateHullShape (chassis, &shape, &hardtop.base);
      const b3BoxHull visor =
        b3MakeOffsetBoxHull (0.785f, 0.22f, 0.36f, { 0.0f, 1.40f, 0.0f });
      b3CreateHullShape (chassis, &shape, &visor.base);
      b3MassData mass {};
      mass.mass = caddy_rig::chassis_mass;
      mass.center = { caddy_rig::mass_centre[0],
                      caddy_rig::mass_centre[1],
                      caddy_rig::mass_centre[2] };
      mass.inertia.cx = { caddy_rig::inertia[0], 0.0f, 0.0f };
      mass.inertia.cy = { 0.0f, caddy_rig::inertia[1], 0.0f };
      mass.inertia.cz = { 0.0f, 0.0f, caddy_rig::inertia[2] };
      b3Body_SetMassData (chassis, mass);
    }

    b3JointId create_suspension (int wheel) {
      const b3Quat frame = suspension_rotation ();
      b3WheelJointDef def = b3DefaultWheelJointDef ();
      def.base.bodyIdA = chassis;
      def.base.bodyIdB = wheels[wheel];
      // Spring zero is top-out, a sag below the standing axle.
      def.base.localFrameA = { to_b3 (caddy_rig::axle_of (wheel) -
                                      Vec3 (0.0f, caddy_rig::sag, 0.0f)),
                               frame };
      def.base.localFrameB = { { 0.0f, 0.0f, 0.0f }, frame };
      def.base.collideConnected = false;
      def.enableSuspensionSpring = true;
      def.suspensionHertz = hertz;
      def.suspensionDampingRatio = damping;
      def.enableSuspensionLimit = true;
      def.lowerSuspensionLimit = -(caddy_rig::sag + caddy_rig::bump);
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
        def.lowerSteeringLimit = -(caddy_rig::full_lock + 0.1f);
        def.upperSteeringLimit = caddy_rig::full_lock + 0.1f;
      }
      return b3CreateWheelJoint (world, &def);
    }

    void build (const State& s) {
      destroy ();
      b3WorldDef def = b3DefaultWorldDef ();
      def.gravity = { 0.0f, -caddy_rig::gravity, 0.0f };
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
      const b3Quat q = caddy_rig::level_rotation (heading);
      const std::array<float, 4> rotation = array_of (q);
      Vec3 centre = at;
      float highest = -1e30f;
      for (int wheel = 0; wheel < wheel_count; ++wheel) {
        const Vec3 under = at + rotated (rotation, caddy_rig::axle_of (wheel));
        highest = std::max (highest,
                            surface_elevation_at (surface, under[0], under[2]));
      }
      centre[1] = highest + wheel_radius + 0.04f;
      State s = kept;
      s.chassis = { centre, rotation, Vec3 (), Vec3 () };
      for (int wheel = 0; wheel < wheel_count; ++wheel)
        s.wheels[wheel] = { centre +
                              rotated (rotation, caddy_rig::axle_of (wheel)),
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
      // A Caddy already standing in a world is moved in it; only a patch
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

      // The wheel is turned less far the faster it goes. The
      // joint steers about its axis down the chassis, so a positive
      // angle turns to the right.
      const float lock =
        caddy_rig::full_lock / (1.0f + (speed / caddy_rig::lock_fade_speed) *
                                         (speed / caddy_rig::lock_fade_speed));
      const float steer = std::clamp (kept.turn, -1.0f, 1.0f) * lock;
      b3WheelJoint_SetTargetSteeringAngle (joints[0], steer);
      b3WheelJoint_SetTargetSteeringAngle (joints[1], steer);

      // Engine and brakes through the wheel joints' spin motors: the
      // front pair drives, each within what its load lets it put down;
      // the rear pair only rolls, and brakes. The handbrake (and parking)
      // holds the rear.
      const float drive =
        kept.parked ? 0.0f : std::clamp (kept.drive, -1.0f, 1.0f);
      const float pull =
        std::min (caddy_rig::engine_force,
                  caddy_rig::engine_power / std::max (std::abs (vf), 2.0f));
      kept_throttle = kept.parked ? 0.0f : std::abs (drive);
      for (int wheel = 0; wheel < wheel_count; ++wheel) {
        const bool front = wheel < 2;
        float spin = 0.0f;
        float torque = front ? caddy_rig::engine_brake_torque : 0.0f;
        const float grip = caddy_rig::traction_share *
                           caddy_rig::tyre_friction * kept.load[wheel] * r;
        const float brake_share = front ? caddy_rig::front_brake_share
                                        : 1.0f - caddy_rig::front_brake_share;
        if (kept.parked) {
          torque = caddy_rig::parking_torque;
        } else if (kept.brake || (drive * vf < 0.0f && std::abs (vf) > 1.5f)) {
          // Asking for the other way while still rolling is braking.
          const float effort = kept.brake ? 1.0f : std::abs (drive);
          torque = effort * caddy_rig::brake_force * brake_share * r / 2.0f;
        } else if (std::abs (drive) > 0.02f && front) {
          const float share = drive > 0.0f ? 1.0f : caddy_rig::reverse_share;
          const float wanted = std::abs (drive) * share * pull * r / 2.0f;
          if (kept.touching[wheel]) {
            spin = drive > 0.0f ? (std::max (vf, 0.0f) + 20.0f) / r
                                : -caddy_rig::reverse_speed / r;
            torque = std::min (wanted, grip);
          } else {
            // In the air a wheel only revs up to road speed.
            spin = (vf + std::copysign (1.0f, drive)) / r;
            torque = caddy_rig::free_rev_torque;
          }
        }
        b3WheelJoint_SetSpinMotorSpeed (joints[wheel], spin);
        b3WheelJoint_SetMaxSpinTorque (joints[wheel], torque);
      }

      // The air's drag, and water's up to the sills, on the whole mass.
      const float wet = std::clamp (
        (water_level - (point (Vec3 (0, -0.3f, 0))[1])) / 0.8f, 0.0f, 1.0f);
      const Vec3 force =
        v * (-(caddy_rig::air_drag * speed + caddy_rig::water_drag * wet) *
             caddy_rig::total_mass);
      b3Body_ApplyForceToCenter (chassis, to_b3 (force), true);

      // Hung from the helicopter's line and off the ground, the air on
      // its flanks steadies its swinging and turns its nose the way it
      // is carried.
      const bool aloft = std::none_of (kept.touching.begin (),
                                       kept.touching.end (),
                                       [] (bool touching) { return touching; });
      if (kept.slung && aloft) {
        const Vec3 w = from_b3 (b3Body_GetAngularVelocity (chassis));
        Vec3 wanted = w * -caddy_rig::slung_damping;
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
          wanted[1] +=
            off * caddy_rig::slung_vane * std::min (1.0f, carried / 10.0f);
        }
        const b3Quat q = b3Body_GetRotation (chassis);
        const b3Vec3 local = b3InvRotateVector (q, to_b3 (wanted));
        const b3Vec3 torque { local.x * caddy_rig::inertia[0],
                              local.y * caddy_rig::inertia[1],
                              local.z * caddy_rig::inertia[2] };
        b3Body_ApplyTorque (chassis, b3RotateVector (q, torque), true);
      }

      b3World_Step (world, dt, caddy_rig::substeps);

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

  Caddy::Caddy (const map::SurfaceGeometry& surface)
      : m_impl (std::make_unique<Impl> (surface)) {
    m_impl->place (Vec3 (), Vec3 (0, 0, 1));
  }

  Caddy::~Caddy () = default;

  Caddy::State Caddy::state () const {
    return m_impl->snapshot ();
  }

  void Caddy::restore (const State& state) {
    m_impl->build (state);
  }

  void Caddy::place (const Vec3& at, const Vec3& heading) {
    m_impl->place (at, heading);
  }

  void Caddy::set_trunks (const TrunkField* trunks) {
    Impl& m = *m_impl;
    if (trunks == m.trunks)
      return;
    const State s = m.snapshot ();
    m.trunks = trunks;
    m.build (s);
  }

  void Caddy::set_drive (float drive) {
    m_impl->kept.drive = drive;
  }

  void Caddy::set_turn (float turn) {
    m_impl->kept.turn = turn;
  }

  void Caddy::set_brake (bool brake) {
    m_impl->kept.brake = brake;
  }

  void Caddy::set_slung (bool slung) {
    State& kept = m_impl->kept;
    if (slung)
      kept.asleep = false;
    kept.slung = slung;
  }

  void Caddy::set_parked (bool parked) {
    State& kept = m_impl->kept;
    if (kept.parked && !parked)
      kept.asleep = false;
    kept.parked = parked;
  }

  void Caddy::update (float dt, float water_level) {
    if (m_impl->kept.asleep || dt <= 0.0f)
      return;
    m_impl->step (dt, water_level);
  }

  Vec3 Caddy::position () const {
    return m_impl->point (Vec3 ());
  }

  Vec3 Caddy::forward () const {
    return m_impl->axis (Vec3 (0, 0, 1));
  }

  Vec3 Caddy::up () const {
    return m_impl->axis (Vec3 (0, 1, 0));
  }

  Vec3 Caddy::left () const {
    return m_impl->axis (Vec3 (1, 0, 0));
  }

  Vec3 Caddy::velocity () const {
    return from_b3 (b3Body_GetLinearVelocity (m_impl->chassis));
  }

  bool Caddy::grounded () const {
    const auto& touching = m_impl->kept.touching;
    return std::any_of (
      touching.begin (), touching.end (), [] (bool wheel) { return wheel; });
  }

  bool Caddy::asleep () const {
    return m_impl->kept.asleep;
  }

  float Caddy::mass () const {
    return caddy_rig::total_mass;
  }

  void Caddy::push (const Vec3& at, const Vec3& impulse) {
    Impl& m = *m_impl;
    m.kept.asleep = false;
    m.kept.still = 0.0f;
    b3Body_ApplyLinearImpulse (m.chassis, to_b3 (impulse), to_b3 (at), true);
  }

  Vec3 Caddy::point (const Vec3& local) const {
    return m_impl->point (local);
  }

  float Caddy::wheel_drop (int wheel) const {
    const Impl& m = *m_impl;
    const Vec3 at = from_b3 (b3Body_GetPosition (m.wheels[wheel]));
    return dot (at - m.point (caddy_rig::axle_of (wheel)),
                m.axis (Vec3 (0, -1, 0)));
  }

  float Caddy::steer () const {
    return b3WheelJoint_GetSteeringAngle (m_impl->joints[0]);
  }

  float Caddy::wheel_spin () const {
    return m_impl->kept.wheel_spin;
  }

  float Caddy::throttle () const {
    return m_impl->kept_throttle;
  }
}
