// Stand-scale quotient of the actual retained forest population. The RGBA
// moment field is rasterized once from ForestInstances: closure, mean crown
// height, upper crown height, and mean moisture. Geometry is only the finite
// outer roof and exposed stand boundary; interior work-cell walls vanish.

#include "forest_medium.h"

struct ForestCanopyVaryings {
  float4 position [[position]];
  float3 world_pos;
  float3 normal;
  float boundary;
  float side;
  float2 motion [[center_no_perspective]];
};

struct ForestCanopyPayload {
  uint count;
  uint2 patches[MOPPE_FOREST_CANOPY_OBJECT_THREADS];
};

using ForestCanopyMesh = metal::mesh<ForestCanopyVaryings,
                                     void,
                                     MOPPE_FOREST_CANOPY_MESH_VERTICES,
                                     MOPPE_FOREST_CANOPY_MESH_PRIMITIVES,
                                     metal::topology::triangle>;

static inline float4
forest_canopy_moments (float2 world_xz,
                       constant MoppeForestCanopyUniforms& u,
                       texture2d<float> canopy) {
  constexpr sampler smp (coord::normalized, address::repeat, filter::linear);
  return canopy.sample (smp, world_xz * u.field.xy);
}

static inline float
forest_canopy_ground (float2 world_xz,
                      constant MoppeForestCanopyUniforms& u,
                      texture2d<float, access::read> heights) {
  const float period = u.terrain.w;
  float gx = world_xz.x * u.terrain.x;
  float gz = world_xz.y * u.terrain.y;
  gx -= floor (gx / period) * period;
  gz -= floor (gz / period) * period;
  const uint2 i0 = uint2 ((uint)gx, (uint)gz);
  const uint2 i1 = (i0 + uint2 (1)) % uint (period);
  const float2 f = float2 (gx, gz) - float2 (i0);
  const float h00 = heights.read (i0).r;
  const float h10 = heights.read (uint2 (i1.x, i0.y)).r;
  const float h01 = heights.read (uint2 (i0.x, i1.y)).r;
  const float h11 = heights.read (i1).r;
  return mix (mix (h00, h10, f.x), mix (h01, h11, f.x), f.y) * u.terrain.z;
}

static inline float forest_canopy_height (float4 moments) {
  const float mean_height = moments.g * MOPPE_FOREST_CANOPY_HEIGHT_RANGE_METRES;
  const float upper_height =
    moments.b * MOPPE_FOREST_CANOPY_HEIGHT_RANGE_METRES;
  return mix (mean_height, upper_height, 0.58);
}

