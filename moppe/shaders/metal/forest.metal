// Production trees are assembled on the GPU from compact individuals.
//
// The object stage chooses an organism's projected detail and schedules a
// bounded set of reusable organs. The mesh stage expands one organ -- a stem,
// conifer bough tier, broadleaf crown lobe, or distant crown proxy. No complete
// tree mesh exists in CPU or GPU memory, and no alpha-tested foliage is used.

#include "common.h"

struct ForestVaryings {
  float4 position [[position]];
  float3 world_pos;
  float3 normal;
  float3 albedo;
  float exposure;
  float leaf;
  float2 motion [[center_no_perspective]];
};

struct ForestShadowVaryings {
  float4 position [[position]];
};

struct ForestPart {
  uint tree;
  uint part;
  uint lod;
  uint copy;
};

struct ForestPayload {
  uint count;
  ForestPart parts[MOPPE_FOREST_PAYLOAD_PARTS];
};

struct ForestShadowPayload {
  uint count;
  ForestPart parts[MOPPE_FOREST_OBJECT_THREADS];
};

using ForestMesh = metal::mesh<ForestVaryings,
                               void,
                               MOPPE_FOREST_MESH_VERTICES,
                               MOPPE_FOREST_MESH_PRIMITIVES,
                               metal::topology::triangle>;
using ForestShadowMesh = metal::mesh<ForestShadowVaryings,
                                     void,
                                     MOPPE_FOREST_MESH_VERTICES,
                                     MOPPE_FOREST_MESH_PRIMITIVES,
                                     metal::topology::triangle>;

static inline uint forest_mix (uint value) {
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  value ^= value >> 16;
  return value;
}

static inline float forest_hash (uint seed, uint lane) {
  return float (forest_mix (seed ^ lane * 0x9e3779b9u) & 0x00ffffffu) /
         float (0x01000000u);
}

static inline float3 forest_root (thread const MoppeForestInstance& tree,
                                  constant MoppeForestUniforms& u,
                                  uint copy,
                                  bool shadow) {
  float3 root = tree.root_height.xyz;
  const float2 period = u.world.xy;
  if (shadow) {
    const int tile = int (copy);
    root.x += float (tile % 3 - 1) * period.x;
    root.z += float (tile / 3 - 1) * period.y;
  } else {
    if (period.x > 0.0)
      root.x += round ((u.camera_pos.x - root.x) / period.x) * period.x;
    if (period.y > 0.0)
      root.z += round ((u.camera_pos.z - root.z) / period.y) * period.y;
  }
  return root;
}

static inline float3 forest_up (thread const MoppeForestInstance& tree) {
  return normalize (mix (float3 (0.0, 1.0, 0.0),
                         tree.up_radius.xyz,
                         tree.identity.y == 1u ? 0.20 : 0.28));
}

static inline uint forest_part_count (float pixels, uint seed) {
  // A stable per-individual threshold avoids a circular LOD front in which a
  // whole stand changes construction on the same frame.
  const float threshold = 0.88 + 0.24 * forest_hash (seed, 191u);
  if (pixels < 2.0)
    return 0u;
  if (pixels < 14.0 * threshold)
    return 1u;
  if (pixels < 48.0 * threshold)
    return 3u;
  if (pixels < 160.0 * threshold)
    return MOPPE_FOREST_PARTS_PER_TREE;
  return MOPPE_FOREST_HERO_PARTS;
}

