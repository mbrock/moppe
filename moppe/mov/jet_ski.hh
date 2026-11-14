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
    static constexpr float hull_length = 2.9f;
    static constexpr float half_beam = 0.52f;
    static constexpr float transom = -1.05f;
    static constexpr float stem = transom + hull_length;
    // The pump's intake grate and its nozzle, the saddle where the rider
    // sits, and the foot of the steering column.
    static constexpr float intake[3] = { 0.0f, 0.03f, -0.55f };
    static constexpr float nozzle[3] = { 0.0f, 0.13f, -1.09f };
    static constexpr float saddle[3] = { 0.0f, 0.66f, -0.30f };
    static constexpr float column[3] = { 0.0f, 0.80f, 0.42f };

    struct State {
      RigidBodyState body;
      int ground_column = 0, ground_row = 0;
      // What the rider asks: ahead (positive) or astern, and a turn to
      // the right (positive).
      float drive = 0.0f;
      float turn = 0.0f;
      // The engine's speed as a share of full, how far the nozzle is
      // swung (positive turns the bow to the right), how far the rider
      // leans (positive to the right), and how far round the impeller
      // has turned.
      float rpm = 0.0f;
      float steer = 0.0f;
      float lean = 0.0f;
      float impeller_angle = 0.0f;
      // How deep the intake ran in the last step, and how hard the jet
      // pushed, newtons.
      float intake_depth = 0.0f;
      float thrust = 0.0f;
      bool parked = true;
      bool afloat = false;
      bool grounded = false;
      float still = 0.0f;
      float overturned = 0.0f;
      bool asleep = false;
    };

    explicit JetSki (const map::SurfaceGeometry& surface);
    ~JetSki ();
    JetSki (const JetSki&) = delete;
    JetSki& operator= (const JetSki&) = delete;

    State state () const;
    void restore (const State& state);

    // Floats it upright at `at` on water at `level`, facing `heading`.
    void place (const Vec3& at, const Vec3& heading, float level);
    void set_trunks (const TrunkField* trunks);

    void set_drive (float drive);
    void set_turn (float turn);
    // No one on it: the engine stops, and the rider's weight is gone
    // from it.
    void set_parked (bool parked);

    void update (float dt, const BoatWater& water);

    Vec3 position () const;
    Vec3 forward () const;
    Vec3 up () const;
    Vec3 left () const;
    Vec3 velocity () const;
    Vec3 point (const Vec3& local) const;
    // Its whole weight, kilograms: the rider's too while someone is on
    // it (not parked).
    float mass () const;
    // A blow at `at` in the world, newton-seconds. It wakes one asleep.
    void push (const Vec3& at, const Vec3& impulse);
    bool asleep () const;
    bool afloat () const;
    bool grounded () const;
    Vec3 seat () const {
      return point (Vec3 (saddle[0], saddle[1], saddle[2]));
    }

    float rpm () const;
    float steer () const;
    float lean () const;
    float impeller_angle () const;
    float thrust () const;
    // Where the jet leaves the nozzle, and the way it is thrown, in the
    // world; and how deep under the water the intake last ran (negative
    // is out of it).
    Vec3 jet () const;
    Vec3 jet_direction () const;
    float intake_depth () const;

    // The most the jet pushes, newtons, standing.
    static float static_thrust ();

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
  };
}

#endif
