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
      constexpr float pi = 3.14159265f;

      // The hull's lines, as tools/figure/boat.py draws them.
      constexpr float depth = 0.33f;     // the sheer over the keel aft
      constexpr float spring = 0.03f;    // the sheer's rise to the bow
      constexpr float rocker = 0.24f;    // the bottom's rise at the stem
      constexpr float flare = 0.82f;     // the chine's breadth to the sheer's
      constexpr float stem_half = 0.12f; // half the blunt bow's width
      const float rake = std::tan (10.0f * pi / 180.0f) * depth;

      // The skiff, some 45 kg of 9 mm birch plywood, glass, epoxy, paint,
      // and two plastic seats; its long-tail, some 23 kg of engine, frame,
      // and shaft hung on the transom; and a couple of litres of petrol.
      // And the helmsman, sat on the after seat, who comes aboard and
      // steps off.
      constexpr kilograms_t mass = 70.0f * u::kg;
      constexpr float mass_centre[3] = { 0.0f, 0.20f, 0.15f };
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
      constexpr float crew_centre[3] = { 0.0f, 0.52f, -0.33f };
      constexpr float fore_centre[3] = { 0.0f, 0.52f, 0.59f };

      // The boat's mass, with or without the helmsman aboard and someone
      // on the fore seat.
      b3MassData mass_data (bool manned, bool passenger) {
        const Vec3 hull (mass_centre[0], mass_centre[1], mass_centre[2]);
        const Vec3 seat (crew_centre[0], crew_centre[1], crew_centre[2]);
        const Vec3 fore (fore_centre[0], fore_centre[1], fore_centre[2]);
        const kilograms_t nobody = 0.0f * u::kg;
        const kilograms_t helmsman = manned ? crew : nobody;
        const kilograms_t forward = passenger ? crew : nobody;
        const kilograms_t total = mass + helmsman + forward;
        const auto kg = [] (kilograms_t m) {
          return m.numerical_value_in (u::kg);
        };
        const Vec3 centre =
          (hull * kg (mass) + seat * kg (helmsman) + fore * kg (forward)) /
          kg (total);
        // Each part's own inertia about its centre, carried to the
        // whole's by the parallel axis rule.
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
      constexpr HullWaterTraits water_traits { .ram = 1.0f * one,
                                               .suction = 0.25f * one,
                                               .damping = 0.6f * u::m / u::s,
                                               .friction = 0.0055f * one };
      // The hump of wave-making resistance, as a share of the weight, and
      // the volume Froude number it stands at and how broad it is.
      constexpr float hump = 0.17f;
      constexpr float hump_froude = 1.95f;
      constexpr float hump_width = 0.45f;

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
      constexpr float efficiency = 0.55f;
      constexpr newtons_t static_thrust = 520.0f * u::N;
      constexpr speed_t pitch_speed = 15.0f * u::m / u::s;
      constexpr float boosted_power = 1.5f;
      constexpr float boosted_pitch = 1.25f;
      constexpr float astern = 0.35f;
      constexpr seconds_t spin_up = 0.6f * u::s;
      constexpr float idle = 0.12f;
      // Revolutions a second as drawn, at full speed.
      constexpr per_second_t propeller_rate = 9.0f / u::s;
      constexpr float most_swing = 0.5f;
      constexpr radians_per_second_t swing_rate = 2.2f * u::rad / u::s;
      // Tipped up, out of the water, as it is left; and how fast the
      // helmsman lets it down or lifts it.
      constexpr float stowed_tilt = 0.45f;
      constexpr radians_per_second_t tilt_rate = 0.5f * u::rad / u::s;
      constexpr float shaft_reach = 1.77f;
      // How far the sickle skeg under the propeller reaches below its hub,
      // keeping it off the bottom.
      constexpr float skeg = 0.14f;
      // The water the boat heaps up behind its transom, round the
      // propeller, once it is under way.
      constexpr float stern_wave = 0.12f;
      // The air's drag on what stands out of the water: half its density
      // times the drag area of the hull and those in it.
      constexpr auto air_drag = 0.8f * u::kg / u::m;

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
      float planform (float s) {
        const float t = std::max (0.0f, (s - 0.45f) / 0.55f);
        const float full =
          Boat::half_beam * (0.97f + 0.03f * std::min (1.0f, s / 0.45f));
        return std::max (stem_half, full * (1.0f - std::pow (t, 2.1f)));
      }
      float chine_y (float s) {
        const float t = std::max (0.0f, (s - 0.45f) / 0.55f);
        return rocker * t * t;
      }
      float chine_hb (float s) {
        return std::max (stem_half, planform (s) * flare);
      }
      float sheer_y (float s) {
        return depth + spring * s * s;
      }
      Vec3 chine (float s, float side) {
        return Vec3 (side * chine_hb (s),
                     chine_y (s),
                     Boat::transom + s * Boat::hull_length);
      }
      Vec3 sheer (float s, float side) {
        return Vec3 (side * planform (s),
                     sheer_y (s),
                     Boat::transom - rake + s * (Boat::hull_length + rake));
      }

      std::vector<HullPanel> hull_panels () {
        std::vector<HullPanel> panels;
        constexpr int stations = 24;
        for (int k = 0; k < stations; ++k) {
          const float s0 = float (k) / stations, s1 = float (k + 1) / stations;
          const float sm = 0.5f * (s0 + s1);
          const Vec3 middle (0,
                             0.5f * (chine_y (sm) + sheer_y (sm)),
                             Boat::transom + sm * Boat::hull_length);
          add_hull_quad (panels,
                         chine (s0, 1),
                         chine (s1, 1),
                         chine (s1, -1),
                         chine (s0, -1),
                         1,
                         4,
                         middle + Vec3 (0, 0.3f, 0));
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
                       chine (0, 1),
                       chine (0, -1),
                       sheer (0, -1),
                       sheer (0, 1),
                       4,
                       4,
                       Vec3 (0, 0.25f, 0),
                       true);
        // The cockpit is open: each panel floods to the sheer over it.
        for (HullPanel& p : panels) {
          const float station = std::clamp (
            (p.centre[2] - Boat::transom) / Boat::hull_length, 0.0f, 1.0f);
          p.open = true;
          p.rim = Vec3 (p.centre[0], sheer_y (station), p.centre[2]);
        }
        return panels;
      }

      const std::vector<HullPanel>& panels () {
        static const std::vector<HullPanel> all = hull_panels ();
        return all;
      }

      // Rotates `v` about y by `swing` after about x by `tilt`.
      Vec3 swing_tilt (const Vec3& v, float swing, float tilt) {
        const float ct = std::cos (tilt), st = std::sin (tilt);
        const Vec3 t (v[0], v[1] * ct - v[2] * st, v[1] * st + v[2] * ct);
        const float cs = std::cos (swing), ss = std::sin (swing);
        return Vec3 (t[0] * cs + t[2] * ss, t[1], -t[0] * ss + t[2] * cs);
      }
    }
  }

  Vec3 Boat::tail_point (const Vec3& local, radians_t swing, radians_t tilt) {
    const Vec3 p (pivot[0], pivot[1], pivot[2]);
    return p + boat_rig::swing_tilt (
                 local - p, radians_value (swing), radians_value (tilt));
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
          patch (
            surface,
            { .ground_friction = 0.6f,
              .obstacle_friction = 0.3f,
              .obstacle_restitution = 0.2f,
              .ground_bit = boat_rig::ground_bit,
              .ground_mask = boat_rig::body_bit,
              .obstacle_bit = boat_rig::trunk_bit,
              .obstacle_mask = boat_rig::body_bit,
              .samples = boat_rig::patch_samples,
              .recentre_distance = boat_rig::patch_recentre_distance * u::m,
              .trunk_reach = boat_rig::trunk_reach * u::m }) {}

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
        const float s = k / 12.0f;
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

    void place (const Vec3& at, const Vec3& heading, float level) {
      const b3Quat q = level_rotation (heading);
      Vec3 centre = at;
      // Its draft at rest, a hand's breadth.
      centre[1] = std::max (
        level - 0.06f, surface_elevation_at (surface, at[0], at[2]) + 0.02f);
      State s = kept;
      s.body = { centre, array_of (q), Vec3 (), Vec3 () };
      patch.origin_for (centre, s.ground_column, s.ground_row);
      s.still = s.overturned = seconds_t {};
      s.asleep = false;
      s.tilt = boat_rig::stowed_tilt * u::rad;
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

      const Vec3 v = from_b3 (b3Body_GetLinearVelocity (body));
      const Vec3 at = point (Vec3 ());
      const Vec3 current = water.current_at (at);
      Vec3 way = v - current;
      way[1] = 0.0f;
      const float speed = length (way);

      // The water on each panel.
      const square_meters_t wetted =
        press_hull (body, panels (), water, water_traits);
      kept.afloat = wetted > 0.5f * u::m2;
      kept.grounded = touching_ground ();

      // The long-tail.
      const float drive =
        kept.parked ? 0.0f
                    : std::clamp (scalar_value (kept.drive), -1.0f, 1.0f);
      const float turn =
        kept.parked ? 0.0f : std::clamp (scalar_value (kept.turn), -1.0f, 1.0f);
      const float boost =
        kept.parked || drive <= 0.0f
          ? 0.0f
          : std::clamp (scalar_value (kept.boost), 0.0f, 1.0f);
      const float wanted_rpm = kept.parked
                                 ? 0.0f
                                 : std::max (idle, std::abs (drive)) *
                                     (1.0f + (boosted_pitch - 1.0f) * boost);
      float rpm = scalar_value (kept.rpm);
      rpm += (wanted_rpm - rpm) *
             std::min (1.0f, (dt / spin_up).numerical_value_in (one));
      kept.rpm = rpm * one;
      kept.propeller_angle =
        std::remainder (
          radians_value (kept.propeller_angle) +
            (rpm * 2.0f * pi * propeller_rate * dt).numerical_value_in (one),
          2.0f * pi) *
        u::rad;
      // Left alone the helmsman's hand holds the course, against the
      // pull of the boat heeling under their weight and the engine's.
      const float yaw =
        dot (from_b3 (b3Body_GetAngularVelocity (body)), axis (Vec3 (0, 1, 0)));
      const float hold = kept.parked
                           ? 0.0f
                           : (1.0f - std::min (1.0f, std::abs (turn) * 8.0f)) *
                               std::clamp (2.5f * yaw, -0.15f, 0.15f);
      const float swing_to = turn * most_swing + hold;
      const float swing_step = radians_value (swing_rate * dt);
      float swing = radians_value (kept.swing);
      swing += std::clamp (swing_to - swing, -swing_step, swing_step);
      kept.swing = swing * u::rad;

      // Down into the water while someone holds it, but lifted over the
      // bottom where it would strike; tipped up out of it when left.
      const Vec3 hub_at (hub[0], hub[1], hub[2]);
      float tilt_to = stowed_tilt;
      if (!kept.parked) {
        const Vec3 low = point (tail_point (hub_at, kept.swing, radians_t {}));
        const float floor =
          surface_elevation_at (surface, low[0], low[2]) + 0.06f + skeg;
        tilt_to =
          std::clamp ((floor - low[1]) / shaft_reach, 0.0f, stowed_tilt);
      }
      const float tilt_step = radians_value (tilt_rate * dt);
      float tilt = radians_value (kept.tilt);
      tilt += std::clamp (tilt_to - tilt, -tilt_step, tilt_step);
      kept.tilt = tilt * u::rad;

      const Vec3 hub_local = tail_point (hub_at, kept.swing, kept.tilt);
      const Vec3 prop = point (hub_local);
      const float rise = stern_wave * smoothstep (1.5f, 6.0f, speed);
      const float propeller_depth = water.level_at (prop) + rise - prop[1];
      kept.propeller_depth = propeller_depth * u::m;
      const float bite = smoothstep (-0.04f, 0.12f, propeller_depth);
      // The helmsman holds the tail with its push aimed through the
      // boat's centre of mass; steering swings it from there, moving the
      // propeller across and turning its push with it.
      const Vec3 centre = from_b3 (mass_data (true, kept.passenger).center);
      const Vec3 shaft = normalized (
        axis (swing_tilt (normalized (centre - hub_at), swing, tilt)));
      const speed_t ahead = dot (v - current, shaft) * u::m / u::s;
      const float revs = std::min (rpm, 1.0f + (boosted_pitch - 1.0f));
      const float slip =
        std::clamp ((ahead / (pitch_speed * std::max (revs, 0.1f)))
                      .numerical_value_in (one),
                    0.0f,
                    1.0f);
      const watts_t shaft_power =
        power * revs * (1.0f + (boosted_power / boosted_pitch - 1.0f) * boost);
      const speed_t steerage = 0.5f * u::m / u::s;
      newtons_t thrust = bite *
                         std::min (static_thrust * revs * revs,
                                   newtons_t (efficiency * shaft_power /
                                              std::max (ahead, steerage))) *
                         (1.0f - slip * slip * slip);
      if (drive < 0.0f)
        thrust = -astern * static_thrust * bite * rpm;
      else if (drive == 0.0f)
        thrust = 0.0f * u::N;
      kept.thrust = thrust;
      if (thrust != 0.0f * u::N)
        apply_force (body, shaft, thrust, prop);

      // The air's drag on what stands out of the water.
      apply_drag (body, v, air_drag * (length (v) * u::m / u::s));

      // The waves it makes: the panels feel no wave-making resistance, so
      // its hump is added whole. As the speed nears that of a wave as long
      // as the hull, the boat climbs its own bow wave, a fifth of its
      // weight dragging it back, until it gets over onto the plane. By
      // the volume Froude number, v / sqrt (g * displacement^(1/3)), the
      // hump stands at 1.75: some 15 km/h for the helmsman alone, who
      // gets over it, and 16 two-up, who do not, without the governor
      // tied back.
      if (kept.afloat && speed > 0.3f) {
        const kilograms_t nobody = 0.0f * u::kg;
        const kilograms_t laden = boat_rig::mass +
                                  (kept.parked ? nobody : crew) +
                                  (kept.passenger ? crew : nobody);
        // The displaced volume's side, and the speed of a wave that long.
        const meters_t side =
          std::cbrt ((laden / rho).numerical_value_in (pow<3> (u::m))) * u::m;
        const speed_t wave = sqrt (g * side);
        const float froude =
          ((speed * u::m / u::s) / wave).numerical_value_in (one);
        const float k = (froude - hump_froude) / hump_width;
        const newtons_t drag = hump * laden * g * std::exp (-k * k);
        apply_drag (body, way, drag / (speed * u::m / u::s));
      }

      b3World_Step (world, seconds_value (dt), substeps);

      // Capsized and drifting: righted where it floats.
      const Vec3 moving = from_b3 (b3Body_GetLinearVelocity (body));
      const Vec3 turning = from_b3 (b3Body_GetAngularVelocity (body));
      const float upright = axis (Vec3 (0, 1, 0))[1];
      kept.overturned = upright < 0.25f && length2 (moving) < 4.0f
                          ? kept.overturned + dt
                          : seconds_t {};
      if (kept.overturned > 3.0f * u::s) {
        const Vec3 here = point (Vec3 ());
        const bool parked = kept.parked;
        const bool passenger = kept.passenger;
        place (here, axis (Vec3 (0, 0, 1)), water.level_at (here));
        kept.parked = parked;
        kept.passenger = passenger;
        b3Body_SetMassData (body, mass_data (!parked, passenger));
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

  Boat::Boat (const map::SurfaceGeometry& surface)
      : m_impl (std::make_unique<Impl> (surface)) {
    m_impl->place (Vec3 (), Vec3 (0, 0, 1), -1e6f);
  }

  Boat::~Boat () = default;

  Boat::State Boat::state () const {
    return m_impl->snapshot ();
  }

  void Boat::restore (const State& state) {
    m_impl->build (state);
  }

  void Boat::place (const Vec3& at, const Vec3& heading, meters_t level) {
    m_impl->place (at, heading, meters_value (level));
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

  Vec3 Boat::propeller () const {
    const State& k = m_impl->kept;
    return m_impl->point (
      tail_point (Vec3 (hub[0], hub[1], hub[2]), k.swing, k.tilt));
  }

  meters_t Boat::propeller_depth () const {
    return m_impl->kept.propeller_depth;
  }
}
