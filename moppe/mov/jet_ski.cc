#include <moppe/mov/box3d_vec.hh>
#include <moppe/mov/ground_patch.hh>
#include <moppe/mov/hull_water.hh>
#include <moppe/mov/jet_ski.hh>

#include <algorithm>
#include <cmath>
#include <vector>

#include <box3d/box3d.h>

namespace moppe::mov {
  namespace {
    // Kept in a namespace of its own: the engine is a unity build.
    namespace jet_ski_rig {
      constexpr metres_per_second_squared_t g = standard_gravity;
      constexpr kilograms_per_cubic_metre_t rho = water_density;
      constexpr int substeps = 4;
      constexpr proportion_t none = 0.0f * proportion[one];
      constexpr proportion_t whole = 1.0f * proportion[one];

      // The hull's lines, as tools/figure/jet_ski.py draws them: a vee
      // bottom of some fifteen degrees between hard chines, sides that
      // stand up to the bond line, and a crowned deck over it. Forward
      // of the middle the keel sweeps up and the breadth draws in to
      // the stem.
      constexpr meters_t deadrise = 0.12f * u::m; // the chine over the keel
      constexpr proportion_t chine_share = 0.86f * proportion[one];
      constexpr meters_t sheer = 0.36f * u::m;  // the bond line over the keel
      constexpr meters_t spring = 0.10f * u::m; // its rise to the stem
      constexpr meters_t rocker = 0.30f * u::m; // the keel's rise at the stem
      constexpr meters_t crown = 0.16f * u::m;  // the deck over the bond line
      constexpr meters_t stem_half = 0.05f * u::m;

      // The hull, its engine and pump, and a tank of two-stroke mix; and
      // the rider on the saddle, who comes aboard and steps off.
      constexpr kilograms_t mass = 205.0f * u::kg;
      const displacement_t mass_centre =
        displacement (Vec3 (0.0f, 0.24f, -0.12f));
      // Pitch, yaw, and roll about the centre of mass.
      constexpr kilogram_square_metres_t inertia[3] = { 100.0f * u::kg * u::m2,
                                                        110.0f * u::kg* u::m2,
                                                        18.0f * u::kg* u::m2 };
      // The seated rider about their own middle, per kilogram of them.
      constexpr square_meters_t seated[3] = { 0.07f * u::m2,
                                              0.03f * u::m2,
                                              0.07f * u::m2 };
      constexpr kilograms_t crew = 80.0f * u::kg;
      const displacement_t crew_centre =
        displacement (Vec3 (0.0f, 0.92f, -0.25f));

      b3MassData mass_data (bool manned) {
        const displacement_t& hull = mass_centre;
        const displacement_t& seat = crew_centre;
        const kilograms_t aboard = manned ? crew : 0.0f * u::kg;
        const kilograms_t total = mass + aboard;
        const displacement_t centre = quantity_cast<isq::displacement> (
          (hull * mass + seat * aboard) / total);
        std::array<kilogram_square_metres_t, 3> about = { inertia[0],
                                                          inertia[1],
                                                          inertia[2] };
        const auto carry = [&] (const displacement_t& at, kilograms_t m) {
          const displacement_t d = at - centre;
          const auto lane = [&] (int i) -> meters_t {
            return displacement_value (d)[i] * u::m;
          };
          about[0] += m * (lane (1) * lane (1) + lane (2) * lane (2));
          about[1] += m * (lane (0) * lane (0) + lane (2) * lane (2));
          about[2] += m * (lane (0) * lane (0) + lane (1) * lane (1));
        };
        carry (hull, mass);
        if (manned) {
          carry (seat, crew);
          for (int i = 0; i < 3; ++i)
            about[i] += crew * seated[i];
        }
        return body_mass (total, centre, about);
      }

      // How the water presses on its panels (mov/hull_water.hh): a
      // glass-fibre bottom slicker than the skiff's film.
      constexpr HullWaterTraits water_traits {
        .ram = 1.0f * proportion[one],
        .suction = 0.25f * proportion[one],
        .damping = 0.8f * u::m / u::s,
        .friction = 0.0040f * isq::drag_coefficient[one]
      };

