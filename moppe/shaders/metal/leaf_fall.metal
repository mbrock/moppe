// Falling leaves: in a grove that has turned, leaves drift down from the
// crowns, swaying and spinning on the way. A lattice of cells anchored to
// the world follows the camera; each cell owns one leaf whose whole fall is
// a function of its identity and the time, so nothing is simulated or
// stored. The litter field -- the turned leaf area the birches actually
// hold -- decides which cells have a leaf, so leaves fall only where gold
// crowns stand overhead.

#include "common.h"

struct LeafVaryings {
  float4 position [[position]];
  float3 world_pos;
  float3 normal;
  float3 albedo [[flat]];
  float2 motion [[center_no_perspective]];
};

using LeafMesh = metal::mesh<LeafVaryings,
                             void,
                             4 * MOPPE_LEAF_FALL_THREADS,
                             2 * MOPPE_LEAF_FALL_THREADS,
                             metal::topology::triangle>;

static inline float leaf_hash (int2 cell, uint lane) {
  uint value = uint (cell.x) * 0x9e3779b9u ^ uint (cell.y) * 0x85ebca6bu ^
               lane * 0xc2b2ae35u;
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  value ^= value >> 16;
  return float (value & 0x00ffffffu) / float (0x01000000u);
}

static inline float leaf_ground (float2 world_xz,
                                 constant MoppeLeafFallUniforms& u,
                                 texture2d<float, access::read> heights) {
  const float period = u.lattice.w;
  float gx = world_xz.x * u.lattice.x;
  float gz = world_xz.y * u.lattice.y;
  gx -= floor (gx / period) * period;
  gz -= floor (gz / period) * period;
  const uint2 i0 = uint2 ((uint)gx, (uint)gz);
  const uint2 i1 = (i0 + uint2 (1)) % uint (period);
  const float fx = gx - (float)i0.x;
  const float fz = gz - (float)i0.y;
  const float h00 = heights.read (i0).r;
  const float h10 = heights.read (uint2 (i1.x, i0.y)).r;
  const float h01 = heights.read (uint2 (i0.x, i1.y)).r;
  const float h11 = heights.read (i1).r;
  return mix (mix (h00, h10, fx), mix (h01, h11, fx), fz) * u.lattice.z;
}

struct Leaf {
  float3 centre;
  float3 across; // half-extent vectors of the leaf's quad
  float3 along;
};

// Where a cell's leaf is at a given time. It falls from the crowns at about
// a metre a second, drifting downwind and swinging side to side, and turns
// over as it goes. Near the ground it shrinks away rather than lying there:
// the litter on the terrain is already the leaves that have landed.
static inline Leaf leaf_at (int2 cell,
                            float time,
                            float cell_metres,
                            constant MoppeLeafFallUniforms& u,
                            texture2d<float, access::read> heights) {
  const float drop = 6.0 + 9.0 * leaf_hash (cell, 1u);
  const float speed = 0.8 + 0.5 * leaf_hash (cell, 2u);
  const float fall = fract (time * speed / drop + leaf_hash (cell, 3u));
  const float swing_phase = 6.2831853 * leaf_hash (cell, 4u);
  const float2 wind = float2 (0.79, 0.53);
  const float2 home =
    (float2 (cell) + float2 (leaf_hash (cell, 5u), leaf_hash (cell, 6u))) *
    cell_metres;
  const float2 swing = 0.55 * float2 (sin (time * 1.3 + swing_phase),
                                      cos (time * 1.1 + 1.7 * swing_phase));
  // Start upwind, so the leaf lands near its own cell.
  const float2 xz = home + wind * (2.2 * (fall - 0.5)) + swing;
  const float ground = leaf_ground (xz, u, heights);

  Leaf leaf;
  leaf.centre = float3 (xz.x, ground + drop * (1.0 - fall), xz.y);
  const float spin = time * (2.0 + 3.0 * leaf_hash (cell, 7u)) + swing_phase;
  const float tumble = time * (1.1 + 1.5 * leaf_hash (cell, 8u));
  const float3 axis =
    normalize (float3 (cos (tumble), 0.6 + 0.4 * sin (tumble), sin (tumble)));
  const float3 side =
    normalize (cross (axis, float3 (cos (spin), 0.0, sin (spin))));
  const float landing = smoothstep (0.0, 0.35, drop * (1.0 - fall));
  const float size = 0.09 * landing;
  leaf.across = side * size;
  leaf.along = cross (axis, side) * (0.7 * size);
  return leaf;
}

