// Trunk forest: a deliberately simple tree. Each individual is one tapered
// trunk and a few faceted crown masses -- stacked cones for a conifer, lobes
// for a broadleaf -- generated in one meshlet from its retained record. The
// trunk is the element seen at riding height; in a closed stand the crown
// starts high, so the forest is walked through as a hall of columns.

#include "forest_medium.h"

struct TrunkVaryings {
  float4 position [[position]];
  float3 world_pos;
  float3 normal;
  float3 albedo;
  float2 bark;        // x=around the trunk in metres, y=height in metres
  float crown_height; // 0 at the crown's base, 1 at its top
  float foliage [[flat]];
  float conifer [[flat]];
  float2 motion [[center_no_perspective]];
};

using TrunkMesh = metal::mesh<TrunkVaryings,
                              void,
                              MOPPE_FOREST_TRUNK_MESH_VERTICES,
                              MOPPE_FOREST_TRUNK_MESH_PRIMITIVES,
                              metal::topology::triangle>;

struct TrunkShadowVaryings {
  float4 position [[position]];
};

using TrunkShadowMesh = metal::mesh<TrunkShadowVaryings,
                                    void,
                                    MOPPE_FOREST_TRUNK_MESH_VERTICES,
                                    MOPPE_FOREST_TRUNK_MESH_PRIMITIVES,
                                    metal::topology::triangle>;

struct TrunkShadowPayload {
  uint count;
  uint tree[MOPPE_FOREST_OBJECT_THREADS];
  uint copy[MOPPE_FOREST_OBJECT_THREADS];
};

// ---- the tree ------------------------------------------------------

struct TrunkTree {
  float3 root;
  float3 up;
  float3 right;
  float3 forward;
  float height;
  float crown_base; // metres above the root
  float crown_radius;
  float trunk_radius;
  float seed_turn;
  uint seed;
  bool conifer;
  uint sides;       // trunk facets
  uint crown_sides; // crown facets
  uint masses;      // cones or lobes
};

static inline float trunk_hash (uint seed, uint lane) {
  return moppe_forest_hash (seed, lane);
}

