#include <moppe/mov/boat.hh>
#include <moppe/mov/box3d_vec.hh>
#include <moppe/mov/ground_patch.hh>

#include <algorithm>
#include <cmath>
#include <vector>

#include <box3d/box3d.h>

namespace moppe::mov {
  namespace {
    // Kept in a namespace of its own: the engine is a unity build.
    namespace boat_rig {
      constexpr float g = 9.81f;
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
      constexpr float mass = 70.0f;
      constexpr float mass_centre[3] = { 0.0f, 0.20f, 0.15f };
      // Pitch, yaw, and roll about the centre of mass: a 4 m plank with
      // the engine's weight out at one end, and narrow.
      constexpr float inertia[3] = { 110.0f, 120.0f, 9.0f };
      constexpr float crew = 80.0f;
      constexpr float crew_centre[3] = { 0.0f, 0.52f, -0.33f };
      constexpr float fore_centre[3] = { 0.0f, 0.52f, 0.59f };

      // The boat's mass, with or without the helmsman aboard and someone
      // on the fore seat.
      b3MassData mass_data (bool manned, bool passenger) {
        const Vec3 hull (mass_centre[0], mass_centre[1], mass_centre[2]);
        const Vec3 seat (crew_centre[0], crew_centre[1], crew_centre[2]);
        const Vec3 fore (fore_centre[0], fore_centre[1], fore_centre[2]);
        const float total =
          mass + (manned ? crew : 0.0f) + (passenger ? crew : 0.0f);
        const Vec3 centre = (hull * mass + seat * (manned ? crew : 0.0f) +
                             fore * (passenger ? crew : 0.0f)) /
                            total;
        // Each part's own inertia about its centre, carried to the
        // whole's by the parallel axis rule; the seated helmsman about
        // their own middle much as a 0.4 by 0.9 m column.
        float about[3] = { inertia[0], inertia[1], inertia[2] };
        const auto carry = [&] (const Vec3& at, float m) {
          const Vec3 d = at - centre;
          about[0] += m * (d[1] * d[1] + d[2] * d[2]);
          about[1] += m * (d[0] * d[0] + d[2] * d[2]);
          about[2] += m * (d[0] * d[0] + d[1] * d[1]);
        };
        carry (hull, mass);
        for (const auto& [aboard, at] : { std::pair (manned, seat),
                                          std::pair (passenger, fore) })
          if (aboard) {
            carry (at, crew);
            about[0] += crew * 0.07f;
            about[1] += crew * 0.03f;
            about[2] += crew * 0.07f;
          }
        b3MassData m {};
        m.mass = total;
        m.center = to_b3 (centre);
        m.inertia.cx = { about[0], 0.0f, 0.0f };
        m.inertia.cy = { 0.0f, about[1], 0.0f };
        m.inertia.cz = { 0.0f, 0.0f, about[2] };
        return m;
      }

      constexpr float rho = 1000.0f;
      // The pressure where a panel pushes into the water, as a share of
      // the stagnation pressure, and the lesser suction where it draws
      // away; a linear part of it, metres a second, that damps the boat's
      // bobbing; and the skin friction along it.
      constexpr float ram = 1.0f;
      constexpr float suction = 0.25f;
      constexpr float damping = 0.6f;
      constexpr float friction = 0.0055f;
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
      constexpr float power = 4100.0f;
      constexpr float efficiency = 0.55f;
      constexpr float static_thrust = 520.0f;
      constexpr float pitch_speed = 15.0f;
      constexpr float boosted_power = 1.5f;
      constexpr float boosted_pitch = 1.25f;
      constexpr float astern = 0.35f;
      constexpr float spin_up = 0.6f;
      constexpr float idle = 0.12f;
      // Revolutions a second as drawn, at full speed.
      constexpr float propeller_rate = 9.0f;
      constexpr float most_swing = 0.5f;
      constexpr float swing_rate = 2.2f;
      // Tipped up, out of the water, as it is left; and how fast the
      // helmsman lets it down or lifts it.
      constexpr float stowed_tilt = 0.45f;
      constexpr float tilt_rate = 0.5f;
      constexpr float shaft_reach = 1.77f;
      // How far the sickle skeg under the propeller reaches below its hub,
      // keeping it off the bottom.
      constexpr float skeg = 0.14f;
      // The water the boat heaps up behind its transom, round the
      // propeller, once it is under way.
      constexpr float stern_wave = 0.12f;

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

