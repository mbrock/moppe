#ifndef MOPPE_MOV_BOX3D_VEC_HH
#define MOPPE_MOV_BOX3D_VEC_HH

#include <moppe/gfx/math.hh>
#include <moppe/map/surface.hh>

#include <array>

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

  // The ground's height under a point: the height field, and over a road's
  // tread the road itself.
  float ground_height (const map::SurfaceGeometry& surface, float x, float z);

  inline float
  surface_elevation_at (const map::SurfaceGeometry& surface, float x, float z) {
    return ground_height (surface, x, z);
  }

  // Whether a wheel touched the ground in the last step, and the mean
  // normal force the ground pressed it with.
  inline bool wheel_touches_ground (b3BodyId wheel, float dt, float& load) {
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
    load = impulse / dt;
    return touching;
  }
}

#endif
