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
      // Ground force on the rear tire in the last step.
      newtons_t rear_load {};
      // The lean the rider is holding the bike to, positive right.
      radians_t lean {};
      // How far the rear tyre has broken loose under the locked rear
      // brake, 0 rolling to 1 sliding.
      float skid = 0.0f;
      // How far back the rider's weight is along the chassis (negative
      // forward), and the throttle they have rolled on while leaning back
      // for a wheelie.
      meters_t rider_shift {};
      float twist = 0.0f;
      // How long since either wheel last touched the ground.
      seconds_t airborne {};
    };

    // What the rider asks of the assemblage for one step.
    struct RigidBikeControls {
      // Normalized throttle: positive drives the rear wheel; negative brakes
      // both wheels and, once nearly stopped, reverses.
      float throttle = 0.0f;
      // Handlebar command as an angle of the old yaw input; positive
      // turns right.
      radians_t steer {};
      // Jump-jet acceleration of the whole bike, world frame.
      acceleration_t boost_acceleration =
        Vec3 () * isq::acceleration[si::metre / pow<2> (si::second)];
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
      // much speed. Only a bike on its wheels hops.
      speed_t hop {};
      // The rider leans back and pulls on the bars, 0..1. Their weight
      // goes back over the rear wheel, the traction control stops holding
      // the front down, and the throttle rolls on progressively, so the
      // drive lifts the front wheel and the bike pivots on the rear. Up
      // there the rider balances it with their body, as far as it reaches;
      // the throttle lifts it further (too much loops it over) and the
      // rear brake, covered rather than stamped on, brings it down.
      float wheelie = 0.0f;
    };

    // The shape and temperament of a bike: where its wheels are, how it is
    // sprung and shod, how it steers, and how it brakes. The chassis frame
    // has x to the rider's left, y up, and z forward, with its origin at
    // the centre of mass of the frame and rider; the reference point the
    // game draws from is straight above or below it.
    struct BikeRig {
      // The rear wheel's radius, and the front's where it is smaller (a
      // cargo bike's); zero has both alike. The axles stand at
      // axle_height over the rear one's centre, so on flat ground the
      // smaller front axle stands as much lower as its wheel is.
      meters_t wheel_radius;
      meters_t front_wheel_radius {};
      kilograms_t wheel_mass;
      // Isotropic, so a steered or leaned wheel carries no gyroscopic
      // self-torque of its own.
      kilogram_square_metres_t wheel_inertia;
      meters_t half_wheelbase;
      // The reference point and the axles, above the centre of mass.
      meters_t reference_height;
      meters_t axle_height;
      // Width, height, and length of the box of frame and rider whose
      // inertia the chassis has.
      Vec3 inertia_box;
      // Its colliders against trunks and the ground: the frame from end
      // to end and the rider, each a capsule (a, b, radius).
      Vec3 frame_a, frame_b;
      meters_t frame_radius;
      Vec3 rider_a, rider_b;
      meters_t rider_radius;

      // Travel and static sag of each wheel, and the damping as a share of
      // critical for the sprung mass.
      meters_t front_travel;
      meters_t rear_travel;
      meters_t front_sag;
      meters_t rear_sag;
      float damping_ratio;

      // The tyres' grip, rolling and sliding under the locked rear brake.
      float tire_friction;
      float skid_friction;

      // Full lock swings the heading at steering_rate per radian of input,
      // fading with speed, and air_steering_rate in the air; a held skid
      // swings it at skid_rate per unit of input.
      per_second_t steering_rate;
      speed_t steering_fade_speed;
      per_second_t air_steering_rate;
      per_second_t skid_rate;
      radians_t max_fork_angle;
      radians_t max_lean;

      // Pitch at which the rider has fully closed the throttle or released
      // the front brake; they start backing off 0.15 rad earlier.
      radians_t wheelie_limit;
      radians_t stoppie_limit;
      // Leaning back for a wheelie: how far the rider's weight goes back,
      // metres, and the pitch above the ground they try to hold the bike
      // at, radians, a little short of where it balances on its own.
      meters_t wheelie_lean;
      radians_t wheelie_pitch;
      // The most the drive may rear it up by about the rear tyre beyond
      // what its weight holds it down with, newton-metres.
      newton_metres_t wheelie_torque;

      speed_t reverse_speed;
      float reverse_force_share;
      newtons_t front_brake_force;
      newtons_t rear_brake_force;
      // The drag of the drive off the throttle, the lock that holds a
      // parked bike, and the locked rear brake, newton-metres.
      newton_metres_t engine_brake_torque;
      newton_metres_t parking_torque;
      newton_metres_t skid_torque;
      float traction_share;
      float wheelie_share;
      newton_metres_t free_rev_torque;
      // Drag of the whole bike, as accelerations: linear in speed per
      // second, and quadratic per metre.
      per_second_t rolling_drag;
      per_metre_t air_drag;
      // A pedalled bike of one gear turns its crank this many times slower
      // than its rear wheel; zero rides through the gears.
      float gear_ratio = 0.0f;

      meters_t front_radius () const {
        return front_wheel_radius > meters_t {} ? front_wheel_radius
                                                : wheel_radius;
      }

      // Height of the reference point above flat ground at static sag.
      meters_t ride_height () const {
        return wheel_radius - axle_height + reference_height;
      }
    };

    // The Honda CT90 Trail 90 at life size, to Honda's figures: 2.75-17
    // tyres 1.22 m apart, its reference point midway between the axles at
    // their height, and the weight of the bike and its seated rider, some
    // 175 kg, 0.42 m over that. It is soft and short of travel, stops on
    // drums, and has no reverse: backing up is paddled with the feet.
    inline const BikeRig trail90_rig {
      .wheel_radius = 0.29f * u::m,
      .wheel_mass = 7.0f * u::kg,
      .wheel_inertia = 0.35f * u::kg * u::m2,
      .half_wheelbase = 0.61f * u::m,
      .reference_height = -0.42f * u::m,
      .axle_height = -0.42f * u::m,
      .inertia_box = { 0.5f, 1.2f, 1.3f },
      // The engine and the pressed body from axle to axle, its underside
      // at the skid plate, reaching out over the front tyre; and the
      // seated rider, hips to head.
      .frame_a = { 0.0f, -0.32f, -0.62f },
      .frame_b = { 0.0f, -0.37f, 0.62f },
      .frame_radius = 0.17f * u::m,
      .rider_a = { 0.0f, 0.18f, -0.22f },
      .rider_b = { 0.0f, 0.63f, -0.1f },
      .rider_radius = 0.22f * u::m,
      // The telescopic fork's 11 cm and the twin shocks' 7.
      .front_travel = 0.11f * u::m,
      .rear_travel = 0.07f * u::m,
      .front_sag = 0.035f * u::m,
      .rear_sag = 0.025f * u::m,
      .damping_ratio = 0.4f,
      // Trail tyres: a block tread, neither road nor knobbly.
      .tire_friction = 1.35f,
      .skid_friction = 0.65f,
      .steering_rate = 1.7f / u::s,
      .steering_fade_speed = 10.0f * u::m / u::s,
      .air_steering_rate = 1.0f / u::s,
      .skid_rate = 2.8f / u::s,
      .max_fork_angle = 0.7f * u::rad,
      .max_lean = 0.75f * u::rad,
      .wheelie_limit = 0.35f * u::rad,
      .stoppie_limit = 0.3f * u::rad,
      // Its weight stands some 0.42 m over the axles, so it balances on
      // its back wheel near 50 degrees, the rider sat back.
      .wheelie_lean = 0.2f * u::m,
      .wheelie_pitch = 0.8f * u::rad,
      .wheelie_torque = 300.0f * u::N * u::m,
      .reverse_speed = 1.5f * u::m / u::s,
      .reverse_force_share = 0.25f,
      .front_brake_force = 900.0f * u::N,
      .rear_brake_force = 800.0f * u::N,
      .engine_brake_torque = 20.0f * u::N * u::m,
      .parking_torque = 1500.0f * u::N * u::m,
      .skid_torque = 1200.0f * u::N * u::m,
      .traction_share = 0.9f,
      .wheelie_share = 0.8f,
      .free_rev_torque = 10.0f * u::N * u::m,
      // Its own air and rolling: a seated rider's 0.6 m2 or so, and tyres
      // at a fiftieth of the weight, which give the 5.2 kW engine the
      // CT90's 85 km/h flat out.
      .rolling_drag = 0.015f / u::s,
      .air_drag = 0.0018f / u::m,
    };

    // A classic mountainbike at life size: 26-inch wheels 1.06 m apart, a
    // short suspension fork, and a rigid rear end whose only give is the
    // fat tyre. Its reference point is midway between the axles, with the
    // weight of bike and seated rider 0.55 m above it.
    inline const BikeRig mountain_bike_rig {
      .wheel_radius = 0.335f * u::m,
      .wheel_mass = 3.0f * u::kg,
      .wheel_inertia = 0.12f * u::kg * u::m2,
      .half_wheelbase = 0.53f * u::m,
      .reference_height = -0.55f * u::m,
      .axle_height = -0.55f * u::m,
      .inertia_box = { 0.45f, 1.3f, 1.3f },
      .frame_a = { 0.0f, -0.32f, -0.55f },
      .frame_b = { 0.0f, -0.32f, 0.55f },
      .frame_radius = 0.17f * u::m,
      .rider_a = { 0.0f, 0.0f, -0.18f },
      .rider_b = { 0.0f, 0.62f, 0.05f },
      .rider_radius = 0.24f * u::m,
      // Five centimetres of fork, and the tyres' give behind.
      .front_travel = 0.06f * u::m,
      .rear_travel = 0.03f * u::m,
      .front_sag = 0.02f * u::m,
      .rear_sag = 0.012f * u::m,
      .damping_ratio = 0.5f,
      .tire_friction = 1.3f,
      .skid_friction = 0.62f,
      // Full lock turns it hard at a walk and fades soon after, keeping a
      // flat-out turn within what its narrow tyres hold.
      .steering_rate = 1.7f / u::s,
      .steering_fade_speed = 4.0f * u::m / u::s,
      .air_steering_rate = 1.4f / u::s,
      .skid_rate = 3.2f / u::s,
      .max_fork_angle = 0.7f * u::rad,
      .max_lean = 0.7f * u::rad,
      .wheelie_limit = 0.4f * u::rad,
      .stoppie_limit = 0.3f * u::rad,
      .wheelie_lean = 0.18f * u::m,
      .wheelie_pitch = 0.9f * u::rad,
      .wheelie_torque = 60.0f * u::N * u::m,
      // Walked back, slowly.
      .reverse_speed = 1.2f * u::m / u::s,
      .reverse_force_share = 0.5f,
      .front_brake_force = 420.0f * u::N,
      .rear_brake_force = 260.0f * u::N,
      // A freewheel: next to no drag off the pedals.
      .engine_brake_torque = 0.3f * u::N * u::m,
      .parking_torque = 300.0f * u::N * u::m,
      .skid_torque = 400.0f * u::N * u::m,
      .traction_share = 0.9f,
      .wheelie_share = 0.8f,
      .free_rev_torque = 4.0f * u::N * u::m,
      .rolling_drag = 0.05f / u::s,
      .air_drag = 0.0035f / u::m,
    };

    // Krisjanis's Long John at life size, to its photogrammetric twin
    // (tools/figure/long_john_source.py): a 26 x 2.0 rear wheel and a
    // 20 x 2.0 front 1.894 m apart under a welded box, rigid both ends,
    // single speed at 38/18 with a hub motor in the rear wheel. Its
    // reference point is midway between the axles at the rear one's
    // height; the weight of bike and seated rider, some 115 kg, rides
    // 0.45 m over it, nearer the back. Long and heavy, it turns wide and
    // slowly, leans little, and will not rear up.
    inline const BikeRig long_john_rig {
      .wheel_radius = 0.335f * u::m,
      .front_wheel_radius = 0.253f * u::m,
      .wheel_mass = 4.0f * u::kg,
      .wheel_inertia = 0.16f * u::kg * u::m2,
      .half_wheelbase = 0.947f * u::m,
      .reference_height = -0.45f * u::m,
      .axle_height = -0.45f * u::m,
      .inertia_box = { 0.6f, 1.2f, 2.4f },
      // The box and girder from the rear wheel to the bulkhead, low
      // between the axles; the rider on the saddle over the back wheel.
      .frame_a = { 0.0f, -0.2f, -0.75f },
      .frame_b = { 0.0f, -0.2f, 0.85f },
      .frame_radius = 0.2f * u::m,
      .rider_a = { 0.0f, 0.15f, -0.65f },
      .rider_b = { 0.0f, 0.75f, -0.45f },
      .rider_radius = 0.24f * u::m,
      // Only the tyres give.
      .front_travel = 0.03f * u::m,
      .rear_travel = 0.03f * u::m,
      .front_sag = 0.012f * u::m,
      .rear_sag = 0.014f * u::m,
      .damping_ratio = 0.5f,
      .tire_friction = 1.2f,
      .skid_friction = 0.6f,
      .steering_rate = 1.1f / u::s,
      .steering_fade_speed = 4.0f * u::m / u::s,
      .air_steering_rate = 0.6f / u::s,
      .skid_rate = 1.8f / u::s,
      .max_fork_angle = 0.6f * u::rad,
      .max_lean = 0.45f * u::rad,
      .wheelie_limit = 0.2f * u::rad,
      .stoppie_limit = 0.25f * u::rad,
      .wheelie_lean = 0.1f * u::m,
      .wheelie_pitch = 0.3f * u::rad,
      .wheelie_torque = 0.0f * u::N * u::m,
      .reverse_speed = 1.0f * u::m / u::s,
      .reverse_force_share = 0.5f,
      .front_brake_force = 450.0f * u::N,
      .rear_brake_force = 350.0f * u::N,
      .engine_brake_torque = 0.3f * u::N * u::m,
      .parking_torque = 400.0f * u::N * u::m,
      .skid_torque = 450.0f * u::N * u::m,
      .traction_share = 0.9f,
      .wheelie_share = 0.5f,
      .free_rev_torque = 4.0f * u::N * u::m,
      // Fat tyres and an upright rider behind an open box.
      .rolling_drag = 0.06f / u::s,
      .air_drag = 0.0045f / u::m,
      .gear_ratio = 38.0f / 18.0f,
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
        // At the rear contact.
        newtons_t max_drive_force = 1800.0f * u::N;
        watts_t power = 5200.0f * u::W;
        // The whole bike and its rider.
        kilograms_t mass = 175.0f * u::kg;
        BikeRig rig = trail90_rig;
      };

      // Height of the Trail 90's reference point above flat ground at
      // static sag, trail90_rig.ride_height ().
      static constexpr float ride_height = 0.29f;

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
      meters_t front_extension () const;
      meters_t rear_extension () const;
      // The fork's current angle about the steering axis; positive is right.
      radians_t steer_angle () const;
      // Rear wheel spin relative to the chassis.
      radians_per_second_t wheel_spin_rate () const;
      // How far the rear tyre slides under the locked brake, 0..1.
      float skid () const;
      // The rider's weight back from where they sit, as a share of
      // wheelie_lean: 1 leaning right back, negative forward.
      float rider_lean () const;
      // The chassis' pitch above the ground under it, nose up.
      radians_t pitch () const;
      // On the back wheel alone, the front up off the ground.
      bool wheeling () const;
      // Approach speeds of the hardest collisions in the last step.
      speed_t trunk_hit () const;
      speed_t body_hit () const;
      // A blow to the chassis at `at`, newton-seconds.
      void push (const Vec3& at, const impulse_t& impulse);

    private:
      struct Impl;
      std::unique_ptr<Impl> m_impl;
    };
  }
}

#endif
