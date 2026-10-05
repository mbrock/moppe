#include <moppe/mov/box3d_vec.hh>
#include <moppe/mov/rigid_bike.hh>

#include <algorithm>
#include <cmath>
#include <vector>

#include <box3d/box3d.h>

namespace moppe {
  namespace mov {
    namespace {
      // The assemblage matches the drawn bike, which is modelled at one and a
      // half times life size: wheels of 0.64 m radius 2.25 m apart, axles
      // 0.325 m below the reference point the renderer draws from. The
      // chassis origin is its centre of mass, a little below that point.
      namespace bike_rig {
        constexpr int substeps = 4;
        constexpr float gravity = 9.82f;

        constexpr float wheel_radius = 0.64f;
        constexpr float wheel_mass = 12.0f;
        // Isotropic, so a steered or leaned wheel carries no gyroscopic
        // self-torque of its own; spin-up is still quick, as on a light rim.
        constexpr float wheel_inertia = 0.9f;
        constexpr float half_wheelbase = 1.125f;
        constexpr float reference_height = 0.15f;
        constexpr float axle_height = -0.175f;

        // Dirt-bike travel with about a third of it used at rest. The springs
        // are linear from top-out, so sag sets the rate; the damping is a
        // fraction of critical for the sprung mass, light enough to bounce.
        constexpr float front_travel = 0.25f;
        constexpr float rear_travel = 0.30f;
        constexpr float front_sag = 0.08f;
        constexpr float rear_sag = 0.10f;
        constexpr float sprung_damping_ratio = 0.35f;

        // Knobbies on loose ground, generous for an arcade bike. Box3D mixes
        // two shapes' friction as a geometric mean, so the tire carries the
        // square of the wanted coefficient against unit ground friction.
        constexpr float tire_friction = 1.6f;
        constexpr float ground_friction = 1.0f;

        // The classic bike's steering: full lock swings the heading at 1.6
        // rad/s per radian of input, fading with speed, and 0.9 in the air.
        constexpr float steering_rate = 1.6f;
        constexpr float steering_fade_speed = 25.0f;
        constexpr float air_steering_rate = 0.9f;
        constexpr float max_fork_angle = 0.6f;

        constexpr float max_lean = 0.7f;
        // Pitch at which the rider has fully closed the throttle or released
        // the front brake; they start backing off 0.15 rad earlier.
        constexpr float wheelie_limit = 0.35f;
        constexpr float stoppie_limit = 0.3f;

        // Engine and brakes.
        constexpr float reverse_speed = 10.0f;
        constexpr float reverse_force_share = 0.6f;
        constexpr float front_brake_force = 2000.0f;
        constexpr float rear_brake_force = 1400.0f;
        constexpr float engine_brake_torque = 90.0f;
        constexpr float parking_torque = 4000.0f;
        constexpr float traction_share = 0.9f;
        constexpr float wheelie_share = 0.8f;
        constexpr float free_rev_torque = 60.0f;

        // Linear rolling drag plus quadratic air drag, as the classic bike.
        constexpr float rolling_drag = 0.05f;
        constexpr float air_drag = 0.0035f;
        constexpr float water_drag = 1.4f;

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

      constexpr float ride_height_error =
        bike_rig::wheel_radius - bike_rig::axle_height +
        bike_rig::reference_height - RigidBike::ride_height;
      static_assert (ride_height_error * ride_height_error < 1e-10f);

      b3Quat quat_of (const std::array<float, 4>& q) {
        return { { q[0], q[1], q[2] }, q[3] };
      }

      std::array<float, 4> array_of (const b3Quat& q) {
        return { q.v.x, q.v.y, q.v.z, q.s };
      }