[[object]] void forest_canopy_object (
  object_data ForestCanopyPayload& payload [[payload]],
  metal::mesh_grid_properties mesh_grid,
  uint thread_id [[thread_index_in_threadgroup]],
  uint3 grid_pos [[thread_position_in_grid]],
  constant MoppeForestCanopyUniforms& u [[buffer (MOPPE_BUF_FRAME)]],
  texture2d<float, access::read> heights [[texture (MOPPE_TEX_HEIGHTS)]],
  texture2d<float> canopy [[texture (MOPPE_TEX_FOREST_CANOPY)]]) {
  threadgroup atomic_uint survivors;
  if (thread_id == 0u)
    atomic_store_explicit (&survivors, 0u, metal::memory_order_relaxed);
  threadgroup_barrier (metal::mem_flags::mem_threadgroup);

  const uint patches_side = uint (u.tiles.z);
  const uint index = grid_pos.x;
  bool visible = index < patches_side * patches_side;
  const uint patch_x = index % max (patches_side, 1u);
  const uint patch_z = index / max (patches_side, 1u);
  if (visible) {
    const int2 patch = int2 (u.tiles.xy) + int2 (patch_x, patch_z);
    const float2 centre = (float2 (patch) + 0.5) * u.tiles.w;
    const float4 centre_moments = forest_canopy_moments (centre, u, canopy);
    float closure = centre_moments.r;
    const float2 taps[4] = {
      float2 (-0.45, -0.45),
      float2 (0.45, -0.45),
      float2 (-0.45, 0.45),
      float2 (0.45, 0.45),
    };
    for (uint tap = 0u; tap < 4u; ++tap)
      closure = max (
        closure,
        forest_canopy_moments (centre + taps[tap] * u.tiles.w, u, canopy).r);

    const float crown_height = forest_canopy_height (centre_moments);
    const float ground = forest_canopy_ground (centre, u, heights);
    const float3 centre_world =
      float3 (centre.x, ground + 0.5 * crown_height, centre.y);
    const float distance = length (centre_world - u.camera_pos.xyz);
    const float horizontal_distance = length (centre - u.camera_pos.xz);
    const float focal =
      moppe_vertical_focal_pixels (u.unjittered_view_proj, u.temporal.y);
    const float aggregate = moppe_forest_aggregate_fraction (focal, distance);
    const float edge =
      1.0 - smoothstep (0.91 * u.lod.x, 0.995 * u.lod.x, horizontal_distance);
    visible = closure * aggregate * edge > 0.006;
    if (visible) {
      // The work window is radial so walking, riding, and flying share one
      // bounded population. Projection culling prevents its off-screen half
      // from becoming mesh work, using the stand's actual vertical extent.
      const float horizontal_radius = 0.72 * u.tiles.w;
      const float radius =
        length (float2 (horizontal_radius, 0.5 * crown_height));
      const float4 clip = u.unjittered_view_proj * float4 (centre_world, 1.0);
      const float2 clip_radius =
        radius * moppe_projection_scale (u.unjittered_view_proj);
      visible = clip.w > -radius && abs (clip.x) < clip.w + clip_radius.x &&
                abs (clip.y) < clip.w + clip_radius.y;
    }
    if (visible) {
      const uint slot =
        atomic_fetch_add_explicit (&survivors, 1u, metal::memory_order_relaxed);
      payload.patches[slot] = uint2 (patch_x, patch_z);
    }
  }

  threadgroup_barrier (metal::mem_flags::mem_threadgroup);
  if (thread_id == 0u) {
    payload.count =
      atomic_load_explicit (&survivors, metal::memory_order_relaxed);
    mesh_grid.set_threadgroups_per_grid (uint3 (payload.count, 1, 1));
  }
}

static inline float2 forest_canopy_side_point (uint side, uint endpoint) {
  if (side == 0u)
    return float2 (0.0, float (endpoint));
  if (side == 1u)
    return float2 (1.0, float (endpoint));
  if (side == 2u)
    return float2 (float (endpoint), 0.0);
  return float2 (float (endpoint), 1.0);
}

static inline float2 forest_canopy_side_outward (uint side) {
  return float2 (side == 0u   ? -1.0
                 : side == 1u ? 1.0
                              : 0.0,
                 side == 2u   ? -1.0
                 : side == 3u ? 1.0
                              : 0.0);
}

