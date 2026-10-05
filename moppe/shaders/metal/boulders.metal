// Boulders: faceted, flat-shaded lumps of rock. Each one is an icosahedron
// shaped from its seed -- turned, jittered, and cleaved by a few planes so
// large flat faces break its roundness -- squashed broader than tall and
// settled into the ground by the planner. One meshlet emits one boulder.

#include "common.h"

struct BoulderVaryings {
  float4 position [[position]];
  float3 world_pos;
  float3 albedo [[flat]];
  float moisture [[flat]];
  float rise; // radii above the ground the boulder settled on
  float2 motion [[center_no_perspective]];
};

using BoulderMesh = metal::mesh<BoulderVaryings,
                                void,
                                MOPPE_BOULDER_MESH_VERTICES,
                                MOPPE_BOULDER_MESH_PRIMITIVES,
                                metal::topology::triangle>;

struct BoulderShadowVaryings {
  float4 position [[position]];
};

using BoulderShadowMesh = metal::mesh<BoulderShadowVaryings,
                                      void,
                                      MOPPE_BOULDER_MESH_VERTICES,
                                      MOPPE_BOULDER_MESH_PRIMITIVES,
                                      metal::topology::triangle>;

struct BoulderShadowPayload {
  uint count;
  uint boulder[MOPPE_FOREST_OBJECT_THREADS];
  uint copy[MOPPE_FOREST_OBJECT_THREADS];
};

// The planner's body: vertical half-height and burial, in radii.
constant float boulder_squash = 0.7;
constant float boulder_burial = 0.3;
// Below its widest girth a boulder reaches deeper than it stands tall, so
// its underside stays in the ground on a slope. Nobody sees this part.
constant float boulder_root_depth = 1.6;

constant float3 boulder_icosahedron[12] = {
  float3 (-0.525731, 0.850651, 0.0),  float3 (0.525731, 0.850651, 0.0),
  float3 (-0.525731, -0.850651, 0.0), float3 (0.525731, -0.850651, 0.0),
  float3 (0.0, -0.525731, 0.850651),  float3 (0.0, 0.525731, 0.850651),
  float3 (0.0, -0.525731, -0.850651), float3 (0.0, 0.525731, -0.850651),
  float3 (0.850651, 0.0, -0.525731),  float3 (0.850651, 0.0, 0.525731),
  float3 (-0.850651, 0.0, -0.525731), float3 (-0.850651, 0.0, 0.525731),
};

constant ushort3 boulder_faces[20] = {
  ushort3 (0, 11, 5), ushort3 (0, 5, 1),   ushort3 (0, 1, 7),
  ushort3 (0, 7, 10), ushort3 (0, 10, 11), ushort3 (1, 5, 9),
  ushort3 (5, 11, 4), ushort3 (11, 10, 2), ushort3 (10, 7, 6),
  ushort3 (7, 1, 8),  ushort3 (3, 9, 4),   ushort3 (3, 4, 2),
  ushort3 (3, 2, 6),  ushort3 (3, 6, 8),   ushort3 (3, 8, 9),
  ushort3 (4, 9, 5),  ushort3 (2, 4, 11),  ushort3 (6, 2, 10),
  ushort3 (8, 6, 7),  ushort3 (9, 8, 1),
};

static inline float boulder_hash (uint seed, uint lane) {
  uint value = seed ^ lane * 0x9e3779b9u;
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  value ^= value >> 16;
  return float (value & 0x00ffffffu) / float (0x01000000u);
}

struct Boulder {
  float3 centre;
  float3 up;
  float3 right;
  float3 forward;
  float radius;
  float3 stretch; // body scale along right, up, forward
  float3x3 turn;  // the unit shape's own orientation
  float3 cleave[4];
  float cleave_depth[4];
  uint seed;
};

static inline float3 boulder_centre (thread const MoppeBoulderInstance& rock,
                                     constant MoppeForestUniforms& u,
                                     uint copy,
                                     bool tiled) {
  float3 centre = rock.centre_radius.xyz;
  const float2 period = u.world.xy;
  if (tiled) {
    const int tile = int (copy);
    centre.x += float (tile % 3 - 1) * period.x;
    centre.z += float (tile / 3 - 1) * period.y;
  } else {
    if (period.x > 0.0)
      centre.x += round ((u.camera_pos.x - centre.x) / period.x) * period.x;
    if (period.y > 0.0)
      centre.z += round ((u.camera_pos.z - centre.z) / period.y) * period.y;
  }
  return centre;
}

