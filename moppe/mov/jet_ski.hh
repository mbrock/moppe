#ifndef MOPPE_MOV_JET_SKI_HH
#define MOPPE_MOV_JET_SKI_HH

#include <moppe/map/surface.hh>
#include <moppe/mov/hull_water.hh>
#include <moppe/mov/rigid_bike.hh>
#include <moppe/mov/trunk_field.hh>

#include <memory>

namespace moppe::mov {
  // A sit-down jet ski of the early nineties (models/jet_ski.blend): a
  // glass-fibre runabout 2.9 m long and a metre in the beam, a 650 cc
  // two-stroke twin of 37 kW under its saddle turning a jet pump. Like
  // the skiff (mov/boat.hh) it is one Box3D body in a private world
  // holding a streamed patch of the height field and the trunks near
  // it, so that it runs aground, beaches, and meets trees on the bank,
  // and the water acts on it panel by panel (mov/hull_water.hh) over
  // its own lines: a vee bottom that lifts it onto the plane within a
  // few lengths, hard chines that grip in a turn, and a deck that
  // closes it, so that it shrugs off a wave over the bow and floats
  // however it lies.
  //
  // The pump draws water through a grate under the hull and throws it
  // out of a nozzle on the transom: it pushes only while the grate is
  // in the water, so the engine races and the push is gone while the
  // hull is in the air off a wave. The bars swing the nozzle, and the
  // jet steers it: with the throttle shut there is next to nothing to
  // steer with, as on the real thing. Astern, a bucket dropped over the
  // nozzle turns a part of the jet forward. The rider's weight goes
  // with them: they hold it upright with their body and lean it into a
  // turn, as far as a body reaches.
  //
  // Left alone its engine is stopped (the lanyard is the rider's), and
  // it floats as it was left. Its frame is the world's: x to port (the
  // rider's left), y up, z forward, with the origin on the keel under
  // the saddle's front. Lengths are metres, times seconds, and angles
  // radians.
  class JetSki {
  public:
    // models/jet_ski.blend agrees.
    static constexpr meters_t hull_length = 2.9f * u::m;
    static constexpr meters_t half_beam = 0.52f * u::m;
    static constexpr meters_t transom = -1.05f * u::m;
    static constexpr meters_t stem = transom + hull_length;
    // The pump's intake grate and its nozzle, the saddle where the rider
    // sits, and the foot of the steering column.
    static inline const displacement_t intake =
      displacement (Vec3 (0.0f, 0.03f, -0.55f));
    static inline const displacement_t nozzle =
      displacement (Vec3 (0.0f, 0.13f, -1.09f));
    static inline const displacement_t saddle =
      displacement (Vec3 (0.0f, 0.66f, -0.30f));
    static inline const displacement_t column =
      displacement (Vec3 (0.0f, 0.80f, 0.42f));

    struct State {
      RigidBodyState body;
      int ground_column = 0, ground_row = 0;
      // What the rider asks: ahead (positive) or astern, and a turn to
      // the right (positive).
      control_signal_t drive {};
      control_signal_t turn {};
      // The engine's speed as a share of full, how far the nozzle is
      // swung (positive turns the bow to the right), how far the rider
      // leans (positive to the right), and how far round the impeller
      // has turned.
      proportion_t rpm {};
      radians_t steer {};
      radians_t lean {};
      radians_t impeller_angle {};
      // How deep the intake ran in the last step, and how hard the jet
      // pushed, newtons.
      meters_t intake_depth {};
      newtons_t thrust {};
      bool parked = true;
      bool afloat = false;
      bool grounded = false;
      seconds_t still {};
      seconds_t overturned {};
      bool asleep = false;
    };

    explicit JetSki (const map::SurfaceGeometry& surface);
    ~JetSki ();
    JetSki (const JetSki&) = delete;
    JetSki& operator= (const JetSki&) = delete;

    State state () const;
    void restore (const State& state);

    // Floats it upright at `at` on water at `level`, facing `heading`.
    void place (position_t at, direction_t heading, meters_t level);
    void set_trunks (const TrunkField* trunks);

    void set_drive (control_signal_t drive);
    void set_turn (control_signal_t turn);
    // No one on it: the engine stops, and the rider's weight is gone
    // from it.
    void set_parked (bool parked);

    void update (seconds_t dt, const BoatWater& water);

    position_t position () const;
    direction_t forward () const;
    direction_t up () const;
    direction_t left () const;
    velocity_t velocity () const;
    position_t point (const displacement_t& local) const;
    // Its whole weight, kilograms: the rider's too while someone is on
    // it (not parked).
    kilograms_t mass () const;
    // A blow at `at` in the world, newton-seconds. It wakes one asleep.
    void push (position_t at, const impulse_t& impulse);
    bool asleep () const;
    bool afloat () const;
    bool grounded () const;
    position_t seat () const {
      return point (saddle);
    }

    // The engine's speed, a share of full.
    proportion_t rpm () const;
    radians_t steer () const;
    radians_t lean () const;
    radians_t impeller_angle () const;
    newtons_t thrust () const;
    // Where the jet leaves the nozzle, and the way it is thrown, in the
    // world; and how deep under the water the intake last ran (negative
    // is out of it).
    position_t jet () const;
    direction_t jet_direction () const;
    meters_t intake_depth () const;

    // The most the jet pushes, newtons, standing.
    static newtons_t static_thrust ();

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
  };
}

#endif
