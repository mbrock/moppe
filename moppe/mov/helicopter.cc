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
      constexpr float gravity = 9.81f;
      constexpr int substeps = 4;

      constexpr float mass = 1100.0f;
      // Pitch, yaw, and roll about the centre of mass, under the mast.
      constexpr float inertia[3] = { 2400.0f, 2600.0f, 700.0f };
      constexpr float mass_centre[3] = { 0.0f, 1.15f, 0.05f };

      // The rotor at flying speed lifts twice the helicopter's weight;
      // it takes a few seconds to get there and longer to run down.
      constexpr float thrust_to_weight = 2.0f;
      constexpr float spool_up = 3.5f;
      constexpr float spool_down = 10.0f;
      // Revolutions a second at flying speed, main and tail, as drawn:
      // slower than a real rotor's six or seven, which a frame at a time
      // would only smear into nothing.
      constexpr float rotor_rate = 2.2f;
      constexpr float tail_rate = 5.0f;

      // What the flight computer asks of it.
      constexpr float top_speed = 45.0f;
      constexpr float back_speed = 9.0f;
      constexpr float climb_rate = 7.5f;
      constexpr float descent_rate = 6.0f;
      constexpr float max_tilt = 0.52f; // 30 degrees
      constexpr float speed_gain = 0.7f;
      constexpr float climb_gain = 2.2f;
      constexpr float yaw_rate = 1.6f;
      // The attitude loop, as a natural frequency and damping.
      constexpr float attitude_hertz = 0.85f;
      constexpr float attitude_damping = 0.85f;
      constexpr float yaw_gain = 4.0f;
      // Kept above the ground, metres: the hover taxi's height, and more
      // the faster it goes.
      constexpr float taxi_height = 1.6f;
      constexpr float clearance_per_speed = 0.12f;

      // Half the air's density times the fuselage's drag area.
      constexpr float air_drag = 0.85f;
      constexpr float water_drag = 1.6f;

      constexpr int patch_samples = 97;
      constexpr float patch_recentre_distance = 24.0f;
      constexpr float trunk_reach = 72.0f;

      constexpr std::uint64_t ground_bit = 1;
      constexpr std::uint64_t trunk_bit = 2;
      constexpr std::uint64_t body_bit = 8;

      constexpr float pi = 3.14159265f;

      b3Quat level_rotation (const Vec3& heading) {
        Vec3 f = heading;
        f[1] = 0.0f;
        f = length2 (f) > 1e-8f ? normalized (f) : Vec3 (0, 0, 1);
        const Vec3 up (0, 1, 0);
        const Vec3 left = cross (up, f);
        const b3Matrix3 m { to_b3 (left), to_b3 (up), to_b3 (f) };
        return b3NormalizeQuat (b3MakeQuatFromMatrix (&m));
      }

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
      b3MassData m {};
      m.mass = heli_rig::mass;
      m.center = { heli_rig::mass_centre[0],
                   heli_rig::mass_centre[1],
                   heli_rig::mass_centre[2] };
      m.inertia.cx = { heli_rig::inertia[0], 0.0f, 0.0f };
      m.inertia.cy = { 0.0f, heli_rig::inertia[1], 0.0f };
      m.inertia.cz = { 0.0f, 0.0f, heli_rig::inertia[2] };
      b3Body_SetMassData (body, m);
    }

    void build (const State& s) {
      destroy ();
      b3WorldDef def = b3DefaultWorldDef ();
      def.gravity = { 0.0f, -heli_rig::gravity, 0.0f };
      def.enableSleep = false;
      def.workerCount = 1;
      world = b3CreateWorld (&def);
      patch.build (world, trunks, s.ground_column, s.ground_row);
      create_body (s.body);
      kept = s;
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
      const b3Quat q = heli_rig::level_rotation (heading);
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
      s.still = s.overturned = 0.0f;
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

    // A torque in the body's frame, each axis by its own inertia, given
    // as the angular acceleration wanted in the world.
    b3Vec3 torque_for (const Vec3& acceleration) const {
      const b3Quat q = b3Body_GetRotation (body);
      const b3Vec3 local = b3InvRotateVector (q, to_b3 (acceleration));
      const b3Vec3 scaled { local.x * heli_rig::inertia[0],
                            local.y * heli_rig::inertia[1],
                            local.z * heli_rig::inertia[2] };
      return b3RotateVector (q, scaled);
    }

    void step (float dt, float water_level) {
      using namespace heli_rig;
      patch.recentre (world, trunks, from_b3 (b3Body_GetPosition (body)));

      kept.rotor = kept.parked ? std::max (0.0f, kept.rotor - dt / spool_down)
                               : std::min (1.0f, kept.rotor + dt / spool_up);
      kept.rotor_angle = std::remainder (
        kept.rotor_angle + kept.rotor * 2.0f * pi * rotor_rate * dt, 2.0f * pi);
      kept.tail_angle = std::remainder (
        kept.tail_angle + kept.rotor * 2.0f * pi * tail_rate * dt, 2.0f * pi);

      const Vec3 up = axis (Vec3 (0, 1, 0));
      const Vec3 v = from_b3 (b3Body_GetLinearVelocity (body));
      const Vec3 w = from_b3 (b3Body_GetAngularVelocity (body));
      const float height = altitude ();
      kept.grounded = height < 0.12f;

      const float drive =
        kept.parked ? 0.0f : std::clamp (kept.drive, -1.0f, 1.0f);
      const float turn =
        kept.parked ? 0.0f : std::clamp (kept.turn, -1.0f, 1.0f);
      const float lift =
        kept.parked ? 0.0f : std::clamp (kept.lift, -1.0f, 1.0f);
      const float authority = kept.rotor * kept.rotor;

      // The way it heads, level.
      Vec3 ahead = axis (Vec3 (0, 0, 1));
      ahead[1] = 0.0f;
      ahead = length2 (ahead) > 1e-6f ? normalized (ahead) : Vec3 (0, 0, 1);
      Vec3 across = v;
      across[1] = 0.0f;
      const float speed = length (across);

      // Sitting on its skids with nothing asked of it, it stays sat.
      const bool sitting = kept.grounded && lift <= 0.05f &&
                           std::abs (drive) < 0.05f && std::abs (turn) < 0.05f;
      const float weight = mass * heli_rig::gravity;
      Vec3 thrust_force;
      Vec3 angular;
      if (sitting || kept.parked) {
        thrust_force = up * (0.25f * weight * authority);
      } else {
        // Climb or descend as asked, or hold the height; slow for the
        // touchdown; keep clear of the ground ahead when not descending.
        float climb = lift > 0.0f ? lift * climb_rate : lift * descent_rate;
        if (lift < 0.0f)
          climb = std::max (climb, -(0.6f + 0.5f * height));
        if (lift >= 0.0f) {
          const Vec3 at = from_b3 (b3Body_GetPosition (body));
          float terrain = ground (at);
          for (const float seconds : { 1.0f, 2.0f, 3.5f })
            terrain = std::max (terrain, ground (at + across * seconds));
          const float wanted = taxi_height + clearance_per_speed * speed;
          const float clear = at[1] - terrain;
          if (clear < wanted)
            climb = std::max (climb, (wanted - clear) * 1.5f);
        }
        const float rise =
          std::clamp (climb_gain * (climb - v[1]), -5.0f, 7.0f);
        const float thrust = std::clamp (mass * (heli_rig::gravity + rise) /
                                           std::max (up[1], 0.5f),
                                         0.0f,
                                         thrust_to_weight * weight * authority);
        thrust_force = up * thrust;

        // Lean toward the speed asked for: a coordinated turn falls out
        // of the heading turning away from the way it is going.
        const float target =
          drive > 0.0f ? drive * top_speed : drive * back_speed;
        Vec3 push = (ahead * target - across) * speed_gain;
        const float most = heli_rig::gravity * std::tan (max_tilt);
        if (length (push) > most)
          push = normalized (push) * most;
        const Vec3 wanted_up =
          normalized (Vec3 (push[0], heli_rig::gravity, push[2]));
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
        (water_level - point (Vec3 (0, 0.6f, 0))[1]) / 0.8f, 0.0f, 1.0f);
      const Vec3 drag =
        v * (-(air_drag * length (v) + water_drag * wet * mass));
      b3Body_ApplyForceToCenter (body, to_b3 (thrust_force + drag), true);
      if (length2 (angular) > 0.0f)
        b3Body_ApplyTorque (body, torque_for (angular), true);

      b3World_Step (world, dt, substeps);

      // Left on its side or back, it is set on its skids again.
      const Vec3 moving = from_b3 (b3Body_GetLinearVelocity (body));
      const Vec3 turning = from_b3 (b3Body_GetAngularVelocity (body));
      const float upright = axis (Vec3 (0, 1, 0))[1];
      kept.overturned =
        upright < 0.3f && length2 (moving) < 4.0f ? kept.overturned + dt : 0.0f;
      if (kept.overturned > 2.5f) {
        place (point (Vec3 ()), axis (Vec3 (0, 0, 1)));
        return;
      }

      // Parked, its rotor stopped, and still: it stays where it is.
      const bool quiet = length2 (moving) < 0.08f * 0.08f &&
                         length2 (turning) < 0.08f * 0.08f && kept.grounded;
      kept.still =
        kept.parked && kept.rotor <= 0.0f && quiet ? kept.still + dt : 0.0f;
      if (kept.still > 1.0f)
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

  void Helicopter::set_drive (float drive) {
    m_impl->kept.drive = drive;
  }

  void Helicopter::set_turn (float turn) {
    m_impl->kept.turn = turn;
  }

  void Helicopter::set_lift (float lift) {
    m_impl->kept.lift = lift;
  }

  void Helicopter::set_parked (bool parked) {
    State& kept = m_impl->kept;
    if (kept.parked && !parked)
      kept.asleep = false;
    kept.parked = parked;
  }

  void Helicopter::update (float dt, float water_level) {
    if (m_impl->kept.asleep || dt <= 0.0f)
      return;
    m_impl->step (dt, water_level);
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

  Vec3 Helicopter::point (const Vec3& local) const {
    return m_impl->point (local);
  }

  float Helicopter::altitude () const {
    return m_impl->altitude ();
  }

  float Helicopter::rotor () const {
    return m_impl->kept.rotor;
  }

  float Helicopter::rotor_angle () const {
    return m_impl->kept.rotor_angle;
  }

  float Helicopter::tail_angle () const {
    return m_impl->kept.tail_angle;
  }
}
