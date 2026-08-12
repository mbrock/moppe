// Production trees are assembled on the GPU from compact individuals.
//
// The object stage chooses an organism's projected detail and schedules a
// bounded set of reusable organs. The mesh stage expands one organ -- a stem,
// conifer bough tier, broadleaf crown lobe, or distant crown proxy. No complete
// tree mesh exists in CPU or GPU memory, and no alpha-tested foliage is used.

#include "forest_medium.h"

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
  float pixels;
  float crown_pixels;
};

struct ForestPayload {
  uint count;
  float stand_closure;
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

static inline float forest_hash (uint seed, uint lane) {
  return moppe_forest_hash (seed, lane);
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

// A stable per-individual threshold avoids a circular LOD front in which a
// whole stand changes construction on the same frame.
static inline float forest_lod_threshold (uint seed) {
  return 0.88 + 0.24 * forest_hash (seed, 191u);
}

static inline float forest_individual_vanish_pixels (uint seed) {
  // By four to five crown pixels the stand field already carries more than
  // eighty percent of its aggregate response. Keeping a separate conifer
  // below that point spends mesh work on a silhouette the pixel grid cannot
  // repeat while making the far stand look spiky instead of closed.
  return 4.0 + 5.0 * (forest_lod_threshold (seed) - 0.88);
}

// A conifer's bough count as a continuous function of projected size. Detail
// must arrive organ by organ, never construction by construction: a discrete
// representation switch concentrates its whole visual error into one frame,
// while organs that grow in one at a time spread it below notice (Kuth 2025,
// measured in Fig. 12 of the paper; #6S29CJ in the research library).
static inline float forest_bough_count (float pixels, float threshold) {
  // The ramp saturates by ninety projected scene pixels -- at the game's
  // lens, a tree around a tenth of the frame's height. Perception, not
  // geometry, sets this point: a rider's eye is exquisitely sensitive to
  // elements appearing on a tree large enough to watch, so every arrival
  // must happen while the whole crown is a small figure in the frame.
  // Saturating at three hundred meant crowns still assembling while they
  // filled a third of the screen, which read as the forest morphing. The
  // floor of twenty-one keeps the sparsest assembly reading as a small
  // solid tree: below that a spruce degenerates into a pole with stubs,
  // which no distance excuses.
  return clamp (
    (pixels - 14.0 * threshold) * 63.0 / (76.0 * threshold), 21.0, 63.0);
}

// The ramp measures projected pixels, so on its own a completion point is a
// completion DISTANCE proportional to tree height: a twenty-metre tree
// finishes at forty metres, but a five-metre sapling keeps growing until the
// walker stands beside it. Boosting a short tree's measure makes every tree
// complete near forty-five metres, so growth happens where a bough is around
// a pixel, never in front of the walker. Gates and bundling keep the raw
// measure: a distant sapling is still a distant sapling.
static inline float forest_bough_measure (float pixels, float tree_height) {
  return pixels * clamp (22.5 / max (tree_height, 0.01), 1.0, 4.5);
}

// Fraction of a conifer crown whose projected support can be carried by
// resolved stations. Before a station is a repeatable image feature, coarse
// envelopes on the same stable bough axes carry the crown. The broad interval
// moves area from those envelopes into nested stations without changing the
// organism's large-scale topology.
static inline float forest_bough_resolution (float crown_pixels,
                                             float threshold) {
  return smoothstep (16.0 * threshold, 28.0 * threshold, crown_pixels);
}

// Emission rank to whorl-grid slot. The first nine ranks sketch the whole
// silhouette with one bough per whorl, bottom to top. Later ranks add one
// spoke per whorl per round, top whorl first, so the crown densifies
// evenly everywhere instead of completing region by region: filling the
// top whorls last read as the tree growing taller against the sky, and
// filling any whorl's spokes consecutively read as a lopsided branch.
// Consecutive arrivals within a whorl land on opposite azimuths.
//
// TOTAL over any rank: bundling rounds the scheduled rank range up past
// the sixty-three real slots (a two-bough meshlet on a full crown asks
// for rank sixty-three), and an out-of-range read here fed NaN through
// the bough frame -- zero grow times NaN is still NaN -- which rasterized
// as screen-sized stretched triangles. Phantom ranks wrap onto valid
// slots and their zero grow keeps them degenerate.
static inline uint forest_bough_slot (uint rank) {
  if (rank < 9u)
    return rank * 7u;
  const uint fill = (rank - 9u) % 54u;
  const uint spread[6] = { 4u, 2u, 6u, 1u, 5u, 3u };
  return (8u - fill % 9u) * 7u + spread[fill / 9u];
}

// Mesh-group coalescing follows the station hierarchy: the far tier packs
// four four-station boughs, the middle packs two eight-station boughs, and a
// hero meshlet carries one twenty-four-station bough. The stations subdivide
// two continuous crossed ribbons, so refinement bends an existing bough
// surface instead of inserting disconnected foliage particles. A hero bough
// keeps its own meshlet:
// two attempts at packing a pair produced garbage triangles in motion near
// Metal's silent 16 KB mesh-output ceiling. Retry only under the debugger.
// Per-individual thresholds keep a stand from crossing a boundary together.
static inline uint forest_bough_bundle (float pixels, float threshold) {
  if (pixels < 55.0 * threshold)
    return 4u;
  if (pixels < 90.0 * threshold)
    return 2u;
  return 1u;
}

static inline uint forest_bough_tufts (uint bundle) {
  return bundle == 4u ? 4u : bundle == 2u ? 8u : 24u;
}

static inline uint forest_bough_station (uint bundle, uint station) {
  if (bundle == 4u)
    return station == 3u ? 31u : station * 8u;
  if (bundle == 2u)
    return station == 7u ? 31u : station * 4u;
  const uint hero_stations[24] = { 0u,  2u,  4u,  5u,  6u,  8u,  9u,  10u,
                                   12u, 13u, 14u, 16u, 17u, 18u, 20u, 21u,
                                   22u, 24u, 25u, 26u, 28u, 29u, 30u, 31u };
  return hero_stations[station];
}

static inline float
forest_bough_refinement (float pixels, float threshold, uint bundle) {
  if (bundle == 4u)
    return 0.0;
  if (bundle == 2u)
    return smoothstep (55.0 * threshold, 75.0 * threshold, pixels);
  return smoothstep (90.0 * threshold, 120.0 * threshold, pixels);
}

static inline uint forest_part_count (
  float pixels, float crown_pixels, float height, uint seed, bool conifer) {
  const float threshold = forest_lod_threshold (seed);
  // Individual identity ends when the crown, rather than the much taller
  // trunk-to-tip measure, is no longer a repeatable image feature. Keeping a
  // two-pixel-tall tree meant carrying subpixel crown widths across almost the
  // whole world. Seed staggering prevents the retirement boundary becoming a
  // camera-centred ring; the stand aggregate receives the same crown measure.
  if (crown_pixels < forest_individual_vanish_pixels (seed))
    return 0u;
  if (conifer) {
    const float resolved = forest_bough_resolution (crown_pixels, threshold);
    // Part zero is the unresolved set of coarse bough envelopes. Its nested
    // stations begin at the same sixteen-crown-pixel boundary but grow from
    // zero; large-scale bough identity is unchanged throughout the handoff.
    if (resolved <= 0.0)
      return 1u;
    const uint residual_parent = resolved < 1.0 ? 1u : 0u;
    const float count =
      forest_bough_count (forest_bough_measure (pixels, height), threshold);
    const float bundle = float (forest_bough_bundle (pixels, threshold));
    return 1u + residual_parent + uint (ceil (count / bundle));
  }
  // Broadleafs retain the older compact-to-lobe selection until their crown
  // lobes have an equivalent conserved parent/child construction.
  if (crown_pixels < 16.0 * threshold)
    return 1u;
  if (pixels < 48.0 * threshold)
    return 3u;
  return MOPPE_FOREST_PARTS_PER_TREE;
}

struct ForestSceneSchedule {
  uint parts;
  float pixels;
};

static inline ForestSceneSchedule
forest_scene_schedule (uint tree_index,
                       float pixels,
                       float crown_pixels,
                       constant MoppeForestUniforms& u,
                       device const MoppeForestInstance* trees) {
  ForestSceneSchedule schedule = { 0u, 0.0 };
  if (tree_index >= uint (u.world.z))
    return schedule;

  const MoppeForestInstance tree = trees[tree_index];
  const float height = tree.root_height.w;
  schedule.pixels = pixels;
  schedule.parts = forest_part_count (
    pixels, crown_pixels, height, tree.identity.x, tree.identity.y == 1u);
  return schedule;
}

// One object threadgroup considers one conservative CPU-visible candidate:
// thread zero reads the indexed organism and chooses its exact projected
// detail, then all threads cooperate to schedule its organs. A hero assembly
// owns the whole payload, so dense stands never make neighbouring trees drop
// their boughs.
[[object]] void forest_object (object_data ForestPayload& payload [[payload]],
                               metal::mesh_grid_properties mesh_grid,
                               uint thread_id [[thread_index_in_threadgroup]],
                               uint3 group [[threadgroup_position_in_grid]],
                               constant MoppeForestUniforms& u
                               [[buffer (MOPPE_BUF_FRAME)]],
                               device const MoppeForestInstance* trees
                               [[buffer (MOPPE_BUF_FOREST)]],
                               device const MoppeForestCandidate* candidates
                               [[buffer (MOPPE_BUF_DRAW)]]) {
  const MoppeForestCandidate candidate = candidates[group.x];
  const uint tree_index = candidate.tree;
  // Every thread derives the same cheap verdict, so scheduling needs no
  // shared memory or barrier.
  const ForestSceneSchedule schedule = forest_scene_schedule (
    tree_index, candidate.pixels, candidate.crown_pixels, u, trees);
  if (thread_id == 0u) {
    payload.count = schedule.parts;
    payload.stand_closure =
      schedule.parts > 0u ? trees[tree_index].ecology.z : -1.0;
    mesh_grid.set_threadgroups_per_grid (uint3 (schedule.parts, 1, 1));
  }
  for (uint part = thread_id; part < schedule.parts;
       part += MOPPE_FOREST_OBJECT_THREADS)
    payload.parts[part] = { tree_index,      part,
                            schedule.parts,  0u,
                            schedule.pixels, candidate.crown_pixels };
}

// Shadow detail is deliberately coarser. The whole-world map receives one
// crown envelope because its texels are already larger than a bough. The
// camera-local map instead receives one broad, separated sample bough per
// conifer whorl. A solid cone makes a closed spruce stand a wall to the sun
// even though the visible organism is mostly air; the panels preserve
// crown-scale holes for walking and riding without regenerating the complete
// hero assembly. Broadleaf crowns remain envelopes until their own resolved
// shadow construction exists: pretending their lobes are spruce boughs would
// merely move the representation mismatch.
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
    const float2 clip_radius = radius * moppe_projection_scale (u.view_proj);
    if (clip.w > -radius && abs (clip.x) < clip.w + clip_radius.x &&
        abs (clip.y) < clip.w + clip_radius.y) {
      const uint slot =
        atomic_fetch_add_explicit (&emitted, 1u, metal::memory_order_relaxed);
      payload.parts[slot] = { tree, 0u, 1u, copy, 0.0, 0.0 };
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
  float ensemble; // 1 = individual colour; 0 = collapsed to the stand mean
  uint seed;
  bool wood;
  bool conifer;
  bool proxy;
  // A frond organ carries feathered conifer boughs: nested needle stations
  // along drooping axes. Bough geometry is evaluated per vertex from a stable
  // rank, so one meshlet holds one hero bough or a bundle of unresolved ones.
  bool frond;
  uint bundle;
  uint rank;
  // Continuous LOD: the fractional bough count this crown has reached, and
  // how much the surviving tufts widen to hold coverage while neighbours
  // are still absent.
  float count;
  float boost;
  float crown;
  float stem_radius;
  float individual;
  float refinement;
  float resolution;
  float residual;
};

static inline ForestOrgan
forest_base_organ (thread const MoppeForestInstance& tree,
                   constant MoppeForestUniforms& u,
                   ForestPart part,
                   bool shadow,
                   float stand_closure) {
  ForestOrgan organ;
  organ.ensemble = 1.0;
  organ.root = forest_root (tree, u, part.copy, shadow);
  organ.up = forest_up (tree);
  organ.tree_height = tree.root_height.w;
  organ.seed = tree.identity.x;
  organ.conifer = tree.identity.y == 1u;
  organ.crown = tree.up_radius.w;
  organ.stem_radius = organ.tree_height *
                      mix (0.018, 0.030, float (tree.identity.z) / 3.0) *
                      (organ.conifer ? 0.62 : 1.0);
  organ.resolution = 1.0;
  organ.residual = 1.0;
  if (organ.conifer && !shadow) {
    organ.resolution = forest_bough_resolution (
      part.crown_pixels, forest_lod_threshold (organ.seed));
    organ.residual = sqrt (max (1.0 - organ.resolution, 0.0));
  }
  const bool residual_parent =
    organ.conifer && !shadow && organ.residual > 0.0 && part.lod > 1u;
  // A conifer's part zero remains the coarse bough parent throughout its
  // station transition. While it exists, the stem is part one and boughs
  // follow; when its residual reaches zero the old stem/bough numbering is
  // restored. Broadleafs retain the one-part proxy convention.
  organ.proxy = part.lod == 1u || (residual_parent && part.part == 0u);
  const uint stem_part = residual_parent ? 1u : 0u;
  organ.wood = part.part == stem_part && !organ.proxy;
  organ.frond = organ.conifer && !organ.proxy && !organ.wood;
  organ.bundle = 1u;
  organ.rank = 0u;
  organ.count = 0.0;
  organ.boost = 1.0;
  organ.individual = 1.0;
  organ.refinement = 0.0;
  if (stand_closure >= 0.0) {
    const float crown_pixels = part.crown_pixels;
    const float vanish = forest_individual_vanish_pixels (organ.seed);
    const float terminal = smoothstep (vanish, vanish + 4.0, crown_pixels);
    const float transfer = moppe_forest_stand_support (stand_closure) *
                           moppe_forest_identity_transfer (crown_pixels);
    // Identity yields throughout the same projected-crown interval in which
    // the stand quotient arrives, but only where closure makes that quotient
    // truthful. Open woodland retains its organisms until their final
    // subpixel fade instead of dissolving into a false surface.
    organ.individual = (1.0 - transfer) * terminal;
    organ.ensemble = organ.individual;
  }

  const float heading = 6.2831853 * forest_hash (organ.seed, 3u);
  const float3 reference =
    abs (organ.up.y) > 0.92 ? float3 (0.0, 0.0, 1.0) : float3 (0.0, 1.0, 0.0);
  const float3 basis = normalize (cross (organ.up, reference));
  const float3 tangent = normalize (cross (basis, organ.up));
  organ.across = basis * cos (heading) + tangent * sin (heading);
  organ.forward = normalize (cross (organ.across, organ.up));
  return organ;
}

static inline void forest_configure_proxy (thread ForestOrgan& organ,
                                           ForestPart part) {
  organ.centre =
    organ.root + organ.up * (organ.conifer ? 0.55 : 0.62) * organ.tree_height;
  organ.radius_x = organ.crown * (organ.conifer ? 0.86 : 1.08);
  organ.radius_z = organ.crown * (organ.conifer ? 0.80 : 0.98);
  organ.half_height = organ.tree_height * (organ.conifer ? 0.52 : 0.35);
  organ.bend = 0.38;
  organ.flutter = 0.04;
  // The scene vertex applies the shared individual-to-stand contraction to
  // every crown construction. Keeping it out of this proxy alone prevents a
  // tree from regaining full size when projected height selects boughs.
}

static inline void
forest_configure_stem (thread ForestOrgan& organ,
                       thread const MoppeForestInstance& tree) {
  const float trunk_rise = organ.conifer ? 0.46 : 0.40;
  organ.centre = organ.root + organ.up * trunk_rise * organ.tree_height;
  organ.radius_x = organ.stem_radius;
  organ.radius_z = organ.radius_x;
  organ.half_height = trunk_rise * organ.tree_height;
  if (organ.conifer)
    organ.radius_x = organ.radius_z = organ.radius_x * sqrt (organ.resolution);
  organ.bend = 0.12;
  organ.flutter = 0.0;
}

static inline void forest_configure_frond (thread ForestOrgan& organ,
                                           ForestPart part) {
  const float threshold = forest_lod_threshold (organ.seed);
  organ.count = forest_bough_count (
    forest_bough_measure (part.pixels, organ.tree_height), threshold);
  organ.bundle = forest_bough_bundle (part.pixels, threshold);
  const bool residual_parent = organ.residual > 0.0 && part.lod > 1u;
  const uint first_bough = residual_parent ? 2u : 1u;
  organ.rank = (part.part - first_bough) * organ.bundle;
  organ.refinement =
    forest_bough_refinement (part.pixels, threshold, organ.bundle);
  // Preserve foliage area while boughs or within-bough stations are absent.
  // At each bundle boundary the finer set starts with exactly the same live
  // stations and area boost as its parent; new stations grow while all
  // survivors continuously yield their excess area.
  const float sparse = clamp (sqrt (63.0 / max (organ.count, 1.0)), 1.0, 2.0);
  // Stations now subdivide continuous ribbons; adding a station does not add
  // optical area. Only absent boughs need compensation. Keeping width
  // invariant across station tiers also makes the shared vertices describe
  // exactly the same surface on both sides of a bundle boundary.
  organ.boost = min (sparse, 1.22) * sqrt (organ.resolution);
  organ.centre = organ.root;
  organ.radius_x = organ.crown;
  organ.radius_z = organ.crown;
  organ.half_height = 0.0;
  organ.bend = 0.55;
  organ.flutter = 0.38;
}

static inline void forest_configure_crown_lobe (thread ForestOrgan& organ,
                                                ForestPart part) {
  const float lobe = float (part.part - 1u);
  const float count = float (max (part.lod - 1u, 1u));
  const float turn =
    2.3999632 * lobe + 0.55 * forest_hash (organ.seed, part.part + 51u);
  const float ring = count > 2.0 ? mix (0.26, 0.58, fract (lobe * 0.61)) : 0.30;
  const float rise = 0.56 + 0.26 * forest_hash (organ.seed, part.part + 61u);
  organ.centre = organ.root + organ.up * rise * organ.tree_height +
                 (organ.across * cos (turn) + organ.forward * sin (turn)) *
                   organ.crown * ring;
  const float scale = count > 2.0 ? 0.37 : 0.68;
  organ.radius_x = organ.crown * scale *
                   (0.84 + 0.28 * forest_hash (organ.seed, part.part + 71u));
  organ.radius_z = organ.crown * scale *
                   (0.82 + 0.30 * forest_hash (organ.seed, part.part + 79u));
  organ.half_height = organ.crown * scale *
                      (0.78 + 0.38 * forest_hash (organ.seed, part.part + 83u));
  organ.bend = 0.40 + 0.48 * rise;
  organ.flutter = 0.34;
}

static inline ForestOrgan forest_organ (thread const MoppeForestInstance& tree,
                                        constant MoppeForestUniforms& u,
                                        ForestPart part,
                                        bool shadow,
                                        float stand_closure) {
  ForestOrgan organ = forest_base_organ (tree, u, part, shadow, stand_closure);
  if (organ.proxy)
    forest_configure_proxy (organ, part);
  else if (organ.wood)
    forest_configure_stem (organ, tree);
  else if (organ.frond)
    forest_configure_frond (organ, part);
  else
    forest_configure_crown_lobe (organ, part);
  return organ;
}

static inline float3 forest_palette (thread const MoppeForestInstance& tree,
                                     thread const ForestOrgan& organ,
                                     float exposure) {
  const float wet = tree.ecology.y;
  const float cover = tree.ecology.x;

  const float variation = forest_hash (tree.identity.x, 97u) - 0.5;
  if (organ.wood) {
    // These are display-space reflectances and will be decoded to linear in
    // the fragment stage. The old 0.17 spruce value became nearly zero after
    // that decode and made every shaded trunk an ink-black cylinder.
    float3 bark = organ.conifer ? float3 (0.395, 0.305, 0.220)
                                : float3 (0.420, 0.325, 0.230);
    bark *= 0.88 + 0.18 * wet + 0.16 * variation;
    return bark * (0.72 + 0.28 * exposure);
  }
  // Individuals sit on a warm-olive to cool blue-green axis in addition to
  // the brightness spread; a stand of one green reads as painted, not grown.
  const float hue =
    (forest_hash (tree.identity.x, 113u) - 0.5) * organ.ensemble;
  float3 leaf =
    organ.conifer
      ? moppe_forest_conifer_tint (wet, cover)
      : float3 (0.275, 0.455, 0.165) *
          float3 (1.08 - 0.20 * wet, 0.88 + 0.26 * wet, 0.90 + 0.16 * cover);
  leaf *= float3 (1.0 + 0.30 * hue, 1.0, 1.0 - 0.34 * hue);
  leaf *= 0.90 + 0.24 * variation * organ.ensemble;
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

static inline ForestPoint forest_stem_vertex (thread const ForestOrgan& organ,
                                              uint vertex_index) {
  const uint side = vertex_index % 12u;
  const uint ring = vertex_index / 12u;
  const float turn = 6.2831853 * float (side) / 12.0 +
                     0.06 * (forest_hash (organ.seed, side + 171u) - 0.5);
  const float taper = ring == 0u ? 1.70 : 0.30;
  const float girth =
    0.91 + 0.18 * forest_hash (organ.seed, side * 7u + ring + 177u);
  const float3 radial = organ.across * cos (turn) + organ.forward * sin (turn);

  ForestPoint point;
  point.position =
    organ.centre +
    organ.up * (ring == 0u ? -organ.half_height : organ.half_height) +
    radial * organ.radius_x * taper * girth;
  point.normal = radial;
  point.exposure = ring == 0u ? 0.18 : 0.72;
  return point;
}

static inline ForestPoint forest_crown_vertex (thread const ForestOrgan& organ,
                                               uint vertex_index) {
  ForestPoint point;
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
  const float jitter = organ.conifer ? 0.40 : 0.22;
  const float radius =
    forest_ring_radius (ring, organ.conifer, organ.proxy) *
    (1.0 - 0.5 * jitter +
     jitter * forest_hash (organ.seed, ring * 23u + side + 131u));
  const float level =
    forest_ring_level (ring, organ.conifer, organ.proxy) +
    (organ.conifer ? 0.10 : 0.07) *
      (forest_hash (organ.seed, ring * 29u + side + 151u) - 0.5);
  const float x = cos (turn) * organ.radius_x * radius;
  const float z = sin (turn) * organ.radius_z * radius;
  point.position = organ.centre + organ.across * x + organ.forward * z +
                   organ.up * organ.half_height * level;
  point.normal = normalize (organ.across * (x / max (organ.radius_x, 0.001)) +
                            organ.forward * (z / max (organ.radius_z, 0.001)) +
                            organ.up * level * 0.72);
  point.exposure = saturate (0.40 + 0.38 * level + 0.22 * point.normal.y);
  return point;
}

static inline ForestPoint
forest_conifer_parent_vertex (thread const ForestOrgan& organ,
                              uint vertex_index) {
  // One irregular tetrahedron carries each of the first twenty-one stable
  // bough axes. The refined representation evaluates the same rank through
  // forest_frond_vertex below, so the parent is a coarse error bound around an
  // existing organ rather than a different crown. Its cross section contracts
  // to zero as nested bough ribbons receive the represented area.
  constexpr uint bough_vertices = 21u * 4u;
  if (vertex_index >= bough_vertices) {
    const uint stem_vertex = vertex_index - bough_vertices;
    const uint stem_side = stem_vertex % 12u;
    const uint stem_ring = stem_vertex / 12u;
    const float stem_turn = 6.2831853 * float (stem_side) / 12.0;
    const float taper = stem_ring == 0u ? 1.70 : 0.30;
    const float3 radial =
      organ.across * cos (stem_turn) + organ.forward * sin (stem_turn);
    ForestPoint stem;
    stem.position =
      organ.root + organ.up * 0.46 * organ.tree_height +
      organ.up * (stem_ring == 0u ? -0.46 : 0.46) * organ.tree_height +
      radial * organ.stem_radius * taper * organ.residual;
    stem.normal = radial;
    stem.exposure = stem_ring == 0u ? 0.18 : 0.72;
    return stem;
  }

  const uint rank = vertex_index / 4u;
  const uint local = vertex_index % 4u;
  const uint slot = forest_bough_slot (rank);
  const uint whorl = slot / 7u;
  const uint spoke = slot % 7u;
  const float t = float (whorl) / 8.0;
  const float rise = mix (0.22, 0.97, t);
  const float turn =
    6.2831853 * (float (spoke) / 7.0 + 0.618034 * float (whorl) +
                 0.07 * (forest_hash (organ.seed, slot + 211u) - 0.5));
  const float3 along =
    normalize (organ.across * cos (turn) + organ.forward * sin (turn));
  const float3 side = normalize (cross (organ.up, along));
  const float reach = 0.72 + 0.50 * forest_hash (organ.seed, slot + 223u);
  const float length = organ.crown * mix (1.35, 0.18, t) * reach;
  const float3 origin = organ.root + organ.up * rise * organ.tree_height;
  const float3 tip = origin + along * length - organ.up * length * 0.30;
  const float3 base = origin + along * length * 0.10 + organ.up * length * 0.02;

  ForestPoint point;
  if (local == 0u) {
    point.position = tip;
    point.normal = normalize (along - organ.up * 0.22);
    point.exposure = saturate (0.38 + 0.48 * t);
    return point;
  }

  const float phase = 6.2831853 * forest_hash (organ.seed, slot * 13u + 271u);
  const float arc = phase + 6.2831853 * float (local - 1u) / 3.0;
  const float radius =
    length * mix (0.14, 0.09, t) * organ.residual *
    (0.88 + 0.24 * forest_hash (organ.seed, slot * 17u + local + 277u));
  const float3 radial = side * cos (arc) + organ.up * sin (arc);
  point.position = base + radial * radius;
  point.normal = normalize (-along * 0.18 + radial);
  point.exposure = saturate (0.30 + 0.44 * t + 0.16 * radial.y);
  return point;
}

static inline ForestPoint forest_frond_vertex (thread const ForestOrgan& organ,
                                               uint vertex_index) {
  ForestPoint point;
  // Each bough is a pair of crossed, continuous needle-mass ribbons. Four
  // vertices per station give finite support from the side, below, and above
  // without alpha testing or a camera-facing sheet. Refinement only
  // subdivides and bends these surfaces; it never inserts a disconnected
  // tetrahedron that could flash as a triangular shard. The
  // bough frame derives per vertex from the stable rank, which is what
  // lets a bundle pack several distant boughs into one meshlet. The same
  // four-triangle station is retained at every tier; only its nested sampling
  // density changes (Kuth 2025's pixels-per-triangle discipline at organ
  // scale).
  const uint per_tuft = 4u;
  const uint tufts = forest_bough_tufts (organ.bundle);
  const uint stride = tufts * per_tuft;
  const uint rank = organ.rank + vertex_index / stride;
  const uint rem = vertex_index % stride;
  const uint fan = rem / per_tuft;
  const uint local = rem % per_tuft;
  const uint slot = forest_bough_slot (rank);
  const uint whorl = slot / 7u;
  const uint spoke = slot % 7u;
  const float t = float (whorl) / 8.0;
  const float rise = mix (0.22, 0.97, t);
  const float turn =
    6.2831853 * (float (spoke) / 7.0 + 0.618034 * float (whorl) +
                 0.07 * (forest_hash (organ.seed, slot + 211u) - 0.5));
  const float3 along =
    normalize (organ.across * cos (turn) + organ.forward * sin (turn));
  const float3 side = normalize (cross (organ.up, along));
  const float reach = 0.72 + 0.50 * forest_hash (organ.seed, slot + 223u);
  // Large low boughs fade in over many ranks and small high ones over
  // few, so whatever arrives while the rider is close changes the crown
  // imperceptibly per frame. The floor complement never arrives -- those
  // boughs exist at every distance -- so it stands at full growth;
  // half-grown permanent boughs would leak crown mass at the far end.
  const float grow =
    rank < 21u
      ? 1.0
      : saturate ((organ.count - float (rank)) / (3.0 + 10.0 * (1.0 - t)));
  const float length = organ.crown * mix (1.35, 0.18, t) * reach * grow;
  const float3 origin = organ.root + organ.up * rise * organ.tree_height;
  // Every refinement is nested in one 32-sample domain. The far
  // carrier uses {0, 8, 16, 31}; the middle adds every fourth station and
  // retains endpoint 31; the hero fills all intervening stations. A survivor
  // never moves when its parent representation yields to finer work.
  const uint station = forest_bough_station (organ.bundle, fan);
  const float s = mix (0.06, 1.0, float (station) / 31.0);
  const bool new_station =
    organ.bundle == 2u
      ? station == 4u || station == 12u || station == 20u || station == 24u
    : organ.bundle == 1u ? station != 31u && station % 4u != 0u
                         : false;
  const uint sample = slot * 32u + station;
  // The axis droops in proportion to its reach, so long lower boughs
  // sweep down through the band beneath their whorl.
  const float droop = 0.10 - 0.40 * s * s;
  const float wobble = (station / 4u) % 2u == 0u ? 1.0 : -1.0;
  const float3 fine_axis_point =
    origin + along * length * s +
    side * length * 0.055 * wobble *
      (forest_hash (organ.seed, sample + 17u) - 0.5) +
    organ.up * length * droop;
  float3 axis_point = fine_axis_point;
  float radius = length * mix (0.028, 0.010, s) * organ.boost *
                 (0.82 + 0.36 * forest_hash (organ.seed, sample + 31u));
  if (new_station) {
    uint lower;
    uint upper;
    if (organ.bundle == 2u) {
      lower = station < 16u ? station - 4u : 16u;
      upper = station < 16u ? station + 4u : 31u;
    } else {
      lower = station < 24u ? (station / 4u) * 4u : 24u;
      upper = station < 24u ? lower + 4u : 31u;
    }
    const float lower_s = mix (0.06, 1.0, float (lower) / 31.0);
    const float upper_s = mix (0.06, 1.0, float (upper) / 31.0);
    const float lower_droop = 0.10 - 0.40 * lower_s * lower_s;
    const float upper_droop = 0.10 - 0.40 * upper_s * upper_s;
    const float lower_wobble = (lower / 4u) % 2u == 0u ? 1.0 : -1.0;
    const float upper_wobble = (upper / 4u) % 2u == 0u ? 1.0 : -1.0;
    const uint lower_sample = slot * 32u + lower;
    const uint upper_sample = slot * 32u + upper;
    const float3 lower_point =
      origin + along * length * lower_s +
      side * length * 0.055 * lower_wobble *
        (forest_hash (organ.seed, lower_sample + 17u) - 0.5) +
      organ.up * length * lower_droop;
    const float3 upper_point =
      origin + along * length * upper_s +
      side * length * 0.055 * upper_wobble *
        (forest_hash (organ.seed, upper_sample + 17u) - 0.5) +
      organ.up * length * upper_droop;
    const float interval =
      float (station - lower) / max (float (upper - lower), 1.0);
    const float coarse_radius =
      mix (length * mix (0.028, 0.010, lower_s) * organ.boost *
             (0.82 + 0.36 * forest_hash (organ.seed, lower_sample + 31u)),
           length * mix (0.028, 0.010, upper_s) * organ.boost *
             (0.82 + 0.36 * forest_hash (organ.seed, upper_sample + 31u)),
           interval);
    axis_point = mix (mix (lower_point, upper_point, interval),
                      fine_axis_point,
                      organ.refinement);
    radius = mix (coarse_radius, radius, organ.refinement);
  }
  const float skew = 0.22 * (forest_hash (organ.seed, slot + 41u) - 0.5);
  const float3 ribbon_width[2] = {
    normalize (side + organ.up * skew),
    normalize (organ.up - side * skew),
  };
  const uint ribbon = local / 2u;
  const float edge = local % 2u == 0u ? -1.0 : 1.0;
  const float3 tangent = normalize (along - organ.up * (0.18 + 0.42 * s));
  point.position = axis_point + ribbon_width[ribbon] * edge * radius;
  point.normal = normalize (cross (tangent, ribbon_width[ribbon]));
  point.exposure = saturate (0.38 + 0.46 * s + 0.10 * edge);
  return point;
}

static inline ForestPoint forest_vertex (thread const ForestOrgan& organ,
                                         uint vertex_index) {
  if (organ.wood)
    return forest_stem_vertex (organ, vertex_index);
  if (organ.frond)
    return forest_frond_vertex (organ, vertex_index);
  if (organ.conifer && organ.proxy)
    return forest_conifer_parent_vertex (organ, vertex_index);
  return forest_crown_vertex (organ, vertex_index);
}

template <typename Mesh>
static inline void forest_indices (thread Mesh& out,
                                   thread const ForestOrgan& organ,
                                   uint primitive) {
  uint3 triangle;
  if (organ.conifer && organ.proxy) {
    constexpr uint bough_primitives = 21u * 4u;
    if (primitive < bough_primitives) {
      const uint face = primitive % 4u;
      const uint base = (primitive / 4u) * 4u;
      triangle =
        face < 3u ? uint3 (base, base + 1u + face, base + 1u + (face + 1u) % 3u)
                  : uint3 (base + 1u, base + 2u, base + 3u);
    } else {
      const uint stem_primitive = primitive - bough_primitives;
      const uint side = stem_primitive / 2u;
      const uint next = (side + 1u) % 12u;
      constexpr uint base = 21u * 4u;
      triangle = stem_primitive % 2u == 0u
                   ? uint3 (base + side, base + next, base + 12u + next)
                   : uint3 (base + side, base + 12u + next, base + 12u + side);
    }
  } else if (organ.wood) {
    const uint side = primitive / 2u;
    const uint next = (side + 1u) % 12u;
    triangle = primitive % 2u == 0u ? uint3 (side, next, 12u + next)
                                    : uint3 (side, 12u + next, 12u + side);
  } else if (organ.frond) {
    const uint tufts = forest_bough_tufts (organ.bundle);
    constexpr uint per_verts = 4u;
    constexpr uint prims_per_segment = 4u;
    const uint prims_per_bough = (tufts - 1u) * prims_per_segment;
    const uint bough = primitive / prims_per_bough;
    const uint rem = primitive % prims_per_bough;
    const uint segment = rem / prims_per_segment;
    const uint ribbon = (rem % prims_per_segment) / 2u;
    const uint face = rem % 2u;
    const uint base =
      bough * tufts * per_verts + segment * per_verts + ribbon * 2u;
    const uint next = base + per_verts;
    triangle = face == 0u ? uint3 (base, next, next + 1u)
                          : uint3 (base, next + 1u, base + 1u);
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

struct ForestMeshCounts {
  uint vertices;
  uint primitives;
};

static inline ForestMeshCounts
forest_mesh_counts (thread const ForestOrgan& organ) {
  const uint tufts = organ.frond ? forest_bough_tufts (organ.bundle) : 0u;
  return {
    organ.conifer && organ.proxy ? 108u
    : organ.wood                 ? 24u
    : organ.frond                ? organ.bundle * tufts * 4u
                                 : 32u,
    organ.conifer && organ.proxy ? 108u
    : organ.wood                 ? 24u
    : organ.frond                ? organ.bundle * (tufts - 1u) * 4u
                                 : 60u,
  };
}

static inline ForestVaryings
forest_scene_vertex (thread const MoppeForestInstance& tree,
                     thread const ForestOrgan& organ,
                     uint vertex_index,
                     constant MoppeForestUniforms& u) {
  ForestPoint base = forest_vertex (organ, vertex_index);
  if (organ.individual < 0.999) {
    // A canopy does not make a distant tree uniformly shrink into its
    // midpoint. It occludes the organism from below, leaving the top as the
    // last stable identity above the stand roof. Wood has no such claim and
    // contracts into its root as crown identity transfers to the medium.
    const float3 anchor = organ.wood
                            ? organ.root
                            : organ.root + organ.up * 1.04 * organ.tree_height;
    base.position = mix (anchor, base.position, organ.individual);
  }
  const float rise = saturate (dot (base.position - organ.root, organ.up) /
                               max (organ.tree_height, 0.01));
  const float bend = organ.bend * rise * rise;
  const float flutter = organ.flutter * rise;
  const float3 current = moppe_wind (base.position, bend, flutter, u.params.x);
  const float3 previous =
    moppe_wind (base.position, bend, flutter, u.temporal.z);

  ForestVaryings result;
  result.position = u.view_proj * float4 (current, 1.0);
  result.world_pos = current;
  result.normal = base.normal;
  result.albedo = forest_palette (tree, organ, base.exposure);
  result.exposure = base.exposure;
  result.leaf = organ.wood ? 0.0 : 1.0;
  result.motion =
    moppe_motion_vector (u.unjittered_view_proj * float4 (current, 1.0),
                         u.previous_view_proj * float4 (previous, 1.0),
                         u.temporal.xy);
  return result;
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
  const ForestOrgan organ =
    forest_organ (tree, u, part, false, payload.stand_closure);
  const ForestMeshCounts counts = forest_mesh_counts (organ);
  if (thread_id == 0u)
    out.set_primitive_count (counts.primitives);

  if (thread_id < counts.vertices)
    out.set_vertex (thread_id, forest_scene_vertex (tree, organ, thread_id, u));
  if (thread_id < counts.primitives)
    forest_indices (out, organ, thread_id);
}

static inline float3
forest_shadow_crown_vertex (thread const ForestOrgan& crown,
                            uint vertex_index) {
  if (vertex_index == 0u || vertex_index == 11u)
    return crown.centre +
           crown.up * crown.half_height * (vertex_index == 0u ? -1.0 : 1.0);
  const uint side = vertex_index - 1u;
  const float turn = 6.2831853 * float (side) / 10.0;
  return crown.centre + crown.across * cos (turn) * crown.radius_x +
         crown.forward * sin (turn) * crown.radius_z;
}

static inline float3
forest_shadow_bough_vertex (thread const ForestOrgan& crown,
                            uint vertex_index) {
  const uint bough = vertex_index / 4u;
  const uint local = vertex_index % 4u;
  // The first nine visible bough ranks are exactly one stable bough in each
  // whorl. Reuse that organism sample here: a binary depth map cannot carry
  // foliage transmittance, but spatially separated samples retain its holes
  // without inventing a second crown construction for shadows.
  const uint slot = forest_bough_slot (bough);
  const uint whorl = slot / 7u;
  const uint spoke = slot % 7u;
  const float t = float (whorl) / 8.0;
  const float rise = mix (0.22, 0.97, t);
  const float turn =
    6.2831853 * (float (spoke) / 7.0 + 0.618034 * float (whorl) +
                 0.07 * (forest_hash (crown.seed, slot + 211u) - 0.5));
  const float3 along =
    normalize (crown.across * cos (turn) + crown.forward * sin (turn));
  const float3 side = normalize (cross (crown.up, along));
  const float reach = 0.72 + 0.50 * forest_hash (crown.seed, slot + 223u);
  const float length = crown.crown * mix (1.35, 0.18, t) * reach;
  const float half_width = length * mix (0.18, 0.24, t);
  const float3 origin = crown.root + crown.up * rise * crown.tree_height;
  const bool outer = local >= 2u;
  const float edge = local % 2u == 0u ? -1.0 : 1.0;
  const float3 axis = outer ? origin + along * length - crown.up * length * 0.28
                            : origin + along * length * 0.06;
  return axis + side * half_width * edge * (outer ? 1.0 : 0.32);
}

static inline float3 forest_shadow_stem_vertex (thread const ForestOrgan& stem,
                                                uint vertex_index) {
  const uint side = vertex_index % 8u;
  const uint ring = vertex_index / 8u;
  const float turn = 6.2831853 * float (side) / 8.0;
  const float taper = ring == 0u ? 1.70 : 0.30;
  const float3 radial = stem.across * cos (turn) + stem.forward * sin (turn);
  return stem.centre +
         stem.up * (ring == 0u ? -stem.half_height : stem.half_height) +
         radial * stem.radius_x * taper;
}

static inline uint3 forest_shadow_crown_triangle (uint primitive) {
  const uint side = primitive / 2u;
  const uint next = (side + 1u) % 10u;
  return primitive % 2u == 0u ? uint3 (0u, 1u + next, 1u + side)
                              : uint3 (11u, 1u + side, 1u + next);
}

static inline uint3 forest_shadow_bough_triangle (uint primitive) {
  const uint base = (primitive / 2u) * 4u;
  return primitive % 2u == 0u ? uint3 (base, base + 2u, base + 1u)
                              : uint3 (base + 1u, base + 2u, base + 3u);
}

static inline uint3 forest_shadow_stem_triangle (uint primitive,
                                                 uint vertex_offset) {
  const uint side = primitive / 2u;
  const uint next = (side + 1u) % 8u;
  return (primitive % 2u == 0u ? uint3 (side, next, 8u + next)
                               : uint3 (side, 8u + next, 8u + side)) +
         vertex_offset;
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
  const ForestOrgan organ = forest_organ (tree, u, part, world_map, -1.0);
  // A ground shadow reads as a shadow only when it can be attributed: the
  // sun-elongated trunk line attaches the crown's shade to its tree.
  ForestOrgan stem = organ;
  if (!world_map)
    stem = forest_organ (
      tree, u, { part.tree, 0u, 2u, part.copy, 0.0, 0.0 }, world_map, -1.0);
  constexpr uint boughs = 9u;
  constexpr uint bough_vertices = boughs * 4u;
  constexpr uint bough_primitives = boughs * 2u;
  constexpr uint stem_vertices = 16u;
  constexpr uint stem_primitives = 16u;
  const bool bough_shadow = !world_map && organ.conifer;
  const uint crown_vertices = bough_shadow ? bough_vertices : 12u;
  const uint crown_primitives = bough_shadow ? bough_primitives : 20u;
  const uint vertices = crown_vertices + (world_map ? 0u : stem_vertices);
  const uint primitives = crown_primitives + (world_map ? 0u : stem_primitives);
  if (thread_id == 0u)
    out.set_primitive_count (primitives);
  if (thread_id < vertices) {
    ForestShadowVaryings v;
    const float3 position =
      !bough_shadow
        ? thread_id < crown_vertices
            ? forest_shadow_crown_vertex (organ, thread_id)
            : forest_shadow_stem_vertex (stem, thread_id - crown_vertices)
      : thread_id < bough_vertices
        ? forest_shadow_bough_vertex (organ, thread_id)
        : forest_shadow_stem_vertex (stem, thread_id - bough_vertices);
    v.position = u.view_proj * float4 (position, 1.0);
    out.set_vertex (thread_id, v);
  }
  if (thread_id < primitives) {
    const uint3 triangle =
      !bough_shadow ? thread_id < crown_primitives
                        ? forest_shadow_crown_triangle (thread_id)
                        : forest_shadow_stem_triangle (
                            thread_id - crown_primitives, crown_vertices)
      : thread_id < bough_primitives
        ? forest_shadow_bough_triangle (thread_id)
        : forest_shadow_stem_triangle (thread_id - bough_primitives,
                                       bough_vertices);
    const uint slot = thread_id * 3u;
    out.set_index (slot + 0u, triangle.x);
    out.set_index (slot + 1u, triangle.y);
    out.set_index (slot + 2u, triangle.z);
  }
}

struct ForestFragmentFrame {
  float3 normal;
  float3 light;
  float3 to_eye;
  float distance;
  float fog;
  float visibility;
};

static inline float forest_fragment_visibility (float3 world_pos,
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

  // The coarse proxy gets several metres of light-depth margin, preventing
  // it from becoming precise self-occlusion inside the detailed crown.
  constexpr sampler shadow_smp (coord::normalized,
                                address::clamp_to_edge,
                                filter::linear,
                                compare_func::less_equal);
  const float margin = 9.0 / 1240.0;
  const float lit = shadow_map.sample_compare (
    shadow_smp, projection.xy, projection.z - margin);
  return visibility * mix (1.0, mix (0.22, 1.0, lit), u.shadow.x);
}

static inline ForestFragmentFrame
forest_fragment_frame (thread const ForestVaryings& in,
                       bool front_facing,
                       constant MoppeForestUniforms& u,
                       depth2d<float> shadow_map) {
  ForestFragmentFrame frame;
  frame.normal = normalize (front_facing ? in.normal : -in.normal);
  frame.light = normalize (u.sun_dir.xyz);
  frame.to_eye = u.camera_pos.xyz - in.world_pos;
  frame.distance = length (frame.to_eye);
  frame.fog =
    moppe_relief_haze (moppe_distance_fog (frame.distance, u.fog_color.w),
                       in.world_pos.y,
                       u.params.z,
                       u.params.w);
  frame.visibility =
    forest_fragment_visibility (in.world_pos, frame.light, u, shadow_map);
  return frame;
}

static inline float3 forest_fragment_albedo (thread const ForestVaryings& in,
                                             float3 normal) {
  float3 albedo = moppe_srgb (in.albedo);
  if (in.leaf >= 0.5)
    return albedo;

  // Bark fissures use two stretched world-space planes blended by facing.
  const float2 stretch = float2 (3.1, 0.33);
  const float2 px = float2 (in.world_pos.z, in.world_pos.y) * stretch;
  const float2 pz = float2 (in.world_pos.x, in.world_pos.y) * stretch;
  const float sx =
    0.65 * moppe_value_noise (px) + 0.35 * moppe_value_noise (px * 3.13);
  const float sz =
    0.65 * moppe_value_noise (pz) + 0.35 * moppe_value_noise (pz * 3.13);
  const float wx = normal.x * normal.x;
  const float wz = normal.z * normal.z;
  const float stria = (wx * sx + wz * sz) / max (wx + wz, 0.001);
  return albedo * (0.62 + 0.74 * stria);
}

struct ForestLeafTransmission {
  float amount;
  float toward;
  float resolvable;
};

static inline ForestLeafTransmission
forest_leaf_transmission (thread const ForestVaryings& in,
                          thread const ForestFragmentFrame& frame,
                          constant MoppeForestUniforms& u) {
  const float focal_pixels =
    moppe_vertical_focal_pixels (u.unjittered_view_proj, u.temporal.y);
  ForestLeafTransmission transmission = {
    0.0,
    0.0,
    smoothstep (1.5, 4.0, 0.3 * focal_pixels / max (frame.distance, 0.5)),
  };
  if (in.leaf <= 0.5)
    return transmission;

  const float back =
    mix (0.30,
         pow (max (dot (-frame.normal, frame.light), 0.0), 1.45),
         transmission.resolvable);
  const float thin = exp (-2.5 * (1.0 - in.exposure));
  transmission.amount = back * thin * frame.visibility;
  transmission.toward =
    saturate (dot (-frame.to_eye / max (frame.distance, 0.001), frame.light));
  return transmission;
}

static inline float3
forest_fragment_lighting (thread const ForestVaryings& in,
                          float3 albedo,
                          thread const ForestFragmentFrame& frame,
                          thread const ForestLeafTransmission& transmission,
                          constant MoppeForestUniforms& u) {
  if (in.leaf > 0.5 && transmission.resolvable < 0.999) {
    const float ensemble = 1.0 - transmission.resolvable;
    const float3 leaf_display = in.albedo;
    const float grain =
      mix (1.0,
           0.84 + 0.32 * moppe_value_noise (in.world_pos.xz * 0.17 +
                                            float2 (13.1, 4.7)),
           ensemble);
    const MoppeForestEnsembleLight limit =
      moppe_forest_distribution_light (leaf_display,
                                       frame.normal,
                                       frame.light,
                                       normalize (frame.to_eye),
                                       u.sun_diffuse.rgb,
                                       u.ambient.rgb,
                                       frame.visibility,
                                       1.0,
                                       grain);
    if (transmission.resolvable <= 0.001)
      return limit.radiance;

    // Settle the final crown-proxy shading into exactly the distribution
    // evaluated by the stand field before individual identity retires. This
    // is a matching condition, not a distance tint.
    const float wrap =
      saturate ((dot (frame.normal, frame.light) + 0.26) / 1.26);
    const float3 resolved =
      albedo * (moppe_hemisphere_light (u.ambient.rgb, frame.normal) *
                  (0.62 + 0.38 * in.exposure) +
                u.sun_diffuse.rgb * wrap * frame.visibility *
                  (1.0 - 0.45 * transmission.amount));
    return mix (limit.radiance, resolved, transmission.resolvable);
  }

  const float wrap = saturate ((dot (frame.normal, frame.light) + 0.26) / 1.26);
  float3 color =
    albedo * (moppe_hemisphere_light (u.ambient.rgb, frame.normal) *
                (0.62 + 0.38 * in.exposure) +
              u.sun_diffuse.rgb * wrap * frame.visibility *
                (1.0 - 0.45 * transmission.amount));
  color +=
    albedo * float3 (0.10, 0.16, 0.20) * (0.35 + 0.65 * in.exposure) * in.leaf;
  if (in.leaf <= 0.5) {
    // Bark retains a warm, low-frequency bounce under the crown. Its direct
    // albedo is decoded to linear above, so omitting this term collapsed
    // shaded trunks to nearly zero even while the surrounding floor remained
    // readable.
    color += sqrt (albedo) * u.ambient.rgb * float3 (0.24, 0.18, 0.12);
    return color;
  }

  const float3 chlorophyll (0.92, 1.0, 0.24);
  const float lobe = 0.20 + 0.80 * transmission.toward * transmission.toward *
                              transmission.toward;
  color += sqrt (albedo) * u.sun_diffuse.rgb * chlorophyll *
           transmission.amount * lobe * 4.0;
  const float3 half_vector = normalize (frame.light + normalize (frame.to_eye));
  color += u.sun_specular.rgb *
           pow (max (dot (frame.normal, half_vector), 0.0), 18.0) *
           frame.visibility * 0.055;
  return color;
}

fragment MoppeTemporalOutput forest_fragment (ForestVaryings in [[stage_in]],
                                              bool front_facing
                                              [[front_facing]],
                                              constant MoppeForestUniforms& u
                                              [[buffer (MOPPE_BUF_FRAME)]],
                                              depth2d<float> shadow_map
                                              [[texture (MOPPE_TEX_SHADOW)]]) {
  const ForestFragmentFrame frame =
    forest_fragment_frame (in, front_facing, u, shadow_map);
  const float3 albedo = forest_fragment_albedo (in, frame.normal);
  const ForestLeafTransmission transmission =
    forest_leaf_transmission (in, frame, u);
  float3 color = forest_fragment_lighting (in, albedo, frame, transmission, u);
  const float3 fog_color = moppe_warmed_fog (
    u.fog_color.rgb, -frame.to_eye / max (frame.distance, 0.001), frame.light);
  color = mix (color, fog_color, smoothstep (0.0, 0.92, frame.fog));
  return moppe_temporal_output (
    float4 (color, 1.0),
    in.motion,
    in.leaf > 0.5 ? 0.48 + 0.30 * transmission.amount * transmission.toward *
                             transmission.resolvable
                  : 0.12);
}
