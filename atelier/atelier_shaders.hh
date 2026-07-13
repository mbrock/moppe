#pragma once

#include <string_view>

namespace atelier {
  inline constexpr std::string_view metal_shader_source = R"(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
  float4x4 world_to_clip;
};

vertex float4 atelier_vertex(const device float3* positions [[buffer(0)]],
                             constant Uniforms& uniforms [[buffer(1)]],
                             uint id [[vertex_id]]) {
  return uniforms.world_to_clip * float4(positions[id], 1.0);
}

fragment half4 atelier_fragment() {
  return half4(0.94h, 0.72h, 0.27h, 1.0h);
}
)";
}