static inline float3 boulder_unit_direction (uint seed, uint lane) {
  const float z = 2.0 * boulder_hash (seed, lane) - 1.0;
  const float a = 6.2831853 * boulder_hash (seed, lane + 1u);
  const float s = sqrt (max (1.0 - z * z, 0.0));
  return float3 (s * cos (a), z, s * sin (a));
}

static inline Boulder boulder_of (thread const MoppeBoulderInstance& rock,
                                  float3 centre) {
  Boulder b;
  b.seed = rock.identity.x;
  b.centre = centre;
  b.radius = rock.centre_radius.w;
  // A stone settles partly into the slope: halfway between plumb and the
  // ground normal.
  b.up = normalize (float3 (0.0, 1.0, 0.0) + rock.up_moisture.xyz);
  const float yaw = 6.2831853 * boulder_hash (b.seed, 1u);
  const float3 heading = float3 (cos (yaw), 0.0, sin (yaw));
  b.right = normalize (heading - b.up * dot (heading, b.up));
  b.forward = cross (b.right, b.up);
  const float elongation = 0.85 + 0.40 * boulder_hash (b.seed, 2u);
  b.stretch =
    float3 (elongation,
            boulder_squash * (0.88 + 0.24 * boulder_hash (b.seed, 3u)),
            1.0 / sqrt (elongation));
  const float3 axis = boulder_unit_direction (b.seed, 4u);
  const float angle = 6.2831853 * boulder_hash (b.seed, 6u);
  const float c = cos (angle), s = sin (angle), t = 1.0 - c;
  b.turn = float3x3 (float3 (t * axis.x * axis.x + c,
                             t * axis.x * axis.y + s * axis.z,
                             t * axis.x * axis.z - s * axis.y),
                     float3 (t * axis.x * axis.y - s * axis.z,
                             t * axis.y * axis.y + c,
                             t * axis.y * axis.z + s * axis.x),
                     float3 (t * axis.x * axis.z + s * axis.y,
                             t * axis.y * axis.z - s * axis.x,
                             t * axis.z * axis.z + c));
  // Rock splits along joints: a few planes shear the lump flat, the way
  // weathered blocks show broad faces -- a tilted crown and three flanks.
  const float3 crown = boulder_unit_direction (b.seed, 8u);
  b.cleave[0] = normalize (float3 (0.0, 1.0, 0.0) + 0.45 * crown);
  b.cleave_depth[0] = 0.55 + 0.20 * boulder_hash (b.seed, 19u);
  for (uint k = 1u; k < 4u; ++k) {
    float3 n = boulder_unit_direction (b.seed, 10u + 2u * k);
    n.y = abs (n.y) * 0.6;
    b.cleave[k] = normalize (n);
    b.cleave_depth[k] = 0.50 + 0.22 * boulder_hash (b.seed, 20u + k);
  }
  return b;
}

static inline float3 boulder_cleave (thread const Boulder& b, float3 p) {
  for (uint k = 0u; k < 4u; ++k) {
    const float reach = dot (p, b.cleave[k]) - b.cleave_depth[k];
    if (reach > 0.0)
      p -= reach * b.cleave[k];
  }
  return p;
}

// A corner of the unit lump.
static inline float3 boulder_corner (thread const Boulder& b, uint i) {
  float3 p = b.turn * boulder_icosahedron[i];
  p *= 0.80 + 0.36 * boulder_hash (b.seed, 40u + i);
  p = boulder_cleave (b, p);
  if (p.y < 0.0)
    p.y *= boulder_root_depth;
  return p;
}

static inline float3 boulder_world (thread const Boulder& b, float3 p) {
  const float3 local = p * b.stretch * b.radius;
  return b.centre + b.right * local.x + b.up * local.y + b.forward * local.z;
}

