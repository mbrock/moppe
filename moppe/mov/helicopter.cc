#include <moppe/mov/box3d_vec.hh>
#include <moppe/mov/ground_patch.hh>
#include <moppe/mov/helicopter.hh>

#include <algorithm>
#include <cmath>

#include <box3d/box3d.h>

namespace moppe::mov {
  namespace {
    // Kept in a namespace of its own: the engine is a unity build.
    namespace heli_rig {
      constexpr int substeps = 4;

      constexpr kilograms_t mass = 1100.0f * u::kg;
      // Pitch, yaw, and roll about the centre of mass, under the mast.
      constexpr kilogram_square_metres_t inertia[3] = { 2400.0f * u::kg * u::m2,
                                                        2600.0f * u::kg* u::m2,
                                                        700.0f * u::kg* u::m2 };
      constexpr float mass_centre[3] = { 0.0f, 1.15f, 0.05f };

      // The rotor at flying speed lifts twice the helicopter's weight;
      // it takes a few seconds to get there and longer to run down.
      constexpr float thrust_to_weight = 2.0f;
      constexpr seconds_t spool_up = 3.5f * u::s;
      constexpr seconds_t spool_down = 10.0f * u::s;
      // Revolutions a second at flying speed, main and tail, as drawn:
      // slower than a real rotor's six or seven, which a frame at a time
      // would only smear into nothing.
      constexpr frequency_t rotor_rate = 2.2f * u::Hz;
      constexpr frequency_t tail_rate = 5.0f * u::Hz;

      // What the flight computer asks of it.
      constexpr speed_t top_speed = 45.0f * u::m / u::s;
      constexpr speed_t back_speed = 9.0f * u::m / u::s;
      constexpr speed_t climb_rate = 7.5f * u::m / u::s;
      constexpr speed_t descent_rate = 6.0f * u::m / u::s;
      constexpr radians_t max_tilt = 0.52f * u::rad; // 30 degrees
      // The speed and climb loops, the acceleration asked for a speed
      // short of the one wanted.
      constexpr damping_t speed_gain = 0.7f / u::s;
      constexpr damping_t climb_gain = 2.2f / u::s;
      // The heading loop turns at most this many radians a second.
      constexpr float yaw_rate = 1.6f;
      // The attitude loop, as a natural frequency and damping.
      constexpr float attitude_hertz = 0.85f;
      constexpr float attitude_damping = 0.85f;
      constexpr float yaw_gain = 4.0f;
      // Kept above the ground, metres: the hover taxi's height, and more
      // the faster it goes.
      constexpr meters_t taxi_height = 1.6f * u::m;
      constexpr seconds_t clearance_per_speed = 0.12f * u::s;

      // Half the air's density times the fuselage's drag area, and the
      // water's hold on it, per kilogram of it, once it is in up to the
      // cabin's floor.
      constexpr auto air_drag = 0.85f * u::kg / u::m;
      constexpr damping_t water_drag = 1.6f / u::s;

      constexpr int patch_samples = 97;
      constexpr float patch_recentre_distance = 24.0f;
      constexpr float trunk_reach = 72.0f;

      constexpr std::uint64_t ground_bit = 1;
      constexpr std::uint64_t trunk_bit = 2;
      constexpr std::uint64_t body_bit = 8;

      constexpr float pi = 3.14159265f;

      // The skids' ends, where it meets the ground.
      constexpr float skid_ends[4][3] = { { Helicopter::skid, 0.0f, 1.35f },
                                          { -Helicopter::skid, 0.0f, 1.35f },
                                          { Helicopter::skid, 0.0f, -1.35f },
                                          { -Helicopter::skid, 0.0f, -1.35f } };
    }
  }

  struct Helicopter::Impl {
    const map::SurfaceGeometry& surface;
    GroundPatch patch;
    const TrunkField* trunks = nullptr;
    b3WorldId world = b3_nullWorldId;
    b3BodyId body = b3_nullBodyId;
    State kept;

