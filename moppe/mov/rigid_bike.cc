#include <moppe/mov/box3d_vec.hh>
#include <moppe/mov/ground_patch.hh>
#include <moppe/mov/rigid_bike.hh>

#include <algorithm>
#include <cmath>
#include <vector>

#include <box3d/box3d.h>

namespace moppe {
  namespace mov {
    namespace {
      // How far a quantity settling at `rate` goes toward its goal over
      // `step`: 1 - e^(-rate step), in std::exp as the bike always has.
      blend_weight_t settle (damping_t rate, seconds_t step) {
        return (1.0f - std::exp (-scalar_value (rate * step))) *
               blend_weight[one];
      }

      // Moves `value` that weight of the way to `goal`.
      template <class Q>
      void approach (Q& value, Q goal, blend_weight_t weight) {
        value += (goal - value) * weight.numerical_value_in (one);
      }

      // What every bike's world shares; the bike's own shape is its
      // BikeRig.
      namespace bike_rig {
        constexpr int substeps = 4;

        // Box3D mixes two shapes' friction as a geometric mean, so a tire
        // carries the square of the wanted coefficient against unit ground
        // friction.
        constexpr friction_coefficient_t ground_friction =
          1.0f * isq::static_friction_coefficient[one];

        // The water's hold on a wading bike, per kilogram of it.
        constexpr damping_t water_drag = 1.4f / u::s;

        // Ground patch: samples at half the terrain lattice spacing, so every
        // other sample is a lattice node and the facets follow the bilinear
        // surface closely. The patch recentres long before a wheel nears
        // its edge.
        constexpr int patch_samples = 97;
        constexpr float patch_recentre_distance = 24.0f;
        constexpr float trunk_reach = 72.0f;

        constexpr std::uint64_t ground_bit = 1;
        constexpr std::uint64_t trunk_bit = 2;
        constexpr std::uint64_t wheel_bit = 4;
        constexpr std::uint64_t chassis_bit = 8;
      }

      // The chassis frame: x to the rider's left, y up, z forward.
      b3Quat chassis_rotation (const Vec3& forward, const Vec3& up) {
        Vec3 f = normalized (forward);
        Vec3 left = cross (up, f);
        if (length2 (left) < 1e-8f)
          left = cross (Vec3 (0, 1, 0), f);
        if (length2 (left) < 1e-8f)
          left = Vec3 (1, 0, 0);
        normalize (left);
        const Vec3 u = cross (f, left);
        const b3Matrix3 m { to_b3 (left), to_b3 (u), to_b3 (f) };
        return b3NormalizeQuat (b3MakeQuatFromMatrix (&m));
      }

      // Where each axle stands at static sag, in the chassis frame.
      displacement_t front_axle (const BikeRig& rig) {
        return displacement (
          Vec3 (0.0f,
                meters_value (rig.axle_height -
                              (rig.wheel_radius - rig.front_radius ())),
                meters_value (rig.half_wheelbase)));
      }

      displacement_t rear_axle (const BikeRig& rig) {
        return displacement (Vec3 (0.0f,
                                   meters_value (rig.axle_height),
                                   -meters_value (rig.half_wheelbase)));
      }

      // Signed angle of the rotation about `axis` that carries `from` onto
      // `to`, both taken perpendicular to the axis.
      radians_t
      angle_about (const Vec3& axis, const Vec3& from, const Vec3& to) {
        const Vec3 a = from - axis * dot (from, axis);
        const Vec3 b = to - axis * dot (to, axis);
        if (length2 (a) < 1e-10f || length2 (b) < 1e-10f)
          return radians_t {};
        return libm::atan2 (dot (cross (a, b), axis), dot (a, b));
      }

      // The ground's normal, over a road's tread (a bridge's deck) the
      // road's.
      Vec3 normal_at (const map::SurfaceGeometry& surface, float x, float z) {
        return ground_normal (surface, x, z);
      }

      // Where a ballistic reference point next meets the ground at ride
      // height, within a few seconds; the normal there is the plane the
      // airborne bike prepares for.
      bool predict_landing (const map::SurfaceGeometry& surface,
                            meters_t ride_height,
                            const Vec3& position,
                            const Vec3& velocity,
                            Vec3& normal) {
        constexpr seconds_t step = 0.08f * u::s;
        constexpr seconds_t horizon = 3.0f * u::s;
        for (seconds_t t = step; t <= horizon; t += step) {
          Vec3 sample = position + velocity * seconds_value (t);
          sample[1] -= meters_value (0.5f * standard_gravity * t * t);
          if (!(std::isfinite (sample[0]) && std::isfinite (sample[2])))
            return false;
          const meters_t ground =
            surface_elevation_at (surface, sample[0], sample[2]) * u::m +
            ride_height;
          if (static_cast<float> (sample[1]) * u::m > ground)
            continue;
          normal = normal_at (surface, sample[0], sample[2]);
          return true;
        }
        return false;
      }

    }

    struct RigidBike::Impl {
      const map::SurfaceGeometry& surface;
      Params params;
      const BikeRig& rig;
      kilograms_t chassis_mass;
      // Pitch, yaw, and roll about the centre of mass.
      std::array<kilogram_square_metres_t, 3> chassis_inertia {};
      float front_hertz = 0.0f, front_damping = 0.0f;
      float rear_hertz = 0.0f, rear_damping = 0.0f;
      GroundPatch patch;

      const TrunkField* trunks = nullptr;
      b3WorldId world = b3_nullWorldId;
      b3BodyId chassis = b3_nullBodyId;
      b3BodyId front = b3_nullBodyId;
      b3BodyId rear = b3_nullBodyId;
      b3JointId front_joint = b3_nullJointId;
      b3JointId rear_joint = b3_nullJointId;
      b3ShapeId rear_tire = b3_nullShapeId;
      bool front_contact = false, rear_contact = false;
      newtons_t rear_load {};
      radians_t lean {};
      proportion_t skid {};
      meters_t rider_shift {};
      control_signal_t twist {};
      seconds_t airborne {};
      speed_t trunk_hit {}, body_hit {};

