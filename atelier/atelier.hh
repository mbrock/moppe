#pragma once

#include <simd/simd.h>

#include <string_view>
#include <vector>

namespace atelier {
  using GpuVector = simd_float3;

  struct Uniforms {
    simd_float4x4 world_to_clip;
  };

  struct Frame {
    Uniforms uniforms;
  };

  std::vector<GpuVector> coin_wire_mesh ();
  std::string_view shader_source ();
  Frame frame (float elapsed_seconds, float aspect);
}