[[mesh]] void leaf_fall_mesh (LeafMesh out,
                              uint2 block [[threadgroup_position_in_grid]],
                              uint thread_id [[thread_index_in_threadgroup]],
                              constant MoppeLeafFallUniforms& u
                              [[buffer (MOPPE_BUF_FRAME)]],
                              texture2d<float, access::read> heights
                              [[texture (MOPPE_TEX_HEIGHTS)]],
                              texture2d<float> litter
                              [[texture (MOPPE_TEX_FOREST_LITTER)]]) {
  if (thread_id == 0u)
    out.set_primitive_count (2u * MOPPE_LEAF_FALL_THREADS);

  const uint side = MOPPE_LEAF_FALL_BLOCK_CELLS;
  const int2 cell = int2 (u.grid.xy) + int2 (block * side) +
                    int2 (thread_id % side, thread_id / side);
  const float cell_metres = u.grid.z;
  const float2 centre = (float2 (cell) + 0.5) * cell_metres;

  constexpr sampler field (coord::normalized, address::repeat, filter::linear);
  const float density = litter.sample (field, centre * u.field.xy).r;
  const float distance = length (centre - u.camera_pos.xz);
  const float reach = 1.0 - smoothstep (0.70, 1.0, distance / u.grid.w);
  const bool present = leaf_hash (cell, 0u) < 0.85 * density * reach;

  const Leaf now = leaf_at (cell, u.params.x, cell_metres, u, heights);
  const Leaf before = leaf_at (cell, u.temporal.z, cell_metres, u, heights);
  const float scale = present ? 1.0 : 0.0;
  const float amber = leaf_hash (cell, 9u);
  const float3 albedo = moppe_srgb (
    mix (float3 (0.95, 0.74, 0.22), float3 (0.90, 0.46, 0.13), amber * amber));
  const float3 normal = normalize (cross (now.across, now.along));
  for (uint corner = 0u; corner < 4u; ++corner) {
    const float2 at =
      float2 (corner & 1u ? 1.0 : -1.0, corner & 2u ? 1.0 : -1.0);
    // The tip narrows: a leaf, not a card.
    const float taper = at.y > 0.0 ? 0.45 : 1.0;
    const float3 current =
      now.centre + scale * (at.x * taper * now.across + at.y * now.along);
    const float3 previous =
      before.centre +
      scale * (at.x * taper * before.across + at.y * before.along);
    LeafVaryings v;
    v.position = u.view_proj * float4 (current, 1.0);
    v.world_pos = current;
    v.normal = normal;
    v.albedo = albedo;
    v.motion =
      moppe_motion_vector (u.unjittered_view_proj * float4 (current, 1.0),
                           u.previous_view_proj * float4 (previous, 1.0),
                           u.temporal.xy);
    out.set_vertex (4u * thread_id + corner, v);
  }
  const uint first = 4u * thread_id;
  out.set_index (6u * thread_id + 0u, first);
  out.set_index (6u * thread_id + 1u, first + 1u);
  out.set_index (6u * thread_id + 2u, first + 2u);
  out.set_index (6u * thread_id + 3u, first + 1u);
  out.set_index (6u * thread_id + 4u, first + 3u);
  out.set_index (6u * thread_id + 5u, first + 2u);
}

fragment MoppeTemporalOutput leaf_fall_fragment (
  LeafVaryings in [[stage_in]],
  bool front [[front_facing]],
  constant MoppeLeafFallUniforms& u [[buffer (MOPPE_BUF_FRAME)]],
  depth2d<float> shadow_map [[texture (MOPPE_TEX_SHADOW)]]) {
  const float3 to_eye = u.camera_pos.xyz - in.world_pos;
  const float distance = length (to_eye);
  const float3 view = to_eye / max (distance, 1e-3);
  const float3 light = u.sun_dir.xyz;
  const float3 n = front ? in.normal : -in.normal;

  float visibility =
    moppe_cloud_transmission (in.world_pos, light, u.params.x, u.params.y);
  if (u.shadow.x > 0.01) {
    const float4 coord = u.light_matrix * float4 (in.world_pos, 1.0);
    const float3 p = coord.xyz / coord.w;
    if (all (p >= 0.0) && all (p <= 1.0)) {
      constexpr sampler shadow_smp (coord::normalized,
                                    address::clamp_to_edge,
                                    filter::linear,
                                    compare_func::less_equal);
      const float lit =
        shadow_map.sample_compare (shadow_smp, p.xy, p.z - 2.0 / 1240.0);
      visibility *= mix (1.0, mix (0.25, 1.0, lit), u.shadow.x);
    }
  }
  // A thin leaf is lit from either side and glows against the sun.
  const float sun = abs (dot (n, light));
  float3 color = in.albedo * (sun * visibility * 0.95 * u.sun_diffuse.rgb +
                              0.9 * moppe_hemisphere_light (u.ambient.rgb, n)) +
                 in.albedo * u.sun_diffuse.rgb * visibility * 0.5 *
                   pow (saturate (dot (-view, light)), 3.0);

  const float fog =
    moppe_relief_haze (moppe_distance_fog (distance, u.fog_color.w),
                       in.world_pos.y,
                       u.params.z,
                       u.params.w);
  color = mix (color,
               moppe_warmed_fog (u.fog_color.rgb, -view, light),
               smoothstep (0.0, 0.92, fog));
  return moppe_temporal_output (float4 (color, 1.0), in.motion, 0.6);
}
