// Stand-scale quotient of the actual retained forest population. The RGBA
// moment field is rasterized once from ForestInstances: closure, mean crown
// height, upper crown height, and mean moisture. A second field divides the
// same conserved optical depth among four crown-height strata. Geometry is a
// sparse lattice of soft ellipsoid impostors rather than a connected top
// sheet, so flying beside a stand sees crown mass instead of a horizontal
// shelf.

#include "forest_medium.h"

struct ForestCanopyVaryings {
  float4 position [[position]];
  float3 world_pos;
  float3 normal;
  float stand_closure;
  float layer [[flat]];
  float2 volume_uv;
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
  constexpr sampler smp (
    coord::normalized, address::repeat, filter::linear, mip_filter::linear);
  return canopy.sample (smp, world_xz * u.field.xy);
}

static inline float4
forest_canopy_cell_moments (float2 world_xz,
                            float footprint,
                            constant MoppeForestCanopyUniforms& u,
                            texture2d<float> canopy) {
  constexpr sampler smp (
    coord::normalized, address::repeat, filter::linear, mip_filter::linear);
  const float2 texel =
    1.0 / (u.field.xy * float2 (canopy.get_width (), canopy.get_height ()));
  const float lod = clamp (log2 (max (footprint / max (texel.x, texel.y), 1.0)),
                           0.0,
                           float (canopy.get_num_mip_levels () - 1u));
  return canopy.sample (smp, world_xz * u.field.xy, level (lod));
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

static inline float forest_canopy_variance_grain (float3 world_pos,
                                                  float footprint_metres) {
  // Crown-scale variance retires as its carrier becomes sub-pixel while a
  // stand-scale octave survives. Both phases are fixed in world space and
  // selected by projected footprint rather than camera radius.
  const float crown_visible = 1.0 - smoothstep (0.75, 4.0, footprint_metres);
  const float crown =
    moppe_value_noise (world_pos.xz * 0.17 + float2 (13.1, 4.7));
  const float stand =
    moppe_value_noise (world_pos.xz * 0.047 + float2 (2.9, 31.3));
  return (1.0 + 0.34 * (crown - 0.5) * crown_visible) *
         (1.0 + 0.22 * (stand - 0.5));
}

static inline float
forest_canopy_roof_height (float2 world_xz,
                           float footprint,
                           constant MoppeForestCanopyUniforms& u,
                           texture2d<float> canopy) {
  const float4 moments =
    forest_canopy_cell_moments (world_xz, footprint, u, canopy);
  const float base_height = max (1.8, 0.28 * moments.g * u.field.z);
  const float fine =
    moppe_value_noise (world_xz / 11.0 + float2 (31.7, 8.3)) - 0.5;
  const float broad =
    moppe_value_noise (world_xz / 29.0 + float2 (4.1, 47.9)) - 0.5;
  const float variation = moments.r * (4.2 * fine + 2.4 * broad);
  return max (base_height + 0.8, forest_canopy_height (moments) + variation);
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
    const float4 centre_moments =
      forest_canopy_cell_moments (centre, u.lod.y, u, canopy);
    float closure = centre_moments.r;
    const float2 taps[4] = {
      float2 (-0.45, -0.45),
      float2 (0.45, -0.45),
      float2 (-0.45, 0.45),
      float2 (0.45, 0.45),
    };
    for (uint tap = 0u; tap < 4u; ++tap)
      closure = max (closure,
                     forest_canopy_cell_moments (
                       centre + taps[tap] * u.tiles.w, u.lod.y, u, canopy)
                       .r);

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
    visible =
      closure * moppe_forest_stand_support (closure) * aggregate * edge > 0.006;
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
      // Mesh strata use ordinary alpha blending and must arrive back to
      // front. Pack which side of the stand the camera occupies into a spare
      // coordinate bit; patch coordinates never approach that range.
      const bool camera_above =
        u.camera_pos.y >= centre_world.y + 0.5 * crown_height;
      payload.patches[slot] =
        uint2 (patch_x | (camera_above ? 0x80000000u : 0u), patch_z);
    }
  }

  threadgroup_barrier (metal::mem_flags::mem_threadgroup);
  if (thread_id == 0u) {
    payload.count =
      atomic_load_explicit (&survivors, metal::memory_order_relaxed);
    mesh_grid.set_threadgroups_per_grid (
      uint3 (payload.count * MOPPE_FOREST_CANOPY_DENSITY_SLICES, 1, 1));
  }
}

