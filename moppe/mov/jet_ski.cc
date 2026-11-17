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
      constexpr float pi = 3.14159265f;

      // The hull's lines, as tools/figure/jet_ski.py draws them: a vee
      // bottom of some fifteen degrees between hard chines, sides that
      // stand up to the bond line, and a crowned deck over it. Forward
      // of the middle the keel sweeps up and the breadth draws in to
      // the stem.
      constexpr float deadrise = 0.12f; // the chine over the keel, aft
      constexpr float chine_share = 0.86f;
      constexpr float sheer = 0.36f;  // the bond line over the keel, aft
      constexpr float spring = 0.10f; // its rise to the stem
      constexpr float rocker = 0.30f; // the keel's rise at the stem
      constexpr float crown = 0.16f;  // the deck over the bond line
      constexpr float stem_half = 0.05f;

      // The hull, its engine and pump, and a tank of two-stroke mix; and
      // the rider on the saddle, who comes aboard and steps off.
      constexpr kilograms_t mass = 205.0f * u::kg;
      constexpr float mass_centre[3] = { 0.0f, 0.24f, -0.12f };
      // Pitch, yaw, and roll about the centre of mass.
      constexpr kilogram_square_metres_t inertia[3] = { 100.0f * u::kg * u::m2,
                                                        110.0f * u::kg* u::m2,
                                                        18.0f * u::kg* u::m2 };
      // The seated rider about their own middle, per kilogram of them.
      constexpr square_meters_t seated[3] = { 0.07f * u::m2,
                                              0.03f * u::m2,
                                              0.07f * u::m2 };
      constexpr kilograms_t crew = 80.0f * u::kg;
      constexpr float crew_centre[3] = { 0.0f, 0.92f, -0.25f };

      b3MassData mass_data (bool manned) {
        const Vec3 hull (mass_centre[0], mass_centre[1], mass_centre[2]);
        const Vec3 seat (crew_centre[0], crew_centre[1], crew_centre[2]);
        const kilograms_t aboard = manned ? crew : 0.0f * u::kg;
        const kilograms_t total = mass + aboard;
        const auto kg = [] (kilograms_t m) {
          return m.numerical_value_in (u::kg);
        };
        const Vec3 centre =
          (hull * kg (mass) + seat * kg (aboard)) / kg (total);
        std::array<kilogram_square_metres_t, 3> about = { inertia[0],
                                                          inertia[1],
                                                          inertia[2] };
        const auto carry = [&] (const Vec3& at, kilograms_t m) {
          const Vec3 d = at - centre;
          about[0] += m * ((d[1] * d[1] + d[2] * d[2]) * u::m2);
          about[1] += m * ((d[0] * d[0] + d[2] * d[2]) * u::m2);
          about[2] += m * ((d[0] * d[0] + d[1] * d[1]) * u::m2);
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
      constexpr HullWaterTraits water_traits { .ram = 1.0f * one,
                                               .suction = 0.25f * one,
                                               .damping = 0.8f * u::m / u::s,
                                               .friction = 0.0040f * one };

      // The pump: at full speed it throws a jet 75 mm across out of the
      // nozzle at 24 m/s, some 30 kW of the engine's 37 in the water.
      // Its push is the water's change of speed through it, so it falls
      // off as the hull nears the jet's own speed. Astern, the bucket
      // turns a part of the jet forward.
      constexpr square_meters_t nozzle_area = 0.0044f * u::m2;
      constexpr speed_t jet_speed = 24.0f * u::m / u::s;
      constexpr float astern = 0.3f;
      constexpr float astern_revs = 0.6f;
      constexpr seconds_t spin_up = 0.35f * u::s;
      constexpr float idle = 0.14f;
      // Revolutions a second as drawn, at full speed.
      constexpr per_second_t impeller_rate = 9.0f / u::s;
      constexpr float most_steer = 0.42f;
      constexpr radians_per_second_t steer_rate = 3.0f * u::rad / u::s;

      // The rider's balance: how far they lean it into a turn at speed,
      // how hard they right it for how far off it is, and the most
      // their weight, shifted a foot or so across, can do, newton metres.
      constexpr float most_lean = 0.42f;
      constexpr auto balance = 900.0f * u::N * u::m / u::rad;
      constexpr auto balance_damping = 160.0f * u::N * u::m * u::s / u::rad;
      constexpr newton_metres_t most_balance = 260.0f * u::N * u::m;
      // The air's drag on what stands out of the water.
      constexpr auto air_drag = 0.4f * u::kg / u::m;

      // Its draft at rest with no one on it.
      constexpr float draft = 0.15f;

      constexpr int patch_samples = 97;
      constexpr float patch_recentre_distance = 24.0f;
      constexpr float trunk_reach = 72.0f;

      constexpr std::uint64_t ground_bit = 1;
      constexpr std::uint64_t trunk_bit = 2;
      constexpr std::uint64_t body_bit = 8;

      float smoothstep (float a, float b, float x) {
        const float t = std::clamp ((x - a) / (b - a), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
      }

      // Each line by s, 0 at the transom and 1 at the stem.
      float fore (float s) {
        return std::max (0.0f, (s - 0.5f) / 0.5f);
      }
      float planform (float s) {
        return std::max (
          stem_half, JetSki::half_beam * (1.0f - std::pow (fore (s), 2.2f)));
      }
      float keel_y (float s) {
        const float t = fore (s);
        return rocker * t * t;
      }
      float sheer_y (float s) {
        return sheer + spring * s * s;
      }
      float chine_y (float s) {
        const float t = fore (s);
        const float k = t * t;
        return (keel_y (s) + deadrise) * (1.0f - k) + (sheer_y (s) - 0.03f) * k;
      }
      float station (float s) {
        return JetSki::transom + s * JetSki::hull_length;
      }
      Vec3 keel (float s) {
        return Vec3 (0.0f, keel_y (s), station (s));
      }
      Vec3 chine (float s, float side) {
        return Vec3 (side * std::max (stem_half, planform (s) * chine_share),
                     chine_y (s),
                     station (s));
      }
      Vec3 bond (float s, float side) {
        return Vec3 (side * planform (s), sheer_y (s), station (s));
      }
      Vec3 ridge (float s) {
        return Vec3 (
          0.0f, sheer_y (s) + crown * (1.0f - 0.4f * s), station (s));
      }

      // Closed all round: the bottom, the sides, the deck, and the
      // transom.
      std::vector<HullPanel> hull_panels () {
        std::vector<HullPanel> panels;
        constexpr int stations = 14;
        for (int k = 0; k < stations; ++k) {
          const float s0 = float (k) / stations, s1 = float (k + 1) / stations;
          const float sm = 0.5f * (s0 + s1);
          const Vec3 middle (
            0.0f, 0.5f * (keel_y (sm) + sheer_y (sm)), station (sm));
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
                         keel (0),
                         chine (0, side),
                         bond (0, side),
                         ridge (0),
                         2,
                         2,
                         Vec3 (0.0f, 0.25f, 0.0f),
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
        const float s = k / 10.0f;
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

    Vec3 point (const Vec3& local) const {
      return from_b3 (b3Body_GetPosition (body)) + axis (local);
    }

    // The way the jet pushes the hull, in the hull's frame: ahead,
    // swung by the nozzle.
    Vec3 push_local () const {
      const float steer = radians_value (kept.steer);
      return Vec3 (std::sin (steer), 0.0f, std::cos (steer));
    }

    void place (const Vec3& at, const Vec3& heading, float level) {
      const b3Quat q = level_rotation (heading);
      Vec3 centre = at;
      centre[1] =
        std::max (level - jet_ski_rig::draft,
                  surface_elevation_at (surface, at[0], at[2]) + 0.02f);
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

      const Vec3 v = from_b3 (b3Body_GetLinearVelocity (body));
      const Vec3 at = point (Vec3 ());
      const Vec3 current = water.current_at (at);
      Vec3 way = v - current;
      way[1] = 0.0f;
      const float speed = length (way);

      // The water on each panel.
      const square_meters_t wetted =
        press_hull (body, panels (), water, water_traits);
      kept.afloat = wetted > 0.4f * u::m2;
      kept.grounded = touching_ground ();

      // The engine and the bars.
      const float drive =
        kept.parked ? 0.0f
                    : std::clamp (scalar_value (kept.drive), -1.0f, 1.0f);
      const float turn =
        kept.parked ? 0.0f : std::clamp (scalar_value (kept.turn), -1.0f, 1.0f);
      const Vec3 grate (intake[0], intake[1], intake[2]);
      const Vec3 grate_at = point (grate);
      const float intake_depth = water.level_at (grate_at) - grate_at[1];
      kept.intake_depth = intake_depth * u::m;
      const float bite = smoothstep (-0.03f, 0.06f, intake_depth);
      // Out of the water the pump has nothing to work on and the engine
      // races.
      const float load = drive < 0.0f ? astern_revs * -drive : drive;
      const float wanted_rpm =
        kept.parked ? 0.0f
                    : std::max (idle, load) * (1.0f + 0.15f * (1.0f - bite));
      float rpm = scalar_value (kept.rpm);
      rpm += (wanted_rpm - rpm) *
             std::min (1.0f, (dt / spin_up).numerical_value_in (one));
      kept.rpm = rpm * one;
      kept.impeller_angle =
        std::remainder (
          radians_value (kept.impeller_angle) +
            (rpm * 2.0f * pi * impeller_rate * dt).numerical_value_in (one),
          2.0f * pi) *
        u::rad;
      const float steer_step = radians_value (steer_rate * dt);
      float steer = radians_value (kept.steer);
      steer += std::clamp (turn * most_steer - steer, -steer_step, steer_step);
      kept.steer = steer * u::rad;

      // The jet: the water's change of speed through the pump.
      const Vec3 push = axis (push_local ());
      const speed_t still = 0.0f * u::m / u::s;
      const speed_t ahead =
        std::max (still, dot (v - current, push) * u::m / u::s);
      const speed_t jet = jet_speed * std::min (rpm, 1.0f);
      newtons_t thrust =
        bite * rho * nozzle_area * jet * std::max (jet - ahead, still);
      if (drive < 0.0f)
        thrust = -astern * bite * rho * nozzle_area * jet * jet;
      else if (drive == 0.0f)
        thrust = 0.0f * u::N;
      kept.thrust = thrust;
      if (thrust != 0.0f * u::N)
        apply_force (
          body, push, thrust, point (Vec3 (nozzle[0], nozzle[1], nozzle[2])));

      // The rider holds it upright with their body, and leans it into a
      // turn as it gathers way.
      if (!kept.parked) {
        const float tilt = axis (Vec3 (1, 0, 0))[1];
        const float roll = std::asin (std::clamp (tilt, -1.0f, 1.0f));
        const Vec3 forward = axis (Vec3 (0, 0, 1));
        const radians_per_second_t rolling =
          dot (from_b3 (b3Body_GetAngularVelocity (body)), forward) * u::rad /
          u::s;
        const float lean_to = most_lean * turn * smoothstep (1.5f, 7.0f, speed);
        const float lean_step = radians_value (1.6f * u::rad / u::s * dt);
        float lean = radians_value (kept.lean);
        lean += std::clamp (lean_to - lean, -lean_step, lean_step);
        kept.lean = lean * u::rad;
        const newton_metres_t torque =
          std::clamp (newton_metres_t (balance * ((lean - roll) * u::rad) -
                                       balance_damping * rolling),
                      -most_balance,
                      most_balance);
        apply_torque (body, forward, torque);
      } else {
        kept.lean = radians_t {};
      }

      // The air's drag on what stands out of the water.
      apply_drag (body, v, air_drag * (length (v) * u::m / u::s));

      b3World_Step (world, seconds_value (dt), substeps);

      // Capsized and drifting: righted where it floats.
      const Vec3 moving = from_b3 (b3Body_GetLinearVelocity (body));
      const Vec3 turning = from_b3 (b3Body_GetAngularVelocity (body));
      const float upright = axis (Vec3 (0, 1, 0))[1];
      kept.overturned = upright < 0.25f && length2 (moving) < 9.0f
                          ? kept.overturned + dt
                          : seconds_t {};
      if (kept.overturned > 2.0f * u::s) {
        const Vec3 here = point (Vec3 ());
        const bool parked = kept.parked;
        place (here, axis (Vec3 (0, 0, 1)), water.level_at (here));
        kept.parked = parked;
        b3Body_SetMassData (body, mass_data (!parked));
        return;
      }

      // Left alone and still, on still water or on the shore: it rests.
      const bool quiet = length2 (moving) < 0.04f * 0.04f &&
                         length2 (turning) < 0.04f * 0.04f &&
                         length2 (current) < 0.05f * 0.05f;
      kept.still = kept.parked && quiet ? kept.still + dt : seconds_t {};
      if (kept.still > 2.0f * u::s)
        kept.asleep = true;
    }
  };

  JetSki::JetSki (const map::SurfaceGeometry& surface)
      : m_impl (std::make_unique<Impl> (surface)) {
    m_impl->place (Vec3 (), Vec3 (0, 0, 1), -1e6f);
  }

  JetSki::~JetSki () = default;

  JetSki::State JetSki::state () const {
    return m_impl->snapshot ();
  }

  void JetSki::restore (const State& state) {
    m_impl->build (state);
  }

  void JetSki::place (position_t at, direction_t heading, meters_t level) {
    m_impl->place (
      position_value (at), direction_value (heading), meters_value (level));
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
    return moppe::position (m_impl->point (Vec3 ()));
  }

  direction_t JetSki::forward () const {
    return unit_direction (m_impl->axis (Vec3 (0, 0, 1)));
  }

  direction_t JetSki::up () const {
    return unit_direction (m_impl->axis (Vec3 (0, 1, 0)));
  }

  direction_t JetSki::left () const {
    return unit_direction (m_impl->axis (Vec3 (1, 0, 0)));
  }

  velocity_t JetSki::velocity () const {
    return moppe::velocity (from_b3 (b3Body_GetLinearVelocity (m_impl->body)));
  }

  position_t JetSki::point (const displacement_t& local) const {
    return moppe::position (m_impl->point (displacement_value (local)));
  }

  kilograms_t JetSki::mass () const {
    return (jet_ski_rig::mass +
            (m_impl->kept.parked ? 0.0f * u::kg : jet_ski_rig::crew));
  }

  void JetSki::push (position_t at, const impulse_t& impulse) {
    Impl& m = *m_impl;
    m.kept.asleep = false;
    m.kept.still = seconds_t {};
    b3Body_ApplyLinearImpulse (m.body,
                               to_b3 (impulse_value (impulse)),
                               to_b3 (position_value (at)),
                               true);
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

  Vec3 JetSki::jet () const {
    return m_impl->point (Vec3 (nozzle[0], nozzle[1], nozzle[2]));
  }

  Vec3 JetSki::jet_direction () const {
    return -m_impl->axis (m_impl->push_local ());
  }

  meters_t JetSki::intake_depth () const {
    return m_impl->kept.intake_depth;
  }
}
