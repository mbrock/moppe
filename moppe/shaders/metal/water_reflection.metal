#include "common.h"
#include <metal_raytracing>

using namespace metal;
using namespace raytracing;

static float3 reflection_hit_normal (device const packed_float3* vertices,
                                     uint primitive) {
  const uint first = primitive * 3u;
  const float3 a = float3 (vertices[first]);
  const float3 b = float3 (vertices[first + 1u]);
  const float3 c = float3 (vertices[first + 2u]);
  return normalize (cross (b - a, c - a));
}

kernel void
water_reflection_signal (primitive_acceleration_structure terrain
                         [[buffer (MOPPE_BUF_REFLECTION_AS)]],
                         constant MoppeWaterReflectionUniforms& uniforms
                         [[buffer (MOPPE_BUF_REFLECTION_UNIFORMS)]],
                         device const packed_float3* vertices
                         [[buffer (MOPPE_BUF_REFLECTION_VERTICES)]],
                         texture2d<float, access::read> origins
                         [[texture (MOPPE_TEX_REFLECTION_ORIGIN)]],
                         texture2d<half, access::read> optical_normals
                         [[texture (MOPPE_TEX_REFLECTION_OPTICAL_NORMAL)]],
                         texture2d<half, access::write> radiance
                         [[texture (MOPPE_TEX_REFLECTION_RADIANCE)]],
                         texture2d<half, access::write> hit_normals
                         [[texture (MOPPE_TEX_REFLECTION_HIT_NORMAL)]],
                         texture2d<float, access::write> hit_distances
                         [[texture (MOPPE_TEX_REFLECTION_HIT_DISTANCE)]],
                         texture2d<float, access::write> validity
                         [[texture (MOPPE_TEX_REFLECTION_VALIDITY)]],
                         uint2 position [[thread_position_in_grid]]) {
  const uint2 dimensions = uint2 (uniforms.dimensions.xy);
  if (any (position >= dimensions))
    return;

  const float4 origin_sample = origins.read (position);
  const bool input_valid = origin_sample.w > 0.5f;
  if (!input_valid) {
    radiance.write (half4 (0.0h), position);
    hit_normals.write (half4 (0.0h), position);
    hit_distances.write (float4 (0.0f), position);
    validity.write (float4 (0.0f, 0.0f, 0.0f, 1.0f), position);
    return;
  }

  const float3 origin = origin_sample.xyz;
  float3 normal = normalize (float3 (optical_normals.read (position).xyz));
  const float3 camera_to_water = origin - uniforms.camera.xyz;
  const float camera_distance = length (camera_to_water);
  const float3 incident = camera_to_water / max (camera_distance, 1e-4f);

  intersector<triangle_data> trace;
  trace.accept_any_intersection (false);
  ray visibility_ray;
  visibility_ray.origin = uniforms.camera.xyz;
  visibility_ray.direction = incident;
  visibility_ray.min_distance = 0.05f;
  visibility_ray.max_distance = max (0.05f, camera_distance - 0.10f);
  const auto occluder = trace.intersect (visibility_ray, terrain);
  const bool visible = occluder.type == intersection_type::none;
  if (!visible) {
    radiance.write (half4 (0.0h), position);
    hit_normals.write (half4 (0.0h), position);
    hit_distances.write (float4 (0.0f), position);
    validity.write (float4 (1.0f, 0.0f, 0.0f, 1.0f), position);
    return;
  }

  if (dot (normal, -incident) < 0.0f)
    normal = -normal;
  ray reflected;
  reflected.origin = origin + normal * 0.05f;
  reflected.direction = normalize (reflect (incident, normal));
  reflected.min_distance = 0.05f;
  reflected.max_distance = uniforms.camera.w;
  const auto hit = trace.intersect (reflected, terrain);
  const bool found = hit.type != intersection_type::none;

  float3 raw;
  float3 hit_normal (0.0f);
  float hit_distance = 0.0f;
  if (found) {
    hit_normal = reflection_hit_normal (vertices, hit.primitive_id);
    if (dot (hit_normal, reflected.direction) > 0.0f)
      hit_normal = -hit_normal;
    hit_distance = hit.distance;
    const float3 sun = normalize (uniforms.sun_dir.xyz);
    const float sun_light = max (dot (hit_normal, sun), 0.0f);
    const float3 rock = moppe_srgb (float3 (0.30f, 0.28f, 0.23f));
    const float3 grass = moppe_srgb (float3 (0.19f, 0.28f, 0.12f));
    const float grassy = smoothstep (0.45f, 0.92f, hit_normal.y);
    const float3 albedo = mix (rock, grass, grassy);
    raw = albedo * (uniforms.ambient.rgb +
                    uniforms.sun_colour.rgb * (0.12f + 0.88f * sun_light));
    const float haze = 1.0f - exp (-hit_distance * 0.0012f);
    raw = mix (raw, uniforms.fog_colour.rgb, 0.45f * haze);
  } else {
    raw = moppe_sky_radiance (
      uniforms.fog_colour.rgb, reflected.direction, uniforms.sun_dir.xyz);
  }

  radiance.write (half4 (half3 (raw), 1.0h), position);
  hit_normals.write (half4 (half3 (hit_normal), found ? 1.0h : 0.0h), position);
  hit_distances.write (float4 (hit_distance), position);
  validity.write (float4 (1.0f, 1.0f, found ? 1.0f : 0.0f, 1.0f), position);
}