static inline float3 trunk_root (thread const MoppeForestInstance& tree,
                                 constant MoppeForestUniforms& u,
                                 uint copy,
                                 bool tiled) {
  float3 root = tree.root_height.xyz;
  const float2 period = u.world.xy;
  if (tiled) {
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

// A tree grows toward the light, not along the slope: the axis is nearly
// vertical with a small individual lean. Closure decides where the crown
// starts. An open-grown tree keeps its branches to the ground; in a closed
// stand the shaded lower branches have died away and the crown begins
// halfway up a long clear trunk.
static inline TrunkTree
trunk_tree (thread const MoppeForestInstance& tree, float3 root, float pixels) {
  TrunkTree t;
  t.seed = tree.identity.x;
  t.conifer = tree.identity.y == 1u;
  t.root = root;
  const float lean_turn = 6.2831853 * trunk_hash (t.seed, 3u);
  const float lean = 0.035 * trunk_hash (t.seed, 4u);
  t.up = normalize (float3 (0.0, 1.0, 0.0) +
                    0.08 * float3 (tree.up_radius.x, 0.0, tree.up_radius.z) +
                    lean * float3 (cos (lean_turn), 0.0, sin (lean_turn)));
  t.right = normalize (cross (t.up, float3 (0.0, 0.0, 1.0)));
  t.forward = cross (t.right, t.up);
  t.height = tree.root_height.w;
  t.crown_radius = tree.up_radius.w;
  const float closure = smoothstep (MOPPE_FOREST_STAND_OPEN_CLOSURE,
                                    MOPPE_FOREST_STAND_CLOSED_CLOSURE,
                                    tree.ecology.z);
  const float base_share =
    mix (t.conifer ? 0.04 : 0.22, t.conifer ? 0.20 : 0.58, closure) +
    0.08 * (trunk_hash (t.seed, 5u) - 0.5);
  t.crown_base = t.height * base_share;
  t.trunk_radius = t.height * (t.conifer ? 0.0078 : 0.0095) *
                   (0.85 + 0.3 * trunk_hash (t.seed, 6u));
  t.seed_turn = 6.2831853 * trunk_hash (t.seed, 7u);
  t.sides = pixels > 90.0 ? 10u : pixels > 30.0 ? 7u : 5u;
  t.crown_sides = pixels > 90.0 ? 9u : pixels > 30.0 ? 7u : 5u;
  t.masses = 3u;
  return t;
}

static inline uint trunk_vertex_count (thread const TrunkTree& t) {
  return t.sides * 4u;
}
static inline uint trunk_primitive_count (thread const TrunkTree& t) {
  return t.sides * 6u;
}
// A cone is a ring, an apex, and a centre closing it from below; a lobe is
// two rings between a top and a bottom apex.
static inline uint mass_vertex_count (thread const TrunkTree& t) {
  return t.conifer ? t.crown_sides + 2u : 2u * t.crown_sides + 2u;
}
static inline uint mass_primitive_count (thread const TrunkTree& t) {
  return t.conifer ? 2u * t.crown_sides : 4u * t.crown_sides;
}
static inline uint tree_vertex_count (thread const TrunkTree& t) {
  return trunk_vertex_count (t) + t.masses * mass_vertex_count (t);
}
static inline uint tree_primitive_count (thread const TrunkTree& t) {
  return trunk_primitive_count (t) + t.masses * mass_primitive_count (t);
}

static inline float3 tree_point (thread const TrunkTree& t,
                                 float along,
                                 float3 centre,
                                 float radius,
                                 float turn) {
  return centre + t.up * along +
         radius * (t.right * cos (turn) + t.forward * sin (turn));
}

struct TreeVertex {
  float3 position;
  float3 normal;
  float2 bark;
  float crown_height;
  bool foliage;
  uint mass;
};

static inline TreeVertex tree_vertex (thread const TrunkTree& t, uint index) {
  TreeVertex v;
  v.mass = 0u;
  const uint trunk_vertices = trunk_vertex_count (t);
  if (index < trunk_vertices) {
    // Rings: flared root, breast height, crown base, tip.
    const uint ring = index / t.sides;
    const uint side = index % t.sides;
    const float turn = t.seed_turn + 6.2831853 * float (side) / float (t.sides);
    const float top = t.conifer
                        ? 0.94 * t.height
                        : t.crown_base + 0.45 * (t.height - t.crown_base);
    const float heights[4] = { 0.0, 1.3, t.crown_base, top };
    const float widths[4] = { 1.40, 1.0, 0.72, 0.14 };
    const float along = heights[ring];
    const float radius = t.trunk_radius * widths[ring];
    v.position = tree_point (t, along, t.root, radius, turn);
    v.normal = t.right * cos (turn) + t.forward * sin (turn);
    v.bark = float2 (turn * t.trunk_radius, along);
    v.crown_height = 0.0;
    v.foliage = false;
    return v;
  }
  const uint local = index - trunk_vertices;
  const uint per_mass = mass_vertex_count (t);
  const uint mass = local / per_mass;
  const uint corner = local % per_mass;
  v.mass = mass;
  v.foliage = true;
  v.bark = float2 (0.0);
  const float span = t.height - t.crown_base;
  const float n = float (t.crown_sides);
  const float twist = t.seed_turn + 1.7 * float (mass);
  if (t.conifer) {
    // Three overlapping tiers, narrowing upward like a spruce.
    const float bases[3] = { 0.0, 0.30, 0.56 };
    const float tops[3] = { 0.58, 0.82, 1.0 };
    const float radii[3] = { 1.0, 0.74, 0.46 };
    const float jitter = 0.08 * (trunk_hash (t.seed, 20u + mass) - 0.5);
    const float base = t.crown_base + span * (bases[mass] + jitter);
    const float top = t.crown_base + span * tops[mass];
    const float radius = t.crown_radius * radii[mass] *
                         (0.9 + 0.2 * trunk_hash (t.seed, 30u + mass));
    float along;
    if (corner < t.crown_sides) {
      const float turn = twist + 6.2831853 * float (corner) / n;
      // Alternate ring corners droop a little, so the skirt is not a disc.
      along = base - (corner & 1u ? 0.06 : 0.0) * span;
      v.position = tree_point (t, along, t.root, radius, turn);
    } else if (corner == t.crown_sides) {
      along = top;
      v.position = t.root + t.up * along;
    } else {
      along = base + 0.12 * span;
      v.position = t.root + t.up * along;
    }
    v.crown_height = saturate ((along - t.crown_base) / max (span, 0.01));
  } else {
    // Lobes clustered around the upper crown.
    const float3 offsets[3] = { float3 (0.0, 0.62, 0.0),
                                float3 (0.42, 0.40, 0.18),
                                float3 (-0.36, 0.44, -0.30) };
    const float sizes[3] = { 0.80, 0.62, 0.58 };
    const float3 o = offsets[mass];
    const float3 centre = t.root + t.up * (t.crown_base + span * o.y) +
                          t.crown_radius * (t.right * o.x + t.forward * o.z) *
                            (0.8 + 0.4 * trunk_hash (t.seed, 40u + mass));
    const float radius = t.crown_radius * sizes[mass];
    const float half_height = 0.62 * radius;
    float along;
    if (corner < 2u * t.crown_sides) {
      const uint ring = corner / t.crown_sides;
      const uint side = corner % t.crown_sides;
      const float turn =
        twist + 6.2831853 * (float (side) + 0.5 * float (ring)) / n;
      along = (ring == 0u ? -0.42 : 0.38) * half_height;
      v.position = tree_point (
        t, along, centre, radius * (ring == 0u ? 0.92 : 0.84), turn);
    } else {
      along = (corner == 2u * t.crown_sides ? 1.0 : -0.9) * half_height;
      v.position = centre + t.up * along;
    }
    v.crown_height = saturate (
      (dot (v.position - t.root, t.up) - t.crown_base) / max (span, 0.01));
  }
  v.normal =
    normalize (v.position - (t.root + t.up * (t.crown_base + 0.5 * span)));
  return v;
}

static inline uint3 tree_triangle (thread const TrunkTree& t, uint primitive) {
  const uint trunk_primitives = trunk_primitive_count (t);
  if (primitive < trunk_primitives) {
    const uint band = primitive / (2u * t.sides);
    const uint quad = (primitive / 2u) % t.sides;
    const uint next = (quad + 1u) % t.sides;
    const uint a = band * t.sides + quad, b = band * t.sides + next;
    const uint c = a + t.sides, d = b + t.sides;
    return primitive & 1u ? uint3 (b, d, c) : uint3 (a, b, c);
  }
  const uint local = primitive - trunk_primitives;
  const uint per_mass = mass_primitive_count (t);
  const uint mass = local / per_mass;
  const uint p = local % per_mass;
  const uint first = trunk_vertex_count (t) + mass * mass_vertex_count (t);
  const uint n = t.crown_sides;
  if (t.conifer) {
    const uint side = p % n;
    const uint next = (side + 1u) % n;
    const uint centre = p < n ? n : n + 1u; // apex, then the closing centre
    return uint3 (first + side, first + next, first + centre);
  }
  const uint side = p % n;
  const uint next = (side + 1u) % n;
  const uint lower = first, upper = first + n;
  const uint top = first + 2u * n, bottom = top + 1u;
  switch (p / n) {
  case 0u:
    return uint3 (upper + side, upper + next, top);
  case 1u:
    return uint3 (lower + side, lower + next, bottom);
  case 2u:
    return uint3 (lower + side, lower + next, upper + side);
  default:
    return uint3 (lower + next, upper + next, upper + side);
  }
}

// Wind sways the crown about its base; the trunk only bends near the top.
static inline float3
trunk_sway (thread const TrunkTree& t, float3 p, bool foliage, float time) {
  const float rise = saturate (dot (p - t.root, t.up) / max (t.height, 0.01));
  return moppe_wind (p, 0.30 * rise * rise, foliage ? 0.25 * rise : 0.0, time) -
         p;
}

// A retiring individual in a closed stand gives its identity to the stand
// canopy: it contracts toward its crown top while the canopy takes over.
static inline float trunk_individual (thread const MoppeForestInstance& tree,
                                      float crown_pixels) {
  const float transfer =
    smoothstep (MOPPE_FOREST_STAND_OPEN_CLOSURE,
                MOPPE_FOREST_STAND_CLOSED_CLOSURE,
                tree.ecology.z) *
    (1.0 - smoothstep (MOPPE_FOREST_TRANSFER_END_CROWN_PIXELS,
                       MOPPE_FOREST_TRANSFER_START_CROWN_PIXELS,
                       crown_pixels));
  return 1.0 - transfer;
}

static inline float3 trunk_palette (thread const TrunkTree& t,
                                    thread const MoppeForestInstance& tree,
                                    bool foliage,
                                    uint mass) {
  const float moisture = tree.ecology.y;
  if (!foliage)
    return t.conifer ? moppe_srgb (float3 (0.38, 0.30, 0.24))
                     : moppe_srgb (float3 (0.80, 0.78, 0.72));
  const float hue = trunk_hash (t.seed, 50u) - 0.5;
  const float shade =
    0.92 + 0.10 * float (mass) - 0.06 * trunk_hash (t.seed, 51u);
  const float3 needle =
    float3 (0.20 + 0.05 * hue, 0.36 + 0.06 * moisture, 0.22);
  const float3 leaf = float3 (0.46 + 0.10 * hue, 0.58 + 0.05 * moisture, 0.22);
  return moppe_srgb ((t.conifer ? needle : leaf) * shade);
}

// ---- the scene stage -----------------------------------------------

[[mesh]] void forest_trunks_mesh (
  TrunkMesh out,
  uint mesh_id [[threadgroup_position_in_grid]],
  uint thread_id [[thread_index_in_threadgroup]],
  constant MoppeForestUniforms& u [[buffer (MOPPE_BUF_FRAME)]],
  device const MoppeForestInstance* trees [[buffer (MOPPE_BUF_FOREST)]],
  device const MoppeForestCandidate* candidates [[buffer (MOPPE_BUF_DRAW)]]) {
  const MoppeForestCandidate candidate = candidates[mesh_id];
  const MoppeForestInstance tree = trees[candidate.tree];
  const TrunkTree t =
    trunk_tree (tree, trunk_root (tree, u, 4u, false), candidate.pixels);
  const uint vertices = tree_vertex_count (t);
  const uint primitives = tree_primitive_count (t);
  if (thread_id == 0u)
    out.set_primitive_count (primitives);

  if (thread_id < vertices) {
    const TreeVertex v = tree_vertex (t, thread_id);
    const float individual = trunk_individual (tree, candidate.crown_pixels);
    const float3 anchor = v.foliage ? t.root + t.up * t.height : t.root;
    const float3 rest = mix (anchor, v.position, individual);
    const float3 current = rest + trunk_sway (t, rest, v.foliage, u.params.x);
    const float3 previous =
      rest + trunk_sway (t, rest, v.foliage, u.temporal.z);
    TrunkVaryings o;
    o.position = u.view_proj * float4 (current, 1.0);
    o.world_pos = current;
    o.normal = v.normal;
    o.albedo = trunk_palette (t, tree, v.foliage, v.mass);
    o.bark = v.bark;
    o.crown_height = v.crown_height;
    o.foliage = v.foliage ? 1.0 : 0.0;
    o.conifer = t.conifer ? 1.0 : 0.0;
    o.motion =
      moppe_motion_vector (u.unjittered_view_proj * float4 (current, 1.0),
                           u.previous_view_proj * float4 (previous, 1.0),
                           u.temporal.xy);
    out.set_vertex (thread_id, o);
  }
  if (thread_id < primitives) {
    const uint3 tri = tree_triangle (t, thread_id);
    out.set_index (thread_id * 3u + 0u, tri.x);
    out.set_index (thread_id * 3u + 1u, tri.y);
    out.set_index (thread_id * 3u + 2u, tri.z);
  }
}

static inline float trunk_visibility (float3 world_pos,
                                      float3 light,
                                      bool foliage,
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
  // Crowns keep several metres of light-depth margin, so a crown does not
  // shadow itself solid; trunks stay precise.
  const float margin = (foliage ? 7.0 : 1.5) / 1240.0;
  float lit = 0.0;
  for (float dy = -0.5; dy <= 0.5; dy += 1.0)
    for (float dx = -0.5; dx <= 0.5; dx += 1.0)
      lit += 0.25 * shadow_map.sample_compare (shadow_smp,
                                               projection.xy +
                                                 float2 (dx, dy) * u.shadow.y,
                                               projection.z - margin);
  return visibility * mix (1.0, mix (0.18, 1.0, lit), u.shadow.x);
}

// Bark is a few octaves of value noise stretched along the trunk: deep
// vertical furrows for a pine, dark horizontal lenticels on pale birch.
static inline float3 trunk_bark (float3 albedo, float2 bark, bool conifer) {
  if (conifer) {
    const float furrows =
      moppe_value_noise (float2 (bark.x * 9.0, bark.y * 0.9));
    const float plates =
      moppe_value_noise (float2 (bark.x * 3.0, bark.y * 0.35));
    return albedo * (0.62 + 0.45 * furrows) * (0.85 + 0.3 * plates);
  }
  const float marks = moppe_value_noise (float2 (bark.x * 3.5, bark.y * 5.0));
  const float lenticel = smoothstep (0.70, 0.82, marks);
  const float scar = smoothstep (
    0.80, 0.92, moppe_value_noise (float2 (bark.x * 1.2, bark.y * 0.6)));
  return albedo * (1.0 - 0.80 * max (lenticel, scar)) *
         (0.92 + 0.08 * moppe_value_noise (bark * 12.0));
}

fragment MoppeTemporalOutput forest_trunks_fragment (
  TrunkVaryings in [[stage_in]],
  constant MoppeForestUniforms& u [[buffer (MOPPE_BUF_FRAME)]],
  depth2d<float> shadow_map [[texture (MOPPE_TEX_SHADOW)]]) {
  const bool foliage = in.foliage > 0.5;
  const float3 to_eye = u.camera_pos.xyz - in.world_pos;
  const float distance = length (to_eye);
  const float3 view = to_eye / max (distance, 1e-3);
  const float3 light = u.sun_dir.xyz;

  // Crowns are faceted: one normal per triangle, taken from the screen-space
  // derivatives of the surface itself and turned toward the eye.
  float3 n;
  if (foliage) {
    n = normalize (cross (dfdx (in.world_pos), dfdy (in.world_pos)));
    if (dot (n, view) < 0.0)
      n = -n;
  } else {
    n = normalize (in.normal);
  }

  float3 albedo = in.albedo;
  if (!foliage)
    albedo = trunk_bark (albedo, in.bark, in.conifer > 0.5);

  const float visibility =
    trunk_visibility (in.world_pos, light, foliage, u, shadow_map);
  // Foliage wraps the light a little: a crown is porous, not a solid shell.
  const float sun = foliage ? saturate ((dot (n, light) + 0.35) / 1.35)
                            : saturate (dot (n, light));
  // Inner and lower crown faces see less sky; so does the shaded trunk.
  const float occlusion = foliage ? mix (0.72, 1.0, in.crown_height) : 0.80;
  float3 color =
    albedo * (sun * visibility * 0.95 * u.sun_diffuse.rgb +
              occlusion * moppe_hemisphere_light (u.ambient.rgb, n));
  // Seen against the sun, needles and leaves glow with transmitted light.
  if (foliage)
    color += albedo * u.sun_diffuse.rgb * visibility * 0.35 *
             pow (saturate (dot (-view, light)), 4.0);

  const float fog =
    moppe_relief_haze (moppe_distance_fog (distance, u.fog_color.w),
                       in.world_pos.y,
                       u.params.z,
                       u.params.w);
  const float3 fog_color = moppe_warmed_fog (u.fog_color.rgb, -view, light);
  color = mix (color, fog_color, smoothstep (0.0, 0.92, fog));
  return moppe_temporal_output (
    float4 (color, 1.0), in.motion, foliage ? 0.30 : 0.10);
}

// ---- the shadow stages ---------------------------------------------

[[object]] void forest_trunks_shadow_object (
  object_data TrunkShadowPayload& payload [[payload]],
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
    const uint index = candidate % tree_count;
    const uint copy = local ? 4u : candidate / tree_count;
    const MoppeForestInstance tree = trees[index];
    const float3 root = trunk_root (tree, u, copy, !local);
    const float3 centre = root + float3 (0.0, 0.55 * tree.root_height.w, 0.0);
    const float4 clip = u.view_proj * float4 (centre, 1.0);
    const float radius = max (tree.up_radius.w, 0.55 * tree.root_height.w);
    const float2 clip_radius = radius * moppe_projection_scale (u.view_proj);
    if (clip.w > -radius && abs (clip.x) < clip.w + clip_radius.x &&
        abs (clip.y) < clip.w + clip_radius.y) {
      const uint slot =
        atomic_fetch_add_explicit (&emitted, 1u, metal::memory_order_relaxed);
      payload.tree[slot] = index;
      payload.copy[slot] = copy;
    }
  }
  threadgroup_barrier (metal::mem_flags::mem_threadgroup);
  if (thread_id == 0u) {
    payload.count =
      atomic_load_explicit (&emitted, metal::memory_order_relaxed);
    mesh_grid.set_threadgroups_per_grid (uint3 (payload.count, 1, 1));
  }
}

[[mesh]] void forest_trunks_shadow_mesh (
  TrunkShadowMesh out,
  object_data const TrunkShadowPayload& payload [[payload]],
  uint mesh_id [[threadgroup_position_in_grid]],
  uint thread_id [[thread_index_in_threadgroup]],
  constant MoppeForestUniforms& u [[buffer (MOPPE_BUF_FRAME)]],
  device const MoppeForestInstance* trees [[buffer (MOPPE_BUF_FOREST)]]) {
  const MoppeForestInstance tree = trees[payload.tree[mesh_id]];
  const bool local = u.world.w > 0.5;
  // The coarsest facets: shadow texels are already larger than a facet. A
  // crown is porous, so its shadow is a smaller solid core: sun falls
  // between neighbouring cores and dapples the floor of a closed stand.
  TrunkTree t =
    trunk_tree (tree, trunk_root (tree, u, payload.copy[mesh_id], !local), 0.0);
  t.crown_radius *= 0.62;
  const uint vertices = tree_vertex_count (t);
  const uint primitives = tree_primitive_count (t);
  if (thread_id == 0u)
    out.set_primitive_count (primitives);
  if (thread_id < vertices) {
    TrunkShadowVaryings o;
    o.position =
      u.view_proj * float4 (tree_vertex (t, thread_id).position, 1.0);
    out.set_vertex (thread_id, o);
  }
  if (thread_id < primitives) {
    const uint3 tri = tree_triangle (t, thread_id);
    out.set_index (thread_id * 3u + 0u, tri.x);
    out.set_index (thread_id * 3u + 1u, tri.y);
    out.set_index (thread_id * 3u + 2u, tri.z);
  }
}
