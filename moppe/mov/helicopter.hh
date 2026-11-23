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
    static constexpr meters_t rotor_radius = 4.8f * u::m;
    static constexpr meters_t skid = 1.05f * u::m;
    static inline const displacement_t seat =
      displacement (Vec3 (-0.33f, 0.92f, 0.10f));

    struct State {
      RigidBodyState body;
      int ground_column = 0, ground_row = 0;
      // How fast the rotor turns, as a share of flying speed, and how far
      // round each rotor has turned, for drawing them.
      proportion_t rotor {};
      radians_t rotor_angle {};
      radians_t tail_angle {};
      control_signal_t drive {};
      control_signal_t turn {};
      control_signal_t lift {};
      force_t load_pull = force (Vec3 ());
      meters_t load_hang {};
      bool parked = true;
      bool grounded = true;
      seconds_t still {};
      seconds_t overturned {};
      bool asleep = false;
    };

    explicit Helicopter (const map::SurfaceGeometry& surface);
    ~Helicopter ();
    Helicopter (const Helicopter&) = delete;
    Helicopter& operator= (const Helicopter&) = delete;

    State state () const;
    void restore (const State& state);

    // Stands it on its skids on the ground at `at`, facing `heading`.
    void place (position_t at, direction_t heading);
    // Moves it rigidly in the plane without disturbing its motion or sleep.
    void shift (plan_displacement_t delta);
    void set_trunks (const TrunkField* trunks);

    // Ahead (positive) or back, -1..1; turn right (positive) or left;
    // climb (positive) or descend.
    void set_drive (control_signal_t drive);
    void set_turn (control_signal_t turn);
    void set_lift (control_signal_t lift);
    // No one at the controls: the rotor runs down.
    void set_parked (bool parked);
    // What hangs under it: the pull, newtons, of a line on its hook (the
    // caller applies it with push), which the flight computer lifts and
    // leans against as well as its own weight, and how far below the
    // skids the load hangs, which it keeps clear of the ground when under
    // way. Zero for nothing.
    void set_load (const force_t& pull, meters_t hang);

    void update (seconds_t dt, meters_t water_level);

    position_t position () const;
    direction_t forward () const;
    direction_t up () const;
    direction_t left () const;
    velocity_t velocity () const;
    bool grounded () const;
    bool asleep () const;
    // Its whole weight, kilograms.
    kilograms_t mass () const;
    // A blow at `at` in the world, newton-seconds: another machine
    // bumping into it, or someone hauling on it. It wakes one asleep.
    void push (position_t at, const impulse_t& impulse);
    position_t point (const displacement_t& local) const;
    position_t pilot_seat () const {
      return point (seat);
    }
    // Height of the skids over the ground beneath them.
    meters_t altitude () const;
    // How fast the rotor turns, a share of flying speed.
    proportion_t rotor () const;
    radians_t rotor_angle () const;
    radians_t tail_angle () const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
  };
}

#endif
