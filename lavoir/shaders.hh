#pragma once

#include <cstdint>

/// The shader boundary. The uniforms struct is shared verbatim with
/// the MSL source below; the height buffer is a metres column read as
/// raw floats, and ndc_per_metre is the exchange rate that carries a
/// displacement into clip space. Rendering is a report: nothing here
/// writes back.

namespace lavoir {
  struct water_uniforms {
    std::uint32_t site_count;
    float ndc_per_metre;
    float rest_level_ndc;
    float time_seconds;
  };

  inline constexpr const char* water_shader_source = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct water_uniforms {
  uint site_count;
  float ndc_per_metre;
  float rest_level_ndc;
  float time_seconds;
};

struct varyings {
  float4 position [[position]];
  float depth01;
};

/// Vertex pulling from the lent column: heights are metres, and the
/// uniform carries the metre-to-clip exchange rate. Even vertices ride
/// the surface; odd vertices anchor the floor.
vertex varyings water_vertex (uint vid [[vertex_id]],
                              device const water_uniforms& u [[buffer(0)]],
                              device const float* heights [[buffer(1)]]) {
  const uint site = vid >> 1;
  const bool floor = (vid & 1u) != 0u;
  const float x = -1.0 + 2.0 * float (site) / float (u.site_count - 1);
  const float surface = u.rest_level_ndc + heights[site] * u.ndc_per_metre;

  varyings out;
  out.position = float4 (x, floor ? -1.0 : surface, 0.0, 1.0);
  out.depth01 = floor ? 1.0 : 0.0;
  return out;
}

/// Water over linen: a shallow-to-deep gradient with a breath of foam
/// at the surface.
fragment float4 water_fragment (varyings in [[stage_in]]) {
  const float3 shallow = float3 (0.58, 0.76, 0.79);
  const float3 deep = float3 (0.13, 0.32, 0.42);
  float3 colour = mix (shallow, deep, pow (in.depth01, 0.8));
  colour = mix (float3 (0.94, 0.96, 0.95), colour,
                smoothstep (0.0, 0.045, in.depth01));
  return float4 (colour, 1.0);
}
)MSL";
}
