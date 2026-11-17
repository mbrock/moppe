#include <moppe/mov/boat.hh>
#include <moppe/mov/box3d_vec.hh>
#include <moppe/mov/ground_patch.hh>
#include <moppe/mov/hull_water.hh>

#include <algorithm>
#include <cmath>
#include <vector>

#include <box3d/box3d.h>

namespace moppe::mov {
  namespace {
    // Kept in a namespace of its own: the engine is a unity build.
    namespace boat_rig {
      constexpr metres_per_second_squared_t g = standard_gravity;
      constexpr int substeps = 4;
      constexpr proportion_t none = 0.0f * proportion[one];
      constexpr proportion_t whole = 1.0f * proportion[one];
      constexpr scale_factor_t unity = 1.0f * scale_factor[one];

      // The hull's lines, as tools/figure/boat.py draws them.
      constexpr meters_t depth = 0.33f * u::m;  // the sheer over the keel aft
      constexpr meters_t spring = 0.03f * u::m; // the sheer's rise to the bow
      constexpr meters_t rocker = 0.24f * u::m; // the bottom's rise at the stem
      // The chine's breadth to the sheer's.
      constexpr proportion_t flare = 0.82f * proportion[one];
      constexpr meters_t stem_half = 0.12f * u::m; // half the blunt bow's width
      const meters_t rake = std::tan (10.0f * PI / 180.0f) * depth;

      // The skiff, some 45 kg of 9 mm birch plywood, glass, epoxy, paint,
      // and two plastic seats; its long-tail, some 23 kg of engine, frame,
      // and shaft hung on the transom; and a couple of litres of petrol.
      // And the helmsman, sat on the after seat, who comes aboard and
      // steps off.
      constexpr kilograms_t mass = 70.0f * u::kg;
      const displacement_t mass_centre =
        displacement (Vec3 (0.0f, 0.20f, 0.15f));
      // Pitch, yaw, and roll about the centre of mass: a 4 m plank with
      // the engine's weight out at one end, and narrow.
      constexpr kilogram_square_metres_t inertia[3] = { 110.0f * u::kg * u::m2,
                                                        120.0f * u::kg* u::m2,
                                                        9.0f * u::kg* u::m2 };
      // The seated helmsman about their own middle, much as a 0.4 by
      // 0.9 m column, per kilogram of them.
      constexpr square_meters_t seated[3] = { 0.07f * u::m2,
                                              0.03f * u::m2,
                                              0.07f * u::m2 };
      constexpr kilograms_t crew = 80.0f * u::kg;
      const displacement_t crew_centre =
        displacement (Vec3 (0.0f, 0.52f, -0.33f));
      const displacement_t fore_centre =
        displacement (Vec3 (0.0f, 0.52f, 0.59f));

      // The boat's mass, with or without the helmsman aboard and someone
      // on the fore seat.
      b3MassData mass_data (bool manned, bool passenger) {
        const displacement_t& hull = mass_centre;
        const displacement_t& seat = crew_centre;
        const displacement_t& fore = fore_centre;
        const kilograms_t nobody = 0.0f * u::kg;
        const kilograms_t helmsman = manned ? crew : nobody;
        const kilograms_t forward = passenger ? crew : nobody;
        const kilograms_t total = mass + helmsman + forward;
        const displacement_t centre = quantity_cast<isq::displacement> (
          (hull * mass + seat * helmsman + fore * forward) / total);
        // Each part's own inertia about its centre, carried to the
        // whole's by the parallel axis rule.
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
        for (const auto& [aboard, at] :
             { std::pair (manned, seat), std::pair (passenger, fore) })
          if (aboard) {
            carry (at, crew);
            for (int i = 0; i < 3; ++i)
              about[i] += crew * seated[i];
          }
        return body_mass (total, centre, about);
      }

