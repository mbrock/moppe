#ifndef MOPPE_MOV_HELICOPTER_HH
#define MOPPE_MOV_HELICOPTER_HH

#include <moppe/map/surface.hh>
#include <moppe/mov/rigid_bike.hh>
#include <moppe/mov/trunk_field.hh>

#include <memory>

namespace moppe::mov {
  // A small helicopter, one Box3D body (the cabin, the tail boom, and the
  // skids it stands on) in a private world holding a streamed patch of
  // the height field and the trunks near it, like the jeep's.
  //
  // It flies the way a helicopter does -- the rotor's thrust is along the
  // body's up, so it goes where it is tilted -- but a flight computer
  // flies the attitude and the collective for the pilot, who asks only
  // for a speed ahead, a turn, and a climb or descent. Left alone it
  // holds its height and comes to a hover; low and fast it keeps clear of
  // the ground ahead; descending it slows for the touchdown. The rotor
  // spools up when someone takes the seat and runs down when they leave,
  // and a helicopter left on its side is set on its skids again.
  //
  // Its frame is the world's: x to the pilot's left, y up, z forward,
  // with the origin under the mast, level with the bottom of the skids.
  // Lengths are metres, times seconds, and angles radians.
  class Helicopter {
  public:
    // models/helicopter.blend agrees.
    static constexpr float rotor_radius = 4.8f;
    static constexpr float skid = 1.05f;
    static constexpr float seat[3] = { -0.33f, 0.92f, 0.10f };

    struct State {
      RigidBodyState body;
      int ground_column = 0, ground_row = 0;
      // How fast the rotor turns, as a share of flying speed, and how far
      // round each rotor has turned, for drawing them.
      float rotor = 0.0f;
      float rotor_angle = 0.0f;
      float tail_angle = 0.0f;
      float drive = 0.0f;
      float turn = 0.0f;
      float lift = 0.0f;
      Vec3 load_pull {};
      float load_hang = 0.0f;
      bool parked = true;
      bool grounded = true;
      float still = 0.0f;
      float overturned = 0.0f;
      bool asleep = false;
    };

    explicit Helicopter (const map::SurfaceGeometry& surface);
    ~Helicopter ();
    Helicopter (const Helicopter&) = delete;
    Helicopter& operator= (const Helicopter&) = delete;

    State state () const;
    void restore (const State& state);

    // Stands it on its skids on the ground at `at`, facing `heading`.
    void place (const Vec3& at, const Vec3& heading);
    void set_trunks (const TrunkField* trunks);

    // Ahead (positive) or back, -1..1; turn right (positive) or left;
    // climb (positive) or descend.
    void set_drive (float drive);
    void set_turn (float turn);
    void set_lift (float lift);
    // No one at the controls: the rotor runs down.
    void set_parked (bool parked);
    // What hangs under it: the pull, newtons, of a line on its hook (the
    // caller applies it with push), which the flight computer lifts and
    // leans against as well as its own weight, and how far below the
    // skids the load hangs, which it keeps clear of the ground when under
    // way. Zero for nothing.
    void set_load (const Vec3& pull, float hang);

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
    Vec3 point (const Vec3& local) const;
    Vec3 pilot_seat () const {
      return point (Vec3 (seat[0], seat[1], seat[2]));
    }
    // Height of the skids over the ground beneath them.
    float altitude () const;
    float rotor () const;
    float rotor_angle () const;
    float tail_angle () const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
  };
}

#endif