      // A small piece of the hull's skin: its middle, its outward normal
      // and area, and half its two edges, by which its depth across it
      // is reckoned.
      struct Panel {
        Vec3 centre, normal;
        float area;
        Vec3 half_u, half_v;
        // The transom ventilates: the water draws away from it without
        // sucking at it.
        bool transom = false;
      };

      // Splits the quad a, b, c, d (in order round it) into n by m panels
      // whose normals face away from `inside`.
      void add_quad (std::vector<Panel>& out,
                     const Vec3& a,
                     const Vec3& b,
                     const Vec3& c,
                     const Vec3& d,
                     int n,
                     int m,
                     const Vec3& inside,
                     bool transom = false) {
        const auto at = [&] (float u, float v) {
          return (a * (1 - u) + b * u) * (1 - v) + (d * (1 - u) + c * u) * v;
        };
        for (int i = 0; i < n; ++i)
          for (int j = 0; j < m; ++j) {
            const float u0 = float (i) / n, u1 = float (i + 1) / n;
            const float v0 = float (j) / m, v1 = float (j + 1) / m;
            const Vec3 p00 = at (u0, v0), p10 = at (u1, v0);
            const Vec3 p11 = at (u1, v1), p01 = at (u0, v1);
            Vec3 area = cross (p11 - p00, p01 - p10) * 0.5f;
            const float size = length (area);
            if (size < 1e-6f)
              continue;
            Panel p;
            p.centre = (p00 + p10 + p11 + p01) * 0.25f;
            p.normal = area / size;
            if (dot (p.normal, p.centre - inside) < 0.0f)
              p.normal = -p.normal;
            p.area = size;
            p.half_u = ((p10 + p11) - (p00 + p01)) * 0.25f;
            p.half_v = ((p01 + p11) - (p00 + p10)) * 0.25f;
            p.transom = transom;
            out.push_back (p);
          }
      }

      std::vector<Panel> hull_panels () {
        std::vector<Panel> panels;
        constexpr int stations = 24;
        for (int k = 0; k < stations; ++k) {
          const float s0 = float (k) / stations, s1 = float (k + 1) / stations;
          const float sm = 0.5f * (s0 + s1);
          const Vec3 middle (0, 0.5f * (chine_y (sm) + sheer_y (sm)),
                             Boat::transom + sm * Boat::hull_length);
          add_quad (panels,
                    chine (s0, 1),
                    chine (s1, 1),
                    chine (s1, -1),
                    chine (s0, -1),
                    1,
                    4,
                    middle + Vec3 (0, 0.3f, 0));
          for (const float side : { 1.0f, -1.0f })
            add_quad (panels,
                      chine (s0, side),
                      chine (s1, side),
                      sheer (s1, side),
                      sheer (s0, side),
                      1,
                      5,
                      middle);
        }
        add_quad (panels,
                  chine (0, 1),
                  chine (0, -1),
                  sheer (0, -1),
                  sheer (0, 1),
                  4,
                  4,
                  Vec3 (0, 0.25f, 0),
                  true);
        return panels;
      }

      const std::vector<Panel>& panels () {
        static const std::vector<Panel> all = hull_panels ();
        return all;
      }