      Vec3 rotated (const std::array<float, 4>& q, const Vec3& v) {
        return from_b3 (b3RotateVector (quat_of (q), to_b3 (v)));
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

      // A wheel joint slides along its frame's x axis and spins about z:
      // here x points down the chassis and z along the axle to the left.
      b3Quat suspension_rotation () {
        const b3Matrix3 m { { 0.0f, -1.0f, 0.0f },
                            { 0.0f, 0.0f, -1.0f },
                            { 1.0f, 0.0f, 0.0f } };
        return b3NormalizeQuat (b3MakeQuatFromMatrix (&m));
      }

      Vec3 front_axle () {
        return Vec3 (0.0f, bike_rig::axle_height, bike_rig::half_wheelbase);
      }

      Vec3 rear_axle () {
        return Vec3 (0.0f, bike_rig::axle_height, -bike_rig::half_wheelbase);
      }

      // Signed angle of the rotation about `axis` that carries `from` onto
      // `to`, both taken perpendicular to the axis.
      float angle_about (const Vec3& axis, const Vec3& from, const Vec3& to) {
        const Vec3 a = from - axis * dot (from, axis);
        const Vec3 b = to - axis * dot (to, axis);
        if (length2 (a) < 1e-10f || length2 (b) < 1e-10f)
          return 0.0f;
        return std::atan2 (dot (cross (a, b), axis), dot (a, b));
      }

      float
      elevation_at (const map::SurfaceGeometry& surface, float x, float z) {
        return terrain::surface_elevation_value (
          spatial::sample<terrain::surface_elevation> (
            surface, moppe::position (Vec3 (x, 0.0f, z))));
      }

      Vec3 normal_at (const map::SurfaceGeometry& surface, float x, float z) {
        Vec3 n = spatial::sample<terrain::terrain_normal> (
                   surface, moppe::position (Vec3 (x, 0.0f, z)))
                   .numerical_value_in (mp_units::one);
        if (length2 (n) < 1e-8f)
          return Vec3 (0, 1, 0);
        return normalized (n);
      }

      // Where a ballistic reference point next meets the ground at ride
      // height, within a few seconds; the normal there is the plane the
      // airborne bike prepares for.
      bool predict_landing (const map::SurfaceGeometry& surface,
                            const Vec3& position,
                            const Vec3& velocity,
                            Vec3& normal) {
        constexpr float step = 0.08f;
        constexpr float horizon = 3.0f;
        for (float t = step; t <= horizon; t += step) {
          Vec3 sample = position + velocity * t;
          sample[1] -= 0.5f * bike_rig::gravity * t * t;
          if (!(std::isfinite (sample[0]) && std::isfinite (sample[2])))
            return false;
          const float ground = elevation_at (surface, sample[0], sample[2]) +
                               RigidBike::ride_height;
          if (sample[1] > ground)
            continue;
          normal = normal_at (surface, sample[0], sample[2]);
          return true;
        }
        return false;
      }

      // Whether a wheel touched the ground in the last step, and the mean
      // normal force the ground pressed it with.
      bool touches_ground (b3BodyId wheel, float dt, float& load) {
        b3ContactData contacts[8];
        const int count = b3Body_GetContactData (wheel, contacts, 8);
        bool touching = false;
        float impulse = 0.0f;
        for (int i = 0; i < count; ++i)
          for (int m = 0; m < contacts[i].manifoldCount; ++m) {
            const b3Manifold& manifold = contacts[i].manifolds[m];
            for (int p = 0; p < manifold.pointCount; ++p) {
              const b3ManifoldPoint& point = manifold.points[p];
              impulse += point.totalNormalImpulse;
              touching = touching || point.totalNormalImpulse > 0.0f ||
                         point.separation < 0.02f;
            }
          }
        load = impulse / dt;
        return touching;
      }
    }

    struct RigidBike::Impl {
      const map::SurfaceGeometry& surface;
      Params params;
      float chassis_mass;
      Vec3 chassis_inertia; // pitch, yaw, roll about the centre of mass
      float front_hertz = 0.0f, front_damping = 0.0f;
      float rear_hertz = 0.0f, rear_damping = 0.0f;
      float patch_dx, patch_dz;

      const TrunkField* trunks = nullptr;
      b3WorldId world = b3_nullWorldId;
      b3BodyId ground = b3_nullBodyId;
      b3BodyId obstacles = b3_nullBodyId;
      b3BodyId chassis = b3_nullBodyId;
      b3BodyId front = b3_nullBodyId;
      b3BodyId rear = b3_nullBodyId;
      b3JointId front_joint = b3_nullJointId;
      b3JointId rear_joint = b3_nullJointId;
      b3HeightFieldData* height_field = nullptr;
      int ground_column = 0, ground_row = 0;
      bool front_contact = false, rear_contact = false;
      float rear_load = 0.0f;
      float lean = 0.0f;
      float trunk_hit = 0.0f, body_hit = 0.0f;