      // The pump: at full speed it throws a jet 75 mm across out of the
      // nozzle at 24 m/s, some 30 kW of the engine's 37 in the water.
      // Its push is the water's change of speed through it, so it falls
      // off as the hull nears the jet's own speed. Astern, the bucket
      // turns a part of the jet forward.
      constexpr square_meters_t nozzle_area = 0.0044f * u::m2;
      constexpr speed_t jet_speed = 24.0f * u::m / u::s;
      constexpr proportion_t astern = 0.3f * proportion[one];
      constexpr proportion_t astern_revs = 0.6f * proportion[one];
      constexpr seconds_t spin_up = 0.35f * u::s;
      constexpr proportion_t idle = 0.14f * proportion[one];
      // Revolutions a second as drawn, at full speed.
      constexpr per_second_t impeller_rate = 9.0f / u::s;
      constexpr radians_t most_steer = 0.42f * u::rad;
      constexpr radians_per_second_t steer_rate = 3.0f * u::rad / u::s;

      // The rider's balance: how far they lean it into a turn at speed,
      // how hard they right it for how far off it is, and the most
      // their weight, shifted a foot or so across, can do, newton metres.
      constexpr radians_t most_lean = 0.42f * u::rad;
      constexpr auto balance = 900.0f * u::N * u::m / u::rad;
      constexpr auto balance_damping = 160.0f * u::N * u::m * u::s / u::rad;
      constexpr newton_metres_t most_balance = 260.0f * u::N * u::m;
      // The air's drag on what stands out of the water.
      constexpr auto air_drag = 0.4f * u::kg / u::m;

      // Its draft at rest with no one on it.
      constexpr meters_t draft = 0.15f * u::m;

      constexpr int patch_samples = 97;
      constexpr float patch_recentre_distance = 24.0f;
      constexpr float trunk_reach = 72.0f;

      constexpr std::uint64_t ground_bit = 1;
      constexpr std::uint64_t trunk_bit = 2;
      constexpr std::uint64_t body_bit = 8;

      // A stick's deflection, either way, as a share of all the way.
      proportion_t ski_share (control_signal_t signal) {
        return quantity_cast<proportion> (
          quantity_cast<mp_units::dimensionless> (abs (signal)));
      }

      template <class Q>
      proportion_t smoothstep (Q a, Q b, Q x) {
        const proportion_t t = std::clamp (
          quantity_cast<proportion> ((x - a) / (b - a)), none, whole);
        return quantity_cast<proportion> (t * t *
                                          (3.0f * proportion[one] - 2.0f * t));
      }

      displacement_t ski_offset (meters_t x, meters_t y, meters_t z) {
        return displacement (
          Vec3 (meters_value (x), meters_value (y), meters_value (z)));
      }

      // Each line by s, 0 at the transom and 1 at the stem.
      proportion_t fore (proportion_t s) {
        return std::max (none, (s - 0.5f * proportion[one]) / 0.5f);
      }
      meters_t planform (proportion_t s) {
        return std::max (
          stem_half,
          JetSki::half_beam *
            (1.0f - std::pow (fore (s).numerical_value_in (one), 2.2f)));
      }
      meters_t keel_y (proportion_t s) {
        const proportion_t t = fore (s);
        return quantity_cast<isq::length> (rocker * t * t);
      }
      meters_t sheer_y (proportion_t s) {
        return sheer + quantity_cast<isq::length> (spring * s * s);
      }
      meters_t chine_y (proportion_t s) {
        const proportion_t t = fore (s);
        const proportion_t k = quantity_cast<proportion> (t * t);
        return quantity_cast<isq::length> ((keel_y (s) + deadrise) *
                                           (whole - k)) +
               quantity_cast<isq::length> ((sheer_y (s) - 0.03f * u::m) * k);
      }
      meters_t station (proportion_t s) {
        return JetSki::transom +
               quantity_cast<isq::length> (s * JetSki::hull_length);
      }
      displacement_t keel (proportion_t s) {
        return ski_offset (0.0f * u::m, keel_y (s), station (s));
      }
      displacement_t chine (proportion_t s, float side) {
        return ski_offset (
          side * std::max (stem_half,
                           meters_t (quantity_cast<isq::length> (planform (s) *
                                                                 chine_share))),
          chine_y (s),
          station (s));
      }
      displacement_t bond (proportion_t s, float side) {
        return ski_offset (side * planform (s), sheer_y (s), station (s));
      }
      displacement_t ridge (proportion_t s) {
        return ski_offset (
          0.0f * u::m,
          sheer_y (s) + quantity_cast<isq::length> (crown * (whole - 0.4f * s)),
          station (s));
      }

