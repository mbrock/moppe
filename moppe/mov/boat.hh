#ifndef MOPPE_MOV_BOAT_HH
#define MOPPE_MOV_BOAT_HH

#include <moppe/map/surface.hh>
#include <moppe/mov/hull_water.hh>
#include <moppe/mov/rigid_bike.hh>
#include <moppe/mov/trunk_field.hh>

#include <memory>

namespace moppe::mov {
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
    static constexpr meters_t hull_length = 3.93f * u::m;
    static constexpr meters_t half_beam = 0.47f * u::m;
    static constexpr meters_t transom = -0.98f * u::m;
    static constexpr meters_t stem = transom + hull_length;
    static inline const displacement_t pivot =
      displacement (Vec3 (0.0f, 0.429f, -0.8982f));
    static inline const displacement_t hub =
      displacement (Vec3 (0.0f, -0.05f, -2.6f));
    static inline const displacement_t helm =
      displacement (Vec3 (0.0f, 0.2f, -0.3482f));
    // The fore seat, where someone riding along sits.
    static inline const displacement_t fore =
      displacement (Vec3 (0.0f, 0.2f, 0.5718f));

    struct State {
      RigidBodyState body;
      int ground_column = 0, ground_row = 0;
      // What the helmsman asks: ahead (positive) or astern, and a turn
      // to the right (positive).
      control_signal_t drive {};
      control_signal_t turn {};
      control_signal_t boost {};
      // The engine's speed as a share of full, how far the tail is swung
      // (positive puts the propeller to starboard and turns the bow to
      // the right) and tipped up, and how far round the propeller has
      // turned.
      proportion_t rpm {};
      radians_t swing {};
      radians_t tilt {};
      radians_t propeller_angle {};
      // How deep the propeller ran in the last step, and how hard it
      // pushed, newtons.
      meters_t propeller_depth {};
      newtons_t thrust {};
      bool parked = true;
      bool passenger = false;
      bool afloat = false;
      bool grounded = false;
      seconds_t still {};
      seconds_t overturned {};
      bool asleep = false;
    };

    explicit Boat (const map::SurfaceGeometry& surface);
    ~Boat ();
    Boat (const Boat&) = delete;
    Boat& operator= (const Boat&) = delete;

    State state () const;
    void restore (const State& state);

    // Floats it upright at `at` on water at `level`, facing `heading`.
    void place (const Vec3& at, const Vec3& heading, meters_t level);
    void set_trunks (const TrunkField* trunks);

    void set_drive (control_signal_t drive);
    // The governor tied back (0..1): the engine runs past its rated
    // speed and gives some half again its power, as these engines are
    // run on the long-tails, and smokes for it.
    void set_boost (control_signal_t boost);
    void set_turn (control_signal_t turn);
    // No one at the helm: the engine stops, the tail is tipped up, and
    // the helmsman's weight is gone from it.
    void set_parked (bool parked);
    // Someone sits on the fore seat (another player riding along), and
    // their 80 kg with them.
    void set_passenger (bool aboard);

    void update (seconds_t dt, const BoatWater& water);

    Vec3 position () const;
    Vec3 forward () const;
    Vec3 up () const;
    Vec3 left () const;
    Vec3 velocity () const;
    Vec3 point (const Vec3& local) const;
    // Its whole weight, kilograms: the helmsman's too while someone is
    // aboard (not parked), so that it sits deeper with them in it.
    kilograms_t mass () const;
    // A blow at `at` in the world, newton-seconds. It wakes one asleep.
    void push (const Vec3& at, const impulse_t& impulse);
    bool asleep () const;
    bool afloat () const;
    bool grounded () const;
    Vec3 helm_seat () const {
      return point (displacement_value (helm));
    }

    // The engine's speed, a share of its rated speed.
    proportion_t rpm () const;
    radians_t swing () const;
    radians_t tilt () const;
    radians_t propeller_angle () const;
    newtons_t thrust () const;
    // Where the propeller's hub is in the world, and how deep under the
    // water it last ran (negative is out of it).
    position_t propeller () const;
    meters_t propeller_depth () const;

    // The tail's frame about the pivot, in the boat's: swung by `swing`
    // and tipped up by `tilt`, as a rotation of the boat's own axes.
    static displacement_t
    tail_point (const displacement_t& local, radians_t swing, radians_t tilt);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
  };
}

#endif
