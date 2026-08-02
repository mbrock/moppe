#include "../../render/metal/shader_types.h"
#include <metal_raytracing>

using namespace metal;
using namespace raytracing;

kernel void reflection_geometry_atelier (
  primitive_acceleration_structure terrain [[buffer (MOPPE_BUF_REFLECTION_AS)]],
  constant MoppeReflectionGeometryUniforms& uniforms
  [[buffer (MOPPE_BUF_REFLECTION_UNIFORMS)]],
  device half4* output [[buffer (MOPPE_BUF_REFLECTION_OUTPUT)]],
  device const packed_float3* vertices
  [[buffer (MOPPE_BUF_REFLECTION_VERTICES)]],
  uint2 position [[thread_position_in_grid]]) {
  const uint width = uint (uniforms.projection.z);
  const uint height = uint (uniforms.projection.w);
  if (position.x >= width || position.y >= height)
    return;

  const uint panel_width = max (1u, width / 2u);
  const uint panel_height = max (1u, height / 2u);
  const uint2 panel =
    uint2 (position.x / panel_width, position.y / panel_height);
  const uint2 local =
    uint2 (position.x % panel_width, position.y % panel_height);
  const float2 uv =
    (float2 (local) + 0.5f) / float2 (panel_width, panel_height);
  const float2 ndc = float2 (uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
  const float3 camera_direction = normalize (float3 (
    ndc.x / uniforms.projection.x, ndc.y / uniforms.projection.y, -1.0f));
  const float3 direction =
    normalize (uniforms.camera_right.xyz * camera_direction.x +
               uniforms.camera_up.xyz * camera_direction.y +
               uniforms.camera_back.xyz * camera_direction.z);

  ray query;
  query.origin = uniforms.camera.xyz;
  query.direction = direction;
  query.min_distance = 0.05f;
  query.max_distance = uniforms.camera.w;
  intersector<triangle_data> trace;
  trace.accept_any_intersection (false);
  const auto hit = trace.intersect (query, terrain);
  const bool found = hit.type != intersection_type::none;

  float3 colour = float3 (0.02f, 0.0f, 0.04f);
  if (found) {
    const uint first = hit.primitive_id * 3u;
    const float3 a = float3 (vertices[first]);
    const float3 b = float3 (vertices[first + 1u]);
    const float3 c = float3 (vertices[first + 2u]);
    const float3 normal = normalize (cross (b - a, c - a));
    const float2 barycentric = hit.triangle_barycentric_coord;
    const float edge = min (min (barycentric.x, barycentric.y),
                            1.0f - barycentric.x - barycentric.y);
    if (panel.y == 0u && panel.x == 0u) {
      colour = normal * 0.5f + 0.5f;
    } else if (panel.y == 0u) {
      const float distance =
        log2 (1.0f + hit.distance) / log2 (1.0f + uniforms.camera.w);
      colour = float3 (distance);
    } else if (panel.x == 0u) {
      const uint primitive = hit.primitive_id;
      colour = float3 (float ((primitive * 17u) & 255u),
                       float ((primitive * 67u) & 255u),
                       float ((primitive * 131u) & 255u)) /
               255.0f;
      if (edge < 0.015f)
        colour = float3 (1.0f);
    } else {
      colour = float3 (1.0f);
    }
  }
  output[position.y * uint (uniforms.output.x) + position.x] =
    half4 (half3 (colour), 1.0h);
}