// One object threadgroup considers one individual: thread zero reads the
// organism and chooses its projected detail, then all threads cooperate to
// schedule its organs. A hero assembly owns the whole payload, so dense
// stands never make neighbouring trees drop their boughs.
[[object]] void forest_object (object_data ForestPayload& payload [[payload]],
                               metal::mesh_grid_properties mesh_grid,
                               uint thread_id [[thread_index_in_threadgroup]],
                               uint3 group [[threadgroup_position_in_grid]],
                               constant MoppeForestUniforms& u
                               [[buffer (MOPPE_BUF_FRAME)]],
                               device const MoppeForestInstance* trees
                               [[buffer (MOPPE_BUF_FOREST)]]) {
  // Every thread derives the same cheap verdict for its tree, so the
  // organ schedule needs no shared memory and no barrier.
  const uint tree_index = group.x;
  const uint tree_count = uint (u.world.z);
  uint parts = 0u;
  if (tree_index < tree_count) {
    const MoppeForestInstance tree = trees[tree_index];
    const float3 root = forest_root (tree, u, 4u, false);
    const float3 up = forest_up (tree);
    const float height = tree.root_height.w;
    const float radius = max (tree.up_radius.w, 0.18 * height);
    const float3 centre = root + up * (0.52 * height);
    const float4 clip = u.view_proj * float4 (centre, 1.0);
    const float clip_radius =
      radius * max (abs (u.view_proj[0][0]), abs (u.view_proj[1][1]));
    const bool visible = clip.w > -radius &&
                         abs (clip.x) < clip.w + clip_radius &&
                         abs (clip.y) < clip.w + clip_radius;
    float pixels = 0.0;
    if (visible) {
      const float4 bottom = u.view_proj * float4 (root, 1.0);
      const float4 top = u.view_proj * float4 (root + up * height, 1.0);
      if (bottom.w > 0.01 && top.w > 0.01)
        pixels = abs (top.y / top.w - bottom.y / bottom.w) * 0.5 * u.temporal.y;
      else
        // An endpoint behind the camera plane means the rider is beside or
        // beneath this organism. That is the closest a tree ever gets: it
        // must be a full assembly, never a zero-pixel cull that vanishes the
        // trunk you are about to ride past.
        pixels = 1.0e6;
      parts = forest_part_count (pixels, tree.identity.x);
      // Only conifers own a bough-assembly expansion; a hero broadleaf
      // keeps the ordinary organ set.
      if (parts == MOPPE_FOREST_HERO_PARTS && tree.identity.y != 1u)
        parts = MOPPE_FOREST_PARTS_PER_TREE;
    }
  }
  if (thread_id == 0u) {
    payload.count = parts;
    mesh_grid.set_threadgroups_per_grid (uint3 (parts, 1, 1));
  }
  for (uint part = thread_id; part < parts; part += MOPPE_FOREST_OBJECT_THREADS)
    payload.parts[part] = { tree_index, part, parts, 4u };
}

// Shadow detail is deliberately coarser: every periodic image contributes one
// opaque crown proxy. This is enough to put forest-scale occlusion on the
// ground without multiplying the scene-detail budget by the shadow map.
[[object]] void forest_shadow_object (
  object_data ForestShadowPayload& payload [[payload]],
  metal::mesh_grid_properties mesh_grid,
  uint thread_id [[thread_index_in_threadgroup]],
  uint3 group [[threadgroup_position_in_grid]],
  constant MoppeForestUniforms& u [[buffer (MOPPE_BUF_FRAME)]],
  device const MoppeForestInstance* trees [[buffer (MOPPE_BUF_FOREST)]]) {
  threadgroup atomic_uint emitted;
  if (thread_id == 0u)
    atomic_store_explicit (&emitted, 0u, metal::memory_order_relaxed);
  threadgroup_barrier (metal::mem_flags::mem_threadgroup);

  const uint tree_count = uint (u.world.z);
  const bool local = u.world.w > 0.5;
  const uint image_count = local ? 1u : 9u;
  const uint candidate = group.x * MOPPE_FOREST_OBJECT_THREADS + thread_id;
  if (candidate < tree_count * image_count) {
    const uint tree = candidate % tree_count;
    const uint copy = local ? 4u : candidate / tree_count;
    const MoppeForestInstance individual = trees[tree];
    const float3 root = forest_root (individual, u, copy, !local);
    const float3 centre =
      root + forest_up (individual) * 0.56 * individual.root_height.w;
    const float4 clip = u.view_proj * float4 (centre, 1.0);
    const float radius =
      max (individual.up_radius.w, 0.5 * individual.root_height.w);
    const float clip_radius =
      radius * max (abs (u.view_proj[0][0]), abs (u.view_proj[1][1]));
    if (clip.w > -radius && abs (clip.x) < clip.w + clip_radius &&
        abs (clip.y) < clip.w + clip_radius) {
      const uint slot =
        atomic_fetch_add_explicit (&emitted, 1u, metal::memory_order_relaxed);
      payload.parts[slot] = { tree, 0u, 1u, copy };
    }
  }

  threadgroup_barrier (metal::mem_flags::mem_threadgroup);
  if (thread_id == 0u) {
    payload.count =
      atomic_load_explicit (&emitted, metal::memory_order_relaxed);
    mesh_grid.set_threadgroups_per_grid (uint3 (payload.count, 1, 1));
  }
}

struct ForestOrgan {
  float3 root;
  float3 centre;
  float3 up;
  float3 across;
  float3 forward;
  float radius_x;
  float radius_z;
  float half_height;
  float tree_height;
  float bend;
  float flutter;
  uint seed;
  bool wood;
  bool conifer;
  bool proxy;
  // A frond organ is one feathered bough of the nearest conifers: a comb of
  // serrated needle fans along a drooping axis, one meshlet per bough.
  bool frond;
};