      constexpr kilograms_per_cubic_metre_t rho = water_density;
      // How the water presses on its panels (mov/hull_water.hh).
      constexpr HullWaterTraits water_traits {
        .ram = 1.0f * proportion[one],
        .suction = 0.25f * proportion[one],
        .damping = 0.6f * u::m / u::s,
        .friction = 0.0055f * isq::drag_coefficient[one]
      };
      // The hump of wave-making resistance, as a share of the weight, and
      // the volume Froude number it stands at and how broad it is.
      constexpr proportion_t hump = 0.17f * proportion[one];
      constexpr froude_number_t hump_froude = 1.95f * froude_number[one];
      constexpr froude_number_t hump_width = 0.45f * froude_number[one];

      // The long-tail: a Honda GX200 wearing a GX160's sticker (for legal
      // reasons, its builder says), 4.1 kW at its governed
      // 3600 rpm, turning a small propeller at the surface. Standing, the
      // propeller's grip on the water limits its push; under way, the
      // engine's power does, as much of it as a surface propeller turns
      // into thrust; and toward the speed its pitch allows, it pushes
      // nothing. Astern it pushes a third as hard. With the governor
      // tied back the engine runs half again as fast and gives half
      // again its power.
      constexpr watts_t power = 4100.0f * u::W;
      constexpr efficiency_t efficiency =
        0.55f * isq::mechanical_efficiency[one];
      constexpr newtons_t static_thrust = 520.0f * u::N;
      constexpr speed_t pitch_speed = 15.0f * u::m / u::s;
      constexpr scale_factor_t boosted_power = 1.5f * scale_factor[one];
      constexpr scale_factor_t boosted_pitch = 1.25f * scale_factor[one];
      constexpr proportion_t astern = 0.35f * proportion[one];
      constexpr seconds_t spin_up = 0.6f * u::s;
      constexpr proportion_t idle = 0.12f * proportion[one];
      // Revolutions a second as drawn, at full speed.
      constexpr per_second_t propeller_rate = 9.0f / u::s;
      constexpr radians_t most_swing = 0.5f * u::rad;
      constexpr radians_per_second_t swing_rate = 2.2f * u::rad / u::s;
      // Tipped up, out of the water, as it is left; and how fast the
      // helmsman lets it down or lifts it.
      constexpr radians_t stowed_tilt = 0.45f * u::rad;
      constexpr radians_per_second_t tilt_rate = 0.5f * u::rad / u::s;
      constexpr meters_t shaft_reach = 1.77f * u::m;
      // How far the sickle skeg under the propeller reaches below its hub,
      // keeping it off the bottom.
      constexpr meters_t skeg = 0.14f * u::m;
      // The water the boat heaps up behind its transom, round the
      // propeller, once it is under way.
      constexpr meters_t stern_wave = 0.12f * u::m;
      // The air's drag on what stands out of the water: half its density
      // times the drag area of the hull and those in it.
      constexpr auto air_drag = 0.8f * u::kg / u::m;

      constexpr int patch_samples = 97;
      constexpr float patch_recentre_distance = 24.0f;
      constexpr float trunk_reach = 72.0f;

      constexpr std::uint64_t ground_bit = 1;
      constexpr std::uint64_t trunk_bit = 2;
      constexpr std::uint64_t body_bit = 8;

