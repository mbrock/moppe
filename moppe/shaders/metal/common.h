// Shared MSL helpers for all moppe shaders.
#ifndef MOPPE_MSL_COMMON_H
#define MOPPE_MSL_COMMON_H

#include "../../render/metal/shader_types.h"
#include <metal_stdlib>

using namespace metal;

// The 40-byte streamed vertex from render::DrawList (see
// moppe/render/types.hh).
struct MoppeVertexIn {
  packed_float3 position;
  packed_float3 normal;
  packed_float2 uv;
  uchar4 color;
  uchar4 flags; // x: lit, y: fogged, z: wind, w: grass
};

// Wind sway: a slow prevailing gust plus a faster flutter,
// phased by world position so the field moves as travelling waves
// rather than in lockstep.  `w` is the vertex's wind weight (0..1);
// anchored roots record 0 and skip this entirely.
inline float3 moppe_wind (float3 world, float w, float t) {
  const float ph = world.x * 0.043 + world.z * 0.051;
  const float gust =
    sin (t * 1.13 + ph) + 0.45 * sin (t * 2.63 + ph * 1.7 + 1.3);
  const float flutter = sin (t * 6.8 + ph * 13.0 + world.y * 1.9);
  const float amp = 0.24 * w;
  world.x += (0.79 * gust + 0.30 * flutter) * amp;
  world.z += (0.53 * gust - 0.24 * flutter) * amp;
  // Blades and boughs bow slightly as they bend away from vertical.
  world.y -= 0.15 * abs (gust) * amp;
  return world;
}

// Cheap tiling-free value noise for water surfaces: foam breakup and
// scrolled ripple fields share one look across rivers and lakes.
inline float moppe_hash12 (float2 p) {
  return fract (sin (dot (p, float2 (127.1, 311.7))) * 43758.5453);
}

inline float moppe_value_noise (float2 p) {
  const float2 i = floor (p);
  const float2 f = fract (p);
  const float2 u = f * f * (3.0 - 2.0 * f);
  const float a = moppe_hash12 (i);
  const float b = moppe_hash12 (i + float2 (1, 0));
  const float c = moppe_hash12 (i + float2 (0, 1));
  const float d = moppe_hash12 (i + float2 (1, 1));
  return mix (mix (a, b, u.x), mix (c, d, u.x), u.y);
}

// The scene computes in LINEAR light (half-float targets); art
// colors are still authored as familiar display-space numbers and
// decoded with this at the point of use.  The present pass
// tonemaps (ACES) and encodes for the active SDR or EDR drawable.
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

// Shared terrain occlusion for every sun-lit scene material. Only direct
// sunlight and sun specular use this value; sky fill and reflections remain
// visible in a mountain's shadow.
inline float moppe_sun_visibility (float3 world_pos,
                                   float3 normal,
                                   float3 sun_dir,
                                   float fog,
                                   float4x4 light_matrix,
                                   float shadow_strength,
                                   float shadow_texel,
                                   depth2d<float> shadow_map) {
  if (shadow_strength < 0.01)
    return 1.0;
  const float4 shadow_coord = light_matrix * float4 (world_pos, 1.0);
  const float3 proj = shadow_coord.xyz / shadow_coord.w;
  if (any (proj < 0.0) || any (proj > 1.0))
    return 1.0;
  const float3 n = normalize (normal);
  const float3 l = normalize (sun_dir);
  const float bias = 0.0006 + 0.0025 * (1.0 - max (dot (n, l), 0.0));
  const float z = proj.z - bias;
  constexpr sampler smp (coord::normalized,
                         address::clamp_to_edge,
                         filter::linear,
                         compare_func::less_equal);
  float visibility = 0.4 * shadow_map.sample_compare (smp, proj.xy, z);
  for (float dy = -1.5; dy <= 1.5; dy += 3.0)
    for (float dx = -1.5; dx <= 1.5; dx += 3.0)
      visibility += 0.15 * shadow_map.sample_compare (
                             smp, proj.xy + float2 (dx, dy) * shadow_texel, z);
  visibility = pow (visibility, 1.3);
  const float fade = saturate (2.5 * (1.0 - fog));
  return mix (1.0, visibility, shadow_strength * fade);
}

// Aerial perspective: haze brightens and warms toward the sun (two
// scatter lobes) and cools slightly opposite it, so panning across
// the horizon reads as one continuous atmosphere.
inline float3
moppe_warmed_fog (float3 fog_color, float3 view_dir, float3 sun_dir) {
  const float daylight = smoothstep (-0.08, 0.18, sun_dir.y);
  const float golden = daylight * (1.0 - smoothstep (0.15, 0.65, sun_dir.y));
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
  const float3 base = fog_color * mix (float3 (1.0, 1.0, 1.0),
                                       float3 (0.95, 0.98, 1.05),
                                       0.45 * away * daylight);

  return base +
         sun_color * daylight *
           (broad * (0.05 + 0.14 * horizon) + tight * (0.04 + 0.10 * golden));
}

// Lightweight environment radiance for reflective materials. It follows the
// same sun and horizon palette as the atmospheric shader, but unlike fog it
// varies with the reflected direction: upward-facing water sees cool sky,
// grazing water sees the bright horizon, and only the solar direction carries
// a warm highlight. This is deliberately cheap enough for rivers and lakes.
inline float3
moppe_sky_radiance (float3 fog_color, float3 direction, float3 sun_dir) {
  const float3 d = normalize (direction);
  const float up = saturate (d.y);
  const float horizon = pow (1.0 - up, 2.2);
  const float3 zenith = fog_color * float3 (0.48, 0.72, 1.05);
  float3 sky = mix (zenith, fog_color * 1.12, horizon);

  const float daylight = smoothstep (-0.08, 0.18, sun_dir.y);
  const float golden = daylight * (1.0 - smoothstep (0.15, 0.65, sun_dir.y));
  const float3 solar = mix (moppe_srgb (float3 (1.0, 0.95, 0.80)),
                            moppe_srgb (float3 (1.0, 0.55, 0.24)),
                            golden);
  const float sun_dot = max (dot (d, normalize (sun_dir)), 0.0);
  sky += solar * daylight *
         (0.12 * pow (sun_dot, 12.0) + 0.65 * pow (sun_dot, 96.0));
  return sky;
}

#endif