static inline ForestCanopyVaryings
forest_canopy_vertex (uint vertex_index,
                      uint2 patch,
                      uint layer,
                      constant MoppeForestCanopyUniforms& u,
                      texture2d<float, access::read> heights,
                      texture2d<float> canopy) {
  const int2 world_patch = int2 (u.tiles.xy) + int2 (patch);
  const float2 patch_origin = float2 (world_patch) * u.tiles.w;
  const uint cell = vertex_index / 4u;
  const uint local = vertex_index % 4u;
  const uint cell_x = cell % MOPPE_FOREST_CANOPY_GRID_CELLS;
  const uint cell_z = cell / MOPPE_FOREST_CANOPY_GRID_CELLS;
  const float2 sample_xz =
    patch_origin + (float2 (cell_x, cell_z) + 0.5) * u.lod.y;

  const float4 moments =
    forest_canopy_cell_moments (sample_xz, u.lod.y, u, canopy);
  const float ground = forest_canopy_ground (sample_xz, u, heights);
  const float base_height = max (1.8, 0.28 * moments.g * u.field.z);
  const float crown_height =
    forest_canopy_roof_height (sample_xz, 3.0 * u.lod.y, u, canopy);
  const float layer_lower =
    float (layer) / float (MOPPE_FOREST_CANOPY_DENSITY_SLICES);
  const float layer_upper =
    float (layer + 1u) / float (MOPPE_FOREST_CANOPY_DENSITY_SLICES);
  const float band_lower = mix (base_height, crown_height, layer_lower);
  const float band_upper = mix (base_height, crown_height, layer_upper);
  const float band_centre = 0.5 * (band_lower + band_upper);
  const float vertical_radius = 0.66 * max (band_upper - band_lower, 0.6);
  const float horizontal_radius = 0.72 * u.lod.y;

  const int2 world_cell =
    world_patch * MOPPE_FOREST_CANOPY_GRID_CELLS + int2 (cell_x, cell_z);
  const uint seed =
    moppe_forest_mix (uint (world_cell.x) * 73856093u ^
                      uint (world_cell.y) * 19349663u ^ layer * 83492791u);
  const float2 jitter = u.lod.y * 0.22 *
                        float2 (moppe_forest_hash (seed, 29u) - 0.5,
                                moppe_forest_hash (seed, 31u) - 0.5);
  const float2 world_xz = sample_xz + jitter;
  const float2 volume_uv =
    float2 (local == 0u || local == 2u ? -1.0 : 1.0, local < 2u ? -1.0 : 1.0);
  const float3 centre = float3 (world_xz.x, ground + band_centre, world_xz.y);
  const float3 to_eye = normalize (u.camera_pos.xyz - centre);
  const float3 previous_to_eye = normalize (u.previous_camera_pos.xyz - centre);
  const float3 camera_right = normalize (cross (
    abs (to_eye.y) > 0.96 ? float3 (0.0, 0.0, 1.0) : float3 (0.0, 1.0, 0.0),
    to_eye));
  const float3 camera_up = normalize (cross (to_eye, camera_right));
  const float3 previous_right =
    normalize (cross (abs (previous_to_eye.y) > 0.96 ? float3 (0.0, 0.0, 1.0)
                                                     : float3 (0.0, 1.0, 0.0),
                      previous_to_eye));
  const float3 previous_up =
    normalize (cross (previous_to_eye, previous_right));
  const float up_radius =
    mix (vertical_radius, horizontal_radius, abs (to_eye.y));
  const float previous_up_radius =
    mix (vertical_radius, horizontal_radius, abs (previous_to_eye.y));
  float3 rest = centre + camera_right * volume_uv.x * horizontal_radius +
                camera_up * volume_uv.y * up_radius;
  const float3 previous_rest =
    centre + previous_right * volume_uv.x * horizontal_radius +
    previous_up * volume_uv.y * previous_up_radius;
  const float3 current = moppe_wind (rest, 0.34, 0.025, u.params.x);
  const float3 previous = moppe_wind (previous_rest, 0.34, 0.025, u.temporal.z);

  ForestCanopyVaryings result;
  result.position = u.view_proj * float4 (current, 1.0);
  result.world_pos = current;
  result.normal =
    normalize (float3 (0.35 * volume_uv.x, 1.0, 0.35 * volume_uv.y));
  result.stand_closure =
    forest_canopy_cell_moments (sample_xz, u.tiles.w, u, canopy).r;
  result.layer = float (layer);
  result.volume_uv = volume_uv;
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
  const uint ordinal = mesh_id % MOPPE_FOREST_CANOPY_DENSITY_SLICES;
  const uint patch_index = mesh_id / MOPPE_FOREST_CANOPY_DENSITY_SLICES;
  const uint2 packed_patch =
    payload.patches[min (patch_index, payload.count - 1u)];
  const bool camera_above = (packed_patch.x & 0x80000000u) != 0u;
  const uint layer =
    camera_above ? ordinal : MOPPE_FOREST_CANOPY_DENSITY_SLICES - 1u - ordinal;
  const uint2 patch = uint2 (packed_patch.x & 0x7fffffffu, packed_patch.y);
  for (uint vertex_index = thread_id;
       vertex_index < MOPPE_FOREST_CANOPY_MESH_VERTICES;
       vertex_index += MOPPE_FOREST_CANOPY_MESH_THREADS)
    out.set_vertex (
      vertex_index,
      forest_canopy_vertex (vertex_index, patch, layer, u, heights, canopy));

  for (uint primitive = thread_id;
       primitive < MOPPE_FOREST_CANOPY_MESH_PRIMITIVES;
       primitive += MOPPE_FOREST_CANOPY_MESH_THREADS) {
    const uint cell = primitive / 2u;
    const uint triangle = primitive & 1u;
    const uint base = cell * 4u;
    const uint3 indices = triangle == 0u
                            ? uint3 (base, base + 1u, base + 2u)
                            : uint3 (base + 1u, base + 3u, base + 2u);
    const uint slot = primitive * 3u;
    out.set_index (slot + 0u, indices.x);
    out.set_index (slot + 1u, indices.y);
    out.set_index (slot + 2u, indices.z);
  }
}

