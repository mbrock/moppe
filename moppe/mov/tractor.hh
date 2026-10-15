#ifndef MOPPE_MOV_TRACTOR_HH
#define MOPPE_MOV_TRACTOR_HH

#include <moppe/map/surface.hh>
#include <moppe/mov/rigid_bike.hh>
#include <moppe/mov/trunk_field.hh>

#include <array>
#include <memory>

namespace moppe::mov {
  // The tractor, a Box3D assemblage like the jeep: a heavy chassis of
  // box hulls on two big rear wheels and two small front ones, each a
  // sphere on a wheel joint whose short stiff spring stands in for the
  // tyre's give. The rear pair drive it, slowly and with a great deal of
  // pull, its engine governed to a walking pace in the lowest gear and
  // some thirty kilometres an hour at the most; the front pair steer it.
  // It stands in a private world holding a streamed patch of the height
  // field and the trunks near it; one left on its side or back is set on
  // its wheels again, and a parked one locks its wheels and, once still,
  // is not stepped at all.
  //
  // Its frame is the world's: x to the driver's left, y up, z forward.
  // Lengths are metres, times seconds, and angles radians.
  class Tractor {
  public:
    // Front left, front right, rear left, rear right.
    static constexpr int wheel_count = 4;
    // The origin is on the centre line, midway between the axles and
    // level with the ground the tractor stands on; each wheel's centre is
    // its radius above that, this far to the side, and ahead of or
    // behind it. models/tractor.blend agrees.
    static constexpr float rear_radius = 0.66f;
    static constexpr float rear_track = 0.76f;
    static constexpr float rear_axle = -0.80f;
    static constexpr float front_radius = 0.37f;
    static constexpr float front_track = 0.66f;
    static constexpr float front_axle = 1.10f;
    // The one seat, on the centre line over the gearbox.
    static constexpr float seat[3] = { 0.0f, 1.16f, -1.0f };
    // The mouth of the exhaust stack, on the right.
    static constexpr float exhaust_mouth[3] = { -0.17f, 2.06f, 0.92f };

    static float wheel_radius (int wheel) {
      return wheel < 2 ? front_radius : rear_radius;
    }

    struct State {
      RigidBodyState chassis;
      std::array<RigidBodyState, wheel_count> wheels;
      int ground_column = 0, ground_row = 0;
      std::array<bool, wheel_count> touching {};
      std::array<float, wheel_count> load {};
      // How far each pair of wheels has rolled round, for drawing them.
      float front_spin = 0.0f;
      float rear_spin = 0.0f;
      float drive = 0.0f;
      float turn = 0.0f;
      bool brake = false;
      bool parked = true;
      // How long it has stood still, and lain on its side or back.
      float still = 0.0f;
      float overturned = 0.0f;
      bool asleep = false;
    };

    explicit Tractor (const map::SurfaceGeometry& surface);
    ~Tractor ();
    Tractor (const Tractor&) = delete;
    Tractor& operator= (const Tractor&) = delete;

    State state () const;
    // Rebuilds the assemblage from a snapshot, in one canonical order.
    void restore (const State& state);

    // Stands the tractor on the ground at `at`, facing along `heading`.
    void place (const Vec3& at, const Vec3& heading);
    // The obstacles its chassis meets; rebuilt into its world when the
    // field changes.
    void set_trunks (const TrunkField* trunks);

    // Forward (positive) or back, -1..1; right (positive) or left.
    void set_drive (float drive);
    void set_turn (float turn);
    void set_brake (bool brake);
    void set_parked (bool parked);

    void update (float dt, float water_level);

    Vec3 position () const;
    Vec3 forward () const;
    Vec3 up () const;
    Vec3 left () const;
    Vec3 velocity () const;
    bool grounded () const;
    bool asleep () const;
    // How hard the engine is asked to work, 0..1: the throttle, or a
    // little at idle while someone sits at the wheel.
    float throttle () const;
    // A point given in the tractor's frame, in the world.
    Vec3 point (const Vec3& local) const;
    Vec3 driver_seat () const {
      return point (Vec3 (seat[0], seat[1], seat[2]));
    }
    Vec3 exhaust () const {
      return point (
        Vec3 (exhaust_mouth[0], exhaust_mouth[1], exhaust_mouth[2]));
    }

    // How far a wheel hangs below where it stands; negative when its
    // spring is pressed further.
    float wheel_drop (int wheel) const;
    // The front wheels' turn, positive to the right.
    float steer () const;
    float front_spin () const;
    float rear_spin () const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
  };
}

#endif