static inline ForestCanopyVaryings
forest_canopy_vertex (uint vertex_index,
                      uint2 patch,
                      constant MoppeForestCanopyUniforms& u,
                      texture2d<float, access::read> heights,
                      texture2d<float> canopy) {
  const uint cell_index = vertex_index / MOPPE_FOREST_CANOPY_VERTICES_PER_CELL;
  const uint local = vertex_index % MOPPE_FOREST_CANOPY_VERTICES_PER_CELL;
  const uint cell_x = cell_index % MOPPE_FOREST_CANOPY_CELLS;
  const uint cell_z = cell_index / MOPPE_FOREST_CANOPY_CELLS;
  const float cell_side = u.lod.y;
  const int2 world_patch = int2 (u.tiles.xy) + int2 (patch);
  const int2 world_cell =
    world_patch * MOPPE_FOREST_CANOPY_CELLS + int2 (cell_x, cell_z);
  const float2 cell_origin = float2 (world_cell) * cell_side;
  const float2 centre = cell_origin + 0.5 * cell_side;
  const float4 centre_moments = forest_canopy_moments (centre, u, canopy);

  float2 within;
  bool bottom = false;
  float boundary = 1.0;
  float side_face = 0.0;
  float3 normal = float3 (0.0, 1.0, 0.0);
  if (local < 5u) {
    if (local == 4u) {
      const float2 jitter = float2 (moppe_hash12 (float2 (world_cell) + 7.3),
                                    moppe_hash12 (float2 (world_cell) + 29.1)) -
                            0.5;
      within = 0.5 + 0.28 * jitter;
    } else {
      within = float2 (float (local & 1u), float ((local >> 1u) & 1u));
    }
  } else {
    const uint side = (local - 5u) / 4u;
    const uint side_local = (local - 5u) % 4u;
    within = forest_canopy_side_point (side, side_local & 1u);
    bottom = side_local >= 2u;
    const float2 outward = forest_canopy_side_outward (side);
    const float4 neighbour =
      forest_canopy_moments (centre + outward * cell_side, u, canopy);
    const float height = forest_canopy_height (centre_moments);
    const float neighbour_height = forest_canopy_height (neighbour);
    const float exposure =
      max (centre_moments.r - neighbour.r,
           0.35 * saturate ((height - neighbour_height) / max (height, 1.0)));
    boundary = smoothstep (0.025, 0.30, exposure);
    side_face = 1.0;
    normal = normalize (float3 (outward.x, 0.16, outward.y));
  }

  const float2 world_xz = cell_origin + within * cell_side;
  const float ground = forest_canopy_ground (world_xz, u, heights);
  const float4 moments = forest_canopy_moments (world_xz, u, canopy);
  float crown_height = forest_canopy_height (moments);
  if (local == 4u) {
    const float tip = moppe_hash12 (float2 (world_cell) + float2 (41.7, 3.9));
    crown_height += mix (0.7, 2.3, tip) * centre_moments.r;
  }
  const float base_height = max (1.8, 0.28 * centre_moments.g * u.field.z);
  float3 rest = float3 (
    world_xz.x, ground + (bottom ? base_height : crown_height), world_xz.y);
  const float3 current = moppe_wind (rest, 0.34, 0.025, u.params.x);
  const float3 previous = moppe_wind (rest, 0.34, 0.025, u.temporal.z);

  ForestCanopyVaryings result;
  result.position = u.view_proj * float4 (current, 1.0);
  result.world_pos = current;
  result.normal = normal;
  result.boundary = boundary;
  result.side = side_face;
  result.motion =
    moppe_motion_vector (u.unjittered_view_proj * float4 (current, 1.0),
                         u.previous_view_proj * float4 (previous, 1.0),
                         u.temporal.xy);
  return result;
}

[[mesh]] void forest_canopy_mesh (
  ForestCanopyMesh out,
  object_data const ForestCanopyPayload& payload [[payload]],
  uint mesh_id [[threadgroup_position_in_grid]],
  uint thread_id [[thread_index_in_threadgroup]],
  constant MoppeForestCanopyUniforms& u [[buffer (MOPPE_BUF_FRAME)]],
  texture2d<float, access::read> heights [[texture (MOPPE_TEX_HEIGHTS)]],
  texture2d<float> canopy [[texture (MOPPE_TEX_FOREST_CANOPY)]]) {
  if (thread_id == 0u)
    out.set_primitive_count (MOPPE_FOREST_CANOPY_MESH_PRIMITIVES);
  const uint2 patch = payload.patches[min (mesh_id, payload.count - 1u)];
  if (thread_id < MOPPE_FOREST_CANOPY_MESH_VERTICES)
    out.set_vertex (
      thread_id, forest_canopy_vertex (thread_id, patch, u, heights, canopy));

  if (thread_id < MOPPE_FOREST_CANOPY_MESH_PRIMITIVES) {
    const uint cell = thread_id / MOPPE_FOREST_CANOPY_PRIMITIVES_PER_CELL;
    const uint local = thread_id % MOPPE_FOREST_CANOPY_PRIMITIVES_PER_CELL;
    const uint base = cell * MOPPE_FOREST_CANOPY_VERTICES_PER_CELL;
    uint3 indices;
    if (local < 4u) {
      const uint3 roof[4] = {
        uint3 (0u, 1u, 4u),
        uint3 (1u, 3u, 4u),
        uint3 (3u, 2u, 4u),
        uint3 (2u, 0u, 4u),
      };
      indices = base + roof[local];
    } else {
      const uint side = (local - 4u) / 2u;
      const uint triangle = (local - 4u) & 1u;
      const uint face = base + 5u + side * 4u;
      indices = triangle == 0u ? uint3 (face, face + 1u, face + 3u)
                               : uint3 (face, face + 3u, face + 2u);
    }
    const uint slot = thread_id * 3u;
    out.set_index (slot + 0u, indices.x);
    out.set_index (slot + 1u, indices.y);
    out.set_index (slot + 2u, indices.z);
  }
}

