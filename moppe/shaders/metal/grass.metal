// Dense procedural full-geometry grass. A camera-centered instance grid is
// expanded into segmented curved blades in the vertex stage while sampling
// the authoritative terrain height and normal textures. This keeps the CPU
// submission constant as density grows and is intentionally shaped like the
// compact seed -> blade contract a future mesh shader will consume.

#include "common.h"

struct GrassVaryings {
  float4 position [[position]];
  float3 world_pos;
  float3 normal;
  float3 color;
  float height01;
  float fog;
};

static inline uint grass_hash (uint x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  return x ^ (x >> 16);
}

static inline float grass_random (uint seed) {
  return float (grass_hash (seed) & 0x00ffffffu) * (1.0 / 16777216.0);
}

static inline float3 grass_terrain_normal
  (uint2 p, texture2d<float, access::read> normals) {
  const float2 nxz = normals.read (p).rg;
  return float3 (nxz.x, sqrt (max (1.0 - dot (nxz, nxz), 0.0)), nxz.y);
}

vertex GrassVaryings
grass_vertex (uint vid [[vertex_id]], uint iid [[instance_id]],
	      constant MoppeFrameUniforms& frame [[buffer(MOPPE_BUF_FRAME)]],
	      constant MoppeGrassUniforms& grass [[buffer(MOPPE_BUF_DRAW)]],
	      texture2d<float, access::read> heights
	        [[texture(MOPPE_TEX_HEIGHTS)]],
	      texture2d<float, access::read> normals
	        [[texture(MOPPE_TEX_NORMALS)]]) {
  constexpr float tau = 6.28318530718;
  constexpr uint vertices_per_blade = 18;
  const uint side_count = uint (grass.grid.w);
  const uint cell_x = iid % side_count;
  const uint cell_z = iid / side_count;
  const uint blade = vid / vertices_per_blade;
  const uint local = vid % vertices_per_blade;
  const uint segment = local / 6;
  const uint corner = local % 6;

  const uint seed = grass_hash
    ((cell_x + uint (grass.grid.x / grass.grid.z)) * 73856093u
     ^ (cell_z + uint (grass.grid.y / grass.grid.z)) * 19349663u
     ^ blade * 83492791u);
  const float2 jitter
    (grass_random (seed + 1u) - 0.5, grass_random (seed + 2u) - 0.5);
  float2 world_xz = grass.grid.xy
    + (float2 (cell_x, cell_z) + 0.5 + jitter * 0.92) * grass.grid.z;

  const float2 terrain_limit
    (heights.get_width () - 1, heights.get_height () - 1);
  float2 grid = world_xz / grass.terrain.xz;
  bool valid = true;
  if (grass.limits.z > 0.5) {
    grid -= floor (grid / terrain_limit) * terrain_limit;
  } else {
    valid = all (grid >= 0.0) && all (grid <= terrain_limit);
    grid = clamp (grid, 0.0, terrain_limit);
  }
  const uint2 sample_pos = uint2 (round (grid));
  const float terrain_height = heights.read (sample_pos).r;
  const float3 ground_normal = grass_terrain_normal (sample_pos, normals);
  const float3 root
    (world_xz.x, terrain_height * grass.terrain.y, world_xz.y);

  const float2 camera_delta = world_xz - frame.camera_pos.xz;
  const float distance = length (camera_delta);
  const float patch = saturate (0.52
    + 0.28 * sin (world_xz.x * 0.036
	+ 1.7 * sin (world_xz.y * 0.019))
    + 0.20 * sin (world_xz.y * 0.071 - world_xz.x * 0.043));
  const float occupancy = 0.68 + 0.31 * patch;
  valid = valid && distance < grass.terrain.w
    && terrain_height > grass.limits.x + 2.0 / grass.terrain.y
    && terrain_height < grass.limits.y
    && ground_normal.y >= 0.70
    && grass_random (seed + 3u) <= occupancy;

  // Density LOD: all blades survive nearby, while the outer ring drops a
  // deterministic subset before its height fades, avoiding a hard circle.
  const float outer = smoothstep (0.68 * grass.terrain.w,
				  grass.terrain.w, distance);
  valid = valid && (blade == 0u
    || grass_random (seed + 9u) > outer * (0.35 + 0.25 * blade));

  const float angle = tau * grass_random (seed + 4u);
  const float2 direction (cos (angle), sin (angle));
  const float2 side (-direction.y, direction.x);
  const float dry = saturate
    (0.70 * ((terrain_height - 0.10) / 0.22)
     + 0.20 * grass_random (seed + 5u) + 0.10 * patch);
  const float height = mix (0.32, 0.88, grass_random (seed + 6u))
    * mix (0.72, 1.27, patch)
    * (1.0 - smoothstep (0.88 * grass.terrain.w,
			 grass.terrain.w, distance));
  const float bend = height * mix (0.06, 0.34, grass_random (seed + 7u));
  const float half_width = mix (0.016, 0.036, grass_random (seed + 8u));

  const float t0 = float (segment) / 3.0;
  const float t1 = float (segment + 1u) / 3.0;
  const bool upper = corner == 2u || corner == 3u || corner == 5u;
  const float t = upper ? t1 : t0;
  const float side_sign = (corner == 0u || corner == 2u || corner == 3u)
    ? -1.0 : 1.0;
  const float width = half_width * (1.0 - 0.86 * t);
  float3 world = root + float3 (direction.x * bend * t * t,
				height * t,
				direction.y * bend * t * t)
    + float3 (side.x * width * side_sign, 0, side.y * width * side_sign);
  world = moppe_wind (world, t * t * mix (0.45, 0.90,
					 grass_random (seed + 10u)), frame.misc.x);
  if (!valid)
    world = root;

  const float3 face = normalize (float3 (-side.y, 0.18, side.x));
  const float3 normal = normalize (mix (face, float3 (0, 1, 0),
				       0.25 + 0.60 * t));
  const float3 young (0.24, 0.43, 0.10);
  const float3 old (0.58, 0.62, 0.16);
  const float3 base_color = mix (young, old, dry);
  const float3 tip_color = mix (base_color, float3 (0.62, 0.76, 0.22),
				0.42 + 0.30 * patch);

  GrassVaryings out;
  out.position = frame.view_proj * float4 (world, 1.0);
  out.world_pos = world;
  out.normal = normal;
  out.color = moppe_srgb (mix (base_color, tip_color, t));
  out.height01 = t;
  out.fog = moppe_distance_fog
    (distance, frame.fog_color.w);
  return out;
}