static inline ForestOrgan forest_organ (thread const MoppeForestInstance& tree,
                                        constant MoppeForestUniforms& u,
                                        ForestPart part,
                                        bool shadow) {
  ForestOrgan organ;
  organ.root = forest_root (tree, u, part.copy, shadow);
  organ.up = forest_up (tree);
  organ.tree_height = tree.root_height.w;
  organ.seed = tree.identity.x;
  organ.conifer = tree.identity.y == 1u;
  organ.proxy = part.lod == 1u;
  organ.wood = part.part == 0u && !organ.proxy;
  // A hero conifer is trunk plus boughs and nothing else: no shell or cone
  // primitive exists at that tier.
  organ.frond =
    organ.conifer && part.lod == MOPPE_FOREST_HERO_PARTS && part.part >= 1u;
  const float heading = 6.2831853 * forest_hash (organ.seed, 3u);
  const float3 reference =
    abs (organ.up.y) > 0.92 ? float3 (0.0, 0.0, 1.0) : float3 (0.0, 1.0, 0.0);
  const float3 basis = normalize (cross (organ.up, reference));
  const float3 tangent = normalize (cross (basis, organ.up));
  organ.across = basis * cos (heading) + tangent * sin (heading);
  organ.forward = normalize (cross (organ.across, organ.up));

  const float crown = tree.up_radius.w;
  if (organ.proxy) {
    organ.centre =
      organ.root + organ.up * (organ.conifer ? 0.55 : 0.62) * organ.tree_height;
    organ.radius_x = crown * (organ.conifer ? 0.86 : 1.08);
    organ.radius_z = crown * (organ.conifer ? 0.80 : 0.98);
    organ.half_height = organ.tree_height * (organ.conifer ? 0.52 : 0.35);
    organ.bend = 0.38;
    organ.flutter = 0.04;
  } else if (organ.wood) {
    const float trunk_rise = organ.conifer ? 0.46 : 0.40;
    organ.centre = organ.root + organ.up * trunk_rise * organ.tree_height;
    // A spruce bole is slender for its height; the broadleaf keeps its
    // stouter stem.
    organ.radius_x = organ.tree_height *
                     mix (0.018, 0.030, float (tree.identity.z) / 3.0) *
                     (organ.conifer ? 0.62 : 1.0);
    organ.radius_z = organ.radius_x;
    organ.half_height = trunk_rise * organ.tree_height;
    organ.bend = 0.12;
    organ.flutter = 0.0;
  } else if (organ.frond) {
    // Hero bough: nine whorls of seven boughs, golden-angle interleaved.
    // The organ basis becomes the bough frame: across points along the
    // bough, forward across it, and the fan comb hangs off that axis.
    const uint index = part.part - 1u;
    const uint whorl = index / 7u;
    const uint bough = index % 7u;
    const float t = float (whorl) / 8.0;
    const float rise = mix (0.22, 0.97, t);
    const float turn =
      6.2831853 * (float (bough) / 7.0 + 0.618034 * float (whorl) +
                   0.07 * (forest_hash (organ.seed, part.part + 211u) - 0.5));
    const float3 radial =
      normalize (organ.across * cos (turn) + organ.forward * sin (turn));
    organ.centre = organ.root + organ.up * rise * organ.tree_height;
    organ.across = radial;
    organ.forward = normalize (cross (organ.up, radial));
    const float reach =
      0.72 + 0.50 * forest_hash (organ.seed, part.part + 223u);
    organ.radius_x = crown * mix (1.35, 0.18, t) * reach;
    organ.radius_z = organ.radius_x * 0.34;
    organ.half_height = organ.tree_height * mix (0.055, 0.028, t);
    organ.bend = 0.30 + 0.45 * rise;
    organ.flutter = 0.24 + 0.30 * t;
  } else if (organ.conifer) {
    // A conifer is a cone of mass fringed by bough whorls. Two overlapping
    // cone shells arrive first and are the whole tree at middle distance —
    // five boughs read as a sparse antenna once each fan is a few pixels
    // wide, while a closed conical surface keeps the mass and silhouette of
    // a fir. The nearest trees keep the shells and add five interleaved
    // whorls that break the outline into sprays, so the LOD hop only ever
    // adds fringe to a construction it already contains.
    if (part.part <= 2u) {
      organ.proxy = true;
      const bool lower = part.part == 1u;
      const float rise = lower ? 0.46 : 0.76;
      organ.centre = organ.root + organ.up * rise * organ.tree_height;
      organ.radius_x =
        crown * (lower ? 1.08 : 0.56) *
        (0.92 + 0.16 * forest_hash (organ.seed, part.part + 31u));
      organ.radius_z =
        organ.radius_x *
        (0.86 + 0.22 * forest_hash (organ.seed, part.part + 43u));
      organ.half_height = organ.tree_height * (lower ? 0.28 : 0.24);
      organ.bend = 0.28 + 0.30 * rise;
      organ.flutter = 0.08;
    } else {
      // Whorls turn by the golden angle per tier: aligned tiers read as one
      // plant stamped in registers, interleaved tiers read as growth.
      const float tier = float (part.part - 3u);
      const float t = tier / 4.0;
      const float rise = mix (0.26, 0.96, t);
      const float irregular =
        0.82 + 0.36 * forest_hash (organ.seed, part.part + 31u);
      const float spin = 2.3999632 * tier;
      const float3 across =
        organ.across * cos (spin) + organ.forward * sin (spin);
      organ.across = across;
      organ.forward = normalize (cross (across, organ.up));
      organ.centre = organ.root + organ.up * rise * organ.tree_height;
      organ.radius_x = crown * mix (1.18, 0.26, t) * irregular;
      organ.radius_z =
        organ.radius_x *
        (0.86 + 0.20 * forest_hash (organ.seed, part.part + 43u));
      organ.half_height = organ.tree_height * mix (0.10, 0.055, t);
      organ.bend = 0.34 + 0.48 * rise;
      organ.flutter = 0.20 + 0.30 * t;
    }
  } else {
    const float lobe = float (part.part - 1u);
    const float count = float (max (part.lod - 1u, 1u));
    const float turn =
      2.3999632 * lobe + 0.55 * forest_hash (organ.seed, part.part + 51u);
    const float ring =
      count > 2.0 ? mix (0.26, 0.58, fract (lobe * 0.61)) : 0.30;
    const float rise = 0.56 + 0.26 * forest_hash (organ.seed, part.part + 61u);
    organ.centre =
      organ.root + organ.up * rise * organ.tree_height +
      (organ.across * cos (turn) + organ.forward * sin (turn)) * crown * ring;
    const float scale = count > 2.0 ? 0.37 : 0.68;
    organ.radius_x =
      crown * scale * (0.84 + 0.28 * forest_hash (organ.seed, part.part + 71u));
    organ.radius_z =
      crown * scale * (0.82 + 0.30 * forest_hash (organ.seed, part.part + 79u));
    organ.half_height =
      crown * scale * (0.78 + 0.38 * forest_hash (organ.seed, part.part + 83u));
    organ.bend = 0.40 + 0.48 * rise;
    organ.flutter = 0.34;
  }
  return organ;
}

