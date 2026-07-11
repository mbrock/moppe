// Dense procedural full-geometry grass. Blades are pure functions of a
// world cell index, the frame, and the authoritative terrain textures;
// no per-blade geometry exists on the CPU. Two execution paths share the
// same blade math: an instanced vertex-shader fallback, and the mesh
// pipeline, where an object stage culls patches of cells and a mesh
// stage emits the surviving blades as indexed triangles.

#include "common.h"

struct GrassVaryings {
  float4 position [[position]];
  float3 world_pos;
  float3 normal;
  float3 color;
  float height01;
  float ground_shade;
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

// Bilinear terrain samples: nearest-neighbor roots terrace on slopes
// (every blade in a cell snaps to one sample height) and facet the
// shading per cell, which reads as banded clumps from the saddle.
static inline float grass_ground_height (
  float2 grid, float2 edge, texture2d<float, access::read> heights) {
  const float2 clamped = clamp (grid, 0.0, edge);
  const uint2 i = uint2 (clamped);
  const float2 f = clamped - float2 (i);
  const float h00 = heights.read (i).r;
  const float h10 = heights.read (i + uint2 (1, 0)).r;
  const float h01 = heights.read (i + uint2 (0, 1)).r;
  const float h11 = heights.read (i + uint2 (1, 1)).r;
  return mix (mix (h00, h10, f.x), mix (h01, h11, f.x), f.y);
}

static inline float3 grass_ground_normal (
  float2 grid, float2 edge, texture2d<float, access::read> normals) {
  const float2 clamped = clamp (grid, 0.0, edge);
  const uint2 i = uint2 (clamped);
  const float2 f = clamped - float2 (i);
  const float2 n00 = normals.read (i).rg;
  const float2 n10 = normals.read (i + uint2 (1, 0)).rg;
  const float2 n01 = normals.read (i + uint2 (0, 1)).rg;
  const float2 n11 = normals.read (i + uint2 (1, 1)).rg;
  const float2 nxz = mix (mix (n00, n10, f.x), mix (n01, n11, f.x), f.y);
  return float3 (nxz.x, sqrt (max (1.0 - dot (nxz, nxz), 0.0)), nxz.y);
}

// Everything one blade needs, computed once per blade.
struct GrassBlade {
  bool valid;
  float3 root;
  float3 ground_normal;
  float2 direction;
  float2 side;
  float height;
  float bend;
  float half_width;
  float sway;
  float distance;
  float ground_shade;
  float3 base_color;
  float3 tip_color;
};

static GrassBlade grass_blade (uint world_cell_x,
                               uint world_cell_z,
                               uint blade,
                               constant MoppeFrameUniforms& frame,
                               constant MoppeGrassUniforms& grass,
                               texture2d<float, access::read> heights,
                               texture2d<float, access::read> normals,
                               texture2d<float, access::read> moisture_map) {
  constexpr float tau = 6.28318530718;
  GrassBlade b;
  b.valid = false;
  b.root = float3 (0);
  b.ground_normal = float3 (0, 1, 0);

  const uint seed = grass_hash (world_cell_x * 73856093u ^
                                world_cell_z * 19349663u ^ blade * 83492791u);
  const float2 jitter (grass_random (seed + 1u) - 0.5,
                       grass_random (seed + 2u) - 0.5);
  const float2 world_xz =
    (float2 (int2 (world_cell_x, world_cell_z)) + 0.5 + jitter * 0.92) *
    grass.grid.z;

  const float2 terrain_limit (heights.get_width () - 1,
                              heights.get_height () - 1);
  float2 grid = world_xz / grass.terrain.xz;
  bool valid = true;
  if (grass.limits.z > 0.5) {
    grid -= floor (grid / terrain_limit) * terrain_limit;
  } else {
    valid = all (grid >= 0.0) && all (grid <= terrain_limit);
    grid = clamp (grid, 0.0, terrain_limit);
  }
  const float terrain_height =
    grass_ground_height (grid, terrain_limit - 1.0, heights);
  b.root = float3 (world_xz.x, terrain_height * grass.terrain.y, world_xz.y);

  const float2 camera_delta = world_xz - frame.camera_pos.xz;
  b.distance = length (camera_delta);

  // Reject the whole blade before any styling work: outside the radius,
  // outside the terrain band, or outside the view frustum (with margin
  // for blade height and wind sway).
  valid = valid && b.distance < grass.terrain.w &&
          terrain_height > grass.limits.x + 2.0 / grass.terrain.y &&
          terrain_height < grass.limits.y;
  if (valid && b.distance > 12.0) {
    const float4 clip =
      frame.view_proj * float4 (b.root + float3 (0.0, 0.6, 0.0), 1.0);
    const float margin = 1.30 * clip.w + 4.0;
    valid = clip.w > 0.0 && abs (clip.x) < margin && abs (clip.y) < margin;
  }

  // Patchiness from drifting value noise; plane-wave sines band into
  // visible stripes across the field. The hydrology's moisture field
  // leads: lush dense growth near water, sparse dry tufts on ridges.
  const float moisture =
    grass_ground_height (grid, terrain_limit - 1.0, moisture_map);
  const float patch =
    saturate (0.25 + 0.95 * moppe_value_noise (world_xz * 0.041) *
                       (0.55 + 0.45 * moppe_value_noise (world_xz * 0.013)));
  const float occupancy = (0.55 + 0.31 * patch) * mix (0.72, 1.18, moisture);
  valid = valid && grass_random (seed + 3u) <= occupancy;

  // Density LOD: all blades survive nearby, while the outer ring drops a
  // deterministic subset before its height fades, avoiding a hard circle.
  const float outer =
    smoothstep (0.55 * grass.terrain.w, grass.terrain.w, b.distance);
  valid = valid && (blade == 0u ||
                    grass_random (seed + 9u) > outer * (0.45 + 0.30 * blade));
  b.valid = valid;
  if (!valid)
    return b;

  b.ground_normal = grass_ground_normal (grid, terrain_limit - 1.0, normals);
  // Blades inherit their hillside's light: a slope facing away from
  // the sun shades its whole sward, so grass stops glowing against
  // dark terrain.
  b.ground_shade =
    0.30 + 0.70 * saturate (
                    1.3 * dot (b.ground_normal, normalize (frame.sun_dir.xyz)));
  const float angle = tau * grass_random (seed + 4u);
  b.direction = float2 (cos (angle), sin (angle));
  b.side = float2 (-b.direction.y, b.direction.x);
  const float dry =
    saturate (0.85 * (1.0 - moisture) + 0.20 * grass_random (seed + 5u) +
              0.10 * patch - 0.10);
  b.height =
    mix (0.32, 0.88, grass_random (seed + 6u)) * mix (0.72, 1.27, patch) *
    mix (0.62, 1.30, moisture) *
    (1.0 - smoothstep (0.88 * grass.terrain.w, grass.terrain.w, b.distance));
  b.bend = b.height * mix (0.06, 0.34, grass_random (seed + 7u));
  b.half_width = mix (0.016, 0.036, grass_random (seed + 8u));
  b.sway = mix (0.45, 0.90, grass_random (seed + 10u));

  const float3 young (0.24, 0.43, 0.10);
  const float3 old (0.58, 0.62, 0.16);
  b.base_color = mix (young, old, dry);
  b.tip_color =
    mix (b.base_color, float3 (0.62, 0.76, 0.22), 0.42 + 0.30 * patch);
  return b;
}

static GrassVaryings grass_blade_vertex (const GrassBlade b,
                                         float t,
                                         float side_sign,
                                         constant MoppeFrameUniforms& frame,
                                         constant MoppeGrassUniforms& grass) {
  // A distance floor on width keeps far blades at least a pixel wide
  // instead of shimmering sub-pixel slivers.
  const float width =
    max (b.half_width, 0.0012 * b.distance) * (1.0 - 0.86 * t);
  float3 world =
    b.root +
    float3 (b.direction.x * b.bend * t * t,
            b.height * t,
            b.direction.y * b.bend * t * t) +
    float3 (b.side.x * width * side_sign, 0, b.side.y * width * side_sign);
  world = moppe_wind (world, t * t * b.sway, frame.misc.x);

  // Blades lean with the slope: tilt the up direction toward the ground
  // normal so hillsides read as combed rather than pin-straight rows.
  world.xz += b.ground_normal.xz * (0.35 * b.height * t);

  const float3 face = normalize (float3 (-b.side.y, 0.18, b.side.x));
  const float3 normal =
    normalize (mix (face, float3 (0, 1, 0), 0.25 + 0.60 * t));

  GrassVaryings out;
  out.position = frame.view_proj * float4 (world, 1.0);
  out.world_pos = world;
  out.normal = normal;
  out.color = moppe_srgb (mix (b.base_color, b.tip_color, t));
  out.height01 = t;
  out.ground_shade = b.ground_shade;
  out.fog = moppe_distance_fog (b.distance, frame.fog_color.w);
  return out;
}

// ---- instanced fallback path ---------------------------------------

vertex GrassVaryings grass_vertex (
  uint vid [[vertex_id]],
  uint iid [[instance_id]],
  constant MoppeFrameUniforms& frame [[buffer (MOPPE_BUF_FRAME)]],
  constant MoppeGrassUniforms& grass [[buffer (MOPPE_BUF_DRAW)]],
  texture2d<float, access::read> heights [[texture (MOPPE_TEX_HEIGHTS)]],
  texture2d<float, access::read> normals [[texture (MOPPE_TEX_NORMALS)]],
  texture2d<float, access::read> moisture_map
  [[texture (MOPPE_TEX_MOISTURE)]]) {
  constexpr uint vertices_per_blade = 18;
  const uint side_count = uint (grass.grid.w);
  const uint cell_x = iid % side_count;
  const uint cell_z = iid / side_count;
  const uint blade = vid / vertices_per_blade;
  const uint local = vid % vertices_per_blade;

  // grid.xy carries the window origin as integer cell indices, exact in
  // float, so seeds are bit-stable while the window follows the camera.
  const int2 origin = int2 (grass.grid.xy);
  const uint world_cell_x = uint (origin.x + int (cell_x));
  const uint world_cell_z = uint (origin.y + int (cell_z));

  const GrassBlade b = grass_blade (world_cell_x,
                                    world_cell_z,
                                    blade,
                                    frame,
                                    grass,
                                    heights,
                                    normals,
                                    moisture_map);
  if (!b.valid) {
    GrassVaryings out;
    out.position = float4 (0.0, 0.0, -1.0, 0.0);
    out.world_pos = b.root;
    out.normal = float3 (0, 1, 0);
    out.color = float3 (0);
    out.height01 = 0.0;
    out.ground_shade = 1.0;
    out.fog = 1.0;
    return out;
  }

  const uint segment = local / 6;
  const uint corner = local % 6;
  const bool upper = corner == 2u || corner == 3u || corner == 5u;
  const float t = (float (segment) + (upper ? 1.0 : 0.0)) / 3.0;
  const float side_sign =
    (corner == 0u || corner == 2u || corner == 3u) ? -1.0 : 1.0;
  return grass_blade_vertex (b, t, side_sign, frame, grass);
}

// ---- mesh pipeline path ---------------------------------------------

// A patch is 4x2 grass cells; with up to four blades per cell that is 32
// blades, and at 8 vertices / 6 triangles per blade one mesh threadgroup
// stays inside the 256-vertex / 512-primitive meshlet limits.
#define GRASS_PATCH_X 4
#define GRASS_PATCH_Z 2
#define GRASS_PATCH_BLADES 32
#define GRASS_OBJECT_THREADS 64

struct GrassPayload {
  uint count;
  uint2 patches[GRASS_OBJECT_THREADS];
};

using GrassMesh = metal::mesh<GrassVaryings,
                              void,
                              GRASS_PATCH_BLADES * 8,
                              GRASS_PATCH_BLADES * 6,
                              metal::topology::triangle>;

[[object]] void grass_object (object_data GrassPayload& payload [[payload]],
                              metal::mesh_grid_properties mesh_grid,
                              uint thread_id [[thread_index_in_threadgroup]],
                              uint3 grid_pos [[thread_position_in_grid]],
                              constant MoppeFrameUniforms& frame
                              [[buffer (MOPPE_BUF_FRAME)]],
                              constant MoppeGrassUniforms& grass
                              [[buffer (MOPPE_BUF_DRAW)]],
                              texture2d<float, access::read> heights
                              [[texture (MOPPE_TEX_HEIGHTS)]]) {
  threadgroup atomic_uint survivors;
  if (thread_id == 0u)
    atomic_store_explicit (&survivors, 0u, metal::memory_order_relaxed);
  threadgroup_barrier (metal::mem_flags::mem_threadgroup);

  const uint patches_x = uint (grass.mesh.x);
  const uint patches_z = uint (grass.mesh.y);
  const uint patch_index = grid_pos.x;
  const uint patch_x = patch_index % patches_x;
  const uint patch_z = patch_index / patches_x;
  bool valid = patch_index < patches_x * patches_z && patch_z < patches_z;

  if (valid) {
    // Patch center in world space, with a conservative bounding radius
    // covering the cells plus blade height and wind sway.
    const float2 origin = float2 (int2 (grass.grid.xy));
    const float2 center_cell =
      origin + float2 (patch_x * GRASS_PATCH_X + 0.5 * GRASS_PATCH_X,
                       patch_z * GRASS_PATCH_Z + 0.5 * GRASS_PATCH_Z);
    const float2 world_xz = center_cell * grass.grid.z;
    const float patch_radius =
      0.5 * grass.grid.z * length (float2 (GRASS_PATCH_X, GRASS_PATCH_Z)) + 1.6;

    const float distance = length (world_xz - frame.camera_pos.xz);
    valid = distance < grass.terrain.w + patch_radius;

    if (valid) {
      const float2 terrain_limit (heights.get_width () - 1,
                                  heights.get_height () - 1);
      float2 grid = world_xz / grass.terrain.xz;
      if (grass.limits.z > 0.5)
        grid -= floor (grid / terrain_limit) * terrain_limit;
      grid = clamp (grid, 0.0, terrain_limit);
      const float height = heights.read (uint2 (round (grid))).r;
      const float slack = 0.02;
      valid =
        height > grass.limits.x - slack && height < grass.limits.y + slack;

      if (valid && distance > 12.0) {
        const float4 clip =
          frame.view_proj *
          float4 (world_xz.x, height * grass.terrain.y + 0.6, world_xz.y, 1.0);
        const float margin = 1.30 * clip.w + 4.0 * patch_radius;
        valid = clip.w > 0.0 && abs (clip.x) < margin && abs (clip.y) < margin;
      }
    }

    if (valid) {
      const uint slot =
        atomic_fetch_add_explicit (&survivors, 1u, metal::memory_order_relaxed);
      payload.patches[slot] = uint2 (patch_x, patch_z);
    }
  }

  threadgroup_barrier (metal::mem_flags::mem_threadgroup);
  if (thread_id == 0u) {
    const uint count =
      atomic_load_explicit (&survivors, metal::memory_order_relaxed);
    payload.count = count;
    mesh_grid.set_threadgroups_per_grid (uint3 (count, 1, 1));
  }
}

[[mesh]] void grass_mesh (
  GrassMesh out,
  object_data const GrassPayload& payload [[payload]],
  uint mesh_id [[threadgroup_position_in_grid]],
  uint thread_id [[thread_index_in_threadgroup]],
  constant MoppeFrameUniforms& frame [[buffer (MOPPE_BUF_FRAME)]],
  constant MoppeGrassUniforms& grass [[buffer (MOPPE_BUF_DRAW)]],
  texture2d<float, access::read> heights [[texture (MOPPE_TEX_HEIGHTS)]],
  texture2d<float, access::read> normals [[texture (MOPPE_TEX_NORMALS)]],
  texture2d<float, access::read> moisture_map
  [[texture (MOPPE_TEX_MOISTURE)]]) {
  if (thread_id == 0u)
    out.set_primitive_count (GRASS_PATCH_BLADES * 6);
  if (mesh_id >= payload.count || thread_id >= GRASS_PATCH_BLADES)
    return;

  const uint2 patch = payload.patches[mesh_id];
  const uint cell = thread_id / 4u;
  const uint blade = thread_id % 4u;
  const int2 origin = int2 (grass.grid.xy);
  const uint world_cell_x =
    uint (origin.x + int (patch.x * GRASS_PATCH_X + cell % GRASS_PATCH_X));
  const uint world_cell_z =
    uint (origin.y + int (patch.y * GRASS_PATCH_Z + cell / GRASS_PATCH_X));

  const GrassBlade b = grass_blade (world_cell_x,
                                    world_cell_z,
                                    blade,
                                    frame,
                                    grass,
                                    heights,
                                    normals,
                                    moisture_map);

  const uint v_base = thread_id * 8u;
  const uint p_base = thread_id * 6u;
  if (!b.valid) {
    // Zero-area triangles: all six primitives collapse to one vertex.
    GrassVaryings dead;
    dead.position = float4 (0.0, 0.0, -1.0, 0.0);
    dead.world_pos = float3 (0);
    dead.normal = float3 (0, 1, 0);
    dead.color = float3 (0);
    dead.height01 = 0.0;
    dead.ground_shade = 1.0;
    dead.fog = 1.0;
    for (uint v = 0; v < 8u; ++v)
      out.set_vertex (v_base + v, dead);
    for (uint p = 0; p < 6u; ++p) {
      out.set_index (3u * (p_base + p) + 0u, v_base);
      out.set_index (3u * (p_base + p) + 1u, v_base);
      out.set_index (3u * (p_base + p) + 2u, v_base);
    }
    return;
  }

  // Four rungs of two vertices, three quads of two triangles.
  for (uint rung = 0; rung < 4u; ++rung) {
    const float t = float (rung) / 3.0;
    out.set_vertex (v_base + rung * 2u + 0u,
                    grass_blade_vertex (b, t, -1.0, frame, grass));
    out.set_vertex (v_base + rung * 2u + 1u,
                    grass_blade_vertex (b, t, 1.0, frame, grass));
  }
  for (uint segment = 0; segment < 3u; ++segment) {
    const uint v0 = v_base + segment * 2u;
    const uint p0 = 3u * (p_base + segment * 2u);
    out.set_index (p0 + 0u, v0 + 0u);
    out.set_index (p0 + 1u, v0 + 2u);
    out.set_index (p0 + 2u, v0 + 1u);
    out.set_index (p0 + 3u, v0 + 1u);
    out.set_index (p0 + 4u, v0 + 2u);
    out.set_index (p0 + 5u, v0 + 3u);
  }
}

fragment float4 grass_fragment (GrassVaryings in [[stage_in]],
                                constant MoppeFrameUniforms& frame
                                [[buffer (MOPPE_BUF_FRAME)]],
                                depth2d<float> shadow_map
                                [[texture (MOPPE_TEX_SHADOW)]],
                                bool front_facing [[front_facing]]) {
  float3 n = normalize (in.normal);
  if (!front_facing)
    n = -n;
  n = normalize (mix (n, float3 (0, 1, 0), 0.18 * in.height01));
  const float3 l = normalize (frame.sun_dir.xyz);
  const float3 v = normalize (frame.camera_pos.xyz - in.world_pos);
  const float3 h = normalize (l + v);
  const float lambert = saturate ((dot (n, l) + 0.10) / 1.10);

  // Single-tap shadow: grass covers half the frame, and per-blade
  // color/AO variation hides the missing penumbra taps completely.
  float visibility = 1.0;
  if (frame.shadow.x >= 0.01) {
    const float4 shadow_coord = frame.light_matrix * float4 (in.world_pos, 1.0);
    const float3 proj = shadow_coord.xyz / shadow_coord.w;
    if (all (proj >= 0.0) && all (proj <= 1.0)) {
      constexpr sampler smp (coord::normalized,
                             address::clamp_to_edge,
                             filter::linear,
                             compare_func::less_equal);
      const float lit =
        shadow_map.sample_compare (smp, proj.xy, proj.z - 0.0018);
      const float fade = saturate (2.5 * (1.0 - in.fog));
      visibility = mix (1.0, lit, frame.shadow.x * fade);
    }
  }

  const float root_occlusion =
    mix (0.46, 1.0, smoothstep (0.0, 0.85, in.height01));
  const float sun = visibility * in.ground_shade;
  float3 light =
    moppe_hemisphere_light (frame.ambient.rgb, n) * root_occlusion +
    frame.sun_diffuse.rgb * lambert * sun;
  light +=
    frame.sun_specular.rgb * sun * 0.07 * pow (max (dot (n, h), 0.0), 30.0);
  const float backlight =
    pow (max (dot (-n, l), 0.0), 1.5) * (0.20 + 0.80 * in.height01);
  light += frame.sun_diffuse.rgb * sun * backlight * 0.32;
  float3 color = in.color * light;

  const float3 to_frag = in.world_pos - frame.camera_pos.xyz;
  const float3 fog_color =
    moppe_warmed_fog (frame.fog_color.rgb, normalize (to_frag), l);
  color = mix (color, fog_color, smoothstep (0.0, 0.9, in.fog));
  return float4 (color, 1.0);
}