    explicit Impl (const map::SurfaceGeometry& surface)
        : surface (surface),
          patch (surface,
                 { .ground_friction = 1.0f,
                   .obstacle_friction = 0.3f,
                   .obstacle_restitution = 0.2f,
                   .ground_bit = heli_rig::ground_bit,
                   .ground_mask = heli_rig::body_bit,
                   .obstacle_bit = heli_rig::trunk_bit,
                   .obstacle_mask = heli_rig::body_bit,
                   .samples = heli_rig::patch_samples,
                   .recentre_distance = heli_rig::patch_recentre_distance,
                   .trunk_reach = heli_rig::trunk_reach }) {}

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
      b3BodyDef def = b3DefaultBodyDef ();
      def.type = b3_dynamicBody;
      def.position = to_b3 (s.position);
      def.rotation = quat_of (s.rotation);
      def.linearVelocity = to_b3 (s.linear_velocity);
      def.angularVelocity = to_b3 (s.angular_velocity);
      def.enableSleep = false;
      body = b3CreateBody (world, &def);

      // The cabin and the tail boom meet trunks and the ground; the skids
      // are what it stands on.
      b3ShapeDef shape = b3DefaultShapeDef ();
      shape.updateBodyMass = false;
      shape.baseMaterial.friction = 0.5f;
      shape.filter.categoryBits = heli_rig::body_bit;
      shape.filter.maskBits = heli_rig::ground_bit | heli_rig::trunk_bit;
      const b3BoxHull cabin =
        b3MakeOffsetBoxHull (0.75f, 0.70f, 1.30f, { 0.0f, 1.38f, 0.30f });
      b3CreateHullShape (body, &shape, &cabin.base);
      const b3BoxHull boom =
        b3MakeOffsetBoxHull (0.20f, 0.22f, 2.30f, { 0.0f, 1.75f, -3.60f });
      b3CreateHullShape (body, &shape, &boom.base);
      shape.baseMaterial.friction = 0.7f;
      for (const float x : { skid, -skid }) {
        const b3Capsule runner { { x, 0.05f, -1.40f },
                                 { x, 0.05f, 1.45f },
                                 0.05f };
        b3CreateCapsuleShape (body, &shape, &runner);
      }
      b3Body_SetMassData (body,
                          body_mass (heli_rig::mass,
                                     Vec3 (heli_rig::mass_centre[0],
                                           heli_rig::mass_centre[1],
                                           heli_rig::mass_centre[2]),
                                     { heli_rig::inertia[0],
                                       heli_rig::inertia[1],
                                       heli_rig::inertia[2] }));
    }

