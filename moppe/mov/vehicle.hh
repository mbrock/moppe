
#ifndef MOPPE_VEHICLE_HH
#define MOPPE_VEHICLE_HH

#include <moppe/gfx/math.hh>
#include <moppe/map/surface.hh>
#include <moppe/mov/rigid_bike.hh>
#include <moppe/mov/trunk_field.hh>

#include <algorithm>
#include <memory>

namespace moppe {
  namespace mov {
    using namespace moppe::map;

    // A bike and its rider: the rigid assemblage of rigid_bike.hh, ridden.
    // The rider's controls are smoothed here, the jump jets meter their
    // charge here, and the readings the game scores and draws by (landings,
    // falls, the wheel's roll and the crank's turn) are gathered here.
    class Vehicle {
    public:
      struct State {
        // The coarse pose, derived from the assemblage; restoring a state
        // whose coarse pose was edited moves the assemblage rigidly to
        // match.
        position_t position {};
        velocity_t velocity {};
        Vec3 heading {};
        radians_t yaw {};
        radians_t yaw_target {};
        radians_t wheel_spin {};
        bool boost_flight {};
        control_signal_t thrust {};
        float boost_input {};
        float boost_drive {};
        float boost_level {};
        float boost_charge {};
        seconds_t boost_recharge_delay {};
        meters_t water_level {};
        seconds_t airborne_time {};
        speed_t impact {};
        meters_t fall_top {};
        meters_t fall_drop {};
        RigidBikeState rigid {};
        bool parked {};
        float skid {};
        float wheelie {};
        radians_t crank {};
      };

      // Stands the bike at rest on the ground under `position`, facing
      // `orientation` degrees from +z toward +x. max_thrust caps the
      // wheel force (launch punch); power caps force * speed, so
      // acceleration tapers like a real engine.
      Vehicle (position_t position,
               degrees_t orientation,
               const SurfaceGeometry& surface,
               newtons_t max_thrust,
               watts_t power,
               kilograms_t mass,
               const BikeRig& rig = trail90_rig);
      ~Vehicle ();

      void update (seconds_t dt);

      State state () const;
      void restore (const State& state);

      // The throttle is a normalized control signal in [-1, 1] that
      // commands the engine's force capability.
      void set_thrust (control_signal_t thrust) {
        m_thrust = thrust;
      }

      control_signal_t thrust () const {
        return m_thrust;
      }

      void set_yaw (degrees_t degrees) {
        m_yaw_target = degrees;
      }

      // Continuous jump jets.  boost is 0..1; drive is -1..1 and tilts
      // the jet backward/vertical/forward to match the driving stick.
      void set_boost (float boost, float drive);
      void replenish_boost (float amount) {
        m_boost_charge = std::min (1.0f, m_boost_charge + amount);
      }

      // The rear brake stamped on, 0..1: the rear wheel locks and slides,
      // and the handlebar swings the tail round (RigidBikeControls::skid).
      void set_skid (float skid) {
        m_skid = std::clamp (skid, 0.0f, 1.0f);
      }
      // How far the rear tyre is sliding under the locked brake, 0..1.
      float skid () const {
        return m_rigid->skid ();
      }
      // The rider leans back for a wheelie, 0..1
      // (RigidBikeControls::wheelie).
      void set_wheelie (float wheelie) {
        m_wheelie = std::clamp (wheelie, 0.0f, 1.0f);
      }
      // On the back wheel alone.
      bool wheeling () const {
        return m_rigid->wheeling ();
      }
      bool front_down () const {
        return m_rigid->front_contact ();
      }
      // How far back the rider's weight is, a share of how far it goes
      // (negative forward).
      float rider_lean () const {
        return m_rigid->rider_lean ();
      }
      // The chassis' pitch above the ground under it, nose up.
      radians_t pitch () const {
        return m_rigid->pitch ();
      }
      // Springs the bike up off the ground by `speed` over its next
      // update: a bunny hop. Only a bike on its wheels hops.
      void hop (speed_t speed) {
        if (grounded ())
          m_hop = speed;
      }
      // Where a pedalled bike's crank has turned to, radians in [0, 2pi):
      // round at a steady cadence through the gears while the rider
      // pedals, and still while they freewheel.
      radians_t crank () const {
        return m_crank;
      }

      // A bike nobody rides stays where it was left: its wheels lock and,
      // once it has settled on the ground, it is held still until mounted.
      void set_parked (bool parked) {
        m_parked = parked;
      }
      bool parked () const {
        return m_parked;
      }

      // The whole weight of bike and rider.
      kilograms_t mass () const {
        return m_mass;
      }
      // A blow at `at`: another machine bumping into it, or someone
      // hauling on it.
      void push (const Vec3& at, const impulse_t& impulse) {
        m_rigid->push (at, impulse);
      }

