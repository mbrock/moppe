#ifndef MOPPE_MOV_BOAT_HH
#define MOPPE_MOV_BOAT_HH

#include <moppe/map/surface.hh>
#include <moppe/mov/rigid_bike.hh>
#include <moppe/mov/trunk_field.hh>

#include <functional>
#include <memory>

namespace moppe::mov {
  // The water a boat floats on, as the game reads it: the surface's
  // height at (x, z), below the ground where it is dry, and how the water
  // moves there, metres a second: a river's current and the waves' to and
  // fro and up and down.
  struct BoatWater {
    std::function<float (float x, float z)> level;
    std::function<Vec3 (float x, float z)> current;
  };

  // A flat-bottomed plywood skiff driven by a long-tail
  // (models/boat.blend): one Box3D body in a private world holding a
  // streamed patch of the height field and the trunks near it, like the
  // helicopter's, so that it runs aground, beaches, and meets trees on
  // the bank.
  //
  // The water acts on it panel by panel over the hull's own lines (the
  // bottom, both sides, and the transom): each submerged panel feels the
  // water's pressure at its depth, which floats, heels, and trims the
  // boat; the stagnation pressure where it pushes into the water and a
  // lesser suction where it draws away, which hold it to its course and
  // lift its rockered bottom up onto the plane once it trims bow-up at
  // speed; and the water's friction along it. All of this is felt
  // relative to the current, so a river carries the boat. At speed the
  // transom runs dry.
  //
  // The long-tail is a small air-cooled engine on a gimbal over the
  // stern with its propeller on a long shaft two metres behind: the
  // helmsman swings the whole of it to steer, the thrust pushing along
  // the shaft at the propeller, which only drives while it is in the
  // water. Its propeller runs at the surface, in the boat's own stern
  // wave, which rises round it as the boat gathers way -- the trick that
  // lets these boats run in a hand's depth of water. Left alone the
  // engine is stopped and the tail tipped up out of the water, as it is
  // left on the shore.
  //
  // Its frame is the world's: x to port (the helmsman's left), y up, z
  // forward, with the origin on the keel a quarter of the way along.
  // Lengths are metres, times seconds, and angles radians.
  class Boat {
  public:
    // models/boat.blend agrees.
    static constexpr float hull_length = 3.93f;
    static constexpr float half_beam = 0.47f;
    static constexpr float transom = -0.98f;
    static constexpr float stem = transom + hull_length;
    static constexpr float pivot[3] = { 0.0f, 0.429f, -0.8982f };
    static constexpr float hub[3] = { 0.0f, -0.05f, -2.6f };
    static constexpr float helm[3] = { 0.0f, 0.2f, -0.3482f };
    // The fore seat, where someone riding along sits.
    static constexpr float fore[3] = { 0.0f, 0.2f, 0.5718f };

    struct State {
      RigidBodyState body;
      int ground_column = 0, ground_row = 0;
      // What the helmsman asks: ahead (positive) or astern, and a turn
      // to the right (positive).
      float drive = 0.0f;
      float turn = 0.0f;
      float boost = 0.0f;
      // The engine's speed as a share of full, how far the tail is swung
      // (positive puts the propeller to starboard and turns the bow to
      // the right) and tipped up, and how far round the propeller has
      // turned.
      float rpm = 0.0f;
      float swing = 0.0f;
      float tilt = 0.0f;
      float propeller_angle = 0.0f;
      // How deep the propeller ran in the last step, and how hard it
      // pushed, newtons.
      float propeller_depth = 0.0f;
      float thrust = 0.0f;
      bool parked = true;
      bool passenger = false;
      bool afloat = false;
      bool grounded = false;
      float still = 0.0f;
      float overturned = 0.0f;
      bool asleep = false;
    };

    explicit Boat (const map::SurfaceGeometry& surface);
    ~Boat ();
    Boat (const Boat&) = delete;
    Boat& operator= (const Boat&) = delete;

    State state () const;
    void restore (const State& state);

    // Floats it upright at `at` on water at `level`, facing `heading`.
    void place (const Vec3& at, const Vec3& heading, float level);
    void set_trunks (const TrunkField* trunks);

    void set_drive (float drive);
    // The governor tied back (0..1): the engine runs past its rated
    // speed and gives some half again its power, as these engines are
    // run on the long-tails, and smokes for it.
    void set_boost (float boost);
    void set_turn (float turn);
    // No one at the helm: the engine stops, the tail is tipped up, and
    // the helmsman's weight is gone from it.
    void set_parked (bool parked);
    // Someone sits on the fore seat (another player riding along), and
    // their 80 kg with them.
    void set_passenger (bool aboard);

    void update (float dt, const BoatWater& water);

    Vec3 position () const;
    Vec3 forward () const;
    Vec3 up () const;
    Vec3 left () const;
    Vec3 velocity () const;
    Vec3 point (const Vec3& local) const;
    // Its whole weight, kilograms: the helmsman's too while someone is
    // aboard (not parked), so that it sits deeper with them in it.
    float mass () const;
    // A blow at `at` in the world, newton-seconds. It wakes one asleep.
    void push (const Vec3& at, const Vec3& impulse);
    bool asleep () const;
    bool afloat () const;
    bool grounded () const;
    Vec3 helm_seat () const {
      return point (Vec3 (helm[0], helm[1], helm[2]));
    }

    float rpm () const;
    float swing () const;
    float tilt () const;
    float propeller_angle () const;
    float thrust () const;
    // Where the propeller's hub is in the world, and how deep under the
    // water it last ran (negative is out of it).
    Vec3 propeller () const;
    float propeller_depth () const;

    // The tail's frame about the pivot, in the boat's: swung by `swing`
    // and tipped up by `tilt`, as a rotation of the boat's own axes.
    static Vec3 tail_point (const Vec3& local, float swing, float tilt);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
  };
}

#endif