    void build (const State& s) {
      destroy ();
      b3WorldDef def = b3DefaultWorldDef ();
      def.gravity = {
        0.0f, -standard_gravity.numerical_value_in (u::m / pow<2> (u::s)), 0.0f
      };
      def.enableSleep = false;
      def.workerCount = 1;
      world = b3CreateWorld (&def);
      patch.build (world, trunks, s.ground_column, s.ground_row);
      create_body (s.body);
      kept = s;
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

    float ground (const Vec3& p) const {
      return surface_elevation_at (surface, p[0], p[2]);
    }

    float altitude () const {
      float least = 1e30f;
      for (const auto& end : heli_rig::skid_ends) {
        const Vec3 p = point (Vec3 (end[0], end[1], end[2]));
        least = std::min (least, p[1] - ground (p));
      }
      return least;
    }

    State standing (const Vec3& at, const Vec3& heading) const {
      const b3Quat q = level_rotation (heading);
      const std::array<float, 4> rotation = array_of (q);
      float highest = -1e30f;
      for (const auto& end : heli_rig::skid_ends) {
        const Vec3 under =
          at + rotated (rotation, Vec3 (end[0], end[1], end[2]));
        highest = std::max (highest, ground (under));
      }
      Vec3 centre = at;
      centre[1] = highest + 0.03f;
      State s = kept;
      s.body = { centre, rotation, Vec3 (), Vec3 () };
      patch.origin_for (centre, s.ground_column, s.ground_row);
      s.grounded = true;
      s.still = s.overturned = seconds_t {};
      s.asleep = false;
      return s;
    }

    void place (const Vec3& at, const Vec3& heading) {
      const State s = standing (at, heading);
      if (B3_IS_NULL (world)) {
        build (s);
        return;
      }
      b3Body_SetTransform (
        body, to_b3 (s.body.position), quat_of (s.body.rotation));
      b3Body_SetLinearVelocity (body, { 0.0f, 0.0f, 0.0f });
      b3Body_SetAngularVelocity (body, { 0.0f, 0.0f, 0.0f });
      kept = s;
      patch.recentre (world, trunks, s.body.position);
    }

    void step (seconds_t dt, meters_t water_level) {
      using namespace heli_rig;
      patch.recentre (world, trunks, from_b3 (b3Body_GetPosition (body)));

      const seconds_t step = dt;
      const auto share = [] (auto q) { return q.numerical_value_in (one); };
      const float was = scalar_value (kept.rotor);
      const float rotor = kept.parked
                            ? std::max (0.0f, was - share (step / spool_down))
                            : std::min (1.0f, was + share (step / spool_up));
      kept.rotor = rotor * one;
      kept.rotor_angle =
        std::remainder (radians_value (kept.rotor_angle) +
                          share (rotor * 2.0f * pi * rotor_rate * step),
                        2.0f * pi) *
        u::rad;
      kept.tail_angle =
        std::remainder (radians_value (kept.tail_angle) +
                          share (rotor * 2.0f * pi * tail_rate * step),
                        2.0f * pi) *
        u::rad;

      const Vec3 up = axis (Vec3 (0, 1, 0));
      const Vec3 v = from_b3 (b3Body_GetLinearVelocity (body));
      const Vec3 w = from_b3 (b3Body_GetAngularVelocity (body));
      const float height = altitude ();
      kept.grounded = height < 0.12f;

      const float drive =
        kept.parked ? 0.0f
                    : std::clamp (scalar_value (kept.drive), -1.0f, 1.0f);
      const float turn =
        kept.parked ? 0.0f : std::clamp (scalar_value (kept.turn), -1.0f, 1.0f);
      const float lift =
        kept.parked ? 0.0f : std::clamp (scalar_value (kept.lift), -1.0f, 1.0f);
      const float authority = rotor * rotor;

      // The way it heads, level.
      Vec3 ahead = axis (Vec3 (0, 0, 1));
      ahead[1] = 0.0f;
      ahead = length2 (ahead) > 1e-6f ? normalized (ahead) : Vec3 (0, 0, 1);
      Vec3 across = v;
      across[1] = 0.0f;
      const float speed = length (across);
      const speed_t ground_speed = speed * u::m / u::s;

      // Sitting on its skids with nothing asked of it, it stays sat.
      const bool sitting = kept.grounded && lift <= 0.05f &&
                           std::abs (drive) < 0.05f && std::abs (turn) < 0.05f;
      const newtons_t weight = heli_rig::mass * standard_gravity;
      const auto newtons = [] (newtons_t f) {
        return f.numerical_value_in (u::N);
      };
      Vec3 thrust_force;
      Vec3 angular;
      if (sitting || kept.parked) {
        thrust_force = up * newtons (0.25f * weight * authority);
      } else {
        // Climb or descend as asked, or hold the height; slow for the
        // touchdown; keep clear of the ground ahead when not descending.
        speed_t climb = lift > 0.0f ? lift * climb_rate : lift * descent_rate;
        if (lift < 0.0f)
          climb = std::max (
            climb,
            speed_t (-(0.6f * u::m / u::s + 0.5f / u::s * (height * u::m))));
        if (lift >= 0.0f) {
          const Vec3 at = from_b3 (b3Body_GetPosition (body));
          float terrain = ground (at);
          for (const float seconds : { 1.0f, 2.0f, 3.5f })
            terrain = std::max (terrain, ground (at + across * seconds));
          const meters_t wanted =
            taxi_height + clearance_per_speed * ground_speed +
            kept.load_hang * std::clamp (speed / 6.0f, 0.0f, 1.0f);
          const meters_t clear = (at[1] - terrain) * u::m;
          if (clear < wanted)
            climb = std::max (climb, speed_t ((wanted - clear) * 1.5f / u::s));
        }
        const metres_per_second_squared_t rise =
          std::clamp (metres_per_second_squared_t (
                        climb_gain * (climb - v[1] * u::m / u::s)),
                      -5.0f * u::m / pow<2> (u::s),
                      7.0f * u::m / pow<2> (u::s));

        // Lean toward the speed asked for: a coordinated turn falls out
        // of the heading turning away from the way it is going.
        const speed_t target =
          drive > 0.0f ? drive * top_speed : drive * back_speed;
        // The level acceleration asked for, metres a second squared.
        Vec3 push = (ahead * target.numerical_value_in (u::m / u::s) - across) *
                    speed_gain.numerical_value_in (one / u::s);
        const float most =
          standard_gravity.numerical_value_in (u::m / pow<2> (u::s)) *
          std::tan (radians_value (max_tilt));
        if (length (push) > most)
          push = normalized (push) * most;
        // The rotor gives its own weight and the climb asked for, and
        // leans for the speed asked for, against what hangs under it.
        const Vec3 pull = force_value (kept.load_pull);
        const float m = heli_rig::mass.numerical_value_in (u::kg);
        const float hold = std::max (newtons (weight) - pull[1], 0.0f);
        Vec3 level = Vec3 (push[0] * m - pull[0], 0.0f, push[2] * m - pull[2]);
        const float reach = std::tan (radians_value (max_tilt) * 1.3f) * hold;
        if (length (level) > reach)
          level = normalized (level) * reach;
        const float thrust = std::clamp (
          newtons (hold * u::N + heli_rig::mass * rise) /
            std::max (up[1], 0.5f),
          0.0f,
          newtons (thrust_to_weight * weight + length (pull) * u::N) *
            authority);
        thrust_force = up * thrust;
        const Vec3 wanted_up = hold > 1.0f
                                 ? normalized (Vec3 (level[0], hold, level[2]))
                                 : Vec3 (0, 1, 0);
        const float omega = 2.0f * pi * attitude_hertz;
        const Vec3 spin_about_up = up * dot (w, up);
        const Vec3 tilt_rate = w - spin_about_up;
        angular = cross (up, wanted_up) * (omega * omega) -
                  tilt_rate * (2.0f * attitude_damping * omega);
        // Turns quickest at a hover, and no quicker at speed than the
        // tilt can carry it round.
        const float rate =
          std::clamp (1.2f * most / std::max (speed, 1.0f), 0.35f, yaw_rate);
        angular = angular + up * (yaw_gain * (-turn * rate - dot (w, up)));
        angular = angular * authority;
      }

      // The air's drag, and the water's up to the cabin's floor.
      const float wet = std::clamp (
        (meters_value (water_level) - point (Vec3 (0, 0.6f, 0))[1]) / 0.8f,
        0.0f,
        1.0f);
      const auto resistance = air_drag * (length (v) * u::m / u::s) +
                              water_drag * wet * heli_rig::mass;
      const Vec3 drag = v * (-resistance).numerical_value_in (u::kg / u::s);
      b3Body_ApplyForceToCenter (body, to_b3 (thrust_force + drag), true);
      if (length2 (angular) > 0.0f)
        b3Body_ApplyTorque (
          body,
          torque_for (b3Body_GetRotation (body), angular, heli_rig::inertia),
          true);

      b3World_Step (world, seconds_value (dt), substeps);

      // Left on its side or back, it is set on its skids again.
      const Vec3 moving = from_b3 (b3Body_GetLinearVelocity (body));
      const Vec3 turning = from_b3 (b3Body_GetAngularVelocity (body));
      const float upright = axis (Vec3 (0, 1, 0))[1];
      kept.overturned = upright < 0.3f && length2 (moving) < 4.0f
                          ? kept.overturned + dt
                          : seconds_t {};
      if (kept.overturned > 2.5f * u::s) {
        place (point (Vec3 ()), axis (Vec3 (0, 0, 1)));
        return;
      }

      // Parked, its rotor stopped, and still: it stays where it is.
      const bool quiet = length2 (moving) < 0.08f * 0.08f &&
                         length2 (turning) < 0.08f * 0.08f && kept.grounded;
      kept.still = kept.parked && kept.rotor <= proportion_t {} && quiet
                     ? kept.still + dt
                     : seconds_t {};
      if (kept.still > 1.0f * u::s)
        kept.asleep = true;
    }
  };