      void set_water_level (meters_t level) {
        m_water_level = level;
      }

      void set_trunks (const TrunkField* trunks) {
        m_rigid->set_trunks (trunks);
      }

      // What a glider's wing does to the bike hanging from it over the next
      // update: the acceleration the air gives the whole weight, and the
      // way the harness holds the chassis.
      struct Hang {
        acceleration_t acceleration =
          Vec3 () * isq::acceleration[si::metre / pow<2> (si::second)];
        Vec3 forward { 0, 0, 1 };
        Vec3 up { 0, 1, 0 };
      };
      void hang (const Hang& hang) {
        m_hang = hang;
        m_hung = true;
      }
      void unhang () {
        m_hung = false;
      }

      // Respawn: back to a spot, stationary, jets cooled down.
      void reset (const Vec3& position);

      void set_heading (const Vec3& h);

      bool grounded () const {
        return m_rigid->grounded ();
      }

      // Sideways speed relative to where the bike points; big when
      // drifting, ~zero when rolling straight
      speed_t drift_speed () const {
        const Vec3 v = velocity ();
        const Vec3 heading = orientation ();
        return length (v - heading * dot (v, heading)) * u::m / u::s;
      }

      // Downward speed of the last hard landing; reading it clears it
      speed_t pop_impact () {
        const speed_t value = m_impact;
        m_impact = 0 * u::m / u::s;
        return value;
      }

      // How far the last flight fell, peak to touchdown
      meters_t pop_fall_drop () {
        const meters_t value = m_fall_drop;
        m_fall_drop = 0 * u::m;
        return value;
      }

      // Stored energy and current output of the continuous jump jets.
      float boost_charge () const {
        return m_boost_charge;
      }
      float boost_level () const {
        return m_boost_level;
      }
      float boost_drive () const {
        return m_boost_drive;
      }

      // Accumulated wheel roll angle in [0, 2pi).
      radians_t wheel_spin () const {
        return m_wheel_spin;
      }
      bool airborne () const {
        return m_airborne_time > seconds (0.15f);
      }
      seconds_t airtime () const {
        return m_airborne_time;
      }
      // The chassis carries its own roll, so the drawn frame is the
      // assemblage's: where it stands, its forward, and its top.
      Vec3 render_normal () const {
        return m_rigid->up ();
      }
      Vec3 render_orientation () const {
        return orientation ();
      }
      Vec3 render_position () const {
        return position ();
      }
      // How far each wheel hangs below its drawn rest position along the
      // chassis' down axis; negative is compressed.
      meters_t rear_wheel_drop () const {
        return m_rigid->rear_extension ();
      }
      meters_t front_wheel_drop () const {
        return m_rigid->front_extension ();
      }
      // The fork's angle about the steering head; positive turns right.
      radians_t fork_angle () const {
        return m_rigid->steer_angle ();
      }

      Vec3 position () const {
        return m_rigid->reference_position ();
      }
      position_t physical_position () const {
        return moppe::position (position ());
      }
      Vec3 orientation () const {
        return m_rigid->forward ();
      }
      Vec3 velocity () const {
        return m_rigid->velocity ();
      }
      velocity_t physical_velocity () const {
        return moppe::velocity (velocity ());
      }

    private:
      void update_jets (seconds_t dt, bool grounded);
      void substep (seconds_t dt, bool first);

      const SurfaceGeometry& m_map;
      const kilograms_t m_mass;

      radians_t m_yaw {};        // smoothed actual steering
      radians_t m_yaw_target {}; // raw steering input
      radians_t m_wheel_spin {};
      bool m_boost_flight = false; // landing softened after using the jets
      control_signal_t m_thrust {};

      float m_boost_input = 0.0f;
      float m_boost_drive = 0.0f;
      float m_boost_level = 0.0f;
      float m_boost_charge = 1.0f;
      seconds_t m_boost_recharge_delay {};
      meters_t m_water_level = -1000.0f * u::m;

      seconds_t m_airborne_time {};
      speed_t m_impact {};
      meters_t m_fall_top {};  // highest point of the current flight
      meters_t m_fall_drop {}; // set on landing: peak minus touchdown

      std::unique_ptr<RigidBike> m_rigid;
      bool m_parked = false;
      float m_skid = 0.0f;
      float m_wheelie = 0.0f;
      radians_t m_crank {};
      // Set by hop() for the next update only, never restored.
      speed_t m_hop {};
      // Set afresh by the session each step of a glide, never restored.
      bool m_hung = false;
      Hang m_hang {};
    };
  }
}

#endif
