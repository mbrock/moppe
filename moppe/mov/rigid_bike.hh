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
      bool wading = false;
      // Nobody aboard: both wheels are locked, and once the bike has come
      // to rest on the ground it is held there, as on a stand.
      bool parked = false;
    };

    // A motocross as a Box3D assemblage: a chassis carrying the frame and
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
      };

      // Height of the reference point above flat ground at static sag.
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

      // Readings of a snapshot, so a checkpoint's coarse pose can be derived
      // from it without a live world.
      static Vec3 reference_of (const RigidBikeState& state);
      static Vec3 forward_of (const RigidBikeState& state);
      static Vec3 up_of (const RigidBikeState& state);

      // Moves a whole snapshot rigidly: the reference point to `reference`,
      // the chassis turned about the vertical so its heading is `heading`,
      // and the chassis moving at `velocity`.
      static RigidBikeState repose (const RigidBikeState& state,
                                    const Vec3& reference,
                                    const Vec3& heading,
                                    const Vec3& velocity);

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
      // Approach speeds of the hardest collisions in the last step.
      float trunk_hit () const;
      float body_hit () const;

    private:
      struct Impl;
      std::unique_ptr<Impl> m_impl;
    };
  }
}

#endif
