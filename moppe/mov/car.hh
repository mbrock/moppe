#ifndef MOPPE_MOV_CAR_HH
#define MOPPE_MOV_CAR_HH

#include <moppe/map/surface.hh>
#include <moppe/mov/rigid_bike.hh>
#include <moppe/mov/trunk_field.hh>

#include <array>
#include <memory>

namespace moppe::mov {
  // The shape and temperament of a four-wheeled car: its wheels, its
  // chassis' boxes and weight, its springs, tyres, engine, brakes, and
  // steering. The frame is the world's: x to the driver's left, y up, z
  // forward, from an origin on the centre line midway between the axles
  // and at their height when the car stands on level ground.
  struct CarRig {
    meters_t wheel_radius;
    // Each wheel's centre is a track to the side of the origin and an
    // axle ahead of or behind it.
    meters_t front_track;
    meters_t rear_track;
    meters_t axle;
    // The driver's seat, on the left; the passenger's mirrors it.
    displacement_t seat;

    kilograms_t chassis_mass;
    // Pitch, yaw, and roll about the centre of mass.
    kilogram_square_metres_t inertia[3];
    displacement_t mass_centre;
    kilograms_t wheel_mass;
    kilogram_square_metres_t wheel_inertia;
    // Box3D holds a body to a quarter turn a step unless told it may spin
    // faster, which would hold small wheels to a low speed.
    bool fast_wheels;
    // The chassis' boxes, half extents and centres, heights above the
    // axles: they meet trunks and, when the springs are spent or the car
    // goes over, the ground. The wheels meet only the ground.
    struct Box {
      spatial_extent_t half;
      displacement_t centre;
    };
    std::array<Box, 4> boxes;
    int box_count;

    // The springs are linear from top-out, so the sag at rest sets their
    // rate; beyond the sag there is `bump` more before the stops.
    meters_t sag;
    meters_t bump;
    damping_ratio_t sprung_damping_ratio;

    // The tyres' grip, and what it costs a loaded tyre to roll as a share
    // of its load.
    friction_coefficient_t tyre_friction;
    rolling_resistance_t rolling_resistance;

    newtons_t engine_force;
    watts_t engine_power;
    // Only the front pair drives, or all four.
    bool front_drive;
    // The snowmobile's rear pair model the two edges of one driven belt;
    // the ski contacts steer but carry no engine or braking torque.
    meters_t ski_half_length {};
    newtons_t brake_force;
    // The brakes' share at the front.
    proportion_t front_brake_share;
    speed_t reverse_speed;
    proportion_t reverse_share;
    newton_metres_t engine_brake_torque;
    newton_metres_t parking_torque;
    newton_metres_t free_rev_torque;

    // The front wheels' lock, turned less far the faster it goes.
    radians_t full_lock;
    speed_t lock_fade_speed;

    // Drag as a deceleration per unit of speed: the air's per metre and
    // the water's (up to the sills) per second.
    per_metre_t air_drag;
    damping_t water_drag;

    // Slung under the helicopter and off the ground: how quickly the air
    // damps its turning, and how hard it turns its nose into the way it is
    // carried, an angular acceleration a radian off. Zero for none.
    per_second_t slung_damping;
    quantity<one / pow<2> (si::second), float> slung_vane;

    kilograms_t total_mass () const {
      return chassis_mass + 4.0f * wheel_mass;
    }

    // The passenger rides beside the driver in a car, behind on a sled.
    displacement_t passenger_seat () const {
      if (ski_half_length > meters_t {})
        return displacement (Vec3 (0, 0.533f, -0.681f));
      const Vec3& driver = displacement_value (seat);
      return displacement (Vec3 (-driver[0], driver[1], driver[2]));
    }
  };