fragment float4
grass_fragment (GrassVaryings in [[stage_in]],
		 constant MoppeFrameUniforms& frame [[buffer(MOPPE_BUF_FRAME)]],
		 depth2d<float> shadow_map [[texture(MOPPE_TEX_SHADOW)]],
		 bool front_facing [[front_facing]]) {
  float3 n = normalize (in.normal);
  if (!front_facing)
    n = -n;
  n = normalize (mix (n, float3 (0, 1, 0), 0.18 * in.height01));
  const float3 l = normalize (frame.sun_dir.xyz);
  const float3 v = normalize (frame.camera_pos.xyz - in.world_pos);
  const float3 h = normalize (l + v);
  const float lambert = saturate ((dot (n, l) + 0.10) / 1.10);
  const float visibility = moppe_sun_visibility
    (in.world_pos, n, l, in.fog, frame.light_matrix,
     frame.shadow.x, frame.shadow.y, shadow_map);
  const float root_occlusion = mix
    (0.46, 1.0, smoothstep (0.0, 0.85, in.height01));
  float3 light = moppe_hemisphere_light (frame.ambient.rgb, n)
    * root_occlusion + frame.sun_diffuse.rgb * lambert * visibility;
  light += frame.sun_specular.rgb * visibility * 0.07
    * pow (max (dot (n, h), 0.0), 30.0);
  const float backlight = pow (max (dot (-n, l), 0.0), 1.5)
    * (0.20 + 0.80 * in.height01);
  light += frame.sun_diffuse.rgb * visibility * backlight * 0.32;
  float3 color = in.color * light;

  const float3 to_frag = in.world_pos - frame.camera_pos.xyz;
  const float3 fog_color = moppe_warmed_fog
    (frame.fog_color.rgb, normalize (to_frag), l);
  color = mix (color, fog_color, smoothstep (0.0, 0.9, in.fog));
  return float4 (color, 1.0);
}
