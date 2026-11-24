#ifndef MOPPE_MOV_BOX3D_VEC_HH
#define MOPPE_MOV_BOX3D_VEC_HH

#include <moppe/gfx/math.hh>
#include <moppe/map/surface.hh>
#include <moppe/mov/rigid_bike.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

#include <box3d/box3d.h>
#include <box3d/math_functions.h>

// Conversions between moppe's vectors and Box3D's, and what the Box3D
// vehicles share. They live in one header
// because the engine compiles as a unity build, where two files' private
// helpers of the same name would collide.

namespace moppe::mov {
  inline b3Vec3 to_b3 (const Vec3& v) {
    return { v[0], v[1], v[2] };
  }

  inline Vec3 from_b3 (const b3Vec3& v) {
    return Vec3 (v.x, v.y, v.z);
  }

  // A rotation as the four numbers a checkpoint keeps: x, y, z, w.
  inline b3Quat quat_of (const std::array<float, 4>& q) {
    return { { q[0], q[1], q[2] }, q[3] };
  }

  inline std::array<float, 4> array_of (const b3Quat& q) {
    return { q.v.x, q.v.y, q.v.z, q.s };
  }

  inline Vec3 rotated (const std::array<float, 4>& q, const Vec3& v) {
    return from_b3 (b3RotateVector (quat_of (q), to_b3 (v)));
  }

  // A body's motion, as a checkpoint keeps it.
  inline RigidBodyState read_body (b3BodyId body) {
    return { from_b3 (b3Body_GetPosition (body)),
             array_of (b3Body_GetRotation (body)),
             from_b3 (b3Body_GetLinearVelocity (body)),
             from_b3 (b3Body_GetAngularVelocity (body)) };
  }

  // Box3D is unit-blind: kilograms, metres, seconds, and so newtons. These
  // are where the vehicles' typed quantities cross into it and back.
  inline velocity_t linear_velocity (b3BodyId body) {
    return velocity (from_b3 (b3Body_GetLinearVelocity (body)));
  }

  inline void
  apply_force (b3BodyId body, const force_t& force, const Vec3& at) {
    b3Body_ApplyForce (body,
                       to_b3 (force.numerical_value_ref_in (si::newton)),
                       to_b3 (at),
                       true);
  }

  // A push of `magnitude` along the unit `direction`, at `at`.
  inline void apply_force (b3BodyId body,
                           const Vec3& direction,
                           newtons_t magnitude,
                           const Vec3& at) {
    b3Body_ApplyForce (
      body,
      to_b3 (direction * magnitude.numerical_value_in (si::newton)),
      to_b3 (at),
      true);
  }

  // The drag of a body moving at `v` (metres a second) against a
  // resistance of `resistance` (kilograms a second): a force against it.
  inline void
  apply_drag (b3BodyId body,
              const Vec3& v,
              quantity<si::kilogram / si::second, float> resistance) {
    b3Body_ApplyForceToCenter (
      body,
      to_b3 (v * (-resistance).numerical_value_in (si::kilogram / si::second)),
      true);
  }

  // The typed crossings of offsets, places, and ways into and out of
  // Box3D's frame: a body-frame offset, a place in the world, a unit way.
  inline b3Vec3 to_b3 (const displacement_t& v) {
    return to_b3 (displacement_value (v));
  }

  inline b3Vec3 to_b3 (const position_t& at) {
    return to_b3 (position_value (at));
  }

  inline b3Vec3 to_b3 (const direction_t& way) {
    return to_b3 (direction_value (way));
  }

  // How high a place stands, its y.
  inline meters_t height_of (const position_t& at) {
    return position_value (at)[1] * si::metre;
  }

  inline position_t body_position (b3BodyId body) {
    return position (from_b3 (b3Body_GetPosition (body)));
  }

  inline angular_velocity_t angular_velocity (b3BodyId body) {
    return from_b3 (b3Body_GetAngularVelocity (body)) *
           isq::angular_velocity[si::radian / si::second];
  }

  // A body-frame offset or way turned into the world's by `q`, and a
  // world way into the body's frame.
  inline displacement_t turned (b3Quat q, const displacement_t& local) {
    return displacement (from_b3 (b3RotateVector (q, to_b3 (local))));
  }

  inline direction_t turned (b3Quat q, const direction_t& local) {
    return unit_direction (from_b3 (b3RotateVector (q, to_b3 (local))));
  }

  inline direction_t unturned (b3Quat q, const direction_t& way) {
    return unit_direction (from_b3 (b3InvRotateVector (q, to_b3 (way))));
  }

  // The velocity of the body's point at the body-frame offset `local`.
  inline velocity_t point_velocity (b3BodyId body,
                                    const displacement_t& local) {
    return velocity (
      from_b3 (b3Body_GetLocalPointVelocity (body, to_b3 (local))));
  }