fragment MoppeTemporalOutput forest_canopy_fragment (
  ForestCanopyVaryings in [[stage_in]],
  bool front_facing [[front_facing]],
  constant MoppeForestCanopyUniforms& u [[buffer (MOPPE_BUF_FRAME)]],
  texture2d<float> canopy [[texture (MOPPE_TEX_FOREST_CANOPY)]]) {
  const float3 to_eye = u.camera_pos.xyz - in.world_pos;
  const float distance = length (to_eye);
  const float3 eye = to_eye / max (distance, 0.001);
  const float focal =
    moppe_vertical_focal_pixels (u.unjittered_view_proj, u.temporal.y);
  const float aggregate = moppe_forest_aggregate_fraction (focal, distance);
  const float edge =
    1.0 - smoothstep (0.91 * u.lod.x,
                      0.995 * u.lod.x,
                      length (in.world_pos.xz - u.camera_pos.xz));
  const float4 moments = forest_canopy_moments (in.world_pos.xz, u, canopy);
  const float closure = moments.r;
  const float support = in.side > 0.5 ? in.boundary : 1.0;
  const float coverage = 1.0 - exp (-2.15 * closure);
  const float alpha = aggregate * edge * support * coverage;
  if (alpha < 0.012 || closure < 0.035)
    discard_fragment ();

  float3 normal = normalize (front_facing ? in.normal : -in.normal);
  if (in.side < 0.5) {
    const float3 dx = dfdx (in.world_pos);
    const float3 dy = dfdy (in.world_pos);
    const float3 roof = normalize (cross (dy, dx));
    normal = roof.y < 0.0 ? -roof : roof;
  }
  const float3 light = normalize (u.sun_dir.xyz);
  const float moisture = moments.a;
  const float3 leaf =
    moppe_srgb (moppe_forest_conifer_tint (moisture, closure));
  const float crown_grain =
    0.72 * moppe_value_noise (in.world_pos.xz / 7.0 + float2 (13.1, 4.7)) +
    0.28 * moppe_value_noise (in.world_pos.xz / 23.0 + float2 (2.9, 31.3));
  const float3 leaf_normal =
    normalize (mix (normal, float3 (0.0, 1.0, 0.0), 0.25));
  const float visibility =
    moppe_cloud_transmission (in.world_pos, light, u.params.x, u.params.y);
  const float wrap = saturate ((dot (leaf_normal, light) + 0.30) / 1.30);
  float3 color = leaf * mix (0.78, 1.18, crown_grain) *
                 (moppe_hemisphere_light (u.ambient.rgb, leaf_normal) * 0.68 +
                  u.sun_diffuse.rgb * wrap * visibility * 0.68);

  const float back =
    pow (max (dot (-leaf_normal, light), 0.0), 1.4) * visibility;
  const float toward = saturate (dot (eye, -light));
  color += sqrt (leaf) * u.sun_diffuse.rgb * float3 (0.92, 1.0, 0.28) * back *
           (0.08 + 0.26 * toward * toward);

  const float fog =
    moppe_relief_haze (moppe_distance_fog (distance, u.fog_color.w),
                       in.world_pos.y,
                       u.params.z,
                       u.params.w);
  color = mix (color,
               moppe_warmed_fog (u.fog_color.rgb, -eye, light),
               smoothstep (0.0, 0.92, fog));
  return moppe_temporal_output (
    float4 (color, alpha), in.motion, 0.24 + 0.28 * alpha);
}