      Impl (const map::SurfaceGeometry& surface, Params params)
          : surface (surface), params (params),
            chassis_mass (
              std::max (40.0f, params.mass - 2.0f * bike_rig::wheel_mass)),
            patch_dx (0.5f *
                      surface.domain ().spacing_x ().numerical_value_in (u::m)),
            patch_dz (
              0.5f * surface.domain ().spacing_z ().numerical_value_in (u::m)) {
        // A box of frame and rider about 0.5 m wide, 1.2 m tall and 2 m long.
        const float k = chassis_mass / 12.0f;
        chassis_inertia =
          Vec3 (k * (1.44f + 4.0f), k * (0.25f + 4.0f), k * (0.25f + 1.44f));

        // The joint springs are soft constraints tuned in hertz against the
        // joint's effective mass, which the light wheel dominates. Convert
        // the rate and damping wanted for the sprung chassis into those terms.
        const float lever = bike_rig::half_wheelbase;
        const float effective =
          1.0f / (1.0f / bike_rig::wheel_mass + 1.0f / chassis_mass +
                  lever * lever / chassis_inertia[0]);
        const float load = 0.5f * chassis_mass * bike_rig::gravity;
        const auto tune = [&] (float sag, float& hertz, float& damping) {
          const float stiffness = load / sag;
          const float sprung = 0.5f * chassis_mass;
          const float c = 2.0f * bike_rig::sprung_damping_ratio *
                          std::sqrt (stiffness * sprung);
          hertz = std::sqrt (stiffness / effective) / (2.0f * 3.14159265f);
          damping = c / (2.0f * std::sqrt (stiffness * effective));
        };
        tune (bike_rig::front_sag, front_hertz, front_damping);
        tune (bike_rig::rear_sag, rear_hertz, rear_damping);
      }

      ~Impl () {
        destroy ();
      }

      void destroy () {
        if (B3_IS_NON_NULL (world))
          b3DestroyWorld (world);
        world = b3_nullWorldId;
        ground = obstacles = chassis = front = rear = b3_nullBodyId;
        front_joint = rear_joint = b3_nullJointId;
        if (height_field)
          b3DestroyHeightField (height_field);
        height_field = nullptr;
      }

      void patch_origin_for (const Vec3& p, int& column, int& row) const {
        constexpr int half = (bike_rig::patch_samples - 1) / 2;
        column = static_cast<int> (std::floor (p[0] / patch_dx)) - half;
        row = static_cast<int> (std::floor (p[2] / patch_dz)) - half;
      }

      Vec3 patch_centre () const {
        constexpr float half = 0.5f * (bike_rig::patch_samples - 1);
        return Vec3 ((ground_column + half) * patch_dx,
                     0.0f,
                     (ground_row + half) * patch_dz);
      }

      void destroy_ground () {
        if (B3_IS_NON_NULL (obstacles))
          b3DestroyBody (obstacles);
        if (B3_IS_NON_NULL (ground))
          b3DestroyBody (ground);
        obstacles = ground = b3_nullBodyId;
        if (height_field)
          b3DestroyHeightField (height_field);
        height_field = nullptr;
      }