      // Closed all round: the bottom, the sides, the deck, and the
      // transom.
      std::vector<HullPanel> hull_panels () {
        std::vector<HullPanel> panels;
        constexpr int stations = 14;
        for (int k = 0; k < stations; ++k) {
          const proportion_t s0 = float (k) / stations * proportion[one];
          const proportion_t s1 = float (k + 1) / stations * proportion[one];
          const proportion_t sm = 0.5f * (s0 + s1);
          const displacement_t middle = ski_offset (
            0.0f * u::m, 0.5f * (keel_y (sm) + sheer_y (sm)), station (sm));
          for (const float side : { 1.0f, -1.0f }) {
            add_hull_quad (panels,
                           keel (s0),
                           keel (s1),
                           chine (s1, side),
                           chine (s0, side),
                           1,
                           3,
                           middle);
            add_hull_quad (panels,
                           chine (s0, side),
                           chine (s1, side),
                           bond (s1, side),
                           bond (s0, side),
                           1,
                           2,
                           middle);
            add_hull_quad (panels,
                           bond (s0, side),
                           bond (s1, side),
                           ridge (s1),
                           ridge (s0),
                           1,
                           2,
                           middle);
          }
        }
        for (const float side : { 1.0f, -1.0f })
          add_hull_quad (panels,
                         keel (none),
                         chine (none, side),
                         bond (none, side),
                         ridge (none),
                         2,
                         2,
                         displacement (Vec3 (0.0f, 0.25f, 0.0f)),
                         true);
        return panels;
      }

      const std::vector<HullPanel>& panels () {
        static const std::vector<HullPanel> all = hull_panels ();
        return all;
      }
    }
  }

  newtons_t JetSki::static_thrust () {
    using namespace jet_ski_rig;
    return rho * nozzle_area * jet_speed * jet_speed;
  }

  struct JetSki::Impl {
    const map::SurfaceGeometry& surface;
    GroundPatch patch;
    const TrunkField* trunks = nullptr;
    b3WorldId world = b3_nullWorldId;
    b3BodyId body = b3_nullBodyId;
    State kept;

    explicit Impl (const map::SurfaceGeometry& surface)
        : surface (surface),
          patch (surface,
                 { .ground_friction = 0.6f,
                   .obstacle_friction = 0.3f,
                   .obstacle_restitution = 0.2f,
                   .ground_bit = jet_ski_rig::ground_bit,
                   .ground_mask = jet_ski_rig::body_bit,
                   .obstacle_bit = jet_ski_rig::trunk_bit,
                   .obstacle_mask = jet_ski_rig::body_bit,
                   .samples = jet_ski_rig::patch_samples,
                   .recentre_distance = jet_ski_rig::patch_recentre_distance,
                   .trunk_reach = jet_ski_rig::trunk_reach }) {}

    ~Impl () {
      destroy ();
    }

    void destroy () {
      if (B3_IS_NON_NULL (world))
        b3DestroyWorld (world);
      world = b3_nullWorldId;
      body = b3_nullBodyId;
      patch.forget ();
    }

    void create_body (const RigidBodyState& s) {
      using namespace jet_ski_rig;
      b3BodyDef def = b3DefaultBodyDef ();
      def.type = b3_dynamicBody;
      def.position = to_b3 (s.position);
      def.rotation = quat_of (s.rotation);
      def.linearVelocity = to_b3 (s.linear_velocity);
      def.angularVelocity = to_b3 (s.angular_velocity);
      def.enableSleep = false;
      body = b3CreateBody (world, &def);

      // The hull is near enough convex: its keel, chines, bond line,
      // and the deck's ridge.
      std::vector<b3Vec3> points;
      for (int k = 0; k <= 10; ++k) {
        const proportion_t s = k / 10.0f * proportion[one];
        points.push_back (to_b3 (keel (s)));
        points.push_back (to_b3 (ridge (s)));
        for (const float side : { 1.0f, -1.0f }) {
          points.push_back (to_b3 (chine (s, side)));
          points.push_back (to_b3 (bond (s, side)));
        }
      }
      b3HullData* hull =
        b3CreateHull (points.data (), static_cast<int> (points.size ()), 64);
      b3ShapeDef shape = b3DefaultShapeDef ();
      shape.updateBodyMass = false;
      shape.baseMaterial.friction = 0.4f;
      shape.filter.categoryBits = body_bit;
      shape.filter.maskBits = ground_bit | trunk_bit;
      if (hull) {
        b3CreateHullShape (body, &shape, hull);
        b3DestroyHull (hull);
      } else {
        const b3BoxHull box =
          b3MakeOffsetBoxHull (0.45f, 0.25f, 1.4f, { 0.0f, 0.25f, 0.4f });
        b3CreateHullShape (body, &shape, &box.base);
      }
      b3Body_SetMassData (body, mass_data (!kept.parked));
      // The velocity kept is the centre of mass's, which the body was
      // made without: given again now that it has it, a hull that was
      // turning goes on exactly as it went.
      b3Body_SetLinearVelocity (body, to_b3 (s.linear_velocity));
    }

