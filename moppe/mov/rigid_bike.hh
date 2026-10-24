#ifndef MOPPE_MOV_RIGID_BIKE_HH
#define MOPPE_MOV_RIGID_BIKE_HH

#include <moppe/gfx/math.hh>
#include <moppe/map/surface.hh>
#include <moppe/mov/trunk_field.hh>

#include <array>
#include <memory>

namespace moppe {
  namespace mov {
    // One rigid body's motion about its centre of mass, which is also its
    // origin: the bike's bodies are built that way so a snapshot rebuilds
    // them without any change of reference point.
    struct RigidBodyState {
      Vec3 position {};
      std::array<float, 4> rotation { 0.0f, 0.0f, 0.0f, 1.0f }; // x, y, z, w
      Vec3 linear_velocity {};
      Vec3 angular_velocity {};

      friend bool operator== (const RigidBodyState&,
                              const RigidBodyState&) = default;
    };

    // Everything a rigid bike needs to be rebuilt exactly: the three bodies,
    // the streamed ground patch they stand on, and the readings of the last
    // step that the next step's assists act on.
    struct RigidBikeState {
      RigidBodyState chassis;
      RigidBodyState front_wheel;
      RigidBodyState rear_wheel;
      // Lower corner of the resident ground patch, in patch samples.
      int ground_column = 0;
      int ground_row = 0;
      bool front_contact = false;
      bool rear_contact = false;
      // Ground force on the rear tire in the last step, newtons.
      float rear_load = 0.0f;
      // The lean the rider is holding the bike to, radians, positive right.
      float lean = 0.0f;
      // How far the rear tyre has broken loose under the locked rear
      // brake, 0 rolling to 1 sliding.
      float skid = 0.0f;
    };

    // What the rider asks of the assemblage for one step.
    struct RigidBikeControls {
      // Normalized throttle: positive drives the rear wheel; negative brakes
      // both wheels and, once nearly stopped, reverses.
      float throttle = 0.0f;
      // Handlebar command in radians of the old yaw input; positive turns
      // right.
      float steer = 0.0f;
      // Jump-jet acceleration of the whole bike, world frame.
      Vec3 boost_acceleration {};
      // How deep the wheels run in water, from 0 dry to 1 a little over
      // the axles, where the water holds the whole bike back.
      float wading = 0.0f;
      // Hanging under the glider's wing. The wing's pull on the whole
      // weight arrives as `boost_acceleration`, with all the drag there is,
      // and in the air the chassis hangs as the harness holds it: facing
      // `hang_forward` with `hang_up` as its top.
      bool hung = false;
      Vec3 hang_forward { 0, 0, 1 };
      Vec3 hang_up { 0, 1, 0 };
      // Nobody aboard: both wheels are locked, and once the bike has come
      // to rest on the ground it is held there, as on a stand.
      bool parked = false;
      // The rear brake stamped on, 0..1: the rear wheel locks and its tyre
      // slides, and on the ground the handlebar swings the tail out about
      // the front wheel, so a held skid with the bars over brakes the bike
      // round into a new heading.
      float skid = 0.0f;
      // A hop: the rider springs the whole bike up off the ground by this
      // much speed, metres per second. Only a bike on its wheels hops.
      float hop = 0.0f;
    };

    // The shape and temperament of a bike: where its wheels are, how it is
    // sprung and shod, how it steers, and how it brakes. The chassis frame
    // has x to the rider's left, y up, and z forward, with its origin at
    // the centre of mass of the frame and rider; the reference point the
    // game draws from is straight above or below it.
    struct BikeRig {
      float wheel_radius;
      float wheel_mass;
      // Isotropic, so a steered or leaned wheel carries no gyroscopic
      // self-torque of its own.
      float wheel_inertia;
      float half_wheelbase;
      // The reference point and the axles, above the centre of mass.
      float reference_height;
      float axle_height;
      // Width, height, and length of the box of frame and rider whose
      // inertia the chassis has.
      Vec3 inertia_box;
      // Its colliders against trunks and the ground: the frame from end
      // to end and the rider, each a capsule (a, b, radius).
      Vec3 frame_a, frame_b;
      float frame_radius;
      Vec3 rider_a, rider_b;
      float rider_radius;

      // Travel and static sag of each wheel, and the damping as a share of
      // critical for the sprung mass.
      float front_travel, rear_travel;
      float front_sag, rear_sag;
      float damping_ratio;

      // The tyres' grip, rolling and sliding under the locked rear brake.
      float tire_friction;
      float skid_friction;