fragment MoppeTemporalOutput forest_canopy_fragment (
  ForestCanopyVaryings in [[stage_in]],
  constant MoppeForestCanopyUniforms& u [[buffer (MOPPE_BUF_FRAME)]],
  texture2d<float> canopy [[texture (MOPPE_TEX_FOREST_CANOPY)]],
  texture2d<float> density [[texture (MOPPE_TEX_FOREST_DENSITY)]]) {
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
  const float support = moppe_forest_stand_support (in.stand_closure);
  float3 normal = normalize (in.normal);
  constexpr sampler density_smp (
    coord::normalized, address::repeat, filter::linear, mip_filter::linear);
  const float4 layers =
    density.sample (density_smp, in.world_pos.xz * u.field.xy);
  const uint layer =
    min (uint (in.layer + 0.5), uint (MOPPE_FOREST_CANOPY_DENSITY_SLICES - 1));
  // The retained texture stores bounded optical depth in four vertical
  // channels. Each tree contributed a separately normalized footprint to
  // every band, so their sum conserves its projected crown area while the
  // lower, middle, and upper crown retain different horizontal extents.
  // Each channel occupies a genuine world-space crown band. The impostor is
  // only a compact raster domain: an ellipsoid section gives one smooth
  // optical-depth integration rather than front and back polygon surfaces.
  const float vertical_depth =
    MOPPE_FOREST_CANOPY_STRATUM_DEPTH_RANGE * layers[layer];
  const float radius_squared = dot (in.volume_uv, in.volume_uv);
  if (radius_squared >= 1.0)
    discard_fragment ();
  const float shape = sqrt (max (1.0 - radius_squared, 0.0));
  // Many cells, not one analytic slab, lengthen a grazing ray. Bound the
  // correction per element so a side view gains population depth without
  // turning the first encountered cell into an opaque wall.
  const float incidence = max (abs (eye.y), 0.45);
  const float path_depth = vertical_depth / incidence;
  const float coverage = 1.0 - exp (-path_depth);
  const float alpha = aggregate * edge * support * coverage * shape;
  if (alpha < 0.012 || closure < 0.035)
    discard_fragment ();

  const float3 light = normalize (u.sun_dir.xyz);
  const float moisture = moments.a;
  const float3 leaf_normal =
    normalize (mix (normal, float3 (0.0, 1.0, 0.0), 0.25));
  const float visibility =
    moppe_cloud_transmission (in.world_pos, light, u.params.x, u.params.y);
  const float footprint = distance / max (focal, 1.0);
  const float grain = forest_canopy_variance_grain (in.world_pos, footprint);
  const MoppeForestEnsembleLight ensemble =
    moppe_forest_ensemble_light (moisture,
                                 closure,
                                 leaf_normal,
                                 light,
                                 eye,
                                 u.sun_diffuse.rgb,
                                 u.ambient.rgb,
                                 visibility,
                                 coverage,
                                 grain);
  float3 color = ensemble.radiance;

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