      Impl (const map::SurfaceGeometry& surface, Params params)
          : surface (surface), params (params), rig (this->params.rig),
            chassis_mass (std::max (0.3f * params.mass,
                                    params.mass - 2.0f * rig.wheel_mass)),
            patch (
              surface,
              { .ground_friction = bike_rig::ground_friction,
                .obstacle_friction = 0.12f,
                .obstacle_restitution = 0.05f,
                .ground_bit = bike_rig::ground_bit,
                .ground_mask = bike_rig::wheel_bit | bike_rig::chassis_bit,
                .obstacle_bit = bike_rig::trunk_bit,
                .obstacle_mask = bike_rig::chassis_bit,
                .samples = bike_rig::patch_samples,
                .recentre_distance = bike_rig::patch_recentre_distance * u::m,
                .trunk_reach = bike_rig::trunk_reach * u::m }) {
        // A box of frame and rider.
        const kilograms_t k = chassis_mass / 12.0f;
        const Vec3& box = extent_value (rig.inertia_box);
        const square_meters_t w = box[0] * box[0] * u::m2,
                              h = box[1] * box[1] * u::m2,
                              l = box[2] * box[2] * u::m2;
        chassis_inertia = { k * (h + l), k * (w + l), k * (w + h) };

        // The joint springs are soft constraints tuned in hertz against the
        // joint's effective mass, which the light wheel dominates. Convert
        // the rate and damping wanted for the sprung chassis into those terms.
        const meters_t lever = rig.half_wheelbase;
        const kilograms_t effective =
          1.0f / (1.0f / rig.wheel_mass + 1.0f / chassis_mass +
                  lever * lever / chassis_inertia[0]);
        const newtons_t load = 0.5f * chassis_mass * standard_gravity;
        const auto tune = [&] (meters_t sag, float& hertz, float& damping) {
          spring_tuning (load / sag,
                         0.5f * chassis_mass,
                         effective,
                         rig.damping_ratio.numerical_value_in (one),
                         hertz,
                         damping);
        };
        tune (rig.front_sag, front_hertz, front_damping);
        tune (rig.rear_sag, rear_hertz, rear_damping);
      }

      ~Impl () {
        destroy ();
      }

      void destroy () {
        if (B3_IS_NON_NULL (world))
          b3DestroyWorld (world);
        world = b3_nullWorldId;
        chassis = front = rear = b3_nullBodyId;
        front_joint = rear_joint = b3_nullJointId;
        rear_tire = b3_nullShapeId;
        patch.forget ();
      }

      b3BodyId create_body (const RigidBodyState& s,
                            bool fast_rotation = false) {
        b3BodyDef def = b3DefaultBodyDef ();
        def.type = b3_dynamicBody;
        def.position = to_b3 (s.position);
        def.rotation = quat_of (s.rotation);
        def.linearVelocity = to_b3 (s.linear_velocity);
        def.angularVelocity = to_b3 (s.angular_velocity);
        def.enableSleep = false;
        def.allowFastRotation = fast_rotation;
        return b3CreateBody (world, &def);
      }

      // The friction a tyre carries for a wanted coefficient.
      static float tire_material (friction_coefficient_t coefficient) {
        return scalar_value (coefficient * coefficient /
                             bike_rig::ground_friction);
      }

      b3BodyId
      create_wheel (const RigidBodyState& s, meters_t radius, b3ShapeId* tire) {
        const b3BodyId wheel = create_body (s, true);
        b3ShapeDef shape = b3DefaultShapeDef ();
        shape.updateBodyMass = false;
        shape.baseMaterial.friction = tire_material (rig.tire_friction);
        shape.filter.categoryBits = bike_rig::wheel_bit;
        shape.filter.maskBits = bike_rig::ground_bit;
        const b3Sphere sphere { { 0.0f, 0.0f, 0.0f }, meters_value (radius) };
        const b3ShapeId id = b3CreateSphereShape (wheel, &shape, &sphere);
        if (tire)
          *tire = id;
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
        // The frame and rider collide with trunks and, only when the bike
        // bottoms out or falls, with the ground. The wheels never meet a
        // trunk: they are far wider than a tire, and the frame capsule
        // reaches out over them instead.
        b3ShapeDef shape = b3DefaultShapeDef ();
        shape.updateBodyMass = false;
        shape.enableHitEvents = true;
        shape.baseMaterial.friction = 0.4f;
        shape.filter.categoryBits = bike_rig::chassis_bit;
        shape.filter.maskBits = bike_rig::ground_bit | bike_rig::trunk_bit;
        const b3Capsule frame { to_b3 (displacement_value (rig.frame_a)),
                                to_b3 (displacement_value (rig.frame_b)),
                                meters_value (rig.frame_radius) };
        b3CreateCapsuleShape (chassis, &shape, &frame);
        const b3Capsule rider { to_b3 (displacement_value (rig.rider_a)),
                                to_b3 (displacement_value (rig.rider_b)),
                                meters_value (rig.rider_radius) };
        b3CreateCapsuleShape (chassis, &shape, &rider);
        b3Body_SetMassData (chassis,
                            body_mass (chassis_mass, Vec3 (), chassis_inertia));
      }

      b3JointId create_suspension (b3BodyId wheel,
                                   const displacement_t& axle,
                                   meters_t sag,
                                   meters_t travel,
                                   float hertz,
                                   float damping,
                                   bool steers) {
        const b3Quat frame = suspension_rotation ();
        b3WheelJointDef def = b3DefaultWheelJointDef ();
        def.base.bodyIdA = chassis;
        def.base.bodyIdB = wheel;
        // Spring zero is top-out, a sag below the static axle.
        def.base.localFrameA = { to_b3 (displacement_value (axle) -
                                        Vec3 (0.0f, meters_value (sag), 0.0f)),
                                 frame };
        def.base.localFrameB = { { 0.0f, 0.0f, 0.0f }, frame };
        def.base.collideConnected = false;
        def.enableSuspensionSpring = true;
        def.suspensionHertz = hertz;
        def.suspensionDampingRatio = damping;
        def.enableSuspensionLimit = true;
        def.lowerSuspensionLimit = -meters_value (travel);
        def.upperSuspensionLimit = 0.0f;
        def.enableSpinMotor = true;
        def.maxSpinTorque = 0.0f;
        def.spinSpeed = 0.0f;
        if (steers) {
          def.enableSteering = true;
          def.steeringHertz = 11.0f;
          def.steeringDampingRatio = 1.0f;
          def.maxSteeringTorque = 2000.0f;
          def.enableSteeringLimit = true;
          def.lowerSteeringLimit =
            -(radians_value (rig.max_fork_angle) + 0.15f);
          def.upperSteeringLimit = radians_value (rig.max_fork_angle) + 0.15f;
        }
        return b3CreateWheelJoint (world, &def);
      }