static inline float3 forest_palette (thread const MoppeForestInstance& tree,
                                     thread const ForestOrgan& organ,
                                     float exposure) {
  const float wet = tree.ecology.y;
  const float cover = tree.ecology.x;
  const float variation = forest_hash (tree.identity.x, 97u) - 0.5;
  if (organ.wood) {
    float3 bark = organ.conifer ? float3 (0.170, 0.140, 0.115)
                                : float3 (0.235, 0.195, 0.150);
    bark *= 0.88 + 0.18 * wet + 0.16 * variation;
    return bark * (0.60 + 0.40 * exposure);
  }
  // Individuals sit on a warm-olive to cool blue-green axis in addition to
  // the brightness spread; a stand of one green reads as painted, not grown.
  const float hue = forest_hash (tree.identity.x, 113u) - 0.5;
  float3 leaf =
    organ.conifer ? float3 (0.148, 0.280, 0.152) : float3 (0.275, 0.455, 0.165);
  leaf *= float3 (1.08 - 0.20 * wet, 0.88 + 0.26 * wet, 0.90 + 0.16 * cover);
  leaf *= float3 (1.0 + 0.30 * hue, 1.0, 1.0 - 0.34 * hue);
  leaf *= 0.90 + 0.24 * variation;
  // Spruce is read by the contrast between dark needle mass and its lit
  // fringe, so the conifer exposure range runs deeper and brighter.
  return leaf *
         (organ.conifer ? 0.50 + 0.58 * exposure : 0.55 + 0.50 * exposure);
}

static inline float forest_ring_level (uint ring, bool conifer, bool proxy) {
  // A conifer volume is a spire: wide low skirt, straight taper, high apex.
  if (conifer)
    return float3 (-0.52, 0.02, 0.54)[ring];
  return float3 (-0.42, -0.02, 0.40)[ring];
}

static inline float forest_ring_radius (uint ring, bool conifer, bool proxy) {
  if (conifer)
    return float3 (1.00, 0.58, 0.22)[ring];
  return float3 (0.72, 1.00, 0.76)[ring];
}

struct ForestPoint {
  float3 position;
  float3 normal;
  float exposure;
};

