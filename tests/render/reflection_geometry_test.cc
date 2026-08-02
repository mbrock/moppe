#include <moppe/render/reflection_geometry.hh>

#include <tests/test.hh>

#include <vector>

using namespace moppe;

namespace {
  terrain::SurfaceElevation elevation (float value) {
    return terrain::surface_elevation_point (value * u::m);
  }

  render::TerrainParams terrain_params (int width, int height) {
    render::TerrainParams result;
    result.width = width;
    result.height = height;
    result.scale = Vec3 (1.0f, 1.0f, 1.0f);
    return result;
  }
}

MOPPE_TEST (reflection_proxy_is_exact_for_a_plane) {
  const render::TerrainParams params = terrain_params (8, 8);
  std::vector<terrain::SurfaceElevation> heights (64, elevation (7.0f));
  const render::ReflectionTerrainProxy proxy =
    render::build_reflection_terrain_proxy (params,
                                            heights,
                                            Vec3 (4.0f, 8.0f, 4.0f),
                                            Mat4::identity (),
                                            640,
                                            400,
                                            2,
                                            4.0f);
  MOPPE_CHECK (proxy.metrics.triangle_count == 32);
  MOPPE_CHECK_NEAR (proxy.metrics.height_rms_m, 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (proxy.metrics.height_max_m, 0.0f, 0.0f);
  for (const render::ReflectionProxyVertex& vertex : proxy.triangles)
    MOPPE_CHECK_NEAR (vertex.y, 7.0f, 0.0f);
}

MOPPE_TEST (reflection_proxy_measures_discarded_height_detail) {
  const render::TerrainParams params = terrain_params (8, 8);
  std::vector<terrain::SurfaceElevation> heights (64, elevation (0.0f));
  heights[3 * 8 + 3] = elevation (10.0f);
  const render::ReflectionTerrainProxy proxy =
    render::build_reflection_terrain_proxy (params,
                                            heights,
                                            Vec3 (4.0f, 8.0f, 4.0f),
                                            Mat4::identity (),
                                            640,
                                            400,
                                            4,
                                            4.0f);
  MOPPE_CHECK (proxy.metrics.height_rms_m > 0.0f);
  MOPPE_CHECK_NEAR (proxy.metrics.height_max_m, 10.0f, 0.0f);
}

MOPPE_TEST (reflection_proxy_wraps_authoritative_samples_across_the_seam) {
  const render::TerrainParams params = terrain_params (4, 4);
  std::vector<terrain::SurfaceElevation> heights;
  for (int z = 0; z < 4; ++z)
    for (int x = 0; x < 4; ++x)
      heights.push_back (elevation (static_cast<float> (x + 10 * z)));
  const render::ReflectionTerrainProxy proxy =
    render::build_reflection_terrain_proxy (params,
                                            heights,
                                            Vec3 (0.0f, 8.0f, 0.0f),
                                            Mat4::identity (),
                                            640,
                                            400,
                                            2,
                                            2.0f);
  MOPPE_CHECK (proxy.minimum_x < 0.0f);
  MOPPE_CHECK (proxy.minimum_z < 0.0f);
  const render::ReflectionProxyVertex first = proxy.triangles.front ();
  MOPPE_CHECK_NEAR (first.x, -2.0f, 0.0f);
  MOPPE_CHECK_NEAR (first.z, -2.0f, 0.0f);
  MOPPE_CHECK_NEAR (first.y, 22.0f, 0.0f);
}
