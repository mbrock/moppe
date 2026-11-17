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
      constexpr int substeps = 4;

      constexpr kilograms_t chassis_mass = 1700.0f * u::kg;
      // Pitch, yaw, and roll about the centre of mass, which lies over
      // the gearbox, back toward the rear axle and high; the weights
      // hung on the nose keep it from lying further back.
      constexpr kilogram_square_metres_t inertia[3] = { 1350.0f * u::kg * u::m2,
                                                        1450.0f * u::kg* u::m2,
                                                        520.0f * u::kg* u::m2 };
      const displacement_t mass_centre =
        displacement (Vec3 (0.0f, 0.78f, -0.15f));
      constexpr kilograms_t front_wheel_mass = 35.0f * u::kg;
      constexpr kilogram_square_metres_t front_wheel_inertia =
        2.0f * u::kg * u::m2;
      constexpr kilograms_t rear_wheel_mass = 130.0f * u::kg;
      constexpr kilogram_square_metres_t rear_wheel_inertia =
        28.0f * u::kg * u::m2;
      constexpr kilograms_t total_mass =
        chassis_mass + 2.0f * (front_wheel_mass + rear_wheel_mass);

      // The trailer: a plank bed on one axle, its weight a little ahead
      // of the axle so that the drawbar carries some of it. Its leaf
      // springs sag a little further than the tyres.
      constexpr kilograms_t trailer_mass = 380.0f * u::kg;
      constexpr kilogram_square_metres_t trailer_inertia[3] = {
        230.0f * u::kg * u::m2, 290.0f * u::kg* u::m2, 90.0f * u::kg* u::m2
      };
      const displacement_t trailer_centre =
        displacement (Vec3 (0.0f, 0.85f, 0.15f));
      constexpr kilograms_t trailer_wheel_mass = 25.0f * u::kg;
      constexpr kilogram_square_metres_t trailer_wheel_inertia =
        1.2f * u::kg * u::m2;
      constexpr kilograms_t trailer_total_mass =
        trailer_mass + 2.0f * trailer_wheel_mass;
      constexpr meters_t trailer_sag = 0.06f * u::m;
      constexpr meters_t trailer_bump = 0.08f * u::m;
      constexpr friction_coefficient_t trailer_friction =
        1.0f * isq::static_friction_coefficient[one];
      // Its overrun brakes, which hold it back as the tractor's engine
      // does when the throttle is let off and bite with the tractor's
      // brakes, and the hand brake that holds it parked.
      constexpr newton_metres_t trailer_overrun_torque = 60.0f * u::N * u::m;
      constexpr newtons_t trailer_brake_force = 3000.0f * u::N;
      constexpr newton_metres_t trailer_parking_torque = 1500.0f * u::N * u::m;
      // How far it swings or tips about the pin before the drawbar meets
      // the tractor, and how far the ball lets the two roll against each
      // other, so that the tractor tipped up does not drag it over.
      constexpr radians_t hitch_swing = 1.15f * u::rad;
      constexpr radians_t hitch_roll = 1.2f * u::rad;

      // There are no springs but the tyres', which give a little and
      // bounce: a short sag and a short way beyond it to the stops.
      constexpr meters_t sag = 0.05f * u::m;
      constexpr meters_t bump = 0.08f * u::m;
      constexpr damping_ratio_t sprung_damping_ratio =
        0.45f * damping_ratio[one];

      // Box3D mixes two shapes' friction as a geometric mean, so the
      // tyre carries the square of the wanted coefficient against unit
      // ground friction. The rear bars bite.
      constexpr friction_coefficient_t front_friction =
        1.0f * isq::static_friction_coefficient[one];
      constexpr friction_coefficient_t rear_friction =
        1.35f * isq::static_friction_coefficient[one];
      constexpr friction_coefficient_t ground_friction =
        1.0f * isq::static_friction_coefficient[one];

      // The rear wheels pull hard, enough to take the trailer up a slope
      // of one in three, and the engine is governed: the throttle asks
      // for a road speed, which the wheels are driven to and held at.
      constexpr newtons_t engine_force = 10500.0f * u::N;
      constexpr watts_t engine_power = 38000.0f * u::W;
      constexpr speed_t top_speed = 8.5f * u::m / u::s;
      constexpr speed_t reverse_speed = 3.2f * u::m / u::s;
      constexpr newtons_t brake_force = 18000.0f * u::N;
      // Off the throttle the gearbox holds it back.
      constexpr newton_metres_t engine_brake_torque = 350.0f * u::N * u::m;
      constexpr newton_metres_t parking_torque = 9000.0f * u::N * u::m;
      constexpr newton_metres_t free_rev_torque = 60.0f * u::N * u::m;
      constexpr proportion_t traction_share = 0.95f * proportion[one];
      // With its nose off the ground the driver eases off, or it would
      // rear over onto its back.
      constexpr proportion_t rearing_share = 0.35f * proportion[one];

      constexpr radians_t full_lock = 0.62f * u::rad;
      constexpr speed_t lock_fade_speed = 9.0f * u::m / u::s;

      constexpr rolling_resistance_t rolling_resistance =
        0.04f * isq::rolling_resistance_factor[one];
      // Drag as a deceleration per unit of speed: the air's per metre
      // and the water's per second.
      constexpr per_metre_t air_drag = 0.004f / u::m;
      constexpr damping_t water_drag = 1.2f / u::s;

      // The ground patch, as the jeep's.
      constexpr int patch_samples = 97;
      constexpr meters_t patch_recentre_distance = 24.0f * u::m;
      constexpr meters_t trunk_reach = 72.0f * u::m;

      constexpr std::uint64_t ground_bit = 1;
      constexpr std::uint64_t trunk_bit = 2;
      constexpr std::uint64_t wheel_bit = 4;
      constexpr std::uint64_t chassis_bit = 8;

      bool front (int wheel) {
        return wheel < 2;
      }

      Vec3 axle_of (int wheel) {
        const meters_t track =
          front (wheel) ? Tractor::front_track : Tractor::rear_track;
        const meters_t across = wheel % 2 == 0 ? track : -track;
        const meters_t along =
          front (wheel) ? Tractor::front_axle : Tractor::rear_axle;
        return Vec3 (meters_value (across),
                     meters_value (Tractor::wheel_radius (wheel)),
                     meters_value (along));
      }

      kilograms_t wheel_mass (int wheel) {
        return front (wheel) ? front_wheel_mass : rear_wheel_mass;
      }

      // The trailer's left wheel, then its right, in its frame.
      Vec3 trailer_axle_of (int wheel) {
        const meters_t across =
          wheel == 0 ? Tractor::trailer_track : -Tractor::trailer_track;
        return Vec3 (
          meters_value (across), meters_value (Tractor::trailer_radius), 0.0f);
      }

      const Vec3& hitch_pin () {
        return displacement_value (Tractor::hitch_pin);
      }

      const Vec3& trailer_eye () {
        return displacement_value (Tractor::trailer_eye);
      }

      // A joint frame whose z looks back along the body.
      constexpr b3Quat facing_back { { 0.0f, 1.0f, 0.0f }, 0.0f };
    }
  }

  struct Tractor::Impl {
    const map::SurfaceGeometry& surface;
    GroundPatch patch;
    std::array<frequency_t, wheel_count> hertz {};
    std::array<damping_ratio_t, wheel_count> damping {};
    frequency_t trailer_hertz {};
    damping_ratio_t trailer_damping {};

    const TrunkField* trunks = nullptr;
    b3WorldId world = b3_nullWorldId;
    b3BodyId chassis = b3_nullBodyId;
    std::array<b3BodyId, wheel_count> wheels {};
    std::array<b3JointId, wheel_count> joints {};
    b3BodyId trailer = b3_nullBodyId;
    std::array<b3BodyId, trailer_wheel_count> trailer_wheels {};
    std::array<b3JointId, trailer_wheel_count> trailer_joints {};
    b3JointId hitch = b3_nullJointId;

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
      trailer_wheels.fill (b3_nullBodyId);
      trailer_joints.fill (b3_nullJointId);
      // Each wheel carries its share of the chassis as it stands, most
      // of it on the rear pair; the joint springs are soft constraints
      // tuned in hertz against the joint's effective mass, so convert
      // the rate and damping wanted for that share into those terms.
      const meters_t centre_along =
        displacement_value (tractor_rig::mass_centre)[2] * u::m;
      const meters_t base = Tractor::front_axle - Tractor::rear_axle;
      const proportion_t rear_share =
        (Tractor::front_axle - centre_along) / base;
      for (int wheel = 0; wheel < wheel_count; ++wheel) {
        const Vec3 axle = tractor_rig::axle_of (wheel);
        const meters_t along = axle[2] * u::m - centre_along;
        const meters_t across = axle[0] * u::m;
        const kilograms_t effective =
          1.0f / (1.0f / tractor_rig::wheel_mass (wheel) +
                  1.0f / tractor_rig::chassis_mass +
                  along * along / tractor_rig::inertia[0] +
                  across * across / tractor_rig::inertia[2]);
        const kilograms_t sprung (0.5f * tractor_rig::chassis_mass *
                                  (tractor_rig::front (wheel)
                                     ? 1.0f * proportion[one] - rear_share
                                     : rear_share));
        spring_tuning (sprung * standard_gravity / tractor_rig::sag,
                       sprung,
                       effective,
                       tractor_rig::sprung_damping_ratio,
                       hertz[wheel],
                       damping[wheel]);
      }
      // The trailer's two share what the drawbar does not carry.
      {
        const meters_t centre_ahead =
          displacement_value (tractor_rig::trailer_centre)[2] * u::m;
        const meters_t eye_ahead =
          displacement_value (Tractor::trailer_eye)[2] * u::m;
        const meters_t along = -centre_ahead;
        const meters_t across = Tractor::trailer_track;
        const kilograms_t effective =
          1.0f / (1.0f / tractor_rig::trailer_wheel_mass +
                  1.0f / tractor_rig::trailer_mass +
                  along * along / tractor_rig::trailer_inertia[0] +
                  across * across / tractor_rig::trailer_inertia[2]);
        const kilograms_t sprung = 0.5f * tractor_rig::trailer_mass *
                                   (1.0f * one - centre_ahead / eye_ahead);
        spring_tuning (sprung * standard_gravity / tractor_rig::trailer_sag,
                       sprung,
                       effective,
                       tractor_rig::sprung_damping_ratio,
                       trailer_hertz,
                       trailer_damping);
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
      trailer = b3_nullBodyId;
      trailer_wheels.fill (b3_nullBodyId);
      trailer_joints.fill (b3_nullJointId);
      hitch = b3_nullJointId;
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

    // A sphere of `radius` that meets only the ground, gripping it
    // `grip` times as well as unit friction would.
    b3BodyId create_round (const RigidBodyState& s,
                           meters_t radius,
                           kilograms_t weight,
                           kilogram_square_metres_t inertia,
                           friction_coefficient_t grip) {
      const b3BodyId wheel = create_body (s);
      b3ShapeDef shape = b3DefaultShapeDef ();
      shape.updateBodyMass = false;
      shape.baseMaterial.friction =
        (grip * grip / tractor_rig::ground_friction).numerical_value_in (one);
      shape.baseMaterial.rollingResistance =
        tractor_rig::rolling_resistance.numerical_value_in (one);
      shape.filter.categoryBits = tractor_rig::wheel_bit;
      shape.filter.maskBits = tractor_rig::ground_bit;
      const b3Sphere sphere { { 0.0f, 0.0f, 0.0f }, meters_value (radius) };
      b3CreateSphereShape (wheel, &shape, &sphere);
      b3Body_SetMassData (
        wheel, body_mass (weight, Vec3 (), { inertia, inertia, inertia }));
      return wheel;
    }

    b3BodyId create_wheel (int wheel_index, const RigidBodyState& s) {
      const bool front = tractor_rig::front (wheel_index);
      return create_round (s,
                           wheel_radius (wheel_index),
                           tractor_rig::wheel_mass (wheel_index),
                           front ? tractor_rig::front_wheel_inertia
                                 : tractor_rig::rear_wheel_inertia,
                           front ? tractor_rig::front_friction
                                 : tractor_rig::rear_friction);
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
        0.96f,
        0.36f,
        0.62f,
        { 0.0f, 1.02f, meters_value (Tractor::rear_axle - 0.05f * u::m) });
      b3CreateHullShape (chassis, &shape, &rear.base);
      b3Body_SetMassData (chassis,
                          body_mass (tractor_rig::chassis_mass,
                                     tractor_rig::mass_centre,
                                     { tractor_rig::inertia[0],
                                       tractor_rig::inertia[1],
                                       tractor_rig::inertia[2] }));
    }

    void create_trailer (const RigidBodyState& s) {
      trailer = create_body (s);
      // The bed, boards and all, meets trunks and the ground; the
      // drawbar slips under what the bed would meet first.
      b3ShapeDef shape = b3DefaultShapeDef ();
      shape.updateBodyMass = false;
      shape.baseMaterial.friction = 0.45f;
      shape.filter.categoryBits = tractor_rig::chassis_bit;
      shape.filter.maskBits = tractor_rig::ground_bit | tractor_rig::trunk_bit;
      const b3BoxHull bed =
        b3MakeOffsetBoxHull (0.80f, 0.30f, 1.30f, { 0.0f, 1.12f, 0.15f });
      b3CreateHullShape (trailer, &shape, &bed.base);
      b3Body_SetMassData (trailer,
                          body_mass (tractor_rig::trailer_mass,
                                     tractor_rig::trailer_centre,
                                     { tractor_rig::trailer_inertia[0],
                                       tractor_rig::trailer_inertia[1],
                                       tractor_rig::trailer_inertia[2] }));
    }

    b3JointId create_trailer_suspension (int wheel) {
      const b3Quat frame = suspension_rotation ();
      b3WheelJointDef def = b3DefaultWheelJointDef ();
      def.base.bodyIdA = trailer;
      def.base.bodyIdB = trailer_wheels[wheel];
      def.base.localFrameA = {
        to_b3 (tractor_rig::trailer_axle_of (wheel) -
               Vec3 (0.0f, meters_value (tractor_rig::trailer_sag), 0.0f)),
        frame
      };
      def.base.localFrameB = { { 0.0f, 0.0f, 0.0f }, frame };
      def.base.collideConnected = false;
      def.enableSuspensionSpring = true;
      def.suspensionHertz = trailer_hertz.numerical_value_in (si::hertz);
      def.suspensionDampingRatio = trailer_damping.numerical_value_in (one);
      def.enableSuspensionLimit = true;
      def.lowerSuspensionLimit =
        -meters_value (tractor_rig::trailer_sag + tractor_rig::trailer_bump);
      def.upperSuspensionLimit = 0.0f;
      def.enableSpinMotor = true;
      def.maxSpinTorque = 0.0f;
      def.spinSpeed = 0.0f;
      return b3CreateWheelJoint (world, &def);
    }

    // The drawbar's eye over the pin: a ball joint, its swing and roll
    // limited where the drawbar would meet the tractor.
    b3JointId create_hitch () {
      b3SphericalJointDef def = b3DefaultSphericalJointDef ();
      def.base.bodyIdA = chassis;
      def.base.bodyIdB = trailer;
      def.base.localFrameA = { to_b3 (tractor_rig::hitch_pin ()),
                               tractor_rig::facing_back };
      def.base.localFrameB = { to_b3 (tractor_rig::trailer_eye ()),
                               tractor_rig::facing_back };
      def.base.collideConnected = false;
      def.enableConeLimit = true;
      def.coneAngle = radians_value (tractor_rig::hitch_swing);
      def.enableTwistLimit = true;
      def.lowerTwistAngle = -radians_value (tractor_rig::hitch_roll);
      def.upperTwistAngle = radians_value (tractor_rig::hitch_roll);
      return b3CreateSphericalJoint (world, &def);
    }

    b3JointId create_suspension (int wheel) {
      const b3Quat frame = suspension_rotation ();
      b3WheelJointDef def = b3DefaultWheelJointDef ();
      def.base.bodyIdA = chassis;
      def.base.bodyIdB = wheels[wheel];
      // Spring zero is top-out, a sag below the standing axle.
      def.base.localFrameA = {
        to_b3 (tractor_rig::axle_of (wheel) -
               Vec3 (0.0f, meters_value (tractor_rig::sag), 0.0f)),
        frame
      };
      def.base.localFrameB = { { 0.0f, 0.0f, 0.0f }, frame };
      def.base.collideConnected = false;
      def.enableSuspensionSpring = true;
      def.suspensionHertz = hertz[wheel].numerical_value_in (si::hertz);
      def.suspensionDampingRatio = damping[wheel].numerical_value_in (one);
      def.enableSuspensionLimit = true;
      def.lowerSuspensionLimit =
        -meters_value (tractor_rig::sag + tractor_rig::bump);
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
        def.lowerSteeringLimit =
          -radians_value (tractor_rig::full_lock + 0.1f * u::rad);
        def.upperSteeringLimit =
          radians_value (tractor_rig::full_lock + 0.1f * u::rad);
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
        wheels[wheel] = create_wheel (wheel, s.wheels[wheel]);
      for (int wheel = 0; wheel < wheel_count; ++wheel)
        joints[wheel] = create_suspension (wheel);
      create_trailer (s.trailer);
      for (int wheel = 0; wheel < trailer_wheel_count; ++wheel)
        trailer_wheels[wheel] =
          create_round (s.trailer_wheels[wheel],
                        trailer_radius,
                        tractor_rig::trailer_wheel_mass,
                        tractor_rig::trailer_wheel_inertia,
                        tractor_rig::trailer_friction);
      for (int wheel = 0; wheel < trailer_wheel_count; ++wheel)
        trailer_joints[wheel] = create_trailer_suspension (wheel);
      hitch = create_hitch ();
      kept = s;
    }

    State snapshot () const {
      State s = kept;
      s.chassis = read_body (chassis);
      for (int wheel = 0; wheel < wheel_count; ++wheel)
        s.wheels[wheel] = read_body (wheels[wheel]);
      s.trailer = read_body (trailer);
      for (int wheel = 0; wheel < trailer_wheel_count; ++wheel)
        s.trailer_wheels[wheel] = read_body (trailer_wheels[wheel]);
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

    Vec3 trailer_axis (const Vec3& local) const {
      return from_b3 (
        b3RotateVector (b3Body_GetRotation (trailer), to_b3 (local)));
    }

    Vec3 trailer_point (const Vec3& local) const {
      return from_b3 (b3Body_GetPosition (trailer)) + trailer_axis (local);
    }

    // The standing pose at `at`, its origin level with the highest
    // ground under any wheel so that none starts inside it; the springs
    // settle the rest.
    State standing (const Vec3& at, const Vec3& heading) const {
      const b3Quat q = level_rotation (heading);
      const std::array<float, 4> rotation = array_of (q);
      Vec3 centre = at;
      meters_t highest = -1e30f * u::m;
      for (int wheel = 0; wheel < wheel_count; ++wheel) {
        const Vec3 under =
          at + rotated (rotation, tractor_rig::axle_of (wheel));
        highest = std::max (
          highest, surface_elevation_at (surface, under[0], under[2]) * u::m);
      }
      centre[1] = meters_value (highest + 0.04f * u::m);
      State s = kept;
      s.chassis = { centre, rotation, Vec3 (), Vec3 () };
      for (int wheel = 0; wheel < wheel_count; ++wheel)
        s.wheels[wheel] = { centre +
                              rotated (rotation, tractor_rig::axle_of (wheel)),
                            rotation,
                            Vec3 (),
                            Vec3 () };
      // The trailer straight behind, its eye on the pin and its axle
      // over the ground there, pitched to reach both.
      const Vec3 pin = centre + rotated (rotation, tractor_rig::hitch_pin ());
      const Vec3 ahead = rotated (rotation, Vec3 (0, 0, 1));
      const Vec3 left = rotated (rotation, Vec3 (1, 0, 0));
      const Vec3 eye = tractor_rig::trailer_eye ();
      const Vec3 axle = pin - ahead * eye[2];
      meters_t ground = -1e30f * u::m;
      for (int wheel = 0; wheel < trailer_wheel_count; ++wheel) {
        const Vec3 under =
          axle + left * tractor_rig::trailer_axle_of (wheel)[0];
        ground = std::max (
          ground, surface_elevation_at (surface, under[0], under[2]) * u::m);
      }
      const radians_t pitch =
        std::atan2 (pin[1] - eye[1] - meters_value (ground + 0.03f * u::m),
                    eye[2]) *
        u::rad;
      const Vec3 along = ahead * std::cos (radians_value (pitch)) +
                         Vec3 (0, 1, 0) * std::sin (radians_value (pitch));
      const b3Matrix3 frame { to_b3 (left),
                              to_b3 (cross (along, left)),
                              to_b3 (along) };
      const std::array<float, 4> tilt =
        array_of (b3NormalizeQuat (b3MakeQuatFromMatrix (&frame)));
      const Vec3 origin = pin - rotated (tilt, eye);
      s.trailer = { origin, tilt, Vec3 (), Vec3 () };
      for (int wheel = 0; wheel < trailer_wheel_count; ++wheel)
        s.trailer_wheels[wheel] = {
          origin + rotated (tilt, tractor_rig::trailer_axle_of (wheel)),
          tilt,
          Vec3 (),
          Vec3 ()
        };
      patch.origin_for (centre, s.ground_column, s.ground_row);
      s.touching.fill (false);
      s.load.fill (newtons_t {});
      s.still = s.overturned = seconds_t {};
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
      move (trailer, s.trailer);
      for (int wheel = 0; wheel < trailer_wheel_count; ++wheel)
        move (trailer_wheels[wheel], s.trailer_wheels[wheel]);
      kept = s;
      recentre_ground ();
    }

    void step (seconds_t dt, meters_t water_level) {
      recentre_ground ();
      const Vec3 forward = axis (Vec3 (0, 0, 1));
      const Vec3 v = from_b3 (b3Body_GetLinearVelocity (chassis));
      const speed_t vf = dot (v, forward) * u::m / u::s;
      const speed_t speed = length (v) * u::m / u::s;

      // The wheel is turned less far the faster the tractor goes. The
      // joint steers about its axis down the chassis, so a positive
      // angle turns to the right.
      const magnitude_t fade = speed / tractor_rig::lock_fade_speed;
      const radians_t lock =
        tractor_rig::full_lock / (1.0f * one + fade * fade);
      const radians_t steer (std::clamp (kept.turn,
                                         -1.0f * control_signal[one],
                                         1.0f * control_signal[one]) *
                             lock);
      b3WheelJoint_SetTargetSteeringAngle (joints[0], radians_value (steer));
      b3WheelJoint_SetTargetSteeringAngle (joints[1], radians_value (steer));

      // The rear wheels are driven toward the road speed the throttle
      // asks for, each within what its load lets it put down; the brakes
      // act on all four.
      const control_signal_t drive =
        kept.parked ? control_signal_t {}
                    : std::clamp (kept.drive,
                                  -1.0f * control_signal[one],
                                  1.0f * control_signal[one]);
      const newtons_t pull =
        std::min (tractor_rig::engine_force,
                  newtons_t (tractor_rig::engine_power /
                             std::max (abs (vf), 1.0f * u::m / u::s)));
      const bool braking = kept.brake || (drive * vf < speed_t {} &&
                                          abs (vf) > 1.0f * u::m / u::s);
      const bool rearing = !kept.touching[0] && !kept.touching[1] &&
                           (kept.touching[2] || kept.touching[3]);
      for (int wheel = 0; wheel < wheel_count; ++wheel) {
        const meters_t radius = wheel_radius (wheel);
        const bool driven = !tractor_rig::front (wheel);
        radians_per_second_t spin {};
        newton_metres_t torque =
          driven ? tractor_rig::engine_brake_torque : newton_metres_t {};
        const newton_metres_t grip (tractor_rig::traction_share *
                                    tractor_rig::rear_friction *
                                    kept.load[wheel] * radius);
        if (kept.parked) {
          torque = tractor_rig::parking_torque;
        } else if (braking) {
          // Asking for the other way while still rolling is braking.
          const control_signal_t effort =
            kept.brake ? 1.0f * control_signal[one] : abs (drive);
          torque = newton_metres_t (effort * tractor_rig::brake_force * radius /
                                    wheel_count);
        } else if (!driven) {
          // The front wheels roll free.
          torque = newton_metres_t {};
        } else if (abs (drive) > 0.02f * control_signal[one]) {
          const bool ahead = drive > control_signal_t {};
          const newton_metres_t wanted (
            pull * radius / 2.0f *
            (rearing ? tractor_rig::rearing_share : 1.0f * proportion[one]));
          if (kept.touching[wheel]) {
            spin = radians_per_second_t (
              (ahead ? drive * tractor_rig::top_speed / radius
                     : drive * tractor_rig::reverse_speed / radius) *
              u::rad);
            torque = std::min (wanted, grip);
          } else {
            // In the air a wheel only revs up to road speed.
            spin =
              (vf + (ahead ? 1.0f : -1.0f) * u::m / u::s) / radius * u::rad;
            torque = tractor_rig::free_rev_torque;
          }
        }
        b3WheelJoint_SetSpinMotorSpeed (
          joints[wheel], spin.numerical_value_in (u::rad / u::s));
        b3WheelJoint_SetMaxSpinTorque (joints[wheel],
                                       newton_metres_value (torque));
      }
      // The trailer's roll free while the tractor pulls, but for its
      // brakes.
      const newton_metres_t trailer_torque =
        kept.parked ? tractor_rig::trailer_parking_torque
        : braking   ? newton_metres_t (
                      (kept.brake ? 1.0f * control_signal[one] : abs (drive)) *
                      tractor_rig::trailer_brake_force * trailer_radius /
                      trailer_wheel_count)
        : abs (drive) <= 0.02f * control_signal[one]
          ? tractor_rig::trailer_overrun_torque
          : newton_metres_t {};
      for (const b3JointId joint : trailer_joints) {
        b3WheelJoint_SetSpinMotorSpeed (joint, 0.0f);
        b3WheelJoint_SetMaxSpinTorque (joint,
                                       newton_metres_value (trailer_torque));
      }

      // The air's drag, and water's up to the axles, on the whole
      // tractor's mass.
      const proportion_t wet =
        std::clamp (
          (meters_value (water_level) - point (Vec3 (0, 0.2f, 0))[1]) / 0.8f,
          0.0f,
          1.0f) *
        proportion[one];
      apply_drag (chassis,
                  v,
                  (tractor_rig::air_drag * speed +
                   damping_t (tractor_rig::water_drag * wet)) *
                    tractor_rig::total_mass);
      {
        const Vec3 tv = from_b3 (b3Body_GetLinearVelocity (trailer));
        const proportion_t trailer_wet =
          std::clamp ((meters_value (water_level) -
                       trailer_point (Vec3 (0, 0.2f, 0))[1]) /
                        0.8f,
                      0.0f,
                      1.0f) *
          proportion[one];
        apply_drag (trailer,
                    tv,
                    (tractor_rig::air_drag * (length (tv) * u::m / u::s) +
                     damping_t (tractor_rig::water_drag * trailer_wet)) *
                      tractor_rig::trailer_total_mass);
      }

      b3World_Step (world, seconds_value (dt), tractor_rig::substeps);

      for (int wheel = 0; wheel < wheel_count; ++wheel)
        kept.touching[wheel] =
          wheel_touches_ground (wheels[wheel], dt, kept.load[wheel]);
      kept.front_spin = rolled (kept.front_spin, joints[0], dt);
      kept.rear_spin = rolled (kept.rear_spin, joints[2], dt);
      kept.trailer_spin = rolled (kept.trailer_spin, trailer_joints[0], dt);

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

      // Parked and still, trailer and all, it stays where it is.
      const Vec3 towed = from_b3 (b3Body_GetLinearVelocity (trailer));
      const Vec3 towed_turning = from_b3 (b3Body_GetAngularVelocity (trailer));
      const bool quiet = length2 (moving) < 0.08f * 0.08f &&
                         length2 (turning) < 0.08f * 0.08f &&
                         length2 (towed) < 0.08f * 0.08f &&
                         length2 (towed_turning) < 0.08f * 0.08f &&
                         std::any_of (
                           kept.touching.begin (),
                           kept.touching.end (),
                           [] (bool touching) { return touching; });
      kept.still = kept.parked && quiet ? kept.still + dt : seconds_t {};
      if (kept.still > 1.0f * u::s)
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

  void Tractor::place (position_t at, direction_t heading) {
    m_impl->place (position_value (at), direction_value (heading));
  }

  void Tractor::set_trunks (const TrunkField* trunks) {
    Impl& m = *m_impl;
    if (trunks == m.trunks)
      return;
    const State s = m.snapshot ();
    m.trunks = trunks;
    m.build (s);
  }

  void Tractor::set_drive (control_signal_t drive) {
    m_impl->kept.drive = drive;
  }

  void Tractor::set_turn (control_signal_t turn) {
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

  void Tractor::update (seconds_t dt, meters_t water_level) {
    in_substeps (dt, [&] (seconds_t h) {
      if (!m_impl->kept.asleep)
        m_impl->step (h, water_level);
    });
  }

  position_t Tractor::position () const {
    return moppe::position (m_impl->point (Vec3 ()));
  }

  direction_t Tractor::forward () const {
    return unit_direction (m_impl->axis (Vec3 (0, 0, 1)));
  }

  direction_t Tractor::up () const {
    return unit_direction (m_impl->axis (Vec3 (0, 1, 0)));
  }

  direction_t Tractor::left () const {
    return unit_direction (m_impl->axis (Vec3 (1, 0, 0)));
  }

  velocity_t Tractor::velocity () const {
    return moppe::velocity (
      from_b3 (b3Body_GetLinearVelocity (m_impl->chassis)));
  }

  bool Tractor::grounded () const {
    const auto& touching = m_impl->kept.touching;
    return std::any_of (
      touching.begin (), touching.end (), [] (bool wheel) { return wheel; });
  }

  bool Tractor::asleep () const {
    return m_impl->kept.asleep;
  }

  kilograms_t Tractor::mass () const {
    return (tractor_rig::total_mass + tractor_rig::trailer_total_mass);
  }

  kilograms_t Tractor::trailer_mass () const {
    return tractor_rig::trailer_total_mass;
  }

  void Tractor::push (position_t at, const impulse_t& impulse) {
    Impl& m = *m_impl;
    m.kept.asleep = false;
    m.kept.still = seconds_t {};
    const bool towed =
      length2 (position_value (at) -
               m.trailer_point (Vec3 (0.0f, 0.85f, 0.15f))) <
      length2 (position_value (at) - m.point (Vec3 (0.0f, 0.78f, 0.0f)));
    b3Body_ApplyLinearImpulse (towed ? m.trailer : m.chassis,
                               to_b3 (impulse_value (impulse)),
                               to_b3 (at),
                               true);
  }

  control_signal_t Tractor::throttle () const {
    const State& kept = m_impl->kept;
    if (kept.parked)
      return {};
    return std::max (0.15f * control_signal[one],
                     std::min (1.0f * control_signal[one], abs (kept.drive)));
  }

  position_t Tractor::point (const displacement_t& local) const {
    return moppe::position (m_impl->point (displacement_value (local)));
  }

  meters_t Tractor::wheel_drop (int wheel) const {
    const Impl& m = *m_impl;
    const Vec3 at = from_b3 (b3Body_GetPosition (m.wheels[wheel]));
    return dot (at - m.point (tractor_rig::axle_of (wheel)),
                m.axis (Vec3 (0, -1, 0))) *
           u::m;
  }

  radians_t Tractor::steer () const {
    return b3WheelJoint_GetSteeringAngle (m_impl->joints[0]) * u::rad;
  }

  radians_t Tractor::front_spin () const {
    return m_impl->kept.front_spin;
  }

  radians_t Tractor::rear_spin () const {
    return m_impl->kept.rear_spin;
  }

  position_t Tractor::trailer_position () const {
    return moppe::position (m_impl->trailer_point (Vec3 ()));
  }

  direction_t Tractor::trailer_forward () const {
    return unit_direction (m_impl->trailer_axis (Vec3 (0, 0, 1)));
  }

  direction_t Tractor::trailer_up () const {
    return unit_direction (m_impl->trailer_axis (Vec3 (0, 1, 0)));
  }

  position_t Tractor::trailer_point (const displacement_t& local) const {
    return moppe::position (m_impl->trailer_point (displacement_value (local)));
  }

  meters_t Tractor::trailer_wheel_drop (int wheel) const {
    const Impl& m = *m_impl;
    const Vec3 at = from_b3 (b3Body_GetPosition (m.trailer_wheels[wheel]));
    return dot (at - m.trailer_point (tractor_rig::trailer_axle_of (wheel)),
                m.trailer_axis (Vec3 (0, -1, 0))) *
           u::m;
  }

  radians_t Tractor::trailer_spin () const {
    return m_impl->kept.trailer_spin;
  }
}
