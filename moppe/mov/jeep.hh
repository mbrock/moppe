#ifndef MOPPE_MOV_JEEP_HH
#define MOPPE_MOV_JEEP_HH

#include <moppe/map/surface.hh>
#include <moppe/mov/trunk_field.hh>

#include <array>

namespace moppe::mov {
  // The jeep: one rigid body on four sprung wheels. Each wheel is a ray
  // down its suspension strut onto the heightfield; where it finds the
  // ground the spring pushes the body up, and the tyre grips sideways and
  // drives or brakes along its heading, as far as its load lets it. The
  // body's corners and its roll cage hold it off the ground when the
  // wheels cannot, trunks stop it, and a jeep left on its back rights
  // itself. Lengths are metres, times seconds, and angles radians.
  class Jeep {
  public:
    // Front left, front right, rear left, rear right.
    static constexpr int wheel_count = 4;
    static constexpr float wheel_radius = 0.40f;
    // Each wheel's centre in the body's frame (x right, y up, z forward)
    // when the jeep stands on level ground; models/jeep.blend agrees.
    static constexpr float track = 0.80f;
    static constexpr float axle = 1.20f;
    // Where the driver and the passenger sit, in the same frame.
    static constexpr float seat[3] = { 0.33f, 0.43f, -0.16f };
    Vec3 driver_seat () const {
      return point (Vec3 (-seat[0], seat[1], seat[2]));
    }
    Vec3 passenger_seat () const {
      return point (Vec3 (seat[0], seat[1], seat[2]));
    }

    struct State {
      // The origin: on the centre line, midway between the axles, at
      // their standing height.
      Vec3 position {};
      Vec3 right { 1, 0, 0 };
      Vec3 up { 0, 1, 0 };
      Vec3 forward { 0, 0, 1 };
      Vec3 velocity {};
      Vec3 spin {};
      // How far each wheel hangs below where it stands; negative when
      // its spring is pressed further.
      std::array<float, wheel_count> drop {};
      std::array<bool, wheel_count> touching {};
      // The front wheels' turn, positive to the right, and how far the
      // wheels have rolled round.
      float steer = 0.0f;
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

    const State& state () const noexcept {
      return m_state;
    }
    void restore (const State& state) {
      m_state = state;
    }

    // Stands the jeep on the ground at `at`, facing along `heading`.
    void place (const Vec3& at,
                const Vec3& heading,
                const map::SurfaceGeometry& surface);

    // Forward (positive) or back, -1..1; right (positive) or left.
    void set_drive (float drive) {
      m_state.drive = drive;
    }
    void set_turn (float turn) {
      m_state.turn = turn;
    }
    void set_brake (bool brake) {
      m_state.brake = brake;
    }
    // A parked jeep holds its brakes and, once still, stays put.
    void set_parked (bool parked);

    void update (float dt,
                 const map::SurfaceGeometry& surface,
                 float water_level,
                 const TrunkField* trunks = nullptr);

    Vec3 position () const {
      return m_state.position;
    }
    Vec3 forward () const {
      return m_state.forward;
    }
    Vec3 up () const {
      return m_state.up;
    }
    Vec3 velocity () const {
      return m_state.velocity;
    }
    bool grounded () const;
    // A point given in the body's frame, in the world.
    Vec3 point (const Vec3& local) const;

  private:
    void step (float dt,
               const map::SurfaceGeometry& surface,
               float water_level,
               const TrunkField* trunks);
    void push (const Vec3& impulse, const Vec3& at);

    State m_state;
  };
}

#endif