      // Rebuilds the whole world from a snapshot in one canonical order, so
      // a restored checkpoint replays exactly as every other restore of it.
      // The solver's warm-start impulses and cached contacts are not part of
      // a snapshot: a restored ride follows the uninterrupted one closely,
      // not bit for bit.
      void build (const RigidBikeState& s) {
        destroy ();
        b3WorldDef def = b3DefaultWorldDef ();
        def.gravity = { 0.0f,
                        -standard_gravity.numerical_value_in (u::m /
                                                              pow<2> (u::s)),
                        0.0f };
        def.enableSleep = false;
        def.workerCount = 1;
        def.hitEventThreshold = 1.5f;
        world = b3CreateWorld (&def);
        patch.build (world, trunks, s.ground_column, s.ground_row);
        create_chassis (s.chassis);
        front = create_wheel (s.front_wheel, rig.front_radius (), nullptr);
        rear = create_wheel (s.rear_wheel, rig.wheel_radius, &rear_tire);
        front_joint = create_suspension (front,
                                         front_axle (rig),
                                         rig.front_sag,
                                         rig.front_travel,
                                         front_hertz,
                                         front_damping,
                                         true);
        rear_joint = create_suspension (rear,
                                        rear_axle (rig),
                                        rig.rear_sag,
                                        rig.rear_travel,
                                        rear_hertz,
                                        rear_damping,
                                        false);
        front_contact = s.front_contact;
        rear_load = s.rear_load;
        lean = s.lean;
        skid = s.skid;
        rider_shift = s.rider_shift;
        twist = s.twist;
        airborne = s.airborne;
        b3Shape_SetFriction (rear_tire, rear_friction ());
        rear_contact = s.rear_contact;
        trunk_hit = body_hit = speed_t {};
      }

      RigidBikeState snapshot () const {
        RigidBikeState s;
        s.chassis = read_body (chassis);
        s.front_wheel = read_body (front);
        s.rear_wheel = read_body (rear);
        s.ground_column = patch.column ();
        s.ground_row = patch.row ();
        s.front_contact = front_contact;
        s.rear_contact = rear_contact;
        s.rear_load = rear_load;
        s.lean = lean;
        s.skid = skid;
        s.rider_shift = rider_shift;
        s.twist = twist;
        s.airborne = airborne;
        return s;
      }

      // The rear tyre's friction as far as it has broken loose.
      float rear_friction () const {
        return tire_material (quantity_cast<isq::static_friction_coefficient> (
          rig.tire_friction + (rig.skid_friction - rig.tire_friction) *
                                skid.numerical_value_in (one)));
      }

      void recentre_ground () {
        patch.recentre (world, trunks, from_b3 (b3Body_GetPosition (chassis)));
      }

      meters_t extension (b3BodyId wheel, const displacement_t& axle) const {
        const b3Quat q = b3Body_GetRotation (chassis);
        const Vec3 c = from_b3 (b3Body_GetPosition (chassis));
        const Vec3 w = from_b3 (b3Body_GetPosition (wheel));
        const Vec3 down = from_b3 (b3RotateVector (q, { 0.0f, -1.0f, 0.0f }));
        const Vec3 rest =
          c + from_b3 (b3RotateVector (q, to_b3 (displacement_value (axle))));
        return dot (w - rest, down) * u::m;
      }

      // The chassis' pitch above the ground plane under it, nose up.
      radians_t pitch () const {
        const b3Quat q = b3Body_GetRotation (chassis);
        const Vec3 p = from_b3 (b3Body_GetPosition (chassis));
        const Vec3 left = from_b3 (b3RotateVector (q, { 1.0f, 0.0f, 0.0f }));
        const Vec3 forward = from_b3 (b3RotateVector (q, { 0.0f, 0.0f, 1.0f }));
        const Vec3 n = normal_at (surface, p[0], p[2]);
        const Vec3 tangent = forward - n * dot (forward, n);
        // A positive turn about the left axis lowers the nose.
        if (length2 (tangent) < 1e-6f)
          return (dot (forward, n) > 0.0f ? 1.5707963f : -1.5707963f) * u::rad;
        return angle_about (left, forward, normalized (tangent));
      }

      bool wheeling () const {
        return rear_contact && !front_contact && pitch () > 0.12f * u::rad;
      }

      // Velocity-level attitude control: nudge one component of the chassis'
      // angular velocity toward a target, by a torque sized by the inertia
      // the assemblage presents about that axis. `rate` is the inverse time
      // constant of the correction.
      //
      // A bare torque on a bike standing on its tires would be met by tire
      // friction below the centre of mass and push the whole bike along. An
      // assist that turns the chassis about a point on the ground therefore
      // also moves the centre of mass as that rotation would, so the contact
      // itself does not move and the tires feel nothing to resist.
      void steer_rate (const Vec3& axis,
                       radians_per_second_t target,
                       kilogram_square_metres_t inertia,
                       damping_t rate,
                       seconds_t step,
                       const Vec3* pivot = nullptr,
                       const Vec3* tangent_normal = nullptr) {
        const Vec3 w = from_b3 (b3Body_GetAngularVelocity (chassis));
        const blend_weight_t gain = settle (rate, step);
        // Radians a second, as Box3D turns its bodies. A radian is a
        // ratio: the change of angular velocity over the step, times the
        // inertia, is a torque.
        const per_second_t change = (target - dot (w, axis) * u::rad / u::s) *
                                    gain.numerical_value_in (one) / u::rad;
        // The correction is asked for every step, so it goes in as the
        // torque (and force) that delivers it over the step: Box3D spreads
        // those evenly across its substeps, where an impulse would land
        // whole in the first and leave the tires to answer it at once.
        apply_torque (chassis, axis, newton_metres_t (inertia * change / step));
        if (!pivot)
          return;
        const Vec3 centre = from_b3 (b3Body_GetPosition (chassis));
        Vec3 swing = cross (axis * per_second_value (change), centre - *pivot);
        // Never let a roll/yaw pivot compensation push the bike vertically
        // into or off a side-slope: keep the assist strictly in the ground's
        // tangent plane (and horizontal when correcting roll on a slope).
        if (tangent_normal)
          swing -= (*tangent_normal) * dot (swing, *tangent_normal);
        b3Body_ApplyForceToCenter (
          chassis,
          to_b3 (swing *
                 (chassis_mass / step).numerical_value_in (u::kg / u::s)),
          true);
      }

      // A parked bike that has settled on its wheels stops simulating, so
      // it cannot creep down a slope on solver drift. Mounting resumes it
      // from rest.
      bool hold_parked (const RigidBikeControls& controls) {
        if (!controls.parked || !(front_contact || rear_contact))
          return false;
        constexpr speed_t still = 0.25f * u::m / u::s;
        constexpr radians_per_second_t still_turning = 0.25f * u::rad / u::s;
        for (const b3BodyId body : { chassis, front, rear })
          if (b3Length (b3Body_GetLinearVelocity (body)) * u::m / u::s >
                still ||
              (B3_ID_EQUALS (body, chassis) &&
               b3Length (b3Body_GetAngularVelocity (body)) * u::rad / u::s >
                 still_turning))
            return false;
        for (const b3BodyId body : { chassis, front, rear }) {
          b3Body_SetLinearVelocity (body, { 0.0f, 0.0f, 0.0f });
          b3Body_SetAngularVelocity (body, { 0.0f, 0.0f, 0.0f });
        }
        trunk_hit = body_hit = speed_t {};
        return true;
      }