    void build (const State& s) {
      destroy ();
      b3WorldDef def = b3DefaultWorldDef ();
      def.gravity = { 0.0f,
                      -jet_ski_rig::g.numerical_value_in (u::m / pow<2> (u::s)),
                      0.0f };
      def.enableSleep = false;
      def.workerCount = 1;
      world = b3CreateWorld (&def);
      patch.build (world, trunks, s.ground_column, s.ground_row);
      kept = s;
      create_body (s.body);
    }

    State snapshot () const {
      State s = kept;
      s.body = read_body (body);
      s.ground_column = patch.column ();
      s.ground_row = patch.row ();
      return s;
    }

    Vec3 axis (const Vec3& local) const {
      return from_b3 (
        b3RotateVector (b3Body_GetRotation (body), to_b3 (local)));
    }

    direction_t facing (const direction_t& local) const {
      return turned (b3Body_GetRotation (body), local);
    }

    position_t where (const displacement_t& local) const {
      return body_position (body) +
             quantity_cast<isq::position_vector> (
               turned (b3Body_GetRotation (body), local));
    }

    // The way the jet pushes the hull, in the hull's frame: ahead,
    // swung by the nozzle.
    direction_t push_local () const {
      return unit_direction (Vec3 (std::sin (radians_value (kept.steer)),
                                   0.0f,
                                   std::cos (radians_value (kept.steer))));
    }

    void place (const position_t& at, const Vec3& heading, meters_t level) {
      const b3Quat q = level_rotation (heading);
      Vec3 centre = position_value (at);
      centre[1] = meters_value (std::max (
        level - jet_ski_rig::draft, ground_under (surface, at) + 0.02f * u::m));
      State s = kept;
      s.body = { centre, array_of (q), Vec3 (), Vec3 () };
      patch.origin_for (centre, s.ground_column, s.ground_row);
      s.still = s.overturned = seconds_t {};
      s.asleep = false;
      s.steer = s.lean = radians_t {};
      s.rpm = {};
      s.thrust = newtons_t {};
      if (B3_IS_NULL (world)) {
        build (s);
        return;
      }
      b3Body_SetTransform (body, to_b3 (centre), q);
      b3Body_SetLinearVelocity (body, { 0.0f, 0.0f, 0.0f });
      b3Body_SetAngularVelocity (body, { 0.0f, 0.0f, 0.0f });
      kept = s;
      patch.recentre (world, trunks, centre);
    }

    bool touching_ground () const {
      b3ContactData contacts[8];
      const int count = b3Body_GetContactData (body, contacts, 8);
      for (int i = 0; i < count; ++i)
        for (int m = 0; m < contacts[i].manifoldCount; ++m)
          if (contacts[i].manifolds[m].pointCount > 0)
            return true;
      return false;
    }

