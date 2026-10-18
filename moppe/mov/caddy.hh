#ifndef MOPPE_MOV_CADDY_HH
#define MOPPE_MOV_CADDY_HH

#include <moppe/map/surface.hh>
#include <moppe/mov/rigid_bike.hh>
#include <moppe/mov/trunk_field.hh>

#include <array>
#include <memory>

namespace moppe::mov {
  // The Caddy, the Golf-faced pickup, as a Box3D assemblage like the
  // jeep: a chassis of boxes (the body, the cab, the hardtop over the
  // bed and the cab's roof) on four sphere wheels, each on a wheel joint
  // that springs it, spins it for the engine and the brakes, and, at the
  // front, steers it. It stands in a private world holding a streamed
  // patch of the height field and the trunks near it. It is a road car:
  // the front wheels pull it, each within what its load lets it put
  // down, from a small petrol engine good for some 140 km/h, on soft
  // springs and narrow tyres that grip less than the jeep's off the
  // road. Left on its side or back it is set on its wheels again; parked,
  // it locks its wheels and, once still, is not stepped at all.
  //
  // Its frame is the world's: x to the driver's left, y up, z forward.
  // Lengths are metres, times seconds, and angles radians.
  class Caddy {
  public:
    // Front left, front right, rear left, rear right.
    static constexpr int wheel_count = 4;
    static constexpr float wheel_radius = 0.288f;
    // The origin is on the centre line, midway between the axles and at
    // their height when the Caddy stands on level ground; each wheel's
    // centre is a track to the side of it and an axle ahead of or behind
    // it. models/caddy.blend agrees.
    static constexpr float front_track = 0.695f;
    static constexpr float rear_track = 0.685f;
    static constexpr float axle = 1.3125f;
    // The driver's seat, on the left; the passenger's mirrors it.
    static constexpr float seat[3] = { 0.36f, 0.282f, -0.08f };

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

    explicit Caddy (const map::SurfaceGeometry& surface);
    ~Caddy ();
    Caddy (const Caddy&) = delete;
    Caddy& operator= (const Caddy&) = delete;

    State state () const;
    // Rebuilds the assemblage from a snapshot, in one canonical order.
    void restore (const State& state);

    // Stands the Caddy on the ground at `at`, facing along `heading`.
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
    // A point given in the Caddy's frame, in the world.
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
    // How hard the engine is asked to pull, 0..1, for its sound and smoke.
    float throttle () const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
  };
}

#endif