      void step (const RigidBikeControls& controls, seconds_t dt) {
        if (hold_parked (controls))
          return;
        recentre_ground ();

        const b3Quat q = b3Body_GetRotation (chassis);
        const Vec3 p = from_b3 (b3Body_GetPosition (chassis));
        const Vec3 v = from_b3 (b3Body_GetLinearVelocity (chassis));
        const Vec3 left = from_b3 (b3RotateVector (q, { 1.0f, 0.0f, 0.0f }));
        const Vec3 up = from_b3 (b3RotateVector (q, { 0.0f, 1.0f, 0.0f }));
        const Vec3 forward = from_b3 (b3RotateVector (q, { 0.0f, 0.0f, 1.0f }));
        const Vec3 world_up (0.0f, 1.0f, 0.0f);
        const Vec3 ground_normal = normal_at (surface, p[0], p[2]);
        const bool grounded = front_contact || rear_contact;
        const speed_t vf = dot (v, forward) * u::m / u::s;
        const speed_t speed = length (v) * u::m / u::s;
        // Backing up faster than a walk turns the handlebar's sense round.
        const speed_t backing = -1.0f * u::m / u::s;
        const kilograms_t total_mass = chassis_mass + 2.0f * rig.wheel_mass;
        const kilograms_t wheel_lever = 2.0f * rig.wheel_mass;
        const kilogram_square_metres_t pitch_inertia =
          chassis_inertia[0] +
          wheel_lever * (rig.half_wheelbase * rig.half_wheelbase +
                         rig.axle_height * rig.axle_height);
        const kilogram_square_metres_t yaw_inertia =
          chassis_inertia[1] +
          wheel_lever * rig.half_wheelbase * rig.half_wheelbase;
        const kilogram_square_metres_t roll_inertia =
          chassis_inertia[2] + wheel_lever * rig.axle_height * rig.axle_height;

        // Steering. The handlebar asks for a yaw rate; the fork takes the
        // angle a rolling bicycle needs for it, and an assist on the chassis
        // makes up what the tires do not deliver, so a hard turn at speed
        // breaks the rear loose into a drift instead of refusing to turn.
        const radians_per_second_t wanted_rate =
          controls.steer * rig.steering_rate /
          (1.0f + scalar_value (abs (vf) / rig.steering_fade_speed));
        // The fork angle that turns at that rate, the tangent of it the
        // wheelbase over the turn's radius.
        const radians_t fork =
          std::min (rig.max_fork_angle,
                    std::atan (radians_value (
                      abs (wanted_rate) * 2.0f * rig.half_wheelbase /
                      std::max (abs (vf), speed_t (3.0f * u::m / u::s)))) *
                      u::rad);
        b3WheelJoint_SetTargetSteeringAngle (
          front_joint,
          std::copysign (radians_value (fork), radians_value (controls.steer)));

        // Yaw follows the handlebar in the direction of travel. A rider
        // nearly at rest can still paddle the bike round.
        const radians_per_second_t wanted_yaw =
          -wanted_rate *
          std::clamp (
            scalar_value (abs (vf) / (4.0f * u::m / u::s)), 0.35f, 1.0f) *
          (vf < backing ? -1.0f : 1.0f);
        // The locked rear brake breaks the tyre loose as it bites, and lets
        // it grip again as gradually, so a tyre sliding sideways does not
        // snatch the bike over the moment the brake comes off.
        // On the back wheel in a wheelie the rider covers the rear brake
        // rather than stamping on it, and the tyre keeps rolling.
        constexpr control_signal_t released {};
        constexpr control_signal_t full = 1.0f * control_signal[one];
        const control_signal_t wheelie =
          std::clamp (controls.wheelie, released, full);
        const bool on_rear =
          rear_contact && !front_contact && wheelie > released;
        const proportion_t skid_goal =
          on_rear ? proportion_t {}
                  : std::clamp (controls.skid, released, full)
                        .numerical_value_in (one) *
                      proportion[one];
        approach (skid,
                  skid_goal,
                  settle (skid_goal > skid ? 12.0f / u::s : 5.0f / u::s, dt));
        b3Shape_SetFriction (rear_tire, rear_friction ());
        // How far the bike slides round rather than rolls: all the way
        // under the locked brake, dying away as it stops, there being
        // nothing left to slide.
        const proportion_t slide =
          grounded
            ? skid * std::clamp (scalar_value ((speed - 0.5f * u::m / u::s) /
                                               (2.5f * u::m / u::s)),
                                 0.0f,
                                 1.0f)
            : proportion_t {};
        if (grounded) {
          // Sliding, the handlebar swings the tail out about the front
          // tyre's contact, at a rate that does not fade with speed.
          const radians_per_second_t skid_yaw = -controls.steer / 1.5707963f *
                                                rig.skid_rate *
                                                (vf < backing ? -1.0f : 1.0f);
          const Vec3 front_contact_point =
            p +
            from_b3 (b3RotateVector (
              q, to_b3 (displacement_value (front_axle (rig))))) -
            ground_normal * meters_value (rig.front_radius ());
          const Vec3 pivot =
            p + (front_contact_point - p) * slide.numerical_value_in (one);
          steer_rate (ground_normal,
                      wanted_yaw + (skid_yaw - wanted_yaw) *
                                     slide.numerical_value_in (one),
                      yaw_inertia,
                      18.0f / u::s,
                      dt,
                      &pivot);
          // Directional tyre carving: sphere colliders have isotropic
          // friction, so without a lateral bite assist fast turns wash
          // sideways. While actively steering and rolling (not skidding),
          // rotate ground-plane velocity smoothly toward the heading while
          // strictly conserving its speed.
          const proportion_t grip_share =
            (1.0f - slide.numerical_value_in (one)) * proportion[one];
          if (!controls.parked && abs (controls.steer) > 0.05f * u::rad &&
              grip_share > 0.05f * proportion[one] && vf > 1.5f * u::m / u::s) {
            const Vec3 projected =
              forward - ground_normal * dot (forward, ground_normal);
            if (length2 (projected) > 1e-6f) {
              const direction_t plane_fwd =
                unit_direction (normalized (projected));
              const direction_t plane_right = unit_direction (
                cross (direction_value (plane_fwd), ground_normal));
              const speed_t v_lat =
                dot (v, direction_value (plane_right)) * u::m / u::s;
              const speed_t v_fwd =
                dot (v, direction_value (plane_fwd)) * u::m / u::s;
              const speed_t plane_speed =
                std::hypot (speed_value (v_fwd), speed_value (v_lat)) * u::m /
                u::s;
              const blend_weight_t bite =
                settle (4.5f * grip_share.numerical_value_in (one) / u::s, dt);
              const speed_t new_lat =
                v_lat * (1.0f - bite.numerical_value_in (one));
              const speed_t new_fwd =
                std::sqrt (std::max (
                  0.0f,
                  speed_value (plane_speed) * speed_value (plane_speed) -
                    speed_value (new_lat) * speed_value (new_lat))) *
                u::m / u::s;
              const b3Vec3 delta = to_b3 (
                direction_value (plane_right) * speed_value (new_lat - v_lat) +
                direction_value (plane_fwd) * speed_value (new_fwd - v_fwd));
              for (const b3BodyId body : { chassis, front, rear })
                b3Body_SetLinearVelocity (
                  body, b3Add (b3Body_GetLinearVelocity (body), delta));
            }
          }
        } else if (controls.hung) {
          // Under the wing the bike swings round to face the way it flies.
          steer_rate (world_up,
                      angle_about (world_up,
                                   forward,
                                   direction_value (controls.hang_forward)) *
                        4.0f / u::s,
                      yaw_inertia,
                      6.0f / u::s,
                      dt);
        } else {
          // Mid-air the handlebar whips the bike about the vertical.
          steer_rate (world_up,
                      -controls.steer * rig.air_steering_rate,
                      yaw_inertia,
                      6.0f / u::s,
                      dt);
        }

        // Roll. On the ground the bike leans so gravity and the turn's
        // centripetal acceleration balance through the tires; in the air it
        // rolls toward the slope it will land on.
        Vec3 level = world_up - forward * dot (world_up, forward);
        if (length2 (level) < 1e-6f)
          level = up;
        normalize (level);
        const Vec3 right = cross (forward, level);
        Vec3 landing_normal = world_up;
        const bool hanging = controls.hung && !grounded;
        // Wait a short beat before engaging mid-air landing attitude so a
        // wheel briefly skipping over rough terrain does not fight the
        // suspension by torquing the chassis to a distant landing slope.
        const bool midair = !grounded && airborne > 0.12f * u::s;
        const bool landing =
          midair && !hanging &&
          predict_landing (surface,
                           rig.ride_height (),
                           p + up * meters_value (rig.reference_height),
                           v,
                           landing_normal);
        // The rider's intended lean is a smoothed state of its own, so a
        // bike skipping over whoops does not snap between its turn lean and
        // the landing plane at every touch.
        radians_t goal {};
        if (grounded) {
          // The lean follows the turn the rider asks for rather than the
          // one the chassis happens to make: leaning on the measured yaw
          // feeds back through the wheels' gyroscopic coupling into a weave.
          // Sliding round, the tyres carry less of the turn, and the rider
          // leans less into it.
          const Vec3 centripetal = cross (
            Vec3 (0.0f, wanted_yaw.numerical_value_in (u::rad / u::s), 0.0f),
            v);
          goal = libm::atan2 (
                   dot (centripetal, right),
                   standard_gravity.numerical_value_in (u::m / pow<2> (u::s))) *
                 (1.0f - 0.6f * slide.numerical_value_in (one));
        } else if (hanging) {
          // The harness banks the bike with the wing.
          const Vec3& hang_up = direction_value (controls.hang_up);
          goal = libm::atan2 (dot (hang_up, right), dot (hang_up, level));
        } else if (landing) {
          goal = libm::atan2 (dot (landing_normal, right),
                              dot (landing_normal, level));
        }
        const radians_t held = std::clamp (goal, -rig.max_lean, rig.max_lean);
        approach (
          lean, held, settle (grounded ? 12.0f / u::s : 3.5f / u::s, dt));
        const Vec3 target_up =
          level * libm::cos (lean) + right * libm::sin (lean);
        // On the ground the bike leans over its tire contacts, or over the
        // rear one alone in a wheelie.
        const Vec3 under =
          on_rear ? from_b3 (b3Body_GetPosition (rear)) -
                      ground_normal * meters_value (rig.wheel_radius)
                  : p - ground_normal * (meters_value (rig.ride_height ()) -
                                         meters_value (rig.reference_height));
        // A wheelie reared up near the vertical has no roll left to hold:
        // about its forward axis it would only spin round.
        const proportion_t rolls =
          (wheelie > released
             ? std::clamp ((dot (up, level) - 0.1f) / 0.3f, 0.0f, 1.0f)
             : 1.0f) *
          proportion[one];
        steer_rate (forward,
                    angle_about (forward, up, target_up) * 9.5f / u::s,
                    roll_inertia,
                    22.0f * rolls.numerical_value_in (one) / u::s,
                    dt,
                    grounded ? &under : nullptr,
                    grounded ? &world_up : nullptr);

        // Pitch. Grounded, the suspension and the engine pitch the bike
        // freely: nothing may lever the chassis while it stands on its tires,
        // since the tires would turn any such torque into a shove. The rider
        // instead keeps the nose down with the throttle and brakes below.
        // Airborne, the rider prepares the chassis for the landing plane,
        // keeping the chosen heading.
        radians_t nose {}; // positive when the nose is above the ground plane
        {
          Vec3 tangent = forward - ground_normal * dot (forward, ground_normal);
          // A positive turn about the left axis lowers the nose.
          if (length2 (tangent) > 1e-6f)
            nose = angle_about (left, forward, normalized (tangent));
        }
        // A bike in a wheelie that leaves the ground for a moment is left
        // to its own pitch at first, and one that has reared past the
        // vertical goes over: righting it would flip it round.
        const bool reared = wheelie > released &&
                            (airborne < 0.3f * u::s || dot (up, level) < 0.1f);
        if (hanging || (midair && speed > 2.0f * u::m / u::s && !reared)) {
          Vec3 aim;
          if (hanging) {
            aim = direction_value (controls.hang_forward);
          } else if (landing) {
            aim = forward - landing_normal * dot (forward, landing_normal);
          } else {
            Vec3 horizontal (forward[0], 0.0f, forward[2]);
            if (length2 (horizontal) < 1e-6f)
              horizontal = Vec3 (v[0], 0.0f, v[2]);
            aim = normalized (horizontal) * std::hypot (v[0], v[2]) +
                  world_up * v[1];
          }
          if (length2 (aim) > 1e-6f)
            steer_rate (left,
                        angle_about (left, forward, normalized (aim)) * 4.0f /
                          u::s,
                        pitch_inertia,
                        8.0f / u::s,
                        dt);
        }

        // Leaning back for a wheelie, the rider's weight goes back over the
        // rear wheel; once the front is up they balance the bike with it,
        // forward as it rises past the pitch they hold it at, back as it
        // falls, as far as their body reaches. Their weight standing back
        // of the chassis' centre is its own weight with a couple added,
        // which only the ground under the tyres can answer: in the air it
        // turns nothing.
        {
          const radians_per_second_t rising =
            -dot (from_b3 (b3Body_GetAngularVelocity (chassis)), left) *
            u::rad / u::s;
          // Where the rider's body balances it, -1 forward to 1 back.
          const control_signal_t balance =
            front_contact ? full
                          : quantity_cast<control_signal> (
                              (rig.wheelie_pitch - nose) / (0.2f * u::rad) -
                              0.5f * u::s * rising / u::rad);
          const meters_t goal =
            wheelie.numerical_value_in (one) * rig.wheelie_lean *
            std::clamp (balance, -0.6f * control_signal[one], full)
              .numerical_value_in (one);
          approach (rider_shift, goal, settle (8.0f / u::s, dt));
          if (grounded) {
            const newtons_t weight = -chassis_mass * standard_gravity;
            b3Body_ApplyTorque (
              chassis,
              to_b3 (cross (forward * -meters_value (rider_shift),
                            world_up * newtons_value (weight))),
              true);
          }
        }

        // Leaning back, the rider rolls the throttle on over two thirds of
        // a second and shuts it quickly, and the power comes in as the
        // square of the twist, so a tap of it is a blip of power and a
        // held throttle a surge.
        const control_signal_t throttle_goal =
          std::max (controls.throttle, released);
        twist = wheelie <= released
                  ? throttle_goal
                  : std::clamp (
                      throttle_goal,
                      twist - quantity_cast<control_signal> (6.0f / u::s * dt),
                      twist + quantity_cast<control_signal> (1.5f / u::s * dt));
        const control_signal_t throttle =
          controls.throttle <= released ? controls.throttle
          : wheelie > released ? quantity_cast<control_signal> (twist * twist)
                               : twist;
        constexpr control_signal_t idle = 0.02f * control_signal[one];

        // Engine and brakes through the wheel joints' spin motors.
        // The motors' speeds are radians a second, as Box3D spins them.
        const meters_t r = rig.wheel_radius;
        radians_per_second_t rear_speed {};
        newton_metres_t rear_torque = rig.engine_brake_torque;
        newton_metres_t front_torque {};
        // Leaning back for a wheelie, the rider lets the front come up.
        constexpr radians_t backing_off = 0.15f * u::rad;
        constexpr proportion_t none {};
        constexpr proportion_t all = 1.0f * proportion[one];
        const proportion_t wheelie_cut =
          std::max (wheelie.numerical_value_in (one) * proportion[one],
                    std::clamp (quantity_cast<proportion> (
                                  (rig.wheelie_limit - nose) / backing_off),
                                none,
                                all));
        const proportion_t stoppie_cut = std::clamp (
          quantity_cast<proportion> ((rig.stoppie_limit + nose) / backing_off),
          none,
          all);
        if (throttle > idle) {
          // Power is limited against road speed, as on the classic bike.
          // Traction control keeps the drive inside what the tire can put
          // down on its present load, so the tire grips rather than slides,
          // and below the pull that would lift the front: the rider keeps
          // the bike out of a wheelie with the throttle, since anything that
          // levered the nose back down would also shove the bike along. In
          // the air the rider can only rev the wheel up to road speed.
          const newtons_t force = std::min (
            params.max_drive_force,
            newtons_t (params.power /
                       std::max (abs (vf), speed_t (0.5f * u::m / u::s))));
          if (rear_contact) {
            const newtons_t grip =
              newtons_t (rig.traction_share * rig.tire_friction * rear_load);
            const newtons_t lift = newtons_t (
              rig.wheelie_share * total_mass * standard_gravity *
              rig.half_wheelbase / (rig.ride_height () - rig.reference_height));
            // Leaning back, the rider lets the drive lift the front, but
            // its pull about the rear tyre may rear the bike up by no more
            // than wheelie_torque over what the weight holds it down with,
            // so a throttle held open rears it steadily past the balance,
            // to fall over backwards, rather than throwing it into the air.
            newtons_t rearing = lift;
            if (wheelie > released) {
              Vec3 heading = forward - up;
              heading[1] = 0.0f;
              const Vec3 arm = p - forward * meters_value (rider_shift) -
                               from_b3 (b3Body_GetPosition (rear)) +
                               ground_normal * meters_value (r);
              const meters_t height =
                std::max (dot (arm, ground_normal) * u::m, 0.2f * u::m);
              const meters_t reach =
                (length2 (heading) > 1e-6f ? dot (arm, normalized (heading))
                                           : 0.0f) *
                u::m;
              rearing =
                std::max (newtons_t {},
                          newtons_t ((rig.wheelie_torque +
                                      total_mass * standard_gravity * reach) /
                                     height));
            }
            rear_speed =
              (std::max (vf, speed_t {}) + 20.0f * u::m / u::s) / r * u::rad;
            rear_torque = newton_metres_t (
              wheelie_cut *
              std::min ({ newtons_t (throttle * force), grip, rearing }) * r);
          } else {
            rear_speed =
              (std::max (vf, speed_t {}) + 1.0f * u::m / u::s) / r * u::rad;
            rear_torque = rig.free_rev_torque;
          }
        } else if (throttle < -idle) {
          const control_signal_t effort = -throttle;
          if (vf > 1.5f * u::m / u::s) {
            rear_torque = newton_metres_t (effort * rig.rear_brake_force * r);
            front_torque = newton_metres_t (stoppie_cut * effort *
                                            rig.front_brake_force * r);
          } else {
            rear_speed = -rig.reverse_speed / r * u::rad;
            rear_torque = newton_metres_t (effort * rig.reverse_force_share *
                                           params.max_drive_force * r);
          }
        }
        if (on_rear && controls.skid > idle) {
          // The covered rear brake slows the wheel, and the bike with it,
          // which brings the nose down.
          rear_speed = {};
          rear_torque =
            newton_metres_t (controls.skid * 0.5f * rig.rear_brake_force * r);
        } else if (controls.skid > idle) {
          // The rear brake stamped on locks the wheel to the frame,
          // whatever the throttle asks.
          rear_speed = {};
          rear_torque =
            std::max (newton_metres_t (rear_torque * (full - controls.skid)),
                      newton_metres_t (controls.skid * rig.skid_torque));
        }
        if (controls.parked) {
          // The parking lock: both wheels held still against the chassis.
          rear_speed = {};
          rear_torque = front_torque = rig.parking_torque;
        }
        b3WheelJoint_SetSpinMotorSpeed (
          rear_joint, rear_speed.numerical_value_in (u::rad / u::s));
        b3WheelJoint_SetMaxSpinTorque (rear_joint,
                                       newton_metres_value (rear_torque));
        b3WheelJoint_SetSpinMotorSpeed (front_joint, 0.0f);
        b3WheelJoint_SetMaxSpinTorque (front_joint,
                                       newton_metres_value (front_torque));

        // Drag, wading, and the jump jets act on the whole bike's mass.
        // A bike under the wing has its drag counted in the wing's pull.
        const velocity_t moving = moppe::velocity (v);
        force_t force =
          quantity_cast<isq::force> (controls.boost_acceleration * total_mass);
        if (!hanging)
          force -= quantity_cast<isq::force> (
            moving * ((rig.rolling_drag + rig.air_drag * speed) * total_mass));
        force -= quantity_cast<isq::force> (
          moving * (bike_rig::water_drag * controls.wading * total_mass));
        apply_force_to_centre (chassis, force);

        // A hop springs the whole bike up off its wheels, the nose lifting
        // first.
        if (controls.hop > speed_t {} && grounded) {
          const b3Vec3 kick = to_b3 (world_up * speed_value (controls.hop));
          for (const b3BodyId body : { chassis, front, rear })
            b3Body_SetLinearVelocity (
              body, b3Add (b3Body_GetLinearVelocity (body), kick));
          b3Body_SetAngularVelocity (
            chassis,
            b3Add (b3Body_GetAngularVelocity (chassis),
                   to_b3 (left * (-0.6f * speed_value (controls.hop)))));
        }

        const speed_t pre_vy = v[1] * u::m / u::s;
        b3World_Step (world, seconds_value (dt), bike_rig::substeps);

        newtons_t front_load {};
        front_contact = wheel_touches_ground (front, dt, front_load);
        rear_contact = wheel_touches_ground (rear, dt, rear_load);
        airborne = front_contact || rear_contact ? seconds_t {} : airborne + dt;
        trunk_hit = body_hit = speed_t {};
        const b3ContactEvents events = b3World_GetContactEvents (world);
        for (int i = 0; i < events.hitCount; ++i) {
          const b3ContactHitEvent& hit = events.hitEvents[i];
          const b3BodyId a = b3Shape_GetBody (hit.shapeIdA);
          const b3BodyId b = b3Shape_GetBody (hit.shapeIdB);
          const auto is = [&] (b3BodyId body) {
            return B3_IS_NON_NULL (body) &&
                   (B3_ID_EQUALS (a, body) || B3_ID_EQUALS (b, body));
          };
          // Trunks and railings are obstacles, which a bike glances off.
          if (is (patch.obstacles ()))
            trunk_hit = std::max (trunk_hit, hit.approachSpeed * u::m / u::s);
          else if (is (patch.ground ()))
            body_hit = std::max (body_hit, hit.approachSpeed * u::m / u::s);
        }
        // An off-centre hit on a vertical trunk must produce a grounded
        // thud or glance-off, never an airborne front-flip somersault.
        if (trunk_hit > speed_t {}) {
          const speed_t max_vy =
            std::max (pre_vy, speed_t {}) + 0.30f * u::m / u::s;
          for (const b3BodyId body : { chassis, front, rear }) {
            velocity_t body_v =
              moppe::velocity (from_b3 (b3Body_GetLinearVelocity (body)));
            if (static_cast<float> (velocity_value (body_v)[1]) * u::m / u::s >
                max_vy) {
              velocity_value (body_v)[1] = speed_value (max_vy);
              b3Body_SetLinearVelocity (body, to_b3 (velocity_value (body_v)));
            }
          }
          const Vec3 raw_w = from_b3 (b3Body_GetAngularVelocity (chassis));
          const radians_per_second_t yaw_w =
            std::clamp (dot (raw_w, world_up) * u::rad / u::s,
                        -3.0f * u::rad / u::s,
                        3.0f * u::rad / u::s);
          const Vec3 tilt_raw =
            (raw_w - world_up * dot (raw_w, world_up)) * 0.15f;
          const radians_per_second_t tilt_mag =
            length (tilt_raw) * u::rad / u::s;
          constexpr radians_per_second_t max_tilt = 1.2f * u::rad / u::s;
          const scale_factor_t tilt_scale =
            (tilt_mag > max_tilt
               ? (max_tilt / tilt_mag).numerical_value_in (one)
               : 1.0f) *
            scale_factor[one];
          b3Body_SetAngularVelocity (
            chassis,
            to_b3 (world_up * yaw_w.numerical_value_in (u::rad / u::s) +
                   tilt_raw * tilt_scale.numerical_value_in (one)));
        } else if (wheelie <= released) {
          // Outside an intentional wheelie, cap chaotic pitch/roll spin so
          // a stray rock edge cannot tumble the bike like a bowling pin.
          const Vec3 raw_w = from_b3 (b3Body_GetAngularVelocity (chassis));
          const radians_per_second_t yaw_w =
            dot (raw_w, world_up) * u::rad / u::s;
          const Vec3 tilt_raw =
            raw_w - world_up * yaw_w.numerical_value_in (u::rad / u::s);
          const radians_per_second_t tilt_mag =
            length (tilt_raw) * u::rad / u::s;
          constexpr radians_per_second_t max_tumble = 4.5f * u::rad / u::s;
          if (tilt_mag > max_tumble) {
            const scale_factor_t scale =
              (max_tumble / tilt_mag).numerical_value_in (one) *
              scale_factor[one];
            b3Body_SetAngularVelocity (
              chassis,
              to_b3 (world_up * yaw_w.numerical_value_in (u::rad / u::s) +
                     tilt_raw * scale.numerical_value_in (one)));
          }
        }
      }
    };