// The bare icosahedron: one vertex per corner.
static inline float3 boulder_coarse_vertex (thread const Boulder& b, uint i) {
  return boulder_corner (b, i);
}

// The subdivided icosahedron: six vertices per face, its corners and edge
// midpoints, so four flat triangles replace each face. A midpoint stays near
// its edge, lifted a little, so the near rock keeps the distant one's chunky
// silhouette and only gains facets; it is keyed by its edge, not its face, so
// neighbouring faces meet without cracks.
static inline float3 boulder_fine_vertex (thread const Boulder& b, uint i) {
  const ushort3 face = boulder_faces[i / 6u];
  const uint slot = i % 6u;
  const uint corners[3] = { face.x, face.y, face.z };
  if (slot < 3u)
    return boulder_corner (b, corners[slot]);
  const uint a = corners[slot - 3u];
  const uint c = corners[(slot - 2u) % 3u];
  const uint low = min (a, c), high = max (a, c);
  const float3 edge = 0.5 * (boulder_corner (b, a) + boulder_corner (b, c));
  const float lift =
    0.12 * boulder_hash (b.seed, 60u + low * 12u + high) - 0.03;
  return boulder_cleave (b, edge + lift * normalize (edge));
}

static inline uint3 boulder_fine_triangle (uint primitive) {
  // Slots 0..2 are the corners A, B, C; 3..5 the midpoints AB, BC, CA.
  const uint first = (primitive / 4u) * 6u;
  switch (primitive % 4u) {
  case 0u:
    return first + uint3 (0u, 3u, 5u);
  case 1u:
    return first + uint3 (3u, 1u, 4u);
  case 2u:
    return first + uint3 (5u, 4u, 2u);
  default:
    return first + uint3 (3u, 4u, 5u);
  }
}

// Honest rock: a neutral grey, each stone a little warmer or cooler,
// lighter or darker than its neighbours.
static inline float3 boulder_albedo (thread const Boulder& b) {
  const float hue = boulder_hash (b.seed, 30u) - 0.5;
  const float value = 0.86 + 0.24 * boulder_hash (b.seed, 31u);
  return moppe_srgb (float3 (0.56 + 0.07 * hue, 0.54, 0.51 - 0.07 * hue) *
                     value);
}

static inline bool boulder_fine (float pixels) {
  return pixels > 14.0;
}

// ---- the scene stage -----------------------------------------------

[[mesh]] void boulders_mesh (BoulderMesh out,
                             uint mesh_id [[threadgroup_position_in_grid]],
                             uint thread_id [[thread_index_in_threadgroup]],
                             constant MoppeForestUniforms& u
                             [[buffer (MOPPE_BUF_FRAME)]],
                             device const MoppeBoulderInstance* rocks
                             [[buffer (MOPPE_BUF_BOULDERS)]],
                             device const MoppeBoulderCandidate* candidates
                             [[buffer (MOPPE_BUF_DRAW)]]) {
  const MoppeBoulderCandidate candidate = candidates[mesh_id];
  const MoppeBoulderInstance rock = rocks[candidate.boulder];
  const Boulder b = boulder_of (rock, boulder_centre (rock, u, 4u, false));
  const bool fine = boulder_fine (candidate.pixels);
  const uint vertices = fine ? 120u : 12u;
  const uint primitives = fine ? 80u : 20u;
  if (thread_id == 0u)
    out.set_primitive_count (primitives);

  if (thread_id < vertices) {
    const float3 p = fine ? boulder_fine_vertex (b, thread_id)
                          : boulder_coarse_vertex (b, thread_id);
    const float3 world = boulder_world (b, p);
    BoulderVaryings o;
    o.position = u.view_proj * float4 (world, 1.0);
    o.world_pos = world;
    o.albedo = boulder_albedo (b);
    o.moisture = rock.up_moisture.w;
    o.rise = p.y * b.stretch.y + (boulder_squash - boulder_burial);
    // Rocks do not move: only the camera contributes motion.
    o.motion =
      moppe_motion_vector (u.unjittered_view_proj * float4 (world, 1.0),
                           u.previous_view_proj * float4 (world, 1.0),
                           u.temporal.xy);
    out.set_vertex (thread_id, o);
  }
  if (thread_id < primitives) {
    const uint3 tri = fine ? boulder_fine_triangle (thread_id)
                           : uint3 (boulder_faces[thread_id]);
    out.set_index (thread_id * 3u + 0u, tri.x);
    out.set_index (thread_id * 3u + 1u, tri.y);
    out.set_index (thread_id * 3u + 2u, tri.z);
  }
}