      b3Quat level_rotation (const Vec3& heading) {
        Vec3 f = heading;
        f[1] = 0.0f;
        f = length2 (f) > 1e-8f ? normalized (f) : Vec3 (0, 0, 1);
        const Vec3 up (0, 1, 0);
        const Vec3 left = cross (up, f);
        const b3Matrix3 m { to_b3 (left), to_b3 (up), to_b3 (f) };
        return b3NormalizeQuat (b3MakeQuatFromMatrix (&m));
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

  Vec3 Boat::tail_point (const Vec3& local, float swing, float tilt) {
    const Vec3 p (pivot[0], pivot[1], pivot[2]);
    return p + boat_rig::swing_tilt (local - p, swing, tilt);
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
      def.gravity = { 0.0f, -boat_rig::g, 0.0f };
      def.enableSleep = false;
      def.workerCount = 1;
      world = b3CreateWorld (&def);
      patch.build (world, trunks, s.ground_column, s.ground_row);
      kept = s;
      create_body (s.body);
    }

    RigidBodyState read () const {
      RigidBodyState s;
      s.position = from_b3 (b3Body_GetPosition (body));
      s.rotation = array_of (b3Body_GetRotation (body));
      s.linear_velocity = from_b3 (b3Body_GetLinearVelocity (body));
      s.angular_velocity = from_b3 (b3Body_GetAngularVelocity (body));
      return s;
    }

    State snapshot () const {
      State s = kept;
      s.body = read ();
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
      const b3Quat q = boat_rig::level_rotation (heading);
      Vec3 centre = at;
      // Its draft at rest, a hand's breadth.
      centre[1] = std::max (level - 0.06f,
                            surface_elevation_at (surface, at[0], at[2]) +
                              0.02f);
      State s = kept;
      s.body = { centre, array_of (q), Vec3 (), Vec3 () };
      patch.origin_for (centre, s.ground_column, s.ground_row);
      s.still = s.overturned = 0.0f;
      s.asleep = false;
      s.tilt = boat_rig::stowed_tilt;
      s.swing = s.rpm = s.thrust = 0.0f;
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

    void step (float dt, const BoatWater& water) {
      using namespace boat_rig;
      patch.recentre (world, trunks, from_b3 (b3Body_GetPosition (body)));

      const b3Quat q = b3Body_GetRotation (body);
      const Vec3 up_local =
        from_b3 (b3InvRotateVector (q, { 0.0f, 1.0f, 0.0f }));
      const Vec3 v = from_b3 (b3Body_GetLinearVelocity (body));
      const Vec3 at = point (Vec3 ());
      const Vec3 current = water.current (at[0], at[2]);
      Vec3 way = v - current;
      way[1] = 0.0f;
      const float speed = length (way);

      // The water on each panel.
      float wetted = 0.0f;
      for (const Panel& p : panels ()) {
        const Vec3 c = point (p.centre);
        const float level = water.level (c[0], c[2]);
        const float centre_depth = level - c[1];
        // How far the panel reaches up and down about its middle, and so
        // how much of it is under and how deep that part is on average.
        const float reach = std::abs (dot (p.half_u, up_local)) +
                            std::abs (dot (p.half_v, up_local));
        float wet, mean;
        if (reach < 1e-3f) {
          wet = centre_depth > 0.0f ? 1.0f : 0.0f;
          mean = centre_depth;
        } else if (centre_depth >= reach) {
          wet = 1.0f;
          mean = centre_depth;
        } else {
          wet = std::clamp ((centre_depth + reach) / (2.0f * reach), 0.0f, 1.0f);
          mean = 0.5f * (centre_depth + reach);
        }
        if (wet <= 0.0f)
          continue;
        const float area = p.area * wet;
        wetted += area;
        const Vec3 n = axis (p.normal);
        const Vec3 moving =
          from_b3 (b3Body_GetLocalPointVelocity (body, to_b3 (p.centre))) -
          water.current (c[0], c[2]);
        const float vn = dot (moving, n);
        const float through = length (moving);
        float pressure = rho * g * std::max (mean, 0.0f);
        // The bobbing is damped up and down, not along the way.
        pressure += rho * damping * std::abs (p.normal[1]) * vn;
        if (vn > 0.0f)
          pressure += 0.5f * rho * ram * through * vn;
        else if (!p.transom)
          pressure += 0.5f * rho * suction * through * vn;
        pressure = std::max (pressure, -0.5f * rho * g);
        Vec3 force = n * (-pressure * area);
        const Vec3 along = moving - n * vn;
        force = force - along * (0.5f * rho * friction * length (along) * area);
        b3Body_ApplyForce (body, to_b3 (force), to_b3 (c), true);
      }
      kept.afloat = wetted > 0.5f;
      kept.grounded = touching_ground ();

      // The long-tail.
      const float drive =
        kept.parked ? 0.0f : std::clamp (kept.drive, -1.0f, 1.0f);
      const float turn =
        kept.parked ? 0.0f : std::clamp (kept.turn, -1.0f, 1.0f);
      const float boost =
        kept.parked || drive <= 0.0f ? 0.0f
                                     : std::clamp (kept.boost, 0.0f, 1.0f);
      const float wanted_rpm =
        kept.parked ? 0.0f
                    : std::max (idle, std::abs (drive)) *
                        (1.0f + (boosted_pitch - 1.0f) * boost);
      kept.rpm += (wanted_rpm - kept.rpm) * std::min (1.0f, dt / spin_up);
      kept.propeller_angle = std::remainder (
        kept.propeller_angle + kept.rpm * 2.0f * pi * propeller_rate * dt,
        2.0f * pi);
      // Left alone the helmsman's hand holds the course, against the
      // pull of the boat heeling under their weight and the engine's.
      const float yaw = dot (from_b3 (b3Body_GetAngularVelocity (body)),
                             axis (Vec3 (0, 1, 0)));
      const float hold =
        kept.parked ? 0.0f
                    : (1.0f - std::min (1.0f, std::abs (turn) * 8.0f)) *
                        std::clamp (2.5f * yaw, -0.15f, 0.15f);
      const float swing_to = turn * most_swing + hold;
      const float swing_step = swing_rate * dt;
      kept.swing += std::clamp (swing_to - kept.swing, -swing_step, swing_step);

      // Down into the water while someone holds it, but lifted over the
      // bottom where it would strike; tipped up out of it when left.
      const Vec3 hub_at (hub[0], hub[1], hub[2]);
      float tilt_to = stowed_tilt;
      if (!kept.parked) {
        const Vec3 low = point (tail_point (hub_at, kept.swing, 0.0f));
        const float floor =
          surface_elevation_at (surface, low[0], low[2]) + 0.06f + skeg;
        tilt_to =
          std::clamp ((floor - low[1]) / shaft_reach, 0.0f, stowed_tilt);
      }
      const float tilt_step = tilt_rate * dt;
      kept.tilt += std::clamp (tilt_to - kept.tilt, -tilt_step, tilt_step);

      const Vec3 hub_local = tail_point (hub_at, kept.swing, kept.tilt);
      const Vec3 prop = point (hub_local);
      const float rise = stern_wave * smoothstep (1.5f, 6.0f, speed);
      kept.propeller_depth = water.level (prop[0], prop[2]) + rise - prop[1];
      const float bite = smoothstep (-0.04f, 0.12f, kept.propeller_depth);
      // The helmsman holds the tail with its push aimed through the
      // boat's centre of mass; steering swings it from there, moving the
      // propeller across and turning its push with it.
      const Vec3 centre = from_b3 (mass_data (true, kept.passenger).center);
      const Vec3 shaft = normalized (axis (swing_tilt (
        normalized (centre - hub_at), kept.swing, kept.tilt)));
      const float ahead = dot (v - current, shaft);
      const float revs = std::min (kept.rpm, 1.0f + (boosted_pitch - 1.0f));
      const float slip =
        std::clamp (ahead / (pitch_speed * std::max (revs, 0.1f)), 0.0f,
                    1.0f);
      const float shaft_power =
        power * revs *
        (1.0f + (boosted_power / boosted_pitch - 1.0f) * boost);
      float thrust =
        bite *
        std::min (static_thrust * revs * revs,
                  efficiency * shaft_power / std::max (ahead, 0.5f)) *
        (1.0f - slip * slip * slip);
      if (drive < 0.0f)
        thrust = -astern * static_thrust * bite * kept.rpm;
      else if (drive == 0.0f)
        thrust = 0.0f;
      kept.thrust = thrust;
      if (thrust != 0.0f)
        b3Body_ApplyForce (body, to_b3 (shaft * thrust), to_b3 (prop), true);

      // The air's drag on what stands out of the water.
      const Vec3 air = v * (-0.8f * length (v));
      b3Body_ApplyForceToCenter (body, to_b3 (air), true);

      // The waves it makes: the panels feel no wave-making resistance, so
      // its hump is added whole. As the speed nears that of a wave as long
      // as the hull, the boat climbs its own bow wave, a fifth of its
      // weight dragging it back, until it gets over onto the plane. By
      // the volume Froude number, v / sqrt (g * displacement^(1/3)), the
      // hump stands at 1.75: some 15 km/h for the helmsman alone, who
      // gets over it, and 16 two-up, who do not, without the governor
      // tied back.
      if (kept.afloat && speed > 0.3f) {
        const float weight =
          boat_rig::mass + (kept.parked ? 0.0f : crew) +
          (kept.passenger ? crew : 0.0f);
        const float froude =
          speed / std::sqrt (g * std::cbrt (weight / rho));
        const float k = (froude - hump_froude) / hump_width;
        const float drag = hump * weight * g * std::exp (-k * k);
        b3Body_ApplyForceToCenter (body, to_b3 (way * (-drag / speed)), true);
      }

      b3World_Step (world, dt, substeps);

      // Capsized and drifting: righted where it floats.
      const Vec3 moving = from_b3 (b3Body_GetLinearVelocity (body));
      const Vec3 turning = from_b3 (b3Body_GetAngularVelocity (body));
      const float upright = axis (Vec3 (0, 1, 0))[1];
      kept.overturned =
        upright < 0.25f && length2 (moving) < 4.0f ? kept.overturned + dt : 0.0f;
      if (kept.overturned > 3.0f) {
        const Vec3 here = point (Vec3 ());
        const bool parked = kept.parked;
        const bool passenger = kept.passenger;
        place (here, axis (Vec3 (0, 0, 1)), water.level (here[0], here[2]));
        kept.parked = parked;
        kept.passenger = passenger;
        b3Body_SetMassData (body, mass_data (!parked, passenger));
        return;
      }

      // Left alone and still, on still water or on the shore: it rests.
      const bool quiet = length2 (moving) < 0.04f * 0.04f &&
                         length2 (turning) < 0.04f * 0.04f &&
                         length2 (current) < 0.05f * 0.05f;
      kept.still = kept.parked && quiet ? kept.still + dt : 0.0f;
      if (kept.still > 2.0f)
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

  void Boat::place (const Vec3& at, const Vec3& heading, float level) {
    m_impl->place (at, heading, level);
  }

  void Boat::set_trunks (const TrunkField* trunks) {
    Impl& m = *m_impl;
    if (trunks == m.trunks)
      return;
    const State s = m.snapshot ();
    m.trunks = trunks;
    m.build (s);
  }

  void Boat::set_drive (float drive) {
    m_impl->kept.drive = drive;
  }

  void Boat::set_boost (float boost) {
    m_impl->kept.boost = boost;
  }

  void Boat::set_turn (float turn) {
    m_impl->kept.turn = turn;
  }

  void Boat::set_parked (bool parked) {
    Impl& m = *m_impl;
    State& kept = m.kept;
    if (kept.parked == parked)
      return;
    // Someone stepping in or out: the boat settles deeper or lifts.
    kept.asleep = false;
    kept.still = 0.0f;
    kept.parked = parked;
    b3Body_SetMassData (m.body, boat_rig::mass_data (!parked, kept.passenger));
  }

  void Boat::set_passenger (bool aboard) {
    Impl& m = *m_impl;
    State& kept = m.kept;
    if (kept.passenger == aboard)
      return;
    kept.asleep = false;
    kept.still = 0.0f;
    kept.passenger = aboard;
    b3Body_SetMassData (m.body, boat_rig::mass_data (!kept.parked, aboard));
  }

  void Boat::update (float dt, const BoatWater& water) {
    if (m_impl->kept.asleep || dt <= 0.0f)
      return;
    m_impl->step (dt, water);
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

  float Boat::mass () const {
    return boat_rig::mass + (m_impl->kept.parked ? 0.0f : boat_rig::crew) +
           (m_impl->kept.passenger ? boat_rig::crew : 0.0f);
  }

  void Boat::push (const Vec3& at, const Vec3& impulse) {
    Impl& m = *m_impl;
    m.kept.asleep = false;
    m.kept.still = 0.0f;
    b3Body_ApplyLinearImpulse (m.body, to_b3 (impulse), to_b3 (at), true);
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

  float Boat::rpm () const {
    return m_impl->kept.rpm;
  }

  float Boat::swing () const {
    return m_impl->kept.swing;
  }

  float Boat::tilt () const {
    return m_impl->kept.tilt;
  }

  float Boat::propeller_angle () const {
    return m_impl->kept.propeller_angle;
  }

  float Boat::thrust () const {
    return m_impl->kept.thrust;
  }

  Vec3 Boat::propeller () const {
    const State& k = m_impl->kept;
    return m_impl->point (
      tail_point (Vec3 (hub[0], hub[1], hub[2]), k.swing, k.tilt));
  }

  float Boat::propeller_depth () const {
    return m_impl->kept.propeller_depth;
  }
}
