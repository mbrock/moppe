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

// The scene computes in LINEAR light (half-float targets); art
// colors are still authored as familiar display-space numbers and
// decoded with this at the point of use.  The present pass
// tonemaps (ACES) and encodes back to sRGB.
inline float3 moppe_srgb (float3 c) {
  return pow (c, 2.2);
}

// The terrain haze curve (from shaders/test.vert), minus the
// valley-mist term, which stays terrain-only by design.
inline float moppe_distance_fog (float dist, float fog_scale) {
  return saturate (1.0 - exp (-pow (dist * fog_scale, 1.5)));
}

// Soft hemisphere fill keeps silhouettes readable without flattening
// the directional sun: cool sky above, warm earth bounce below.
inline float3 moppe_hemisphere_light (float3 ambient, float3 normal) {
  const float hemi = 0.5 + 0.5 * normalize (normal).y;
  const float3 ground = moppe_srgb (float3 (0.92, 0.74, 0.58));
  const float3 sky = moppe_srgb (float3 (0.74, 0.92, 1.12));
  return ambient * mix (ground, sky, hemi);
}

// Aerial perspective: haze brightens and warms toward the sun (two
// scatter lobes) and cools slightly opposite it, so panning across
// the horizon reads as one continuous atmosphere.
inline float3 moppe_warmed_fog (float3 fog_color, float3 view_dir,
				float3 sun_dir) {
  const float daylight = smoothstep (-0.08, 0.18, sun_dir.y);
  const float golden = daylight
    * (1.0 - smoothstep (0.15, 0.65, sun_dir.y));
  const float sun_dot = max (dot (view_dir, sun_dir), 0.0);
  const float broad = pow (sun_dot, 5.0);
  const float tight = pow (sun_dot, 24.0);
  const float horizon = pow (1.0 - abs (view_dir.y), 3.0);
  const float3 sun_color = mix (moppe_srgb (float3 (1.0, 0.96, 0.84)),
				moppe_srgb (float3 (1.0, 0.58, 0.28)),
				golden);

  // Rayleigh-ish back scatter: haze goes faintly blue away from
  // the sun.
  const float away = max (-dot (view_dir, sun_dir), 0.0);
  const float3 base = fog_color
    * mix (float3 (1.0, 1.0, 1.0), float3 (0.95, 0.98, 1.05),
	   0.45 * away * daylight);

  return base
    + sun_color * daylight
      * (broad * (0.05 + 0.14 * horizon)
	 + tight * (0.04 + 0.10 * golden));
}

#endif