      // A stick's deflection, either way, as a share of all the way.
      proportion_t boat_share (control_signal_t signal) {
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

      displacement_t boat_offset (meters_t x, meters_t y, meters_t z) {
        return displacement (
          Vec3 (meters_value (x), meters_value (y), meters_value (z)));
      }

      // Each line by s, 0 at the transom and 1 at the stem.
      meters_t planform (proportion_t s) {
        const proportion_t t =
          std::max (none, (s - 0.45f * proportion[one]) / 0.55f);
        const meters_t full = quantity_cast<isq::length> (
          Boat::half_beam *
          (0.97f * proportion[one] + 0.03f * std::min (whole, s / 0.45f)));
        return std::max (
          stem_half,
          full * (1.0f - std::pow (t.numerical_value_in (one), 2.1f)));
      }
      meters_t chine_y (proportion_t s) {
        const proportion_t t =
          std::max (none, (s - 0.45f * proportion[one]) / 0.55f);
        return quantity_cast<isq::length> (rocker * t * t);
      }
      meters_t chine_hb (proportion_t s) {
        return std::max (
          stem_half,
          meters_t (quantity_cast<isq::length> (planform (s) * flare)));
      }
      meters_t sheer_y (proportion_t s) {
        return depth + quantity_cast<isq::length> (spring * s * s);
      }
      displacement_t chine (proportion_t s, float side) {
        return boat_offset (
          side * chine_hb (s),
          chine_y (s),
          Boat::transom + quantity_cast<isq::length> (s * Boat::hull_length));
      }
      displacement_t sheer (proportion_t s, float side) {
        return boat_offset (
          side * planform (s),
          sheer_y (s),
          Boat::transom - rake +
            quantity_cast<isq::length> (s * (Boat::hull_length + rake)));
      }

      std::vector<HullPanel> hull_panels () {
        std::vector<HullPanel> panels;
        constexpr int stations = 24;
        for (int k = 0; k < stations; ++k) {
          const proportion_t s0 = float (k) / stations * proportion[one];
          const proportion_t s1 = float (k + 1) / stations * proportion[one];
          const proportion_t sm = 0.5f * (s0 + s1);
          const displacement_t middle =
            boat_offset (0.0f * u::m,
                         0.5f * (chine_y (sm) + sheer_y (sm)),
                         Boat::transom +
                           quantity_cast<isq::length> (sm * Boat::hull_length));
          add_hull_quad (panels,
                         chine (s0, 1),
                         chine (s1, 1),
                         chine (s1, -1),
                         chine (s0, -1),
                         1,
                         4,
                         middle + displacement (Vec3 (0, 0.3f, 0)));
          for (const float side : { 1.0f, -1.0f })
            add_hull_quad (panels,
                           chine (s0, side),
                           chine (s1, side),
                           sheer (s1, side),
                           sheer (s0, side),
                           1,
                           5,
                           middle);
        }
        add_hull_quad (panels,
                       chine (none, 1),
                       chine (none, -1),
                       sheer (none, -1),
                       sheer (none, 1),
                       4,
                       4,
                       displacement (Vec3 (0, 0.25f, 0)),
                       true);
        // The cockpit is open: each panel floods to the sheer over it.
        for (HullPanel& p : panels) {
          const meters_t across = displacement_value (p.centre)[0] * u::m;
          const meters_t along = displacement_value (p.centre)[2] * u::m;
          const proportion_t station =
            std::clamp (quantity_cast<proportion> ((along - Boat::transom) /
                                                   Boat::hull_length),
                        none,
                        whole);
          p.open = true;
          p.rim = boat_offset (across, sheer_y (station), along);
        }
        return panels;
      }

      const std::vector<HullPanel>& panels () {
        static const std::vector<HullPanel> all = hull_panels ();
        return all;
      }

      // Rotates `q` about y by `swing` after about x by `tilt`.
      template <class Q>
      Q swing_tilt (const Q& q, radians_t swing, radians_t tilt) {
        const Vec3& v = q.numerical_value_ref_in (Q::unit);
        const float ct = std::cos (radians_value (tilt));
        const float st = std::sin (radians_value (tilt));
        const Vec3 t (v[0], v[1] * ct - v[2] * st, v[1] * st + v[2] * ct);
        const float cs = std::cos (radians_value (swing));
        const float ss = std::sin (radians_value (swing));
        return Vec3 (t[0] * cs + t[2] * ss, t[1], -t[0] * ss + t[2] * cs) *
               Q::reference;
      }
    }
  }

  displacement_t Boat::tail_point (const displacement_t& local,
                                   radians_t swing,
                                   radians_t tilt) {
    return pivot + boat_rig::swing_tilt (local - pivot, swing, tilt);
  }