static inline ForestPoint forest_vertex (thread const ForestOrgan& organ,
                                         uint vertex_index) {
  ForestPoint point;
  if (organ.wood) {
    // Twelve slightly irregular sides: a trunk is the one organ the camera
    // meets at arm's length, where an even octagonal cone reads as a bollard.
    const uint side = vertex_index % 12u;
    const uint ring = vertex_index / 12u;
    const float turn = 6.2831853 * float (side) / 12.0 +
                       0.06 * (forest_hash (organ.seed, side + 171u) - 0.5);
    const float taper = ring == 0u ? 1.70 : 0.30;
    const float girth =
      0.91 + 0.18 * forest_hash (organ.seed, side * 7u + ring + 177u);
    const float3 radial =
      organ.across * cos (turn) + organ.forward * sin (turn);
    point.position =
      organ.centre +
      organ.up * (ring == 0u ? -organ.half_height : organ.half_height) +
      radial * organ.radius_x * taper * girth;
    point.normal = radial;
    point.exposure = ring == 0u ? 0.18 : 0.72;
    return point;
  }

  if (organ.frond) {
    // One frond meshlet is a comb of thirteen needle tufts along a drooping
    // bough axis: an apex plus four separated two-vertex blades each. A twig
    // is mostly air, so the blades stay disconnected: connected fans read as
    // paddles, and overlapping paddles rebuild the very cone this tier
    // replaces.
    const uint fan = vertex_index / 9u;
    const uint local = vertex_index % 9u;
    const float s = mix (0.08, 1.0, float (fan) / 12.0);
    // The axis droops in proportion to its reach, so long lower boughs
    // sweep down through the band beneath their whorl.
    const float droop = 0.10 - 0.40 * s * s;
    const float side = fan % 2u == 0u ? 1.0 : -1.0;
    const float3 axis_point = organ.centre + organ.across * organ.radius_x * s +
                              organ.forward * organ.radius_z * 0.35 * side *
                                forest_hash (organ.seed, fan + 17u) +
                              organ.up * organ.radius_x * droop;
    if (local == 0u) {
      point.position = axis_point;
      point.normal = normalize (organ.up + organ.across * 0.2);
      point.exposure = 0.32;
      return point;
    }
    const float radius = organ.radius_x * mix (0.22, 0.10, s) *
                         (0.80 + 0.40 * forest_hash (organ.seed, fan + 31u));
    const uint rim = local - 1u;
    const uint blade = rim / 2u;
    const float edge = rim % 2u == 0u ? -1.0 : 1.0;
    const float arc =
      mix (-2.5, 2.5, float (blade) / 3.0) + edge * 0.42 +
      0.20 * (forest_hash (organ.seed, fan * 13u + blade + 41u) - 0.5);
    const float needle =
      0.62 + 0.55 * forest_hash (organ.seed, fan * 29u + blade + 57u);
    const float3 rim_dir =
      normalize (organ.across * cos (arc) + organ.forward * sin (arc));
    point.position =
      axis_point + rim_dir * radius * needle - organ.up * radius * 0.55;
    point.normal = normalize (organ.up + rim_dir * 0.35);
    point.exposure = saturate (0.34 + 0.50 * s + 0.14 * needle);
    return point;
  }

  if (organ.conifer && !organ.proxy) {
    // One conifer organ is a whorl of five explicit bough sprays. Each spray
    // widens away from the trunk, droops through the loaded middle, and turns
    // up at its tip. Gaps between them admit sky and make the silhouette read
    // as branches instead of a single noisy opaque wedge.
    const uint bough = vertex_index / 6u;
    const uint local = vertex_index % 6u;
    const uint section = local / 2u;
    const float side = local % 2u == 0u ? -1.0 : 1.0;
    const float turn =
      6.2831853 * (float (bough) / 5.0 +
                   0.08 * (forest_hash (organ.seed, bough + 211u) - 0.5));
    const float3 radial =
      normalize (organ.across * cos (turn) + organ.forward * sin (turn));
    const float3 sideways = normalize (cross (organ.up, radial));
    const float distance = float3 (0.10, 0.62, 1.0)[section];
    const float breadth = float3 (0.06, 0.30, 0.05)[section];
    const float droop = float3 (0.28, -0.20, 0.06)[section];
    // A spruce silhouette is feathery: bough reach varies by half its
    // length, so the crown edge breaks instead of tracing a smooth cone.
    const float asymmetry =
      0.70 + 0.45 * forest_hash (organ.seed, bough + 229u);
    point.position = organ.centre +
                     radial * organ.radius_x * distance * asymmetry +
                     sideways * organ.radius_z * breadth * side +
                     organ.up * organ.half_height * droop;
    point.normal =
      normalize (organ.up + radial * 0.16 + sideways * side * 0.08);
    // Interior stays dark needle mass; only the outer spray tips carry the
    // bright sunlit fringe the crown is read by.
    point.exposure = saturate (0.30 + 0.50 * distance);
    return point;
  }

  if (vertex_index == 0u || vertex_index == 31u) {
    const float sign = vertex_index == 0u ? -1.0 : 1.0;
    point.position = organ.centre + organ.up * organ.half_height * sign;
    point.normal = organ.up * sign;
    point.exposure = vertex_index == 0u ? 0.28 : 1.0;
    return point;
  }

  const uint offset = vertex_index - 1u;
  const uint ring = offset / 10u;
  const uint side = offset % 10u;
  const float base_turn = 6.2831853 * float (side) / 10.0;
  const float turn =
    base_turn +
    0.09 * (forest_hash (organ.seed, ring * 17u + side + 111u) - 0.5);
  // Conifer shells wear a rougher edge than broadleaf lobes: the cone is a
  // mass reading, and an even rim is what makes it read as felt.
  const float jitter = organ.conifer ? 0.40 : 0.22;
  const float r = forest_ring_radius (ring, organ.conifer, organ.proxy) *
                  (1.0 - 0.5 * jitter +
                   jitter * forest_hash (organ.seed, ring * 23u + side + 131u));
  const float silhouette =
    organ.conifer && !organ.proxy ? (side % 2u == 0u ? 1.18 : 0.70) : 1.0;
  const float y = forest_ring_level (ring, organ.conifer, organ.proxy) +
                  (organ.conifer ? 0.10 : 0.07) *
                    (forest_hash (organ.seed, ring * 29u + side + 151u) - 0.5);
  const float x = cos (turn) * organ.radius_x * r * silhouette;
  const float z = sin (turn) * organ.radius_z * r * silhouette;
  point.position = organ.centre + organ.across * x + organ.forward * z +
                   organ.up * organ.half_height * y;
  point.normal = normalize (organ.across * (x / max (organ.radius_x, 0.001)) +
                            organ.forward * (z / max (organ.radius_z, 0.001)) +
                            organ.up * y * 0.72);
  point.exposure = saturate (0.40 + 0.38 * y + 0.22 * point.normal.y);
  return point;
}

