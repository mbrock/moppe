#include <moppe/game/chase_camera.hh>
#include <moppe/map/surface.hh>

#include <tests/test.hh>

#include <algorithm>
#include <cmath>

namespace {
  using namespace moppe;

  // Level ground ten metres up, two hundred metres across.
  map::SurfaceGeometry level_ground () {
    map::SurfaceGeometry surface =
      map::SurfaceGeometry (terrain::TerrainDomain (
        17, 17, spatial_extent_in_metres (Vec3 (200, 0, 200))));
    std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                       terrain::surface_elevation_point (10.0f * u::m));
    map::rebuild_geometry (surface);
    return surface;
  }
}

// Looking aside at speed, the camera stands beside the machine, not
// dragged round behind it by the speed.
MOPPE_TEST (chase_camera_looks_aside_at_speed) {
  const map::SurfaceGeometry surface = level_ground ();
  game::ChaseCamera camera (18 * u::deg, 6.5f * u::m);
  const seconds_t dt = seconds (1.0f / 60.0f);
  const Vec3 heading (0, 0, 1);
  const Vec3 moving = heading * 25.0f;
  Vec3 subject (100, 11, 10);
  camera.aim (1.5707963f, 0.0f);
  for (int step = 0; step < 240; ++step) {
    subject += moving * (1.0f / 60.0f);
    camera.update (position (subject), heading, velocity (moving), dt);
    camera.limit (surface, dt);
  }
  const Vec3 offset = camera.position () - subject;
  const float along = dot (offset, heading);
  const float across = std::abs (static_cast<float> (offset[0]));
  MOPPE_CHECK (across > 5.0f);
  MOPPE_CHECK (std::abs (along) < 0.15f * across);
}

// Looking up from beside a walker, the camera comes down below their
// shoulders and in over the ground, and looks up past them, but never
// into the ground.
MOPPE_TEST (chase_camera_looks_up_from_low_without_entering_the_ground) {
  const map::SurfaceGeometry surface = level_ground ();
  game::ChaseCamera camera (10 * u::deg, 3.8f * u::m);
  camera.frame (10 * u::deg, 3.8f * u::m, 0.9f * u::m);
  const seconds_t dt = seconds (1.0f / 60.0f);
  const Vec3 pivot (100, 11.35f, 100);
  camera.aim (0.0f, -0.75f - 0.17453f);
  for (int step = 0; step < 180; ++step) {
    camera.update (position (pivot), Vec3 (0, 0, 1), velocity (Vec3 ()), dt);
    camera.limit (surface, dt);
  }
  const Vec3 eye = camera.position ();
  MOPPE_CHECK (eye[1] >= 10.45f);
  MOPPE_CHECK (eye[1] < pivot[1] - 0.3f);
  MOPPE_CHECK (length (eye - pivot) < 3.0f);
  MOPPE_CHECK (camera.forward ()[1] > 0.4f);
}