  struct Boat::Impl {
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
                   .ground_bit = boat_rig::ground_bit,
                   .ground_mask = boat_rig::body_bit,
                   .obstacle_bit = boat_rig::trunk_bit,
                   .obstacle_mask = boat_rig::body_bit,
                   .samples = boat_rig::patch_samples,
                   .recentre_distance = boat_rig::patch_recentre_distance,
                   .trunk_reach = boat_rig::trunk_reach }) {}

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
      using namespace boat_rig;
      b3BodyDef def = b3DefaultBodyDef ();
      def.type = b3_dynamicBody;
      def.position = to_b3 (s.position);
      def.rotation = quat_of (s.rotation);
      def.linearVelocity = to_b3 (s.linear_velocity);
      def.angularVelocity = to_b3 (s.angular_velocity);
      def.enableSleep = false;
      body = b3CreateBody (world, &def);

      // The hull is near enough convex: its chines, its sheer, and its
      // transom's corners.
      std::vector<b3Vec3> points;
      for (int k = 0; k <= 12; ++k) {
        const proportion_t s = k / 12.0f * proportion[one];
        for (const float side : { 1.0f, -1.0f }) {
          points.push_back (to_b3 (chine (s, side)));
          points.push_back (to_b3 (sheer (s, side)));
        }
      }
      b3HullData* hull =
        b3CreateHull (points.data (), static_cast<int> (points.size ()), 64);
      b3ShapeDef shape = b3DefaultShapeDef ();
      shape.updateBodyMass = false;
      shape.baseMaterial.friction = 0.45f;
      shape.filter.categoryBits = body_bit;
      shape.filter.maskBits = ground_bit | trunk_bit;
      if (hull) {
        b3CreateHullShape (body, &shape, hull);
        b3DestroyHull (hull);
      } else {
        const b3BoxHull box =
          b3MakeOffsetBoxHull (0.42f, 0.17f, 1.95f, { 0.0f, 0.17f, 1.0f });
        b3CreateHullShape (body, &shape, &box.base);
      }
      b3Body_SetMassData (body, mass_data (!kept.parked, kept.passenger));
    }

    void build (const State& s) {
      destroy ();
      b3WorldDef def = b3DefaultWorldDef ();
      def.gravity = { 0.0f,
                      -boat_rig::g.numerical_value_in (u::m / pow<2> (u::s)),
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

    direction_t facing (const direction_t& local) const {
      return turned (b3Body_GetRotation (body), local);
    }

    position_t where (const displacement_t& local) const {
      return body_position (body) +
             quantity_cast<isq::position_vector> (
               turned (b3Body_GetRotation (body), local));
    }

    void place (const position_t& at, const Vec3& heading, meters_t level) {
      const b3Quat q = level_rotation (heading);
      Vec3 centre = position_value (at);
      // Its draft at rest, a hand's breadth.
      centre[1] = meters_value (std::max (
        level - 0.06f * u::m, ground_under (surface, at) + 0.02f * u::m));
      State s = kept;
      s.body = { centre, array_of (q), Vec3 (), Vec3 () };
      patch.origin_for (centre, s.ground_column, s.ground_row);
      s.still = s.overturned = seconds_t {};
      s.asleep = false;
      s.tilt = boat_rig::stowed_tilt;
      s.swing = {};
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
      using namespace boat_rig;
      patch.recentre (world, trunks, from_b3 (b3Body_GetPosition (body)));

      const direction_t local_up = unit_direction (Vec3 (0, 1, 0));
      const velocity_t v = linear_velocity (body);
      const position_t at = where (displacement (Vec3 ()));
      const velocity_t current = water.current (at);
      velocity_t way = v - current;
      velocity_value (way)[1] = 0.0f;
      const speed_t speed = length (velocity_value (way)) * u::m / u::s;

      // The water on each panel.
      const square_meters_t wetted =
        press_hull (body, panels (), water, water_traits);
      kept.afloat = wetted > 0.5f * u::m2;
      kept.grounded = touching_ground ();

      // The long-tail.
      constexpr control_signal_t let_go = 0.0f * control_signal[one];
      constexpr control_signal_t full = 1.0f * control_signal[one];
      const control_signal_t drive =
        kept.parked ? let_go : std::clamp (kept.drive, -full, full);
      const control_signal_t turn =
        kept.parked ? let_go : std::clamp (kept.turn, -full, full);
      const control_signal_t boost = kept.parked || drive <= let_go
                                       ? let_go
                                       : std::clamp (kept.boost, let_go, full);
      const scale_factor_t revving =
        unity + quantity_cast<scale_factor> ((boosted_pitch - unity) * boost);
      const proportion_t wanted_rpm =
        kept.parked ? none
                    : quantity_cast<proportion> (
                        std::max (idle, boat_share (drive)) * revving);
      proportion_t rpm = kept.rpm;
      rpm += quantity_cast<proportion> (
        (wanted_rpm - rpm) *
        std::min (1.0f * blend_weight[one],
                  quantity_cast<blend_weight> (dt / spin_up)));
      kept.rpm = rpm;
      kept.propeller_angle =
        std::remainder (
          radians_value (kept.propeller_angle) +
            (rpm * 2.0f * PI * propeller_rate * dt).numerical_value_in (one),
          2.0f * PI) *
        u::rad;
      // Left alone the helmsman's hand holds the course, against the
      // pull of the boat heeling under their weight and the engine's.
      const radians_per_second_t yaw = quantity_cast<isq::angular_velocity> (
        dot (angular_velocity (body), facing (local_up)));
      const radians_t hold =
        kept.parked ? radians_t {}
                    : radians_t (quantity_cast<isq::angular_measure> (
                        (whole - std::min (whole, boat_share (turn) * 8.0f)) *
                        std::clamp (radians_t (2.5f * u::s * yaw),
                                    -0.15f * u::rad,
                                    0.15f * u::rad)));
      const radians_t swing_to =
        quantity_cast<isq::angular_measure> (turn * most_swing) + hold;
      const radians_t swing_step = swing_rate * dt;
      radians_t swing = kept.swing;
      swing += std::clamp (swing_to - swing, -swing_step, swing_step);
      kept.swing = swing;

      // Down into the water while someone holds it, but lifted over the
      // bottom where it would strike; tipped up out of it when left.
      radians_t tilt_to = stowed_tilt;
      if (!kept.parked) {
        const position_t low =
          where (tail_point (Boat::hub, kept.swing, radians_t {}));
        const meters_t floor =
          ground_under (surface, low) + 0.06f * u::m + skeg;
        tilt_to = std::clamp (
          radians_t ((floor - height_of (low)) / shaft_reach * u::rad),
          radians_t {},
          stowed_tilt);
      }
      const radians_t tilt_step = tilt_rate * dt;
      radians_t tilt = kept.tilt;
      tilt += std::clamp (tilt_to - tilt, -tilt_step, tilt_step);
      kept.tilt = tilt;

      const displacement_t hub_local = tail_point (Boat::hub, swing, tilt);
      const position_t prop = where (hub_local);
      const meters_t rise = quantity_cast<isq::length> (
        stern_wave *
        smoothstep (1.5f * u::m / u::s, 6.0f * u::m / u::s, speed));
      const meters_t propeller_depth =
        water.level (prop) + rise - height_of (prop);
      kept.propeller_depth = propeller_depth;
      const proportion_t bite =
        smoothstep (-0.04f * u::m, 0.12f * u::m, propeller_depth);
      // The helmsman holds the tail with its push aimed through the
      // boat's centre of mass; steering swings it from there, moving the
      // propeller across and turning its push with it.
      const displacement_t centre =
        displacement (from_b3 (mass_data (true, kept.passenger).center));
      const direction_t shaft = unit_direction (normalized (direction_value (
        facing (swing_tilt (direction_of (centre - Boat::hub), swing, tilt)))));
      const speed_t ahead =
        quantity_cast<isq::speed> (dot (v - current, shaft));
      const proportion_t revs = std::min (
        rpm,
        quantity_cast<proportion> (quantity_cast<mp_units::dimensionless> (
          unity + (boosted_pitch - unity))));
      const proportion_t slip = std::clamp (
        quantity_cast<proportion> (
          ahead / (pitch_speed * std::max (revs, 0.1f * proportion[one]))),
        none,
        whole);
      const watts_t shaft_power = quantity_cast<isq::power> (
        power * revs *
        (unity +
         quantity_cast<scale_factor> (
           (quantity_cast<scale_factor> (boosted_power / boosted_pitch) -
            unity) *
           boost)));
      const speed_t steerage = 0.5f * u::m / u::s;
      newtons_t thrust = quantity_cast<isq::force> (
        bite *
        std::min (newtons_t (static_thrust * revs * revs),
                  newtons_t (quantity_cast<isq::force> (
                    efficiency * shaft_power / std::max (ahead, steerage)))) *
        (whole - quantity_cast<proportion> (slip * slip * slip)));
      if (drive < let_go)
        thrust =
          quantity_cast<isq::force> (-astern * static_thrust * bite * rpm);
      else if (drive == let_go)
        thrust = 0.0f * u::N;
      kept.thrust = thrust;
      if (thrust != 0.0f * u::N)
        apply_force (body, shaft, thrust, prop);

      // The air's drag on what stands out of the water.
      apply_drag (
        body, v, air_drag * (length (velocity_value (v)) * u::m / u::s));

      // The waves it makes: the panels feel no wave-making resistance, so
      // its hump is added whole. As the speed nears that of a wave as long
      // as the hull, the boat climbs its own bow wave, a fifth of its
      // weight dragging it back, until it gets over onto the plane. By
      // the volume Froude number, v / sqrt (g * displacement^(1/3)), the
      // hump stands at 1.75: some 15 km/h for the helmsman alone, who
      // gets over it, and 16 two-up, who do not, without the governor
      // tied back.
      if (kept.afloat && speed > 0.3f * u::m / u::s) {
        const kilograms_t nobody = 0.0f * u::kg;
        const kilograms_t laden = boat_rig::mass +
                                  (kept.parked ? nobody : crew) +
                                  (kept.passenger ? crew : nobody);
        // The displaced volume's side, and the speed of a wave that long.
        const meters_t side =
          std::cbrt ((laden / rho).numerical_value_in (pow<3> (u::m))) * u::m;
        const speed_t wave = sqrt (g * side);
        const froude_number_t froude =
          quantity_cast<froude_number> (speed / wave);
        const auto off_hump = (froude - hump_froude) / hump_width;
        const newtons_t drag = quantity_cast<isq::force> (
          hump * laden * g *
          std::exp ((-off_hump * off_hump).numerical_value_in (one)));
        apply_drag (body, way, drag / speed);
      }

      b3World_Step (world, seconds_value (dt), substeps);

      // Capsized and drifting: righted where it floats.
      const velocity_t moving = linear_velocity (body);
      const angular_velocity_t turning = angular_velocity (body);
      const auto squared = [] (auto q) { return q * q; };
      kept.overturned = direction_value (facing (local_up))[1] < 0.25f &&
                            dot (moving, moving) < squared (2.0f * u::m / u::s)
                          ? kept.overturned + dt
                          : seconds_t {};
      if (kept.overturned > 3.0f * u::s) {
        const position_t here = where (displacement (Vec3 ()));
        const bool parked = kept.parked;
        const bool passenger = kept.passenger;
        place (here, axis (Vec3 (0, 0, 1)), water.level (here));
        kept.parked = parked;
        kept.passenger = passenger;
        b3Body_SetMassData (body, mass_data (!parked, passenger));
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

  Boat::Boat (const map::SurfaceGeometry& surface)
      : m_impl (std::make_unique<Impl> (surface)) {
    m_impl->place (moppe::position (Vec3 ()), Vec3 (0, 0, 1), -1e6f * u::m);
  }

  Boat::~Boat () = default;

  Boat::State Boat::state () const {
    return m_impl->snapshot ();
  }

  void Boat::restore (const State& state) {
    m_impl->build (state);
  }

  void Boat::place (const Vec3& at, const Vec3& heading, meters_t level) {
    m_impl->place (moppe::position (at), heading, level);
  }

  void Boat::set_trunks (const TrunkField* trunks) {
    Impl& m = *m_impl;
    if (trunks == m.trunks)
      return;
    const State s = m.snapshot ();
    m.trunks = trunks;
    m.build (s);
  }

  void Boat::set_drive (control_signal_t drive) {
    m_impl->kept.drive = drive;
  }

  void Boat::set_boost (control_signal_t boost) {
    m_impl->kept.boost = boost;
  }

  void Boat::set_turn (control_signal_t turn) {
    m_impl->kept.turn = turn;
  }

  void Boat::set_parked (bool parked) {
    Impl& m = *m_impl;
    State& kept = m.kept;
    if (kept.parked == parked)
      return;
    // Someone stepping in or out: the boat settles deeper or lifts.
    kept.asleep = false;
    kept.still = seconds_t {};
    kept.parked = parked;
    b3Body_SetMassData (m.body, boat_rig::mass_data (!parked, kept.passenger));
  }

  void Boat::set_passenger (bool aboard) {
    Impl& m = *m_impl;
    State& kept = m.kept;
    if (kept.passenger == aboard)
      return;
    kept.asleep = false;
    kept.still = seconds_t {};
    kept.passenger = aboard;
    b3Body_SetMassData (m.body, boat_rig::mass_data (!kept.parked, aboard));
  }

  void Boat::update (seconds_t dt, const BoatWater& water) {
    in_substeps (dt, [&] (seconds_t h) {
      if (!m_impl->kept.asleep)
        m_impl->step (h, water);
    });
  }

  Vec3 Boat::position () const {
    return m_impl->point (Vec3 ());
  }

  Vec3 Boat::forward () const {
    return m_impl->axis (Vec3 (0, 0, 1));
  }

  Vec3 Boat::up () const {
    return m_impl->axis (Vec3 (0, 1, 0));
  }

  Vec3 Boat::left () const {
    return m_impl->axis (Vec3 (1, 0, 0));
  }

  Vec3 Boat::velocity () const {
    return from_b3 (b3Body_GetLinearVelocity (m_impl->body));
  }

  Vec3 Boat::point (const Vec3& local) const {
    return m_impl->point (local);
  }

  kilograms_t Boat::mass () const {
    const kilograms_t nobody = 0.0f * u::kg;
    return (boat_rig::mass + (m_impl->kept.parked ? nobody : boat_rig::crew) +
            (m_impl->kept.passenger ? boat_rig::crew : nobody));
  }

  void Boat::push (const Vec3& at, const impulse_t& impulse) {
    Impl& m = *m_impl;
    m.kept.asleep = false;
    m.kept.still = seconds_t {};
    b3Body_ApplyLinearImpulse (
      m.body, to_b3 (impulse_value (impulse)), to_b3 (at), true);
  }

  bool Boat::asleep () const {
    return m_impl->kept.asleep;
  }

  bool Boat::afloat () const {
    return m_impl->kept.afloat;
  }

  bool Boat::grounded () const {
    return m_impl->kept.grounded;
  }

  proportion_t Boat::rpm () const {
    return m_impl->kept.rpm;
  }

  radians_t Boat::swing () const {
    return m_impl->kept.swing;
  }

  radians_t Boat::tilt () const {
    return m_impl->kept.tilt;
  }

  radians_t Boat::propeller_angle () const {
    return m_impl->kept.propeller_angle;
  }

  newtons_t Boat::thrust () const {
    return m_impl->kept.thrust;
  }

  position_t Boat::propeller () const {
    const State& k = m_impl->kept;
    return m_impl->where (tail_point (hub, k.swing, k.tilt));
  }

  meters_t Boat::propeller_depth () const {
    return m_impl->kept.propeller_depth;
  }
}
