// Shared MSL helpers for all moppe shaders.
#ifndef MOPPE_MSL_COMMON_H
#define MOPPE_MSL_COMMON_H

#include <metal_stdlib>
#include "../../render/metal/shader_types.h"

using namespace metal;

// The 40-byte streamed vertex from render::DrawList (see
// moppe/render/types.hh).
struct MoppeVertexIn {
  packed_float3 position;
  packed_float3 normal;
  packed_float2 uv;
  uchar4 color;
  uchar4 flags;               // x: lit, y: fogged
};

// The terrain haze curve (from shaders/test.vert), minus the
// valley-mist term, which stays terrain-only by design.
inline float moppe_distance_fog (float dist, float fog_scale) {
  return saturate (1.0 - exp (-pow (dist * fog_scale, 1.5)));
}

// Aerial perspective: haze brightens and warms toward the sun.
inline float3 moppe_warmed_fog (float3 fog_color, float3 view_dir,
				float3 sun_dir) {
  float sun_amt = pow (max (dot (view_dir, sun_dir), 0.0), 8.0);
  return mix (fog_color, fog_color * float3 (1.25, 1.12, 0.9), sun_amt);
}

#endif