    RigidBike::RigidBike (const map::SurfaceGeometry& surface,
                          Params params,
                          const Vec3& reference,
                          const Vec3& forward)
        : m_impl (std::make_unique<Impl> (surface, params)) {
      place (reference, forward, Vec3 (0, 1, 0), Vec3 ());
    }

    RigidBike::~RigidBike () = default;

    void RigidBike::place (const Vec3& reference,
                           const Vec3& forward,
                           const Vec3& up,
                           const Vec3& velocity) {
      Impl& m = *m_impl;
      const b3Quat q = chassis_rotation (forward, up);
      const std::array<float, 4> rotation = array_of (q);
      const Vec3 centre =
        reference -
        rotated (rotation,
                 Vec3 (0.0f, meters_value (m.rig.reference_height), 0.0f));
      const Vec3 f = rotated (rotation, Vec3 (0, 0, 1));
      const Vec3 axle = rotated (rotation, Vec3 (1, 0, 0));
      const Vec3 roll =
        axle * (dot (velocity, f) / meters_value (m.rig.wheel_radius));

      RigidBikeState s;
      s.chassis = { centre, rotation, velocity, Vec3 () };
      s.front_wheel = {
        centre + rotated (rotation, displacement_value (front_axle (m.rig))),
        rotation,
        velocity,
        roll * (meters_value (m.rig.wheel_radius) /
                meters_value (m.rig.front_radius ()))
      };
      s.rear_wheel = {
        centre + rotated (rotation, displacement_value (rear_axle (m.rig))),
        rotation,
        velocity,
        roll
      };
      m.patch.origin_for (centre, s.ground_column, s.ground_row);

      // A bike already standing in a world is moved in it; only a patch
      // left far behind costs a rebuild.
      if (B3_IS_NULL (m.world)) {
        m.build (s);
        return;
      }
      const auto move = [] (b3BodyId body, const RigidBodyState& b) {
        b3Body_SetTransform (body, to_b3 (b.position), quat_of (b.rotation));
        b3Body_SetLinearVelocity (body, to_b3 (b.linear_velocity));
        b3Body_SetAngularVelocity (body, to_b3 (b.angular_velocity));
      };
      move (m.chassis, s.chassis);
      move (m.front, s.front_wheel);
      move (m.rear, s.rear_wheel);
      m.front_contact = m.rear_contact = false;
      m.recentre_ground ();
    }