      void build_ground (int column, int row) {
        constexpr int n = bike_rig::patch_samples;
        ground_column = column;
        ground_row = row;
        std::vector<float> heights (n * n);
        float lowest = 1e30f, highest = -1e30f;
        for (int j = 0; j < n; ++j)
          for (int i = 0; i < n; ++i) {
            const float h =
              elevation_at (surface,
                            static_cast<float> (column + i) * patch_dx,
                            static_cast<float> (row + j) * patch_dz);
            heights[j * n + i] = h;
            lowest = std::min (lowest, h);
            highest = std::max (highest, h);
          }
        // Quantization spans only this patch, so the height steps stay at a
        // millimetre or so even on a mountainside.
        b3HeightFieldDef field {};
        field.heights = heights.data ();
        field.scale = { patch_dx, 1.0f, patch_dz };
        field.countX = n;
        field.countZ = n;
        field.globalMinimumHeight = lowest - 1.0f;
        field.globalMaximumHeight = highest + 1.0f;
        height_field = b3CreateHeightField (&field);

        b3BodyDef body = b3DefaultBodyDef ();
        body.type = b3_staticBody;
        body.position = { column * patch_dx, 0.0f, row * patch_dz };
        ground = b3CreateBody (world, &body);
        b3ShapeDef shape = b3DefaultShapeDef ();
        shape.baseMaterial.friction = bike_rig::ground_friction;
        shape.filter.categoryBits = bike_rig::ground_bit;
        shape.filter.maskBits = bike_rig::wheel_bit | bike_rig::chassis_bit;
        b3CreateHeightFieldShape (ground, &shape, height_field);

        if (!trunks)
          return;
        const std::vector<Trunk> nearby =
          trunks->gather (patch_centre (), bike_rig::trunk_reach);
        if (nearby.empty ())
          return;
        b3BodyDef obstacle_body = b3DefaultBodyDef ();
        obstacle_body.type = b3_staticBody;
        obstacles = b3CreateBody (world, &obstacle_body);
        b3ShapeDef obstacle = b3DefaultShapeDef ();
        obstacle.baseMaterial.friction = 0.3f;
        obstacle.baseMaterial.restitution = 0.3f;
        obstacle.filter.categoryBits = bike_rig::trunk_bit;
        obstacle.filter.maskBits = bike_rig::chassis_bit;
        for (const Trunk& trunk : nearby) {
          b3Capsule capsule;
          capsule.center1 = to_b3 (trunk.root + trunk.axis * trunk.radius);
          capsule.center2 = to_b3 (trunk.root + trunk.axis * trunk.height);
          capsule.radius = trunk.radius;
          b3CreateCapsuleShape (obstacles, &obstacle, &capsule);
        }
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
        shape.baseMaterial.friction = bike_rig::tire_friction *
                                      bike_rig::tire_friction /
                                      bike_rig::ground_friction;
        shape.filter.categoryBits = bike_rig::wheel_bit;
        shape.filter.maskBits = bike_rig::ground_bit;
        const b3Sphere sphere { { 0.0f, 0.0f, 0.0f }, bike_rig::wheel_radius };
        b3CreateSphereShape (wheel, &shape, &sphere);
        b3MassData mass {};
        mass.mass = bike_rig::wheel_mass;
        mass.inertia.cx = { bike_rig::wheel_inertia, 0.0f, 0.0f };
        mass.inertia.cy = { 0.0f, bike_rig::wheel_inertia, 0.0f };
        mass.inertia.cz = { 0.0f, 0.0f, bike_rig::wheel_inertia };
        b3Body_SetMassData (wheel, mass);
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
        const b3Capsule frame { { 0.0f, -0.2f, -1.35f },
                                { 0.0f, -0.2f, 1.35f },
                                0.35f };
        b3CreateCapsuleShape (chassis, &shape, &frame);
        const b3Capsule rider { { 0.0f, 0.05f, -0.35f },
                                { 0.0f, 0.95f, -0.05f },
                                0.3f };
        b3CreateCapsuleShape (chassis, &shape, &rider);
        b3MassData mass {};
        mass.mass = chassis_mass;
        mass.inertia.cx = { chassis_inertia[0], 0.0f, 0.0f };
        mass.inertia.cy = { 0.0f, chassis_inertia[1], 0.0f };
        mass.inertia.cz = { 0.0f, 0.0f, chassis_inertia[2] };
        b3Body_SetMassData (chassis, mass);
      }