  Helicopter::Helicopter (const map::SurfaceGeometry& surface)
      : m_impl (std::make_unique<Impl> (surface)) {
    m_impl->place (Vec3 (), Vec3 (0, 0, 1));
  }

  Helicopter::~Helicopter () = default;

  Helicopter::State Helicopter::state () const {
    return m_impl->snapshot ();
  }

  void Helicopter::restore (const State& state) {
    m_impl->build (state);
  }

  void Helicopter::place (const Vec3& at, const Vec3& heading) {
    m_impl->place (at, heading);
  }

  void Helicopter::set_trunks (const TrunkField* trunks) {
    Impl& m = *m_impl;
    if (trunks == m.trunks)
      return;
    const State s = m.snapshot ();
    m.trunks = trunks;
    m.build (s);
  }

  void Helicopter::set_drive (control_signal_t drive) {
    m_impl->kept.drive = drive;
  }

  void Helicopter::set_turn (control_signal_t turn) {
    m_impl->kept.turn = turn;
  }

  void Helicopter::set_lift (control_signal_t lift) {
    m_impl->kept.lift = lift;
  }

  void Helicopter::set_load (const force_t& pull, meters_t hang) {
    m_impl->kept.load_pull = pull;
    m_impl->kept.load_hang = hang;
  }