template <typename Mesh>
static inline void forest_indices (thread Mesh& out,
                                   thread const ForestOrgan& organ,
                                   uint primitive) {
  uint3 triangle;
  if (organ.wood) {
    const uint side = primitive / 2u;
    const uint next = (side + 1u) % 12u;
    triangle = primitive % 2u == 0u ? uint3 (side, next, 12u + next)
                                    : uint3 (side, 12u + next, 12u + side);
  } else if (organ.frond) {
    const uint fan = primitive / 4u;
    const uint blade = primitive % 4u;
    const uint base = fan * 9u;
    triangle = uint3 (base, base + 1u + 2u * blade, base + 2u + 2u * blade);
  } else if (organ.conifer && !organ.proxy) {
    const uint bough = primitive / 4u;
    const uint local = primitive % 4u;
    const uint section = local / 2u;
    const uint base = bough * 6u + section * 2u;
    triangle = local % 2u == 0u ? uint3 (base, base + 2u, base + 3u)
                                : uint3 (base, base + 3u, base + 1u);
  } else if (primitive < 10u) {
    const uint side = primitive;
    triangle = uint3 (0u, 1u + (side + 1u) % 10u, 1u + side);
  } else if (primitive < 50u) {
    const uint bridge = (primitive - 10u) / 20u;
    const uint local = (primitive - 10u) % 20u;
    const uint side = local / 2u;
    const uint a = 1u + bridge * 10u + side;
    const uint b = 1u + bridge * 10u + (side + 1u) % 10u;
    const uint c = a + 10u;
    const uint d = b + 10u;
    triangle = local % 2u == 0u ? uint3 (a, b, d) : uint3 (a, d, c);
  } else {
    const uint side = primitive - 50u;
    triangle = uint3 (31u, 21u + side, 21u + (side + 1u) % 10u);
  }
  const uint slot = primitive * 3u;
  out.set_index (slot + 0u, triangle.x);
  out.set_index (slot + 1u, triangle.y);
  out.set_index (slot + 2u, triangle.z);
}