  inline void
  apply_force (b3BodyId body, const force_t& force, const position_t& at) {
    b3Body_ApplyForce (body, to_b3 (force_value (force)), to_b3 (at), true);
  }

  // A push of `magnitude` along `way`, at the place `at`.
  inline void apply_force (b3BodyId body,
                           const direction_t& way,
                           newtons_t magnitude,
                           const position_t& at) {
    apply_force (body, direction_value (way), magnitude, position_value (at));
  }

  inline void
  apply_drag (b3BodyId body,
              const velocity_t& v,
              quantity<si::kilogram / si::second, float> resistance) {
    apply_drag (body, velocity_value (v), resistance);
  }

  inline void apply_force_to_centre (b3BodyId body, const force_t& force) {
    b3Body_ApplyForceToCenter (
      body, to_b3 (force.numerical_value_ref_in (si::newton)), true);
  }

  // A torque of `torque` about the unit `axis`.
  inline void
  apply_torque (b3BodyId body, const Vec3& axis, newton_metres_t torque) {
    b3Body_ApplyTorque (
      body,
      to_b3 (axis * torque.numerical_value_in (si::newton * si::metre)),
      true);
  }

  inline void apply_torque (b3BodyId body,
                            const direction_t& axis,
                            newton_metres_t torque) {
    apply_torque (body, direction_value (axis), torque);
  }

  // A body's mass, its centre in the body's frame, and its moments of
  // inertia about the three axes through that centre.
  inline b3MassData
  body_mass (kilograms_t mass,
             const Vec3& centre,
             const std::array<kilogram_square_metres_t, 3>& inertia) {
    const auto moment = [] (kilogram_square_metres_t i) {
      return i.numerical_value_in (si::kilogram * pow<2> (si::metre));
    };
    b3MassData m {};
    m.mass = mass.numerical_value_in (si::kilogram);
    m.center = to_b3 (centre);
    m.inertia.cx = { moment (inertia[0]), 0.0f, 0.0f };
    m.inertia.cy = { 0.0f, moment (inertia[1]), 0.0f };
    m.inertia.cz = { 0.0f, 0.0f, moment (inertia[2]) };
    return m;
  }

  inline b3MassData
  body_mass (kilograms_t mass,
             const displacement_t& centre,
             const std::array<kilogram_square_metres_t, 3>& inertia) {
    return body_mass (mass, displacement_value (centre), inertia);
  }

  // A box of half extents `half` about `centre`, in a body's frame.
  inline b3BoxHull box_hull (const spatial_extent_t& half,
                             const displacement_t& centre) {
    const Vec3& h = extent_value (half);
    return b3MakeOffsetBoxHull (h[0], h[1], h[2], to_b3 (centre));
  }

  // The torque that turns a body, rotated by `q` and with moments of
  // inertia `inertia` about its own axes, at `acceleration` (radians a
  // second squared, in the world).
  inline b3Vec3 torque_for (b3Quat q,
                            const Vec3& acceleration,
                            const kilogram_square_metres_t (&inertia)[3]) {
    const b3Vec3 local = b3InvRotateVector (q, to_b3 (acceleration));
    const auto moment = [] (kilogram_square_metres_t i) {
      return i.numerical_value_in (si::kilogram * pow<2> (si::metre));
    };
    const b3Vec3 scaled { local.x * moment (inertia[0]),
                          local.y * moment (inertia[1]),
                          local.z * moment (inertia[2]) };
    return b3RotateVector (q, scaled);
  }

  // Box3D's wheel-joint springs are soft constraints tuned by a frequency
  // and a damping ratio against the joint's effective mass, which a light
  // wheel dominates. Turns the spring's stiffness and the damping ratio
  // wanted for the `sprung` mass it carries into those terms.
  inline void spring_tuning (quantity<si::newton / si::metre, float> stiffness,
                             kilograms_t sprung,
                             kilograms_t effective,
                             float damping_ratio,
                             float& hertz,
                             float& damping) {
    const auto c = 2.0f * damping_ratio * sqrt (stiffness * sprung);
    hertz = (sqrt (stiffness / effective) / (2.0f * 3.14159265f))
              .numerical_value_in (one / si::second);
    damping =
      (c / (2.0f * sqrt (stiffness * effective))).numerical_value_in (one);
  }

  // The same, typed: the joint's frequency and damping ratio.
  inline void spring_tuning (quantity<si::newton / si::metre, float> stiffness,
                             kilograms_t sprung,
                             kilograms_t effective,
                             damping_ratio_t ratio,
                             frequency_t& hertz,
                             damping_ratio_t& damping) {
    float h = 0.0f, d = 0.0f;
    spring_tuning (
      stiffness, sprung, effective, ratio.numerical_value_in (one), h, d);
    hertz = h * si::hertz;
    damping = d * damping_ratio[one];
  }