    void step (seconds_t dt, const BoatWater& water) {
      using namespace jet_ski_rig;
      patch.recentre (world, trunks, from_b3 (b3Body_GetPosition (body)));

      const direction_t local_left = unit_direction (Vec3 (1, 0, 0));
      const direction_t local_up = unit_direction (Vec3 (0, 1, 0));
      const direction_t local_forward = unit_direction (Vec3 (0, 0, 1));
      const velocity_t v = linear_velocity (body);
      const position_t at = where (displacement (Vec3 ()));
      const velocity_t current = water.current (at);
      velocity_t way = v - current;
      velocity_value (way)[1] = 0.0f;
      const speed_t speed = length (velocity_value (way)) * u::m / u::s;

      // The water on each panel.
      const square_meters_t wetted =
        press_hull (body, panels (), water, water_traits);
      kept.afloat = wetted > 0.4f * u::m2;
      kept.grounded = touching_ground ();

      // The engine and the bars.
      constexpr control_signal_t let_go = 0.0f * control_signal[one];
      constexpr control_signal_t full = 1.0f * control_signal[one];
      const control_signal_t drive =
        kept.parked ? let_go : std::clamp (kept.drive, -full, full);
      const control_signal_t turn =
        kept.parked ? let_go : std::clamp (kept.turn, -full, full);
      const position_t grate_at = where (intake);
      const meters_t intake_depth =
        water.level (grate_at) - height_of (grate_at);
      kept.intake_depth = intake_depth;
      const proportion_t bite =
        smoothstep (-0.03f * u::m, 0.06f * u::m, intake_depth);
      // Out of the water the pump has nothing to work on and the engine
      // races.
      const proportion_t load = drive < let_go
                                  ? proportion_t (quantity_cast<proportion> (
                                      astern_revs * ski_share (drive)))
                                  : ski_share (drive);
      const proportion_t wanted_rpm =
        kept.parked
          ? none
          : quantity_cast<proportion> (std::max (idle, load) *
                                       (whole + 0.15f * (whole - bite)));
      proportion_t rpm = kept.rpm;
      rpm += quantity_cast<proportion> (
        (wanted_rpm - rpm) *
        std::min (1.0f * blend_weight[one],
                  quantity_cast<blend_weight> (dt / spin_up)));
      kept.rpm = rpm;
      kept.impeller_angle =
        std::remainder (
          radians_value (kept.impeller_angle) +
            (rpm * 2.0f * PI * impeller_rate * dt).numerical_value_in (one),
          2.0f * PI) *
        u::rad;
      const radians_t steer_step = steer_rate * dt;
      radians_t steer = kept.steer;
      steer += std::clamp (
        radians_t (quantity_cast<isq::angular_measure> (turn * most_steer)) -
          steer,
        -steer_step,
        steer_step);
      kept.steer = steer;

      // The jet: the water's change of speed through the pump.
      const direction_t push = facing (push_local ());
      const speed_t still = 0.0f * u::m / u::s;
      const speed_t ahead = std::max (
        still, speed_t (quantity_cast<isq::speed> (dot (v - current, push))));
      const speed_t jet =
        quantity_cast<isq::speed> (jet_speed * std::min (rpm, whole));
      newtons_t thrust = quantity_cast<isq::force> (
        bite * rho * nozzle_area * jet * std::max (jet - ahead, still));
      if (drive < let_go)
        thrust = quantity_cast<isq::force> (-astern * bite * rho * nozzle_area *
                                            jet * jet);
      else if (drive == let_go)
        thrust = 0.0f * u::N;
      kept.thrust = thrust;
      if (thrust != 0.0f * u::N)
        apply_force (body, push, thrust, where (nozzle));

      // The rider holds it upright with their body, and leans it into a
      // turn as it gathers way.
      if (!kept.parked) {
        const radians_t roll =
          std::asin (std::clamp (
            direction_value (facing (local_left))[1], -1.0f, 1.0f)) *
          u::rad;
        const direction_t forward = facing (local_forward);
        const radians_per_second_t rolling =
          quantity_cast<isq::angular_velocity> (
            dot (angular_velocity (body), forward));
        const radians_t lean_to = quantity_cast<isq::angular_measure> (
          most_lean * turn *
          smoothstep (1.5f * u::m / u::s, 7.0f * u::m / u::s, speed));
        const radians_t lean_step = 1.6f * u::rad / u::s * dt;
        radians_t lean = kept.lean;
        lean += std::clamp (lean_to - lean, -lean_step, lean_step);
        kept.lean = lean;
        const newton_metres_t torque = std::clamp (
          newton_metres_t (balance * (lean - roll) - balance_damping * rolling),
          -most_balance,
          most_balance);
        apply_torque (body, forward, torque);
      } else {
        kept.lean = radians_t {};
      }

      // The air's drag on what stands out of the water.
      apply_drag (
        body, v, air_drag * (length (velocity_value (v)) * u::m / u::s));

      b3World_Step (world, seconds_value (dt), substeps);

      // Capsized and drifting: righted where it floats.
      const velocity_t moving = linear_velocity (body);
      const angular_velocity_t turning = angular_velocity (body);
      const auto squared = [] (auto q) { return q * q; };
      kept.overturned = direction_value (facing (local_up))[1] < 0.25f &&
                            dot (moving, moving) < squared (3.0f * u::m / u::s)
                          ? kept.overturned + dt
                          : seconds_t {};
      if (kept.overturned > 2.0f * u::s) {
        const position_t here = where (displacement (Vec3 ()));
        const bool parked = kept.parked;
        place (here, axis (Vec3 (0, 0, 1)), water.level (here));
        kept.parked = parked;
        b3Body_SetMassData (body, mass_data (!parked));
        return;
      }

      // Left alone and still, on still water or on the shore: it rests.
      const bool quiet =
        dot (moving, moving) < squared (0.04f * u::m / u::s) &&
        dot (turning, turning) < squared (0.04f * u::rad / u::s) &&
        dot (current, current) < squared (0.05f * u::m / u::s);
      kept.still = kept.parked && quiet ? kept.still + dt : seconds_t {};
      if (kept.still > 2.0f * u::s)
        kept.asleep = true;
    }
  };