[[mesh]] void forest_mesh (ForestMesh out,
                           object_data const ForestPayload& payload [[payload]],
                           uint mesh_id [[threadgroup_position_in_grid]],
                           uint thread_id [[thread_index_in_threadgroup]],
                           constant MoppeForestUniforms& u
                           [[buffer (MOPPE_BUF_FRAME)]],
                           device const MoppeForestInstance* trees
                           [[buffer (MOPPE_BUF_FOREST)]]) {
  const ForestPart part = payload.parts[min (mesh_id, payload.count - 1u)];
  const MoppeForestInstance tree = trees[part.tree];
  const ForestOrgan organ = forest_organ (tree, u, part, false);
  const bool boughs =
    organ.conifer && !organ.proxy && !organ.wood && !organ.frond;
  const uint vertices = organ.wood    ? 24u
                        : organ.frond ? 117u
                        : boughs      ? 30u
                                      : 32u;
  const uint primitives = organ.wood    ? 24u
                          : organ.frond ? 52u
                          : boughs      ? 20u
                                        : 60u;
  if (thread_id == 0u)
    out.set_primitive_count (primitives);

  if (thread_id < vertices) {
    const ForestPoint base = forest_vertex (organ, thread_id);
    const float rise = saturate (dot (base.position - organ.root, organ.up) /
                                 max (organ.tree_height, 0.01));
    const float bend = organ.bend * rise * rise;
    const float flutter = organ.flutter * rise;
    const float3 current =
      moppe_wind (base.position, bend, flutter, u.params.x);
    const float3 previous =
      moppe_wind (base.position, bend, flutter, u.temporal.z);
    ForestVaryings v;
    v.position = u.view_proj * float4 (current, 1.0);
    v.world_pos = current;
    v.normal = base.normal;
    v.albedo = forest_palette (tree, organ, base.exposure);
    v.exposure = base.exposure;
    v.leaf = organ.wood ? 0.0 : 1.0;
    v.motion =
      moppe_motion_vector (u.unjittered_view_proj * float4 (current, 1.0),
                           u.previous_view_proj * float4 (previous, 1.0),
                           u.temporal.xy);
    out.set_vertex (thread_id, v);
  }
  if (thread_id < primitives)
    forest_indices (out, organ, thread_id);
}

[[mesh]] void forest_shadow_mesh (
  ForestShadowMesh out,
  object_data const ForestShadowPayload& payload [[payload]],
  uint mesh_id [[threadgroup_position_in_grid]],
  uint thread_id [[thread_index_in_threadgroup]],
  constant MoppeForestUniforms& u [[buffer (MOPPE_BUF_FRAME)]],
  device const MoppeForestInstance* trees [[buffer (MOPPE_BUF_FOREST)]]) {
  const ForestPart part = payload.parts[min (mesh_id, payload.count - 1u)];
  const MoppeForestInstance tree = trees[part.tree];
  const bool world_map = u.world.w <= 0.5;
  const ForestOrgan organ = forest_organ (tree, u, part, world_map);
  // The camera-local map also carries a trunk prism. A ground shadow reads
  // as a shadow only when it can be attributed: the sun-elongated trunk line
  // attaches the crown's shade to its tree. Whole-world images stay
  // crown-only, where a trunk is sub-texel anyway.
  const bool trunk = !world_map;
  ForestOrgan stem = organ;
  if (trunk)
    stem = forest_organ (tree, u, { part.tree, 0u, 2u, part.copy }, world_map);
  const uint vertices = trunk ? 28u : 12u;
  const uint primitives = trunk ? 36u : 20u;
  if (thread_id == 0u)
    out.set_primitive_count (primitives);
  if (thread_id < vertices) {
    float3 position;
    if (thread_id == 0u || thread_id == 11u) {
      position = organ.centre +
                 organ.up * organ.half_height * (thread_id == 0u ? -1.0 : 1.0);
    } else if (thread_id < 11u) {
      const uint side = thread_id - 1u;
      const float turn = 6.2831853 * float (side) / 10.0;
      position = organ.centre + organ.across * cos (turn) * organ.radius_x +
                 organ.forward * sin (turn) * organ.radius_z;
    } else {
      const uint index = thread_id - 12u;
      const uint side = index % 8u;
      const uint ring = index / 8u;
      const float turn = 6.2831853 * float (side) / 8.0;
      const float taper = ring == 0u ? 1.70 : 0.30;
      const float3 radial =
        stem.across * cos (turn) + stem.forward * sin (turn);
      position = stem.centre +
                 stem.up * (ring == 0u ? -stem.half_height : stem.half_height) +
                 radial * stem.radius_x * taper;
    }
    ForestShadowVaryings v;
    v.position = u.view_proj * float4 (position, 1.0);
    out.set_vertex (thread_id, v);
  }
  if (thread_id < primitives) {
    uint3 triangle;
    if (thread_id < 20u) {
      const uint side = thread_id / 2u;
      const uint next = (side + 1u) % 10u;
      triangle = thread_id % 2u == 0u ? uint3 (0u, 1u + next, 1u + side)
                                      : uint3 (11u, 1u + side, 1u + next);
    } else {
      const uint prim = thread_id - 20u;
      const uint side = prim / 2u;
      const uint next = (side + 1u) % 8u;
      triangle = (prim % 2u == 0u ? uint3 (side, next, 8u + next)
                                  : uint3 (side, 8u + next, 8u + side)) +
                 12u;
    }
    const uint slot = thread_id * 3u;
    out.set_index (slot + 0u, triangle.x);
    out.set_index (slot + 1u, triangle.y);
    out.set_index (slot + 2u, triangle.z);
  }
}