      // Full lock swings the heading at steering_rate per radian of input,
      // fading with speed, and air_steering_rate in the air; a held skid
      // swings it at skid_rate per unit of input.
      float steering_rate;
      float steering_fade_speed;
      float air_steering_rate;
      float skid_rate;
      float max_fork_angle;
      float max_lean;

      // Pitch at which the rider has fully closed the throttle or released
      // the front brake; they start backing off 0.15 rad earlier.
      float wheelie_limit;
      float stoppie_limit;

      float reverse_speed;
      float reverse_force_share;
      float front_brake_force;
      float rear_brake_force;
      // The drag of the drive off the throttle, the lock that holds a
      // parked bike, and the locked rear brake, newton-metres.
      float engine_brake_torque;
      float parking_torque;
      float skid_torque;
      float traction_share;
      float wheelie_share;
      float free_rev_torque;

      // Height of the reference point above flat ground at static sag.
      float ride_height () const {
        return wheel_radius - axle_height + reference_height;
      }
    };

    // The motocross, modelled at one and a half times life size: wheels of
    // 0.64 m radius 2.25 m apart, axles 0.325 m below the reference point
    // the renderer draws from.
    inline const BikeRig motocross_rig {
      .wheel_radius = 0.64f,
      .wheel_mass = 12.0f,
      .wheel_inertia = 0.9f,
      .half_wheelbase = 1.125f,
      .reference_height = 0.15f,
      .axle_height = -0.175f,
      .inertia_box = { 0.5f, 1.2f, 2.0f },
      .frame_a = { 0.0f, -0.2f, -1.35f },
      .frame_b = { 0.0f, -0.2f, 1.35f },
      .frame_radius = 0.35f,
      .rider_a = { 0.0f, 0.05f, -0.35f },
      .rider_b = { 0.0f, 0.95f, -0.05f },
      .rider_radius = 0.3f,
      // Dirt-bike travel with about a third of it used at rest.
      .front_travel = 0.25f,
      .rear_travel = 0.30f,
      .front_sag = 0.08f,
      .rear_sag = 0.10f,
      .damping_ratio = 0.35f,
      // Knobbies on loose ground, generous for an arcade bike.
      .tire_friction = 1.6f,
      .skid_friction = 0.75f,
      .steering_rate = 1.6f,
      .steering_fade_speed = 25.0f,
      .air_steering_rate = 0.9f,
      .skid_rate = 2.6f,
      .max_fork_angle = 0.6f,
      .max_lean = 0.7f,
      .wheelie_limit = 0.35f,
      .stoppie_limit = 0.3f,
      .reverse_speed = 10.0f,
      .reverse_force_share = 0.6f,
      .front_brake_force = 2000.0f,
      .rear_brake_force = 1400.0f,
      .engine_brake_torque = 90.0f,
      .parking_torque = 4000.0f,
      .skid_torque = 4000.0f,
      .traction_share = 0.9f,
      .wheelie_share = 0.8f,
      .free_rev_torque = 60.0f,
    };

    // A classic mountainbike at life size: 26-inch wheels 1.06 m apart, a
    // short suspension fork, and a rigid rear end whose only give is the
    // fat tyre. Its reference point is midway between the axles, with the
    // weight of bike and seated rider 0.55 m above it.
    inline const BikeRig mountain_bike_rig {
      .wheel_radius = 0.335f,
      .wheel_mass = 3.0f,
      .wheel_inertia = 0.12f,
      .half_wheelbase = 0.53f,
      .reference_height = -0.55f,
      .axle_height = -0.55f,
      .inertia_box = { 0.45f, 1.3f, 1.3f },
      .frame_a = { 0.0f, -0.32f, -0.55f },
      .frame_b = { 0.0f, -0.32f, 0.55f },
      .frame_radius = 0.17f,
      .rider_a = { 0.0f, 0.0f, -0.18f },
      .rider_b = { 0.0f, 0.62f, 0.05f },
      .rider_radius = 0.24f,
      // Five centimetres of fork, and the tyres' give behind.
      .front_travel = 0.06f,
      .rear_travel = 0.03f,
      .front_sag = 0.02f,
      .rear_sag = 0.012f,
      .damping_ratio = 0.5f,
      .tire_friction = 1.3f,
      .skid_friction = 0.62f,
      // Full lock turns it hard at a walk and fades soon after, keeping a
      // flat-out turn within what its narrow tyres hold.
      .steering_rate = 1.7f,
      .steering_fade_speed = 4.0f,
      .air_steering_rate = 1.4f,
      .skid_rate = 3.2f,
      .max_fork_angle = 0.7f,
      .max_lean = 0.7f,
      .wheelie_limit = 0.4f,
      .stoppie_limit = 0.3f,
      // Walked back, slowly.
      .reverse_speed = 1.2f,
      .reverse_force_share = 0.5f,
      .front_brake_force = 420.0f,
      .rear_brake_force = 260.0f,
      // A freewheel: next to no drag off the pedals.
      .engine_brake_torque = 0.3f,
      .parking_torque = 300.0f,
      .skid_torque = 400.0f,
      .traction_share = 0.9f,
      .wheelie_share = 0.8f,
      .free_rev_torque = 4.0f,
    };