  // A machine's frame for a level heading: x to its left, y up, z along
  // the heading.
  inline b3Quat level_rotation (const Vec3& heading) {
    Vec3 f = heading;
    f[1] = 0.0f;
    f = length2 (f) > 1e-8f ? normalized (f) : Vec3 (0, 0, 1);
    const Vec3 up (0, 1, 0);
    const Vec3 left = cross (up, f);
    const b3Matrix3 m { to_b3 (left), to_b3 (up), to_b3 (f) };
    return b3NormalizeQuat (b3MakeQuatFromMatrix (&m));
  }

  // Every machine integrates at the game's own rate, whatever step it is
  // given: a longer one is split into equal substeps of at most this, so
  // a capture's or a video's steps of a sixtieth run the physics play
  // runs.
  inline constexpr seconds_t machine_step = (1.0f / 120.0f) * si::second;

  // Calls `step` with each of the equal substeps `dt` is split into.
  template <class Step>
  void in_substeps (seconds_t dt, Step&& step) {
    if (dt <= seconds_t {})
      return;
    const int count =
      std::max (1,
                static_cast<int> (std::ceil (
                  (dt / machine_step).numerical_value_in (one) - 0.001f)));
    const seconds_t h = dt / static_cast<float> (count);
    for (int i = 0; i < count; ++i)
      step (h);
  }

  // How far round a wheel has rolled after a step of `dt` from `angle`, in
  // (-pi, pi]: what its joint's spin turned it through.
  inline radians_t rolled (radians_t angle, b3JointId joint, seconds_t dt) {
    return std::remainder (radians_value (angle) +
                             b3WheelJoint_GetSpinSpeed (joint) *
                               seconds_value (dt),
                           6.2831853f) *
           si::radian;
  }

  // A wheel joint slides along its frame's x axis and spins about z: in
  // this frame x points down the chassis and z along the axle to the left.
  inline b3Quat suspension_rotation () {
    const b3Matrix3 m { { 0.0f, -1.0f, 0.0f },
                        { 0.0f, 0.0f, -1.0f },
                        { 1.0f, 0.0f, 0.0f } };
    return b3NormalizeQuat (b3MakeQuatFromMatrix (&m));
  }

  // The built roads (terrain/roads.hh) stand over the height field's
  // samples, and every body's footing reads them: the session attaches its
  // world's roads to that world's surface. Ground read off any other
  // surface (a test's, say) is the bare height field. Null detaches.
  void attach_roads (const map::SurfaceGeometry& surface,
                     const terrain::RoadSurface* roads);

  // The roads attached to `surface`, or null.
  const terrain::RoadSurface*
  attached_roads (const map::SurfaceGeometry& surface);

  // Solid frozen alpine lake ice: still water standing at or above the
  // freezing elevation forms a flat load-bearing ice sheet for every body's
  // footing. An empty callback detaches.
  using IceHeightFn =
    std::function<terrain::SurfaceElevation (plan_position_t)>;
  void attach_ice (const map::SurfaceGeometry& surface, IceHeightFn ice_height);

  // True when `at` stands on a solid frozen alpine lake attached to `surface`.
  bool on_frozen_ice (const map::SurfaceGeometry& surface,
                      const position_t& at);

  // The ground's height under a point: the height field, and over a road's
  // tread the road itself.
  float ground_height (const map::SurfaceGeometry& surface, float x, float z);

  inline float
  surface_elevation_at (const map::SurfaceGeometry& surface, float x, float z) {
    return ground_height (surface, x, z);
  }

  // The ground's height under a place.
  inline meters_t ground_under (const map::SurfaceGeometry& surface,
                                const position_t& at) {
    const Vec3& p = position_value (at);
    return ground_height (surface, p[0], p[2]) * si::metre;
  }

  // The unit normal of that ground: the height field's, and over a road's
  // tread (a bridge's deck, an embankment) the road's own.
  Vec3 ground_normal (const map::SurfaceGeometry& surface, float x, float z);

  // Whether a wheel touched the ground in the last step, and the mean
  // normal force the ground pressed it with.
  inline bool
  wheel_touches_ground (b3BodyId wheel, seconds_t dt, newtons_t& load) {
    b3ContactData contacts[8];
    const int count = b3Body_GetContactData (wheel, contacts, 8);
    bool touching = false;
    float impulse = 0.0f;
    for (int i = 0; i < count; ++i)
      for (int m = 0; m < contacts[i].manifoldCount; ++m) {
        const b3Manifold& manifold = contacts[i].manifolds[m];
        for (int p = 0; p < manifold.pointCount; ++p) {
          const b3ManifoldPoint& point = manifold.points[p];
          impulse += point.totalNormalImpulse;
          touching = touching || point.totalNormalImpulse > 0.0f ||
                     point.separation < 0.02f;
        }
      }
    load = impulse * u::N * u::s / dt;
    return touching;
  }
}

#endif