fragment MoppeTemporalOutput forest_fragment (ForestVaryings in [[stage_in]],
                                              bool front_facing
                                              [[front_facing]],
                                              constant MoppeForestUniforms& u
                                              [[buffer (MOPPE_BUF_FRAME)]],
                                              depth2d<float> shadow_map
                                              [[texture (MOPPE_TEX_SHADOW)]]) {
  const float3 n = normalize (front_facing ? in.normal : -in.normal);
  const float3 l = normalize (u.sun_dir.xyz);
  const float3 to_eye = u.camera_pos.xyz - in.world_pos;
  const float distance = length (to_eye);
  const float fog =
    moppe_relief_haze (moppe_distance_fog (distance, u.fog_color.w),
                       in.world_pos.y,
                       u.params.z,
                       u.params.w);
  // The shared shadow map contains deliberately coarse tree proxies, so a
  // plain sample from inside a detailed crown would interpret that proxy as
  // precise self-occlusion and black out the organism. Instead the compare
  // gets a margin of several metres of light depth: the tree's own blob and
  // its close neighbours fall inside the margin, while a hillside or a
  // distant stand still puts the whole organism in shade. Fine-grained crown
  // shaping stays with the continuous exposure signal and the cloud field.
  float visibility =
    moppe_cloud_transmission (in.world_pos, l, u.params.x, u.params.y);
  if (u.shadow.x > 0.01) {
    const float4 shadow_coord = u.light_matrix * float4 (in.world_pos, 1.0);
    const float3 proj = shadow_coord.xyz / shadow_coord.w;
    if (all (proj >= 0.0) && all (proj <= 1.0)) {
      constexpr sampler shadow_smp (coord::normalized,
                                    address::clamp_to_edge,
                                    filter::linear,
                                    compare_func::less_equal);
      // The local light frustum spans ~1240 m of depth; 9 m in that range.
      const float margin = 9.0 / 1240.0;
      const float lit =
        shadow_map.sample_compare (shadow_smp, proj.xy, proj.z - margin);
      visibility *= mix (1.0, mix (0.22, 1.0, lit), u.shadow.x);
    }
  }

  float3 base = moppe_srgb (in.albedo);
  if (in.leaf < 0.5) {
    // Bark fissures: two vertical planes of stretched value noise blended by
    // facing, so striation follows any trunk without a UV seam.
    const float2 stretch = float2 (3.1, 0.33);
    const float2 px = float2 (in.world_pos.z, in.world_pos.y) * stretch;
    const float2 pz = float2 (in.world_pos.x, in.world_pos.y) * stretch;
    const float sx =
      0.65 * moppe_value_noise (px) + 0.35 * moppe_value_noise (px * 3.13);
    const float sz =
      0.65 * moppe_value_noise (pz) + 0.35 * moppe_value_noise (pz * 3.13);
    const float wx = n.x * n.x;
    const float wz = n.z * n.z;
    const float stria = (wx * sx + wz * sz) / max (wx + wz, 0.001);
    base *= 0.62 + 0.74 * stria;
  }
  // A broad wrap term keeps crown volumes legible while retaining a directional
  // sunward side. It is deliberately separate from the sky exposure signal.
  const float wrap = saturate ((dot (n, l) + 0.26) / 1.26);
  float3 color = base * (moppe_hemisphere_light (u.ambient.rgb, n) *
                           (0.62 + 0.38 * in.exposure) +
                         u.sun_diffuse.rgb * wrap * visibility);
  color +=
    base * float3 (0.10, 0.16, 0.20) * (0.35 + 0.65 * in.exposure) * in.leaf;

  if (in.leaf > 0.5) {
    const float back = pow (max (dot (-n, l), 0.0), 1.45);
    const float3 transmission = float3 (1.0, 0.82, 0.46);
    color += base * u.sun_diffuse.rgb * transmission * visibility * back *
             (0.20 + 0.80 * in.exposure) * 0.66;
    const float3 h = normalize (l + normalize (to_eye));
    color += u.sun_specular.rgb * pow (max (dot (n, h), 0.0), 18.0) *
             visibility * 0.055;
  }

  const float3 fog_color =
    moppe_warmed_fog (u.fog_color.rgb, -to_eye / max (distance, 0.001), l);
  color = mix (color, fog_color, smoothstep (0.0, 0.92, fog));
  return moppe_temporal_output (
    float4 (color, 1.0), in.motion, in.leaf > 0.5 ? 0.48 : 0.12);
}