    void RigidBike::set_trunks (const TrunkField* trunks) {
      Impl& m = *m_impl;
      if (trunks == m.trunks)
        return;
      const RigidBikeState s = m.snapshot ();
      m.trunks = trunks;
      m.build (s);
    }

    void RigidBike::step (const RigidBikeControls& controls, seconds_t dt) {
      m_impl->step (controls, dt);
    }

    RigidBikeState RigidBike::state () const {
      return m_impl->snapshot ();
    }

    void RigidBike::restore (const RigidBikeState& state) {
      m_impl->build (state);
    }

    void RigidBike::shift (plan_displacement_t delta) {
      Impl& m = *m_impl;
      RigidBikeState s = m.snapshot ();
      const Vec3 by = lift (plan_value (delta));
      s.chassis.position = s.chassis.position + by;
      s.front_wheel.position = s.front_wheel.position + by;
      s.rear_wheel.position = s.rear_wheel.position + by;
      m.patch.origin_for (s.chassis.position, s.ground_column, s.ground_row);
      m.build (s);
    }

    const BikeRig& RigidBike::rig () const {
      return m_impl->rig;
    }

    Vec3 RigidBike::reference_of (const RigidBikeState& state) const {
      return state.chassis.position +
             rotated (
               state.chassis.rotation,
               Vec3 (0.0f, meters_value (m_impl->rig.reference_height), 0.0f));
    }