kernel void
water_reflection_diagnostic (constant MoppeWaterReflectionUniforms& uniforms
                             [[buffer (MOPPE_BUF_REFLECTION_UNIFORMS)]],
                             device half4* output
                             [[buffer (MOPPE_BUF_REFLECTION_OUTPUT)]],
                             texture2d<float, access::read> origins
                             [[texture (MOPPE_TEX_REFLECTION_ORIGIN)]],
                             texture2d<half, access::read> optical_normals
                             [[texture (MOPPE_TEX_REFLECTION_OPTICAL_NORMAL)]],
                             texture2d<half, access::read> radiance
                             [[texture (MOPPE_TEX_REFLECTION_RADIANCE)]],
                             texture2d<half, access::read> hit_normals
                             [[texture (MOPPE_TEX_REFLECTION_HIT_NORMAL)]],
                             texture2d<float, access::read> hit_distances
                             [[texture (MOPPE_TEX_REFLECTION_HIT_DISTANCE)]],
                             texture2d<float, access::read> validity
                             [[texture (MOPPE_TEX_REFLECTION_VALIDITY)]],
                             uint2 position [[thread_position_in_grid]]) {
  const uint2 signal_dimensions = uint2 (uniforms.dimensions.xy);
  const uint2 output_dimensions = uint2 (uniforms.dimensions.zw);
  if (any (position >= output_dimensions))
    return;

  const uint2 panel = position / signal_dimensions;
  const uint2 local = position % signal_dimensions;
  float3 colour (0.0f);
  if (panel.y == 0u && panel.x == 0u) {
    const float4 sample = origins.read (local);
    const float distance = length (sample.xyz - uniforms.camera.xyz);
    colour =
      sample.w > 0.5f
        ? float3 (log2 (1.0f + distance) / log2 (1.0f + uniforms.camera.w))
        : float3 (0.0f);
  } else if (panel.y == 0u && panel.x == 1u) {
    const half4 sample = optical_normals.read (local);
    colour =
      sample.w > 0.0h ? float3 (sample.xyz) * 0.5f + 0.5f : float3 (0.0f);
  } else if (panel.y == 0u) {
    colour = float3 (radiance.read (local).xyz);
  } else if (panel.x == 0u) {
    const half4 sample = hit_normals.read (local);
    colour =
      sample.w > 0.5h ? float3 (sample.xyz) * 0.5f + 0.5f : float3 (0.0f);
  } else if (panel.x == 1u) {
    const float distance = hit_distances.read (local).x;
    colour =
      distance > 0.0f
        ? float3 (log2 (1.0f + distance) / log2 (1.0f + uniforms.camera.w))
        : float3 (0.0f);
  } else {
    colour = validity.read (local).rgb;
  }
  output[position.y * uint (uniforms.output.x) + position.x] =
    half4 (half3 (colour), 1.0h);
}
