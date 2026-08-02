#include <moppe/render/reflection_geometry.hh>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace moppe::render {
  namespace {
    int wrap (int value, int period) {
      const int remainder = value % period;
      return remainder < 0 ? remainder + period : remainder;
    }

    float percentile_95 (std::vector<float> values) {
      if (values.empty ())
        return 0.0f;
      const std::size_t index = static_cast<std::size_t> (
        std::floor (0.95 * static_cast<double> (values.size () - 1)));
      std::nth_element (
        values.begin (), values.begin () + index, values.end ());
      return values[index];
    }

    float source_height (const TerrainParams& terrain,
                         std::span<const terrain::SurfaceElevation> heights,
                         int x,
                         int z) {
      const std::size_t offset =
        static_cast<std::size_t> (wrap (z, terrain.height)) * terrain.width +
        wrap (x, terrain.width);
      return terrain::surface_elevation_value (heights[offset]);
    }

    float proxy_height (const TerrainParams& terrain,
                        std::span<const terrain::SurfaceElevation> heights,
                        int x,
                        int z,
                        int stride) {
      const int x0 =
        static_cast<int> (std::floor (static_cast<double> (x) / stride)) *
        stride;
      const int z0 =
        static_cast<int> (std::floor (static_cast<double> (z) / stride)) *
        stride;
      const float fx = static_cast<float> (x - x0) / stride;
      const float fz = static_cast<float> (z - z0) / stride;
      const float h00 = source_height (terrain, heights, x0, z0);
      const float h10 = source_height (terrain, heights, x0 + stride, z0);
      const float h01 = source_height (terrain, heights, x0, z0 + stride);
      const float h11 =
        source_height (terrain, heights, x0 + stride, z0 + stride);
      if (fx + fz <= 1.0f)
        return h00 + fx * (h10 - h00) + fz * (h01 - h00);
      return h11 + (1.0f - fz) * (h10 - h11) + (1.0f - fx) * (h01 - h11);
    }

    struct ProjectedPoint {
      float x = 0.0f;
      float y = 0.0f;
      bool visible = false;
    };

    ProjectedPoint
    project (const Mat4& matrix, const Vec3& point, int width, int height) {
      const float clip_x = matrix.m[0] * point[0] + matrix.m[4] * point[1] +
                           matrix.m[8] * point[2] + matrix.m[12];
      const float clip_y = matrix.m[1] * point[0] + matrix.m[5] * point[1] +
                           matrix.m[9] * point[2] + matrix.m[13];
      const float clip_w = matrix.m[3] * point[0] + matrix.m[7] * point[1] +
                           matrix.m[11] * point[2] + matrix.m[15];
      if (clip_w <= 1e-4f)
        return {};
      const float ndc_x = clip_x / clip_w;
      const float ndc_y = clip_y / clip_w;
      return { (ndc_x * 0.5f + 0.5f) * width,
               (0.5f - ndc_y * 0.5f) * height,
               ndc_x >= -1.0f && ndc_x <= 1.0f && ndc_y >= -1.0f &&
                 ndc_y <= 1.0f };
    }
  }

  ReflectionTerrainProxy build_reflection_terrain_proxy (
    const TerrainParams& terrain,
    std::span<const terrain::SurfaceElevation> heights,
    const Vec3& focus,
    const Mat4& view_projection,
    int viewport_width,
    int viewport_height,
    int source_stride,
    float half_extent_m) {
    const std::size_t expected =
      static_cast<std::size_t> (terrain.width) * terrain.height;
    if (terrain.width < 2 || terrain.height < 2 ||
        heights.size () != expected || terrain.scale[0] <= 0.0f ||
        terrain.scale[2] <= 0.0f || source_stride < 1 ||
        half_extent_m <= 0.0f || viewport_width < 1 || viewport_height < 1)
      throw std::invalid_argument ("invalid reflection terrain proxy input");

    ReflectionTerrainProxy result;
    result.source_stride = source_stride;
    result.cells_x =
      std::max (1,
                static_cast<int> (std::ceil (
                  2.0f * half_extent_m / (terrain.scale[0] * source_stride))));
    result.cells_z =
      std::max (1,
                static_cast<int> (std::ceil (
                  2.0f * half_extent_m / (terrain.scale[2] * source_stride))));
    const int center_x = static_cast<int> (std::floor (
                           focus[0] / terrain.scale[0] / source_stride)) *
                         source_stride;
    const int center_z = static_cast<int> (std::floor (
                           focus[2] / terrain.scale[2] / source_stride)) *
                         source_stride;
    result.start_sample_x = center_x - (result.cells_x / 2) * source_stride;
    result.start_sample_z = center_z - (result.cells_z / 2) * source_stride;
    result.minimum_x = result.start_sample_x * terrain.scale[0];
    result.minimum_z = result.start_sample_z * terrain.scale[2];
    result.maximum_x =
      (result.start_sample_x + result.cells_x * source_stride) *
      terrain.scale[0];
    result.maximum_z =
      (result.start_sample_z + result.cells_z * source_stride) *
      terrain.scale[2];

    result.triangles.reserve (static_cast<std::size_t> (result.cells_x) *
                              result.cells_z * 6);
    const auto vertex = [&] (int x, int z) {
      return ReflectionProxyVertex { x * terrain.scale[0],
                                     source_height (terrain, heights, x, z),
                                     z * terrain.scale[2] };
    };
    for (int cell_z = 0; cell_z < result.cells_z; ++cell_z)
      for (int cell_x = 0; cell_x < result.cells_x; ++cell_x) {
        const int x = result.start_sample_x + cell_x * source_stride;
        const int z = result.start_sample_z + cell_z * source_stride;
        const ReflectionProxyVertex v00 = vertex (x, z);
        const ReflectionProxyVertex v10 = vertex (x + source_stride, z);
        const ReflectionProxyVertex v01 = vertex (x, z + source_stride);
        const ReflectionProxyVertex v11 =
          vertex (x + source_stride, z + source_stride);
        result.triangles.insert (result.triangles.end (),
                                 { v00, v01, v10, v10, v01, v11 });
      }

    std::vector<float> height_errors;
    std::vector<float> projected_errors;
    const int fine_width = result.cells_x * source_stride;
    const int fine_height = result.cells_z * source_stride;
    height_errors.reserve (static_cast<std::size_t> (fine_width + 1) *
                           (fine_height + 1));
    double squared_error = 0.0;
    float maximum_height_error = 0.0f;
    float maximum_projected_error = 0.0f;
    for (int local_z = 0; local_z <= fine_height; ++local_z)
      for (int local_x = 0; local_x <= fine_width; ++local_x) {
        const int x = result.start_sample_x + local_x;
        const int z = result.start_sample_z + local_z;
        const float authoritative = source_height (terrain, heights, x, z);
        const float approximated =
          proxy_height (terrain, heights, x, z, source_stride);
        const float error = std::abs (authoritative - approximated);
        height_errors.push_back (error);
        squared_error += static_cast<double> (error) * error;
        maximum_height_error = std::max (maximum_height_error, error);

        const float world_x = x * terrain.scale[0];
        const float world_z = z * terrain.scale[2];
        const ProjectedPoint actual =
          project (view_projection,
                   Vec3 (world_x, authoritative, world_z),
                   viewport_width,
                   viewport_height);
        if (!actual.visible)
          continue;
        const ProjectedPoint proxy =
          project (view_projection,
                   Vec3 (world_x, approximated, world_z),
                   viewport_width,
                   viewport_height);
        if (!proxy.visible)
          continue;
        const float dx = actual.x - proxy.x;
        const float dy = actual.y - proxy.y;
        const float pixel_error = std::sqrt (dx * dx + dy * dy);
        projected_errors.push_back (pixel_error);
        maximum_projected_error =
          std::max (maximum_projected_error, pixel_error);
      }

    result.metrics.triangle_count = result.triangles.size () / 3;
    result.metrics.source_sample_count = height_errors.size ();
    result.metrics.projected_sample_count = projected_errors.size ();
    result.metrics.height_rms_m = static_cast<float> (std::sqrt (
      squared_error / std::max<std::size_t> (1, height_errors.size ())));
    result.metrics.height_p95_m = percentile_95 (height_errors);
    result.metrics.height_max_m = maximum_height_error;
    result.metrics.projected_p95_px = percentile_95 (projected_errors);
    result.metrics.projected_max_px = maximum_projected_error;
    return result;
  }
}