    // A bike as a Box3D assemblage: a chassis carrying the frame and
    // rider, and two sphere wheels on wheel joints. The joints' springs and
    // limits are the fork and the shock, the rear joint's spin motor is the
    // engine, and the front joint steers. Arcade assists sit on top as
    // angular impulses on the chassis: they hold the bike upright and leaned
    // into its turn, turn it at the rate the handlebar asks for, and prepare
    // it in the air for the ground it is about to meet.
    //
    // The bike owns its own single-threaded world. The ground is a Box3D
    // height field resampled from the periodic surface over a patch that
    // recentres as the bike travels; patches are aligned to one lattice, so
    // a recentred patch carries exactly the same vertices and the wheels
    // feel no seam. Trunks and boulders are copied in as static capsules
    // around the same patch.
    class RigidBike {
    public:
      struct Params {
        float max_drive_force = 2600.0f; // newtons at the rear contact
        float power = 30000.0f;          // watts
        float mass = 150.0f;             // whole bike and rider, kilograms
        BikeRig rig = motocross_rig;
      };

      // Height of the motocross' reference point above flat ground at
      // static sag, motocross_rig.ride_height ().
      static constexpr float ride_height = 0.965f;

      // Stands the bike at rest with its reference point at `reference`.
      RigidBike (const map::SurfaceGeometry& surface,
                 Params params,
                 const Vec3& reference,
                 const Vec3& forward);
      ~RigidBike ();
      RigidBike (const RigidBike&) = delete;
      RigidBike& operator= (const RigidBike&) = delete;

      // Places the assemblage with its reference point at `reference`, its
      // chassis facing `forward` with `up` as its top, wheels at static sag,
      // every body moving at `velocity` and the wheels rolling to match.
      void place (const Vec3& reference,
                  const Vec3& forward,
                  const Vec3& up,
                  const Vec3& velocity);

      // Trunk colliders come from the field around the ground patch. A new
      // field rebuilds the world so its bodies stay in canonical order.
      void set_trunks (const TrunkField* trunks);

      void step (const RigidBikeControls& controls, seconds_t dt);

      RigidBikeState state () const;
      void restore (const RigidBikeState& state);

      const BikeRig& rig () const;

      // Readings of a snapshot, so a checkpoint's coarse pose can be derived
      // from it without stepping the world.
      Vec3 reference_of (const RigidBikeState& state) const;
      static Vec3 forward_of (const RigidBikeState& state);
      static Vec3 up_of (const RigidBikeState& state);

      // Moves a whole snapshot rigidly: the reference point to `reference`,
      // the chassis turned about the vertical so its heading is `heading`,
      // and the chassis moving at `velocity`.
      RigidBikeState repose (const RigidBikeState& state,
                             const Vec3& reference,
                             const Vec3& heading,
                             const Vec3& velocity) const;

      Vec3 reference_position () const;
      Vec3 velocity () const;
      Vec3 forward () const;
      Vec3 up () const;
      bool front_contact () const;
      bool rear_contact () const;
      bool grounded () const {
        return front_contact () || rear_contact ();
      }
      // Wheel travel from static sag along the chassis' down axis: positive
      // hangs the wheel lower, negative compresses the spring.
      float front_extension () const;
      float rear_extension () const;
      // The fork's current angle about the steering axis; positive is right.
      float steer_angle () const;
      // Rear wheel spin relative to the chassis, radians per second.
      float wheel_spin_rate () const;
      // How far the rear tyre slides under the locked brake, 0..1.
      float skid () const;
      // Approach speeds of the hardest collisions in the last step.
      float trunk_hit () const;
      float body_hit () const;
      // A blow to the chassis at `at`, newton-seconds.
      void push (const Vec3& at, const Vec3& impulse);

    private:
      struct Impl;
      std::unique_ptr<Impl> m_impl;
    };
  }
}

#endif