  JetSki::JetSki (const map::SurfaceGeometry& surface)
      : m_impl (std::make_unique<Impl> (surface)) {
    m_impl->place (moppe::position (Vec3 ()), Vec3 (0, 0, 1), -1e6f * u::m);
  }

  JetSki::~JetSki () = default;

  JetSki::State JetSki::state () const {
    return m_impl->snapshot ();
  }

  void JetSki::restore (const State& state) {
    m_impl->build (state);
  }

  void JetSki::place (position_t at, direction_t heading, meters_t level) {
    m_impl->place (at, direction_value (heading), level);
  }

  void JetSki::set_trunks (const TrunkField* trunks) {
    Impl& m = *m_impl;
    if (trunks == m.trunks)
      return;
    const State s = m.snapshot ();
    m.trunks = trunks;
    m.build (s);
  }

  void JetSki::set_drive (control_signal_t drive) {
    m_impl->kept.drive = drive;
  }

  void JetSki::set_turn (control_signal_t turn) {
    m_impl->kept.turn = turn;
  }

  void JetSki::set_parked (bool parked) {
    Impl& m = *m_impl;
    State& kept = m.kept;
    if (kept.parked == parked)
      return;
    // Someone getting on or off: it settles deeper or lifts.
    kept.asleep = false;
    kept.still = seconds_t {};
    kept.parked = parked;
    b3Body_SetMassData (m.body, jet_ski_rig::mass_data (!parked));
  }

  void JetSki::update (seconds_t dt, const BoatWater& water) {
    // At speed the water is stiff under the hull: it is pressed anew
    // every substep.
    in_substeps (dt, [&] (seconds_t h) {
      if (!m_impl->kept.asleep)
        m_impl->step (h, water);
    });
  }

  position_t JetSki::position () const {
    return m_impl->where (displacement (Vec3 ()));
  }

  direction_t JetSki::forward () const {
    return m_impl->facing (unit_direction (Vec3 (0, 0, 1)));
  }

  direction_t JetSki::up () const {
    return m_impl->facing (unit_direction (Vec3 (0, 1, 0)));
  }

  direction_t JetSki::left () const {
    return m_impl->facing (unit_direction (Vec3 (1, 0, 0)));
  }

  velocity_t JetSki::velocity () const {
    return moppe::velocity (from_b3 (b3Body_GetLinearVelocity (m_impl->body)));
  }

  position_t JetSki::point (const displacement_t& local) const {
    return m_impl->where (local);
  }

  kilograms_t JetSki::mass () const {
    return (jet_ski_rig::mass +
            (m_impl->kept.parked ? 0.0f * u::kg : jet_ski_rig::crew));
  }

  void JetSki::push (position_t at, const impulse_t& impulse) {
    Impl& m = *m_impl;
    m.kept.asleep = false;
    m.kept.still = seconds_t {};
    b3Body_ApplyLinearImpulse (
      m.body, to_b3 (impulse_value (impulse)), to_b3 (at), true);
  }

  bool JetSki::asleep () const {
    return m_impl->kept.asleep;
  }

  bool JetSki::afloat () const {
    return m_impl->kept.afloat;
  }

  bool JetSki::grounded () const {
    return m_impl->kept.grounded;
  }

  proportion_t JetSki::rpm () const {
    return m_impl->kept.rpm;
  }

  radians_t JetSki::steer () const {
    return m_impl->kept.steer;
  }

  radians_t JetSki::lean () const {
    return m_impl->kept.lean;
  }

  radians_t JetSki::impeller_angle () const {
    return m_impl->kept.impeller_angle;
  }

  newtons_t JetSki::thrust () const {
    return m_impl->kept.thrust;
  }

  position_t JetSki::jet () const {
    return m_impl->where (nozzle);
  }

  direction_t JetSki::jet_direction () const {
    return -m_impl->facing (m_impl->push_local ());
  }

  meters_t JetSki::intake_depth () const {
    return m_impl->kept.intake_depth;
  }
}