static inline float boulder_visibility (float3 world_pos,
                                        float3 light,
                                        constant MoppeForestUniforms& u,
                                        depth2d<float> shadow_map) {
  float visibility =
    moppe_cloud_transmission (world_pos, light, u.params.x, u.params.y);
  if (u.shadow.x <= 0.01)
    return visibility;
  const float4 shadow_coord = u.light_matrix * float4 (world_pos, 1.0);
  const float3 projection = shadow_coord.xyz / shadow_coord.w;
  if (!all (projection >= 0.0) || !all (projection <= 1.0))
    return visibility;
  constexpr sampler shadow_smp (coord::normalized,
                                address::clamp_to_edge,
                                filter::linear,
                                compare_func::less_equal);
  // Half a metre of light-depth margin keeps a facet from shadowing itself.
  const float margin = 0.5 / 1240.0;
  float lit = 0.0;
  for (float dy = -0.5; dy <= 0.5; dy += 1.0)
    for (float dx = -0.5; dx <= 0.5; dx += 1.0)
      lit += 0.25 * shadow_map.sample_compare (shadow_smp,
                                               projection.xy +
                                                 float2 (dx, dy) * u.shadow.y,
                                               projection.z - margin);
  return visibility * mix (1.0, mix (0.18, 1.0, lit), u.shadow.x);
}

fragment MoppeTemporalOutput
boulders_fragment (BoulderVaryings in [[stage_in]],
                   constant MoppeForestUniforms& u [[buffer (MOPPE_BUF_FRAME)]],
                   depth2d<float> shadow_map [[texture (MOPPE_TEX_SHADOW)]]) {
  const float3 to_eye = u.camera_pos.xyz - in.world_pos;
  const float distance = length (to_eye);
  const float3 view = to_eye / max (distance, 1e-3);
  const float3 light = u.sun_dir.xyz;

  // One normal per facet, from the surface's own screen derivatives.
  float3 n = normalize (cross (dfdx (in.world_pos), dfdy (in.world_pos)));
  if (dot (n, view) < 0.0)
    n = -n;

  // Each facet is its own shade of the stone, as cleaved rock catches
  // light unevenly.
  const float facet = moppe_hash12 (floor (n.xz * 7.0) + floor (n.y * 5.0));
  float3 albedo = in.albedo * (0.88 + 0.22 * facet);
  // Lichen crusts the dry tops; damp stones grow moss instead. Both keep to
  // the upward faces, in patches.
  const float patches =
    moppe_value_noise (in.world_pos.xz * 1.7 + in.world_pos.y * 0.9);
  const float growth =
    smoothstep (0.55, 0.90, n.y) *
    smoothstep (0.45, 0.75, patches + 0.35 * in.moisture - 0.10);
  const float3 lichen = moppe_srgb (float3 (0.64, 0.63, 0.52));
  const float3 moss = moppe_srgb (float3 (0.30, 0.38, 0.16));
  albedo = mix (albedo,
                mix (lichen, moss, smoothstep (0.30, 0.65, in.moisture)),
                0.55 * growth);
  // The soil darkens and the light fails where the stone meets the ground.
  const float contact = smoothstep (-0.05, 0.40, in.rise);
  albedo *= mix (0.55, 1.0, contact);

  const float visibility =
    boulder_visibility (in.world_pos, light, u, shadow_map);
  const float sun = saturate (dot (n, light));
  const float occlusion = mix (0.55, 0.92, contact);
  // Sunlit ground around the stone throws warm light back up into its
  // shaded lower faces, which the sky alone would leave cold and blue.
  const float3 bounce = u.sun_diffuse.rgb * float3 (0.30, 0.26, 0.15) *
                        saturate (0.55 - 0.45 * n.y);
  float3 color =
    albedo *
    (sun * visibility * 0.95 * u.sun_diffuse.rgb +
     occlusion * (moppe_hemisphere_light (u.ambient.rgb, n) + 0.45 * bounce));

  const float fog =
    moppe_relief_haze (moppe_distance_fog (distance, u.fog_color.w),
                       in.world_pos.y,
                       u.params.z,
                       u.params.w);
  const float3 fog_color = moppe_warmed_fog (u.fog_color.rgb, -view, light);
  color = mix (color, fog_color, smoothstep (0.0, 0.92, fog));
  return moppe_temporal_output (float4 (color, 1.0), in.motion, 0.05);
}

