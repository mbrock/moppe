#include "common.h"

struct DustVaryings {
  float4 position [[position]];
  float2 uv;
  float4 color;
};

inline uint dust_hash (uint x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  return x ^ (x >> 16);
}

inline float dust_random (uint emission, uint particle, uint property) {
  return float (dust_hash (emission ^ (particle * 0x9e3779b9u) ^
                           (property * 0x85ebca6bu))) /
         4294967295.0;
}

inline DustVaryings dust_vertex_for (constant MoppeFrameUniforms& frame,
                                     constant MoppeDustUniforms& dust,
                                     const MoppeDustEmission emission,
                                     uint particle,
                                     uint corner) {
  const uint eid = uint (emission.color_id.w);
  const float age = dust.params.x - emission.position_birth.w;
  const float lifetime =
    emission.style.y * (0.5 + 0.4 * dust_random (eid, particle, 6));
  const float life01 = saturate (1.0 - age / lifetime);
  const float spread = emission.style.w;
  const float3 offset =
    float3 (dust_random (eid, particle, 0) * 2.0 - 1.0,
            0.4 * (dust_random (eid, particle, 1) * 2.0 - 1.0),
            dust_random (eid, particle, 2) * 2.0 - 1.0) *
    (0.7 * spread);
  const float3 velocity =
    emission.velocity_count.xyz +
    float3 (3.0 * (dust_random (eid, particle, 3) * 2.0 - 1.0),
            2.5 + 2.0 * (dust_random (eid, particle, 4) * 2.0 - 1.0),
            3.0 * (dust_random (eid, particle, 5) * 2.0 - 1.0)) *
      spread;
  float3 center = emission.position_birth.xyz + offset + velocity * age;
  center.y -= 0.5 * emission.style.z * age * age;

  const float base_size =
    emission.style.x * (0.3 + 0.8 * dust_random (eid, particle, 7));
  const float size = base_size * (1.7 - 0.7 * life01);
  const float rotation =
    (dust_random (eid, particle, 8) * 2.0 - 1.0) * 3.14159 +
    (dust_random (eid, particle, 9) * 2.0 - 1.0) * 2.2 * age;
  const float2 corners[4] = {
    float2 (-1, -1), float2 (1, -1), float2 (1, 1), float2 (-1, 1)
  };
  const float2 q = corners[corner];
  const float ca = cos (rotation), sa = sin (rotation);
  const float2 rotated (q.x * ca - q.y * sa, q.x * sa + q.y * ca);
  const float3 world = center + dust.camera_right.xyz * (rotated.x * size) +
                       dust.camera_up.xyz * (rotated.y * size);
  const float age01 = 1.0 - life01;
  const float fade_in = min (1.0, age01 * 8.0);
  const float value = 0.88 + 0.16 * dust_random (eid, particle, 10);

  DustVaryings out;
  out.position =
    life01 > 0.0 ? frame.view_proj * float4 (world, 1.0) : float4 (0, 0, -1, 0);
  out.uv = q;
  out.color =
    float4 (moppe_srgb (emission.color_id.xyz * value), fade_in * life01);
  return out;
}

vertex DustVaryings dust_vertex (uint vid [[vertex_id]],
                                 uint iid [[instance_id]],
                                 constant MoppeFrameUniforms& frame
                                 [[buffer (MOPPE_BUF_FRAME)]],
                                 constant MoppeDustUniforms& dust
                                 [[buffer (MOPPE_BUF_DRAW)]],
                                 constant MoppeDustEmission& emission
                                 [[buffer (MOPPE_BUF_VERTICES)]]) {
  constexpr uint corner_for_vertex[6] = { 0, 1, 2, 0, 2, 3 };
  return dust_vertex_for (frame, dust, emission, iid, corner_for_vertex[vid]);
}

using DustMesh =
  metal::mesh<DustVaryings, void, 256, 128, metal::topology::triangle>;

[[mesh]] void dust_mesh (DustMesh out,
                         uint emission_index [[threadgroup_position_in_grid]],
                         uint thread_id [[thread_index_in_threadgroup]],
                         constant MoppeFrameUniforms& frame
                         [[buffer (MOPPE_BUF_FRAME)]],
                         constant MoppeDustUniforms& dust
                         [[buffer (MOPPE_BUF_DRAW)]],
                         const device MoppeDustEmission* emissions
                         [[buffer (MOPPE_BUF_VERTICES)]]) {
  const MoppeDustEmission emission = emissions[emission_index];
  const uint count = min (uint (emission.velocity_count.w), 64u);
  if (thread_id == 0u)
    out.set_primitive_count (count * 2u);
  if (thread_id >= count)
    return;
  const uint vb = thread_id * 4u;
  const uint pb = thread_id * 2u;
  for (uint corner = 0; corner < 4u; ++corner)
    out.set_vertex (vb + corner,
                    dust_vertex_for (frame, dust, emission, thread_id, corner));
  const uint indices[6] = { 0, 1, 2, 0, 2, 3 };
  for (uint i = 0; i < 6u; ++i)
    out.set_index (pb * 3u + i, vb + indices[i]);
}

fragment float4 dust_fragment (DustVaryings in [[stage_in]]) {
  const float radius = length (in.uv);
  const float soft = 1.0 - smoothstep (0.15, 1.0, radius);
  return float4 (in.color.rgb, in.color.a * soft);
}
