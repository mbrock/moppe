#ifndef MOPPE_MOV_JEEP_HH
#define MOPPE_MOV_JEEP_HH

#include <moppe/map/surface.hh>
#include <moppe/mov/rigid_bike.hh>
#include <moppe/mov/trunk_field.hh>

#include <array>
#include <memory>

namespace moppe::mov {
  // The jeep, a Box3D assemblage like the rigid bike: a chassis of two
  // boxes (the tub and the roll cage) on four sphere wheels, each on a
  // wheel joint that springs it, spins it for the engine and the brakes,
  // and, at the front, steers it. It stands in a private world holding a
  // streamed patch of the height field and the trunks near it. All four
  // wheels drive, each within what its load lets it put down; a jeep left
  // on its side or back is set on its wheels again; a parked one locks
  // its wheels and, once still, is not stepped at all.
  //
  // Its frame is the world's: x to the driver's left, y up, z forward.
  // Lengths are metres, times seconds, and angles radians.
  class Jeep {
  public:
    // Front left, front right, rear left, rear right.
    static constexpr int wheel_count = 4;
    static constexpr float wheel_radius = 0.40f;
    // The origin is on the centre line, midway between the axles and at
    // their height when the jeep stands on level ground; each wheel's
    // centre is this far to the side of it and ahead of or behind it.
    // models/jeep.blend agrees.
    static constexpr float track = 0.80f;
    static constexpr float axle = 1.20f;
    // The driver's seat, on the left; the passenger's mirrors it.
    static constexpr float seat[3] = { 0.33f, 0.43f, -0.16f };

    struct State {
      RigidBodyState chassis;
      std::array<RigidBodyState, wheel_count> wheels;
      int ground_column = 0, ground_row = 0;
      std::array<bool, wheel_count> touching {};
      std::array<float, wheel_count> load {};
      // How far the wheels have rolled round, for drawing them.
      float wheel_spin = 0.0f;
      float drive = 0.0f;
      float turn = 0.0f;
      bool brake = false;
      bool parked = true;
      // How long it has stood still, and lain on its side or back.
      float still = 0.0f;
      float overturned = 0.0f;
      bool asleep = false;
    };

    explicit Jeep (const map::SurfaceGeometry& surface);
    ~Jeep ();
    Jeep (const Jeep&) = delete;
    Jeep& operator= (const Jeep&) = delete;

    State state () const;
    // Rebuilds the assemblage from a snapshot, in one canonical order.
    void restore (const State& state);

    // Stands the jeep on the ground at `at`, facing along `heading`.
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
    // Its whole weight, kilograms.
    float mass () const;
    // A blow at `at` in the world, newton-seconds: another machine
    // bumping into it, or someone hauling on it. It wakes one asleep.
    void push (const Vec3& at, const Vec3& impulse);
    // A point given in the jeep's frame, in the world.
    Vec3 point (const Vec3& local) const;
    Vec3 driver_seat () const {
      return point (Vec3 (seat[0], seat[1], seat[2]));
    }
    Vec3 passenger_seat () const {
      return point (Vec3 (-seat[0], seat[1], seat[2]));
    }

    // How far a wheel hangs below where it stands; negative when its
    // spring is pressed further.
    float wheel_drop (int wheel) const;
    // The front wheels' turn, positive to the right.
    float steer () const;
    float wheel_spin () const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
  };
}

#endif