// ---- the shadow stages ---------------------------------------------

[[object]] void boulders_shadow_object (
  object_data BoulderShadowPayload& payload [[payload]],
  metal::mesh_grid_properties mesh_grid,
  uint thread_id [[thread_index_in_threadgroup]],
  uint3 group [[threadgroup_position_in_grid]],
  constant MoppeForestUniforms& u [[buffer (MOPPE_BUF_FRAME)]],
  device const MoppeBoulderInstance* rocks [[buffer (MOPPE_BUF_BOULDERS)]]) {
  threadgroup atomic_uint emitted;
  if (thread_id == 0u)
    atomic_store_explicit (&emitted, 0u, metal::memory_order_relaxed);
  threadgroup_barrier (metal::mem_flags::mem_threadgroup);

  const uint rock_count = uint (u.world.z);
  const bool local = u.world.w > 0.5;
  const uint image_count = local ? 1u : 9u;
  const uint candidate = group.x * MOPPE_FOREST_OBJECT_THREADS + thread_id;
  if (candidate < rock_count * image_count) {
    const uint index = candidate % rock_count;
    const uint copy = local ? 4u : candidate / rock_count;
    const MoppeBoulderInstance rock = rocks[index];
    const float radius = rock.centre_radius.w;
    // The whole-world map's texels are larger than a small stone.
    if (local || radius >= 1.0) {
      const float3 centre = boulder_centre (rock, u, copy, !local);
      const float4 clip = u.view_proj * float4 (centre, 1.0);
      const float2 clip_radius = radius * moppe_projection_scale (u.view_proj);
      if (clip.w > -radius && abs (clip.x) < clip.w + clip_radius.x &&
          abs (clip.y) < clip.w + clip_radius.y) {
        const uint slot =
          atomic_fetch_add_explicit (&emitted, 1u, metal::memory_order_relaxed);
        payload.boulder[slot] = index;
        payload.copy[slot] = copy;
      }
    }
  }
  threadgroup_barrier (metal::mem_flags::mem_threadgroup);
  if (thread_id == 0u) {
    payload.count =
      atomic_load_explicit (&emitted, metal::memory_order_relaxed);
    mesh_grid.set_threadgroups_per_grid (uint3 (payload.count, 1, 1));
  }
}

[[mesh]] void boulders_shadow_mesh (
  BoulderShadowMesh out,
  object_data const BoulderShadowPayload& payload [[payload]],
  uint mesh_id [[threadgroup_position_in_grid]],
  uint thread_id [[thread_index_in_threadgroup]],
  constant MoppeForestUniforms& u [[buffer (MOPPE_BUF_FRAME)]],
  device const MoppeBoulderInstance* rocks [[buffer (MOPPE_BUF_BOULDERS)]]) {
  const MoppeBoulderInstance rock = rocks[payload.boulder[mesh_id]];
  const bool local = u.world.w > 0.5;
  const Boulder b =
    boulder_of (rock, boulder_centre (rock, u, payload.copy[mesh_id], !local));
  if (thread_id == 0u)
    out.set_primitive_count (20u);
  if (thread_id < 12u) {
    BoulderShadowVaryings o;
    o.position =
      u.view_proj *
      float4 (boulder_world (b, boulder_coarse_vertex (b, thread_id)), 1.0);
    out.set_vertex (thread_id, o);
  }
  if (thread_id < 20u) {
    const ushort3 tri = boulder_faces[thread_id];
    out.set_index (thread_id * 3u + 0u, tri.x);
    out.set_index (thread_id * 3u + 1u, tri.y);
    out.set_index (thread_id * 3u + 2u, tri.z);
  }
}
