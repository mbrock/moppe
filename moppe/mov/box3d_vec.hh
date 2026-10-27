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