    Vec3 RigidBike::forward_of (const RigidBikeState& state) {
      return rotated (state.chassis.rotation, Vec3 (0, 0, 1));
    }

    Vec3 RigidBike::up_of (const RigidBikeState& state) {
      return rotated (state.chassis.rotation, Vec3 (0, 1, 0));
    }

    RigidBikeState RigidBike::repose (const RigidBikeState& state,
                                      const Vec3& reference,
                                      const Vec3& heading,
                                      const Vec3& velocity) const {
      const Vec3 pivot = reference_of (state);
      const Vec3 forward = forward_of (state);
      const radians_t turn = libm::atan2 (forward[0], forward[2]);
      const radians_t wanted =
        length2 (Vec3 (heading[0], 0.0f, heading[2])) > 1e-8f
          ? libm::atan2 (heading[0], heading[2])
          : turn;
      const b3Quat spin = b3MakeQuatFromAxisAngle (
        { 0.0f, 1.0f, 0.0f }, radians_value (wanted - turn));
      const Vec3 carried = state.chassis.linear_velocity;
      const auto move = [&] (const RigidBodyState& b) {
        RigidBodyState out;
        out.position =
          reference +
          from_b3 (b3RotateVector (spin, to_b3 (b.position - pivot)));
        out.rotation =
          array_of (b3NormalizeQuat (b3MulQuat (spin, quat_of (b.rotation))));
        out.linear_velocity =
          velocity +
          from_b3 (b3RotateVector (spin, to_b3 (b.linear_velocity - carried)));
        out.angular_velocity =
          from_b3 (b3RotateVector (spin, to_b3 (b.angular_velocity)));
        return out;
      };
      RigidBikeState out = state;
      out.chassis = move (state.chassis);
      out.front_wheel = move (state.front_wheel);
      out.rear_wheel = move (state.rear_wheel);
      return out;
    }