      b3JointId create_suspension (b3BodyId wheel,
                                   const Vec3& axle,
                                   float sag,
                                   float travel,
                                   float hertz,
                                   float damping,
                                   bool steers) {
        const b3Quat frame = suspension_rotation ();
        b3WheelJointDef def = b3DefaultWheelJointDef ();
        def.base.bodyIdA = chassis;
        def.base.bodyIdB = wheel;
        // Spring zero is top-out, a sag below the static axle.
        def.base.localFrameA = { to_b3 (axle - Vec3 (0.0f, sag, 0.0f)), frame };
        def.base.localFrameB = { { 0.0f, 0.0f, 0.0f }, frame };
        def.base.collideConnected = false;
        def.enableSuspensionSpring = true;
        def.suspensionHertz = hertz;
        def.suspensionDampingRatio = damping;
        def.enableSuspensionLimit = true;
        def.lowerSuspensionLimit = -travel;
        def.upperSuspensionLimit = 0.0f;
        def.enableSpinMotor = true;
        def.maxSpinTorque = 0.0f;
        def.spinSpeed = 0.0f;
        if (steers) {
          def.enableSteering = true;
          def.steeringHertz = 6.0f;
          def.steeringDampingRatio = 1.0f;
          def.maxSteeringTorque = 2000.0f;
          def.enableSteeringLimit = true;
          def.lowerSteeringLimit = -(bike_rig::max_fork_angle + 0.15f);
          def.upperSteeringLimit = bike_rig::max_fork_angle + 0.15f;
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
        def.gravity = { 0.0f, -bike_rig::gravity, 0.0f };
        def.enableSleep = false;
        def.workerCount = 1;
        def.hitEventThreshold = 1.5f;
        world = b3CreateWorld (&def);
        build_ground (s.ground_column, s.ground_row);
        create_chassis (s.chassis);
        front = create_wheel (s.front_wheel);
        rear = create_wheel (s.rear_wheel);
        front_joint = create_suspension (front,
                                         front_axle (),
                                         bike_rig::front_sag,
                                         bike_rig::front_travel,
                                         front_hertz,
                                         front_damping,
                                         true);
        rear_joint = create_suspension (rear,
                                        rear_axle (),
                                        bike_rig::rear_sag,
                                        bike_rig::rear_travel,
                                        rear_hertz,
                                        rear_damping,
                                        false);
        front_contact = s.front_contact;
        rear_load = s.rear_load;
        lean = s.lean;
        rear_contact = s.rear_contact;
        trunk_hit = body_hit = 0.0f;
      }

      RigidBodyState read (b3BodyId body) const {
        RigidBodyState s;
        s.position = from_b3 (b3Body_GetPosition (body));
        s.rotation = array_of (b3Body_GetRotation (body));
        s.linear_velocity = from_b3 (b3Body_GetLinearVelocity (body));
        s.angular_velocity = from_b3 (b3Body_GetAngularVelocity (body));
        return s;
      }

      RigidBikeState snapshot () const {
        RigidBikeState s;
        s.chassis = read (chassis);
        s.front_wheel = read (front);
        s.rear_wheel = read (rear);
        s.ground_column = ground_column;
        s.ground_row = ground_row;
        s.front_contact = front_contact;
        s.rear_contact = rear_contact;
        s.rear_load = rear_load;
        s.lean = lean;
        return s;
      }

      void recentre_ground () {
        const Vec3 p = from_b3 (b3Body_GetPosition (chassis));
        const Vec3 centre = patch_centre ();
        if (std::abs (p[0] - centre[0]) < bike_rig::patch_recentre_distance &&
            std::abs (p[2] - centre[2]) < bike_rig::patch_recentre_distance)
          return;
        int column = 0, row = 0;
        patch_origin_for (p, column, row);
        destroy_ground ();
        build_ground (column, row);
      }

      float extension (b3BodyId wheel, const Vec3& axle) const {
        const b3Quat q = b3Body_GetRotation (chassis);
        const Vec3 c = from_b3 (b3Body_GetPosition (chassis));
        const Vec3 w = from_b3 (b3Body_GetPosition (wheel));
        const Vec3 down = from_b3 (b3RotateVector (q, { 0.0f, -1.0f, 0.0f }));
        const Vec3 rest = c + from_b3 (b3RotateVector (q, to_b3 (axle)));
        return dot (w - rest, down);
      }

      // Velocity-level attitude control: nudge one component of the chassis'
      // angular velocity toward a target, as an impulse sized by the inertia
      // the assemblage presents about that axis. `rate` is the inverse time
      // constant of the correction.
      //
      // A bare torque on a bike standing on its tires would be met by tire
      // friction below the centre of mass and push the whole bike along. An
      // assist that turns the chassis about a point on the ground therefore
      // also moves the centre of mass as that rotation would, so the contact
      // itself does not move and the tires feel nothing to resist.
      void steer_rate (const Vec3& axis,
                       float target,
                       float inertia,
                       float rate,
                       float dt,
                       const Vec3* pivot = nullptr) {
        const Vec3 w = from_b3 (b3Body_GetAngularVelocity (chassis));
        const float gain = 1.0f - std::exp (-rate * dt);
        const float change = (target - dot (w, axis)) * gain;
        b3Body_ApplyAngularImpulse (
          chassis, to_b3 (axis * (inertia * change)), true);
        if (!pivot)
          return;
        const Vec3 centre = from_b3 (b3Body_GetPosition (chassis));
        const Vec3 swing = cross (axis * change, centre - *pivot);
        b3Body_ApplyLinearImpulseToCenter (
          chassis, to_b3 (swing * chassis_mass), true);
      }

      // A parked bike that has settled on its wheels stops simulating, so
      // it cannot creep down a slope on solver drift. Mounting resumes it
      // from rest.
      bool hold_parked (const RigidBikeControls& controls) {
        if (!controls.parked || !(front_contact || rear_contact))
          return false;
        constexpr float still = 0.25f;
        for (const b3BodyId body : { chassis, front, rear })
          if (b3Length (b3Body_GetLinearVelocity (body)) > still ||
              (B3_ID_EQUALS (body, chassis) &&
               b3Length (b3Body_GetAngularVelocity (body)) > still))
            return false;
        for (const b3BodyId body : { chassis, front, rear }) {
          b3Body_SetLinearVelocity (body, { 0.0f, 0.0f, 0.0f });
          b3Body_SetAngularVelocity (body, { 0.0f, 0.0f, 0.0f });
        }
        trunk_hit = body_hit = 0.0f;
        return true;
      }

      void step (const RigidBikeControls& controls, float dt) {
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
        const float vf = dot (v, forward);
        const float speed = length (v);
        const float total_mass = chassis_mass + 2.0f * bike_rig::wheel_mass;
        const float wheel_lever = 2.0f * bike_rig::wheel_mass;
        const float pitch_inertia =
          chassis_inertia[0] +
          wheel_lever * (bike_rig::half_wheelbase * bike_rig::half_wheelbase +
                         bike_rig::axle_height * bike_rig::axle_height);
        const float yaw_inertia =
          chassis_inertia[1] +
          wheel_lever * bike_rig::half_wheelbase * bike_rig::half_wheelbase;
        const float roll_inertia =
          chassis_inertia[2] +
          wheel_lever * bike_rig::axle_height * bike_rig::axle_height;

        // Steering. The handlebar asks for a yaw rate; the fork takes the
        // angle a rolling bicycle needs for it, and an assist on the chassis
        // makes up what the tires do not deliver, so a hard turn at speed
        // breaks the rear loose into a drift instead of refusing to turn.
        const float wanted_rate =
          controls.steer * bike_rig::steering_rate /
          (1.0f + std::abs (vf) / bike_rig::steering_fade_speed);
        const float fork =
          std::copysign (std::min (bike_rig::max_fork_angle,
                                   std::atan (std::abs (wanted_rate) * 2.0f *
                                              bike_rig::half_wheelbase /
                                              std::max (std::abs (vf), 3.0f))),
                         controls.steer);
        b3WheelJoint_SetTargetSteeringAngle (front_joint, fork);

        // Yaw follows the handlebar in the direction of travel. A rider
        // nearly at rest can still paddle the bike round.
        const float wanted_yaw =
          -wanted_rate * std::clamp (std::abs (vf) / 4.0f, 0.35f, 1.0f) *
          (vf < -1.0f ? -1.0f : 1.0f);
        if (grounded) {
          steer_rate (ground_normal, wanted_yaw, yaw_inertia, 12.0f, dt);
        } else {
          // Mid-air the handlebar whips the bike about the vertical.
          steer_rate (world_up,
                      -controls.steer * bike_rig::air_steering_rate,
                      yaw_inertia,
                      6.0f,
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
        const bool landing =
          !grounded &&
          predict_landing (
            surface, p + up * bike_rig::reference_height, v, landing_normal);
        // The rider's intended lean is a smoothed state of its own, so a
        // bike skipping over whoops does not snap between its turn lean and
        // the landing plane at every touch.
        float goal = 0.0f;
        if (grounded) {
          // The lean follows the turn the rider asks for rather than the
          // one the chassis happens to make: leaning on the measured yaw
          // feeds back through the wheels' gyroscopic coupling into a weave.
          const Vec3 centripetal = cross (Vec3 (0.0f, wanted_yaw, 0.0f), v);
          goal = std::atan2 (dot (centripetal, right), bike_rig::gravity);
        } else if (landing) {
          goal = std::atan2 (dot (landing_normal, right),
                             dot (landing_normal, level));
        }
        goal = std::clamp (goal, -bike_rig::max_lean, bike_rig::max_lean);
        lean +=
          (goal - lean) * (1.0f - std::exp (-(grounded ? 8.0f : 3.0f) * dt));
        const Vec3 target_up =
          level * std::cos (lean) + right * std::sin (lean);
        // On the ground the bike leans over its tire contacts.
        const Vec3 under = p - ground_normal * (RigidBike::ride_height -
                                                bike_rig::reference_height);
        steer_rate (forward,
                    angle_about (forward, up, target_up) * 9.0f,
                    roll_inertia,
                    30.0f,
                    dt,
                    grounded ? &under : nullptr);

        // Pitch. Grounded, the suspension and the engine pitch the bike
        // freely: nothing may lever the chassis while it stands on its tires,
        // since the tires would turn any such torque into a shove. The rider
        // instead keeps the nose down with the throttle and brakes below.
        // Airborne, the rider prepares the chassis for the landing plane,
        // keeping the chosen heading.
        float nose = 0.0f; // positive when the nose is above the ground plane
        {
          Vec3 tangent = forward - ground_normal * dot (forward, ground_normal);
          // A positive turn about the left axis lowers the nose.
          if (length2 (tangent) > 1e-6f)
            nose = angle_about (left, forward, normalized (tangent));
        }
        if (!grounded && speed > 2.0f) {
          Vec3 aim;
          if (landing) {
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
                        angle_about (left, forward, normalized (aim)) * 4.0f,
                        pitch_inertia,
                        8.0f,
                        dt);
        }

        // Engine and brakes through the wheel joints' spin motors.
        const float r = bike_rig::wheel_radius;
        float rear_speed = 0.0f;
        float rear_torque = bike_rig::engine_brake_torque;
        float front_torque = 0.0f;
        const float wheelie_cut =
          std::clamp ((bike_rig::wheelie_limit - nose) / 0.15f, 0.0f, 1.0f);
        const float stoppie_cut =
          std::clamp ((bike_rig::stoppie_limit + nose) / 0.15f, 0.0f, 1.0f);
        if (controls.throttle > 0.02f) {
          // Power is limited against road speed, as on the classic bike.
          // Traction control keeps the drive inside what the tire can put
          // down on its present load, so the tire grips rather than slides,
          // and below the pull that would lift the front: the rider keeps
          // the bike out of a wheelie with the throttle, since anything that
          // levered the nose back down would also shove the bike along. In
          // the air the rider can only rev the wheel up to road speed.
          const float force =
            std::min (params.max_drive_force,
                      params.power / std::max (std::abs (vf), 0.5f));
          if (rear_contact) {
            const float grip =
              bike_rig::traction_share * bike_rig::tire_friction * rear_load;
            const float wheelie =
              bike_rig::wheelie_share * total_mass * bike_rig::gravity *
              bike_rig::half_wheelbase /
              (RigidBike::ride_height - bike_rig::reference_height);
            rear_speed = (std::max (vf, 0.0f) + 20.0f) / r;
            rear_torque =
              wheelie_cut *
              std::min ({ controls.throttle * force, grip, wheelie }) * r;
          } else {
            rear_speed = (std::max (vf, 0.0f) + 1.0f) / r;
            rear_torque = bike_rig::free_rev_torque;
          }
        } else if (controls.throttle < -0.02f) {
          const float effort = -controls.throttle;
          if (vf > 1.5f) {
            rear_torque = effort * bike_rig::rear_brake_force * r;
            front_torque =
              stoppie_cut * effort * bike_rig::front_brake_force * r;
          } else {
            rear_speed = -bike_rig::reverse_speed / r;
            rear_torque = effort * bike_rig::reverse_force_share *
                          params.max_drive_force * r;
          }
        }
        if (controls.parked) {
          // The parking lock: both wheels held still against the chassis.
          rear_speed = 0.0f;
          rear_torque = front_torque = bike_rig::parking_torque;
        }
        b3WheelJoint_SetSpinMotorSpeed (rear_joint, rear_speed);
        b3WheelJoint_SetMaxSpinTorque (rear_joint, rear_torque);
        b3WheelJoint_SetSpinMotorSpeed (front_joint, 0.0f);
        b3WheelJoint_SetMaxSpinTorque (front_joint, front_torque);

        // Drag, wading, and the jump jets act on the whole bike's mass.
        Vec3 force =
          v * (-(bike_rig::rolling_drag + bike_rig::air_drag * speed) *
               total_mass) +
          controls.boost_acceleration * total_mass;
        if (controls.wading)
          force -= v * (bike_rig::water_drag * total_mass);
        b3Body_ApplyForceToCenter (chassis, to_b3 (force), true);

        b3World_Step (world, dt, bike_rig::substeps);

        float front_load = 0.0f;
        front_contact = touches_ground (front, dt, front_load);
        rear_contact = touches_ground (rear, dt, rear_load);
        trunk_hit = body_hit = 0.0f;
        const b3ContactEvents events = b3World_GetContactEvents (world);
        for (int i = 0; i < events.hitCount; ++i) {
          const b3ContactHitEvent& hit = events.hitEvents[i];
          const b3BodyId a = b3Shape_GetBody (hit.shapeIdA);
          const b3BodyId b = b3Shape_GetBody (hit.shapeIdB);
          const auto is = [&] (b3BodyId body) {
            return B3_IS_NON_NULL (body) &&
                   (B3_ID_EQUALS (a, body) || B3_ID_EQUALS (b, body));
          };
          if (is (obstacles))
            trunk_hit = std::max (trunk_hit, hit.approachSpeed);
          else if (is (ground))
            body_hit = std::max (body_hit, hit.approachSpeed);
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
        rotated (rotation, Vec3 (0.0f, bike_rig::reference_height, 0.0f));
      const Vec3 f = rotated (rotation, Vec3 (0, 0, 1));
      const Vec3 axle = rotated (rotation, Vec3 (1, 0, 0));
      const Vec3 roll = axle * (dot (velocity, f) / bike_rig::wheel_radius);

      RigidBikeState s;
      s.chassis = { centre, rotation, velocity, Vec3 () };
      s.front_wheel = {
        centre + rotated (rotation, front_axle ()), rotation, velocity, roll
      };
      s.rear_wheel = {
        centre + rotated (rotation, rear_axle ()), rotation, velocity, roll
      };
      m.patch_origin_for (centre, s.ground_column, s.ground_row);

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
      m_impl->step (controls, dt.numerical_value_in (u::s));
    }

    RigidBikeState RigidBike::state () const {
      return m_impl->snapshot ();
    }

    void RigidBike::restore (const RigidBikeState& state) {
      m_impl->build (state);
    }

    Vec3 RigidBike::reference_of (const RigidBikeState& state) {
      return state.chassis.position +
             rotated (state.chassis.rotation,
                      Vec3 (0.0f, bike_rig::reference_height, 0.0f));
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
                                      const Vec3& velocity) {
      const Vec3 pivot = reference_of (state);
      const Vec3 forward = forward_of (state);
      const float turn = std::atan2 (forward[0], forward[2]);
      const float wanted = length2 (Vec3 (heading[0], 0.0f, heading[2])) > 1e-8f
                             ? std::atan2 (heading[0], heading[2])
                             : turn;
      const b3Quat spin =
        b3MakeQuatFromAxisAngle ({ 0.0f, 1.0f, 0.0f }, wanted - turn);
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
             from_b3 (
               b3RotateVector (b3Body_GetRotation (m.chassis),
                               { 0.0f, bike_rig::reference_height, 0.0f }));
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

    float RigidBike::front_extension () const {
      return m_impl->extension (m_impl->front, front_axle ());
    }

    float RigidBike::rear_extension () const {
      return m_impl->extension (m_impl->rear, rear_axle ());
    }

    float RigidBike::steer_angle () const {
      return b3WheelJoint_GetSteeringAngle (m_impl->front_joint);
    }

    float RigidBike::wheel_spin_rate () const {
      return b3WheelJoint_GetSpinSpeed (m_impl->rear_joint);
    }

    float RigidBike::trunk_hit () const {
      return m_impl->trunk_hit;
    }

    float RigidBike::body_hit () const {
      return m_impl->body_hit;
    }
  }
}