  void Helicopter::set_parked (bool parked) {
    State& kept = m_impl->kept;
    if (kept.parked && !parked)
      kept.asleep = false;
    kept.parked = parked;
  }

  void Helicopter::update (seconds_t dt, meters_t water_level) {
    in_substeps (dt, [&] (seconds_t h) {
      if (!m_impl->kept.asleep)
        m_impl->step (h, water_level);
    });
  }

  Vec3 Helicopter::position () const {
    return m_impl->point (Vec3 ());
  }

  Vec3 Helicopter::forward () const {
    return m_impl->axis (Vec3 (0, 0, 1));
  }

  Vec3 Helicopter::up () const {
    return m_impl->axis (Vec3 (0, 1, 0));
  }

  Vec3 Helicopter::left () const {
    return m_impl->axis (Vec3 (1, 0, 0));
  }

  Vec3 Helicopter::velocity () const {
    return from_b3 (b3Body_GetLinearVelocity (m_impl->body));
  }

  bool Helicopter::grounded () const {
    return m_impl->kept.grounded;
  }

  bool Helicopter::asleep () const {
    return m_impl->kept.asleep;
  }

  kilograms_t Helicopter::mass () const {
    return heli_rig::mass;
  }

  void Helicopter::push (const Vec3& at, const impulse_t& impulse) {
    Impl& m = *m_impl;
    m.kept.asleep = false;
    m.kept.still = seconds_t {};
    b3Body_ApplyLinearImpulse (
      m.body, to_b3 (impulse_value (impulse)), to_b3 (at), true);
  }

  Vec3 Helicopter::point (const Vec3& local) const {
    return m_impl->point (local);
  }

  meters_t Helicopter::altitude () const {
    return m_impl->altitude () * u::m;
  }

  proportion_t Helicopter::rotor () const {
    return m_impl->kept.rotor;
  }

  radians_t Helicopter::rotor_angle () const {
    return m_impl->kept.rotor_angle;
  }

  radians_t Helicopter::tail_angle () const {
    return m_impl->kept.tail_angle;
  }
}