  // The jeep: a tub and a roll cage on four driven wheels.
  inline const CarRig jeep_rig {
    .wheel_radius = 0.40f * u::m,
    .front_track = 0.80f * u::m,
    .rear_track = 0.80f * u::m,
    .axle = 1.20f * u::m,
    .seat = displacement (Vec3 (0.33f, 0.43f, -0.16f)),
    .chassis_mass = 1000.0f * u::kg,
    // The centre of mass lies low between the axles.
    .inertia = { 1400.0f * u::kg * u::m2,
                 1500.0f * u::kg* u::m2,
                 560.0f * u::kg* u::m2 },
    .mass_centre = displacement (Vec3 (0.0f, 0.15f, 0.05f)),
    .wheel_mass = 30.0f * u::kg,
    .wheel_inertia = 2.4f * u::kg * u::m2,
    .fast_wheels = false,
    // The tub, wide enough to shield the wheels, and the roll cage over
    // the seats.
    .boxes = { { { spatial_extent_in_metres (Vec3 (0.92f, 0.36f, 1.9f)),
                   displacement (Vec3 (0.0f, 0.42f, 0.0f)) },
                 { spatial_extent_in_metres (Vec3 (0.6f, 0.33f, 0.98f)),
                   displacement (Vec3 (0.0f, 1.1f, -0.55f)) } } },
    .box_count = 2,
    .sag = 0.12f * u::m,
    .bump = 0.17f * u::m,
    .sprung_damping_ratio = 0.5f * damping_ratio[one],
    .tyre_friction = 1.25f * isq::static_friction_coefficient[one],
    .rolling_resistance = 0.025f * isq::rolling_resistance_factor[one],
    .engine_force = 9000.0f * u::N,
    .engine_power = 80000.0f * u::W,
    .front_drive = false,
    .brake_force = 13000.0f * u::N,
    .front_brake_share = 0.5f * proportion[one],
    .reverse_speed = 7.0f * u::m / u::s,
    .reverse_share = 0.6f * proportion[one],
    .engine_brake_torque = 45.0f * u::N * u::m,
    .parking_torque = 5000.0f * u::N * u::m,
    .free_rev_torque = 40.0f * u::N * u::m,
    .full_lock = 0.55f * u::rad,
    .lock_fade_speed = 13.0f * u::m / u::s,
    .air_drag = 0.003f / u::m,
    .water_drag = 1.6f / u::s,
    .slung_damping = 0.0f / u::s,
    .slung_vane = 0.0f / pow<2> (u::s),
  };

  // The Caddy, the Golf-faced pickup under a fibreglass hardtop: a road
  // car, its small petrol engine pulling through the front wheels, good
  // for some 140 km/h, on soft springs and narrow tyres that grip less
  // than the jeep's off the road. models/caddy.blend's measures.
  inline const CarRig caddy_rig {
    .wheel_radius = 0.288f * u::m,
    .front_track = 0.695f * u::m,
    .rear_track = 0.685f * u::m,
    .axle = 1.3125f * u::m,
    .seat = displacement (Vec3 (0.36f, 0.282f, -0.08f)),
    // Some 900 kg at the kerb with the hardtop on, nose-heavy over the
    // engine set across the front.
    .chassis_mass = 860.0f * u::kg,
    .inertia = { 1300.0f * u::kg * u::m2,
                 1450.0f * u::kg* u::m2,
                 380.0f * u::kg* u::m2 },
    .mass_centre = displacement (Vec3 (0.0f, 0.24f, 0.30f)),
    .wheel_mass = 16.0f * u::kg,
    .wheel_inertia = 0.75f * u::kg * u::m2,
    .fast_wheels = true,
    // The body from the sills to the waist, the cab over the front of it,
    // and the hardtop over the bed and the cab's roof.
    .boxes = { { { spatial_extent_in_metres (Vec3 (0.82f, 0.33f, 2.14f)),
                   displacement (Vec3 (0.0f, 0.35f, -0.08f)) },
                 { spatial_extent_in_metres (Vec3 (0.66f, 0.25f, 0.42f)),
                   displacement (Vec3 (0.0f, 0.92f, 0.08f)) },
                 { spatial_extent_in_metres (Vec3 (0.785f, 0.47f, 0.94f)),
                   displacement (Vec3 (0.0f, 1.15f, -1.28f)) },
                 { spatial_extent_in_metres (Vec3 (0.785f, 0.22f, 0.36f)),
                   displacement (Vec3 (0.0f, 1.40f, 0.0f)) } } },
    .box_count = 4,
    // A car's springs: softer and shorter than the jeep's.
    .sag = 0.085f * u::m,
    .bump = 0.10f * u::m,
    .sprung_damping_ratio = 0.42f * damping_ratio[one],
    .tyre_friction = 1.08f * isq::static_friction_coefficient[one],
    .rolling_resistance = 0.015f * isq::rolling_resistance_factor[one],
    // The 1.6 litre's seventy horsepower.
    .engine_force = 5600.0f * u::N,
    .engine_power = 52000.0f * u::W,
    .front_drive = true,
    .brake_force = 9500.0f * u::N,
    .front_brake_share = 0.62f * proportion[one],
    .reverse_speed = 6.0f * u::m / u::s,
    .reverse_share = 0.55f * proportion[one],
    .engine_brake_torque = 28.0f * u::N * u::m,
    .parking_torque = 2600.0f * u::N * u::m,
    .free_rev_torque = 18.0f * u::N * u::m,
    .full_lock = 0.58f * u::rad,
    .lock_fade_speed = 15.0f * u::m / u::s,
    // Set so that the engine's power is spent against it a little short
    // of 140 km/h.
    .air_drag = 0.00105f / u::m,
    .water_drag = 1.6f / u::s,
    .slung_damping = 1.6f / u::s,
    .slung_vane = 2.0f / pow<2> (u::s),
  };