    Vec3 RigidBike::reference_position () const {
      const Impl& m = *m_impl;
      return from_b3 (b3Body_GetPosition (m.chassis)) +
             from_b3 (b3RotateVector (
               b3Body_GetRotation (m.chassis),
               { 0.0f, meters_value (m.rig.reference_height), 0.0f }));
    }

    Vec3 RigidBike::velocity () const {
      return from_b3 (b3Body_GetLinearVelocity (m_impl->chassis));
    }

    Vec3 RigidBike::forward () const {
      return from_b3 (b3RotateVector (b3Body_GetRotation (m_impl->chassis),
                                      { 0.0f, 0.0f, 1.0f }));
    }

    Vec3 RigidBike::up () const {
      return from_b3 (b3RotateVector (b3Body_GetRotation (m_impl->chassis),
                                      { 0.0f, 1.0f, 0.0f }));
    }

    bool RigidBike::front_contact () const {
      return m_impl->front_contact;
    }

    bool RigidBike::rear_contact () const {
      return m_impl->rear_contact;
    }

    meters_t RigidBike::front_extension () const {
      return m_impl->extension (m_impl->front, front_axle (m_impl->rig));
    }

    meters_t RigidBike::rear_extension () const {
      return m_impl->extension (m_impl->rear, rear_axle (m_impl->rig));
    }

    radians_t RigidBike::steer_angle () const {
      return b3WheelJoint_GetSteeringAngle (m_impl->front_joint) * u::rad;
    }

    radians_per_second_t RigidBike::wheel_spin_rate () const {
      return b3WheelJoint_GetSpinSpeed (m_impl->rear_joint) * u::rad / u::s;
    }

    proportion_t RigidBike::skid () const {
      return m_impl->skid;
    }

    control_signal_t RigidBike::rider_lean () const {
      return quantity_cast<control_signal> (m_impl->rider_shift /
                                            m_impl->rig.wheelie_lean);
    }

    radians_t RigidBike::pitch () const {
      return m_impl->pitch ();
    }

    bool RigidBike::wheeling () const {
      return m_impl->wheeling ();
    }

    speed_t RigidBike::trunk_hit () const {
      return m_impl->trunk_hit;
    }

    speed_t RigidBike::body_hit () const {
      return m_impl->body_hit;
    }

    void RigidBike::push (const Vec3& at, const impulse_t& impulse) {
      b3Body_ApplyLinearImpulse (
        m_impl->chassis, to_b3 (impulse_value (impulse)), to_b3 (at), true);
    }
  }
}
