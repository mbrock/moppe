#ifndef MOPPE_RENDER_REFLECTION_GEOMETRY_HH
#define MOPPE_RENDER_REFLECTION_GEOMETRY_HH

#include <moppe/render/renderer.hh>

#include <cstddef>
#include <span>
#include <vector>

namespace moppe::render {
  struct ReflectionProxyVertex {
    float x;
    float y;
    float z;
  };

  struct ReflectionProxyMetrics {
    std::size_t triangle_count = 0;
    std::size_t source_sample_count = 0;
    std::size_t projected_sample_count = 0;
    float height_rms_m = 0.0f;
    float height_p95_m = 0.0f;
    float height_max_m = 0.0f;
    float projected_p95_px = 0.0f;
    float projected_max_px = 0.0f;
  };

  struct ReflectionTerrainProxy {
    std::vector<ReflectionProxyVertex> triangles;
    ReflectionProxyMetrics metrics;
    int source_stride = 0;
    int cells_x = 0;
    int cells_z = 0;
    int start_sample_x = 0;
    int start_sample_z = 0;
    float minimum_x = 0.0f;
    float maximum_x = 0.0f;
    float minimum_z = 0.0f;
    float maximum_z = 0.0f;
  };

  // Builds a bounded, periodic triangle proxy around one forcing viewpoint.
  // The proxy is presentation data: every height is read from the completed
  // authoritative surface, including samples wrapped across the world seam.
  ReflectionTerrainProxy build_reflection_terrain_proxy (
    const TerrainParams& terrain,
    std::span<const terrain::SurfaceElevation> heights,
    const Vec3& focus,
    const Mat4& view_projection,
    int viewport_width,
    int viewport_height,
    int source_stride = 8,
    float half_extent_m = 2048.0f);
}

#endif