  inline const CarRig snowmobile_rig = [] {
    CarRig rig = jeep_rig;
    rig.wheel_radius = 0.23f * u::m;
    rig.front_track = 0.48f * u::m;
    rig.rear_track = 0.17f * u::m;
    rig.axle = 0.90f * u::m;
    rig.seat = displacement (Vec3 (0, 0.465f, -0.198f));
    rig.chassis_mass = 290.0f * u::kg;
    rig.inertia[0] = 180.0f * u::kg * u::m2;
    rig.inertia[1] = 190.0f * u::kg * u::m2;
    rig.inertia[2] = 60.0f * u::kg * u::m2;
    rig.mass_centre = displacement (Vec3 (0, 0.05f, -0.23f));
    rig.wheel_mass = 8.0f * u::kg;
    rig.wheel_inertia = 0.3f * u::kg * u::m2;
    rig.fast_wheels = true;
    rig.boxes[0] = { spatial_extent_in_metres (Vec3 (0.41f, 0.19f, 1.22f)),
                     displacement (Vec3 (0, 0.19f, -0.06f)) };
    rig.box_count = 1;
    rig.ski_half_length = 0.49f * u::m;
    rig.front_brake_share = proportion_t {};
    rig.engine_force = 4200.0f * u::N;
    rig.engine_power = 52.0f * u::kW;
    rig.brake_force = 3200.0f * u::N;
    rig.full_lock = 0.5f * u::rad;
    return rig;
  }();

  // A car as a Box3D assemblage like the rigid bike: a chassis of boxes on
  // four sphere wheels, each on a wheel joint that springs it, spins it
  // for the engine and the brakes, and, at the front, steers it. It
  // stands in a private world holding a streamed patch of the height
  // field and the trunks near it. The driven wheels each pull within what
  // their load lets them put down; left on its side or back it is set on
  // its wheels again; parked, it locks its wheels and, once still, is not
  // stepped at all.
  class Car {
  public:
    // Front left, front right, rear left, rear right.
    static constexpr int wheel_count = 4;

    struct State {
      RigidBodyState chassis;
      std::array<RigidBodyState, wheel_count> wheels;
      int ground_column = 0, ground_row = 0;
      std::array<bool, wheel_count> touching {};
      std::array<newtons_t, wheel_count> load {};
      // How far the wheels have rolled round, for drawing them.
      radians_t wheel_spin {};
      control_signal_t drive {};
      control_signal_t turn {};
      bool brake = false;
      bool parked = true;
      // Hung from the helicopter's line (game/sling.hh).
      bool slung = false;
      // How hard the engine was last asked to pull, for its sound.
      control_signal_t throttle {};
      // How long it has stood still, and lain on its side or back.
      seconds_t still {};
      seconds_t overturned {};
      bool asleep = false;
    };

    Car (const map::SurfaceGeometry& surface, const CarRig& rig);
    ~Car ();
    Car (const Car&) = delete;
    Car& operator= (const Car&) = delete;

    const CarRig& rig () const;

    State state () const;
    // Rebuilds the assemblage from a snapshot, in one canonical order.
    void restore (const State& state);

    // Stands the car on the ground at `at`, facing along `heading`.
    void place (position_t at, direction_t heading);
    // Moves it rigidly in the plane without disturbing its motion or sleep.
    void shift (plan_displacement_t delta);
    // The obstacles its chassis meets; rebuilt into its world when the
    // field changes.
    void set_trunks (const TrunkField* trunks);

    // Forward (positive) or back, -1..1; right (positive) or left.
    void set_drive (control_signal_t drive);
    void set_turn (control_signal_t turn);
    void set_brake (bool brake);
    void set_parked (bool parked);
    // Hung from a line: off the ground, the air damps its swinging and
    // turns its nose the way it is carried.
    void set_slung (bool slung);

    void update (seconds_t dt, meters_t water_level, proportion_t snow = {});

    position_t position () const;
    direction_t forward () const;
    direction_t up () const;
    direction_t left () const;
    velocity_t velocity () const;
    bool grounded () const;
    bool asleep () const;
    // Its whole weight.
    kilograms_t mass () const;
    // A blow at `at` in the world: another machine bumping into it, or
    // someone hauling on it. It wakes one asleep.
    void push (position_t at, const impulse_t& impulse);
    // A point given in the car's frame, in the world.
    position_t point (const displacement_t& local) const;
    position_t driver_seat () const;
    position_t passenger_seat () const;

    // How far a wheel hangs below where it stands; negative when its
    // spring is pressed further.
    meters_t wheel_drop (int wheel) const;
    // The front wheels' turn, positive to the right.
    radians_t steer () const;
    radians_t wheel_spin () const;
    // How hard the engine is asked to pull, 0..1, for its sound and smoke.
    control_signal_t throttle () const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
  };
}

#endif
