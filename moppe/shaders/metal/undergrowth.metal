// Ferns and low shrubs on the forest floor, generated rather than stored.
//
// Nothing here is a mesh. The object stage walks a window of ground tiles
// around the camera and keeps the ones the world's own fields say something
// grows on -- shade under a canopy, water in the soil, no trail worn across
// it, ground the plant could stand on. The mesh stage turns each surviving
// tile into plants: a hash decides where each one sits and what it is, the
// height and normal textures root it on the terrain by construction, and the
// same gust function the trees use moves it. So undergrowth costs no memory,
// cannot drift out of step with the ground it grows on, and can change its
// count and its shape every frame, because nothing is kept to go stale.

#include "common.h"

// A plant is four sprays -- fronds of a fern, leafy shoots of a shrub -- and
// a spray is one arched strip of six cross-sections. Six is what buys the
// outline: a frond's edge is lobed, not smooth, and one whole meshlet of
// smooth-edged leaves reads as plastic cutlery no matter what colour it is.
#define UNDERGROWTH_SPRAY_SECTIONS 6
#define UNDERGROWTH_SPRAY_VERTICES (UNDERGROWTH_SPRAY_SECTIONS * 2)
#define UNDERGROWTH_SPRAY_PRIMITIVES ((UNDERGROWTH_SPRAY_SECTIONS - 1) * 2)
#define UNDERGROWTH_SPRAYS_PER_PLANT 4
#define UNDERGROWTH_PLANTS_PER_TILE 5
#define UNDERGROWTH_TILE_THREADS                                               \
  (UNDERGROWTH_PLANTS_PER_TILE * UNDERGROWTH_SPRAYS_PER_PLANT)
#define UNDERGROWTH_OBJECT_THREADS 64

struct UndergrowthVaryings {
  float4 position [[position]];
  float3 world_pos;
  float3 normal;
  float3 color;
  float exposure;
};

struct UndergrowthTile {
  uint2 index;
  uint plants;
};

struct UndergrowthPayload {
  uint count;
  UndergrowthTile tiles[UNDERGROWTH_OBJECT_THREADS];
};

using UndergrowthMesh =
  metal::mesh<UndergrowthVaryings,
              void,
              UNDERGROWTH_TILE_THREADS * UNDERGROWTH_SPRAY_VERTICES,
              UNDERGROWTH_TILE_THREADS * UNDERGROWTH_SPRAY_PRIMITIVES,
              metal::topology::triangle>;

// ---- reading the world ---------------------------------------------

static inline float undergrowth_hash (uint2 cell, uint lane) {
  uint value = cell.x * 0x9e3779b9u ^ cell.y * 0x85ebca6bu ^ lane * 0xc2b2ae35u;
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  value ^= value >> 16;
  return float (value & 0x00ffffffu) / float (0x01000000u);
}

static inline float2
undergrowth_field_uv (float2 world_xz, constant MoppeUndergrowthUniforms& u) {
  return float2 (world_xz.x * u.lattice.x, world_xz.y * u.lattice.y) /
         u.lattice.w;
}

static inline float4 undergrowth_field (float2 world_xz,
                                        constant MoppeUndergrowthUniforms& u,
                                        texture2d<float> field) {
  constexpr sampler smp (coord::normalized, address::repeat, filter::linear);
  return field.sample (smp, undergrowth_field_uv (world_xz, u));
}

static inline float
undergrowth_ground (float2 world_xz,
                    constant MoppeUndergrowthUniforms& u,
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

static inline float3
undergrowth_ground_normal (float2 world_xz,
                           constant MoppeUndergrowthUniforms& u,
                           texture2d<float> normals) {
  const float2 packed = undergrowth_field (world_xz, u, normals).rg;
  return normalize (
    float3 (packed.x, sqrt (saturate (1.0 - dot (packed, packed))), packed.y));
}

// How much undergrowth one patch of ground carries. Shade is the whole
// argument: a fern lives where a canopy already stands, so the same forest
// field that draws the trees decides what grows beneath them. Soil water
// sets how lush it gets, a worn trail clears it -- ground people ride over
// does not keep its ferns -- and a slope past what a rosette can hold sheds
// it entirely.
static inline float undergrowth_density (float2 world_xz,
                                         constant MoppeUndergrowthUniforms& u,
                                         texture2d<float> forest,
                                         texture2d<float> moisture,
                                         texture2d<float> paths,
                                         float3 ground_normal) {
  const float canopy = saturate (undergrowth_field (world_xz, u, forest).r);
  const float wet = saturate (undergrowth_field (world_xz, u, moisture).r);
  const float2 worn = saturate (undergrowth_field (world_xz, u, paths).rg);
  // Broad clumping so the floor is patchy rather than evenly furred: real
  // understory grows in stands with open leaf litter between them.
  const float clump =
    moppe_value_noise (world_xz * 0.085) * 0.55 +
    moppe_value_noise (world_xz * 0.021 + float2 (17.3, 4.1)) * 0.45;
  const float shade = smoothstep (0.06, 0.55, canopy);
  const float damp = 0.34 + 0.66 * smoothstep (0.02, 0.48, wet);
  const float standable = smoothstep (0.52, 0.78, ground_normal.y);
  const float cleared = 1.0 - saturate (max (worn.x, worn.y) * 1.6);
  return saturate ((0.34 + 1.45 * shade) * damp * standable * cleared *
                   smoothstep (0.20, 0.60, clump) * u.params.w);
}

// ---- the object stage: which tiles are worth a threadgroup ---------

[[object]] void undergrowth_object (
  object_data UndergrowthPayload& payload [[payload]],
  metal::mesh_grid_properties mesh_grid,
  uint thread_id [[thread_index_in_threadgroup]],
  uint3 grid_pos [[thread_position_in_grid]],
  constant MoppeUndergrowthUniforms& u [[buffer (MOPPE_BUF_FRAME)]],
  texture2d<float, access::read> heights [[texture (MOPPE_TEX_HEIGHTS)]],
  texture2d<float> normals [[texture (MOPPE_TEX_TERRAIN_NORMALS)]],
  texture2d<float> forest [[texture (MOPPE_TEX_TERRAIN_FOREST)]],
  texture2d<float> moisture [[texture (MOPPE_TEX_TERRAIN_MOISTURE)]],
  texture2d<float> paths [[texture (MOPPE_TEX_TERRAIN_PATHS)]]) {
  threadgroup atomic_uint survivors;
  if (thread_id == 0u)
    atomic_store_explicit (&survivors, 0u, metal::memory_order_relaxed);
  threadgroup_barrier (metal::mem_flags::mem_threadgroup);

  const uint tiles_side = uint (u.tiles.z);
  const uint index = grid_pos.x;
  const float tile_world = u.tiles.w;
  bool valid = index < tiles_side * tiles_side;
  const uint tile_x = index % max (tiles_side, 1u);
  const uint tile_z = index / max (tiles_side, 1u);
  uint plants = 0u;

  if (valid) {
    const float2 base =
      (float2 (int2 (u.tiles.xy)) + float2 (tile_x, tile_z)) * tile_world;
    const float2 center = base + 0.5 * tile_world;
    const float distance = length (center - u.camera_pos.xz);
    const float reach = u.params.z;
    valid = distance < reach + 0.75 * tile_world;

    if (valid) {
      const float3 ground_normal =
        undergrowth_ground_normal (center, u, normals);
      const float density =
        undergrowth_density (center, u, forest, moisture, paths, ground_normal);
      // The budget is the level of detail. Plants thin out with distance
      // rather than vanishing at a ring, and the mesh stage widens the
      // survivors to hold the coverage the thinned-out ones were carrying.
      const float near_share = 1.0 - smoothstep (0.62 * reach, reach, distance);
      // Rounding a fractional budget draws a contour line on the ground
      // wherever the field crosses a half. Carrying the fraction as odds
      // instead lets a stand thin out plant by plant, which is how a stand
      // actually ends.
      const float wanted =
        density * near_share * float (UNDERGROWTH_PLANTS_PER_TILE);
      plants = uint (
        floor (wanted + undergrowth_hash (uint2 (tile_x, tile_z), 0x51a7u)));
      valid = plants > 0u;
    }

    if (valid) {
      const float ground = undergrowth_ground (center, u, heights);
      const float4 clip =
        u.view_proj * float4 (center.x, ground + 0.5, center.y, 1.0);
      const float margin = 1.25 * clip.w + 2.0 * tile_world;
      valid =
        clip.w > -tile_world && abs (clip.x) < margin && abs (clip.y) < margin;
    }

    if (valid) {
      const uint slot =
        atomic_fetch_add_explicit (&survivors, 1u, metal::memory_order_relaxed);
      payload.tiles[slot].index = uint2 (tile_x, tile_z);
      payload.tiles[slot].plants = plants;
    }
  }

  threadgroup_barrier (metal::mem_flags::mem_threadgroup);
  if (thread_id == 0u) {
    payload.count =
      atomic_load_explicit (&survivors, metal::memory_order_relaxed);
    mesh_grid.set_threadgroups_per_grid (uint3 (payload.count, 1, 1));
  }
}

// ---- the mesh stage: what a plant is -------------------------------

// Everything one spray needs to exist, resolved from a hash and the ground.
// Reach and climb are kept apart because they are what tell the two plants
// apart: a fern throws its fronds outward and lets them fall, a shrub sends
// its shoots up and holds them there.
struct UndergrowthSpray {
  float3 root;
  float3 up;
  float3 out;  // horizontal direction the spray reaches along
  float3 tint; // display-space colour at the lit end
  float reach; // metres out from the crown
  float climb; // metres of rise at the spray's highest
  float width;
  float lift;  // how steeply it leaves the crown
  float arch;  // how far it falls away again before the tip
  float lobed; // depth of the pinnae riding on the width profile
};

// ---- the mesh stage ------------------------------------------------

[[mesh]] void undergrowth_mesh (
  UndergrowthMesh out,
  object_data const UndergrowthPayload& payload [[payload]],
  uint mesh_id [[threadgroup_position_in_grid]],
  uint thread_id [[thread_index_in_threadgroup]],
  constant MoppeUndergrowthUniforms& u [[buffer (MOPPE_BUF_FRAME)]],
  texture2d<float, access::read> heights [[texture (MOPPE_TEX_HEIGHTS)]],
  texture2d<float> normals [[texture (MOPPE_TEX_TERRAIN_NORMALS)]],
  texture2d<float> forest [[texture (MOPPE_TEX_TERRAIN_FOREST)]],
  texture2d<float> moisture [[texture (MOPPE_TEX_TERRAIN_MOISTURE)]]) {
  const UndergrowthTile tile = payload.tiles[min (mesh_id, payload.count - 1u)];
  const uint plants = max (tile.plants, 1u);
  if (thread_id == 0u) {
    out.set_primitive_count (plants * UNDERGROWTH_SPRAYS_PER_PLANT *
                             UNDERGROWTH_SPRAY_PRIMITIVES);
  }

  const uint plant = thread_id / UNDERGROWTH_SPRAYS_PER_PLANT;
  const uint spray = thread_id % UNDERGROWTH_SPRAYS_PER_PLANT;
  if (plant >= plants)
    return;

  const float tile_world = u.tiles.w;
  const uint2 cell = uint2 (int2 (u.tiles.xy) + int2 (tile.index));
  const uint2 identity =
    uint2 (cell.x * 73856093u + plant, cell.y * 19349663u + plant * 83492791u);

  // Where the plant stands inside its tile. Jitter well inside the edge so a
  // tile's plants stay its own: a plant that wanders across the boundary
  // would pop when its own tile fails a cull its neighbour passed.
  const float2 base =
    (float2 (int2 (u.tiles.xy)) + float2 (tile.index)) * tile_world;
  const float2 root_xz =
    base + tile_world * float2 (0.10 + 0.80 * undergrowth_hash (identity, 1u),
                                0.10 + 0.80 * undergrowth_hash (identity, 2u));

  const float3 ground_normal = undergrowth_ground_normal (root_xz, u, normals);
  const float ground = undergrowth_ground (root_xz, u, heights);
  const float3 root = float3 (root_xz.x, ground, root_xz.y);

  const float canopy = saturate (undergrowth_field (root_xz, u, forest).r);
  const float wet = saturate (undergrowth_field (root_xz, u, moisture).r);
  const float vigour = 0.55 + 0.60 * wet + 0.25 * canopy;

  // Two plants, one construction. A fern is a low rosette of long arching
  // fronds and belongs in shade; a shrub is a tighter, more upright, smaller
  // leaved thing that holds the open ground and the forest edge. Which one
  // grows here is mostly the canopy's decision, with enough of a hash left
  // in it that a stand is never uniform.
  const float fern_odds = saturate (0.20 + 1.05 * canopy);
  const bool fern = undergrowth_hash (identity, 3u) < fern_odds;

  // Plants thinned out by distance leave gaps, so the survivors take on the
  // coverage: the floor keeps looking as dense as it did, out of fewer of
  // them. Without this the understory visibly evaporates as you ride away.
  const float thinning =
    sqrt (float (UNDERGROWTH_PLANTS_PER_TILE) / float (plants));
  // Squaring the draw puts most plants small and a few large, which is the
  // shape of any stand that has been competing for light for a while. A
  // uniform draw reads as a planted bed.
  const float draw = undergrowth_hash (identity, 4u);
  const float scale =
    vigour * (0.66 + 1.18 * draw * draw) * mix (1.0, min (thinning, 1.7), 0.7);

  UndergrowthSpray s;
  s.root = root;
  // Undergrowth follows the ground more closely than a tree does; it has no
  // trunk to straighten it out.
  s.up = normalize (mix (ground_normal, float3 (0, 1, 0), 0.35));
  const float turn = 6.2831853 * undergrowth_hash (identity, 5u) +
                     1.5707963 * float (spray) +
                     0.55 * (undergrowth_hash (identity, 6u + spray) - 0.5);
  const float3 across =
    normalize (cross (s.up, float3 (0.0, 0.0, 1.0)) + float3 (0.001, 0.0, 0.0));
  const float3 along = normalize (cross (across, s.up));
  s.out = normalize (across * cos (turn) + along * sin (turn));

  const float spread = 0.80 + 0.45 * undergrowth_hash (identity, 11u + spray);
  if (fern) {
    // A frond leaves the crown steeply, tops out about two thirds along, and
    // falls away to a drooping tip.
    s.reach = scale * 0.70 * spread;
    s.climb = scale * 0.94 * spread;
    s.width = scale * 0.205;
    s.lift = 2.10;
    s.arch = 1.72;
    s.lobed = 0.46;
    s.tint = float3 (0.100, 0.315, 0.086);
  } else {
    // A shoot goes up and stays up, so a shrub reads as a mass at knee
    // height rather than as something spilled on the ground.
    s.reach = scale * 0.46 * spread;
    s.climb = scale * 1.42 * spread;
    s.width = scale * 0.360;
    s.lift = 1.50;
    s.arch = 0.42;
    s.lobed = 0.10;
    s.tint = float3 (0.175, 0.295, 0.098);
  }
  // Damp ground is deeper and greener; a dry shaded floor goes olive.
  s.tint *= float3 (1.18 - 0.34 * wet, 0.80 + 0.42 * wet, 0.80 + 0.30 * wet);
  const float olive = undergrowth_hash (identity, 17u) - 0.5;
  s.tint *= float3 (1.0 + 0.62 * olive, 1.0, 1.0 - 0.58 * olive);
  s.tint *= 0.80 + 0.42 * undergrowth_hash (identity, 19u);

  const uint vertex_base = thread_id * UNDERGROWTH_SPRAY_VERTICES;
  const uint primitive_base = thread_id * UNDERGROWTH_SPRAY_PRIMITIVES;
  const uint index_base = primitive_base * 3u;

  // The spray's spine: it leaves the crown steeply, then falls away. The
  // last cross-section is closed to a point, so a frond ends in a tip
  // rather than in a cut edge.
  for (uint step = 0; step < UNDERGROWTH_SPRAY_SECTIONS; ++step) {
    const float t = float (step) / float (UNDERGROWTH_SPRAY_SECTIONS - 1);
    const float rise = s.lift * t - s.arch * t * t;
    const float3 spine =
      s.root + s.out * (s.reach * t) + s.up * (s.climb * rise);
    // Widest a little past halfway and closed at the tip: a leaf, not a
    // ribbon. The lobes riding on that profile are what a fern's edge is,
    // and they cost nothing but the cross-sections already being emitted.
    const float taper = 0.42 + 1.25 * t - 1.35 * t * t;
    const float lobes = 1.0 + s.lobed * cos (12.566371 * t);
    const float half_width = s.width * (t >= 0.999 ? 0.0 : taper * lobes);
    const float3 side = normalize (cross (s.out, s.up));
    // The blade twists as it falls, so a rosette never shows four identical
    // faces to the light.
    const float3 face = normalize (
      s.up + s.out * (0.55 * rise) +
      side * (0.30 * sin (6.2831853 * undergrowth_hash (identity, 23u + spray) +
                          2.2 * t)));
    const float3 edge = normalize (cross (face, s.out));

    const float bend = 0.30 + 0.70 * t;
    const float flutter = 0.55 + 0.45 * t;
    const float3 left =
      moppe_wind (spine - edge * half_width, bend, flutter, u.params.x);
    const float3 right =
      moppe_wind (spine + edge * half_width, bend, flutter, u.params.x);

    // Exposure along the frond stands in for how much sky its part of the
    // plant sees: the litter at the base of a rosette is nearly black, the
    // tips catch everything.
    const float exposure = 0.12 + 0.88 * t;
    const float3 colour = s.tint * (0.58 + 0.66 * exposure);

    UndergrowthVaryings v;
    v.normal = face;
    v.color = colour;
    v.exposure = exposure;

    v.world_pos = left;
    v.position = u.view_proj * float4 (left, 1.0);
    out.set_vertex (vertex_base + step * 2u, v);

    v.world_pos = right;
    v.position = u.view_proj * float4 (right, 1.0);
    out.set_vertex (vertex_base + step * 2u + 1u, v);
  }

  for (uint quad = 0; quad + 1u < UNDERGROWTH_SPRAY_SECTIONS; ++quad) {
    const uint corner = vertex_base + quad * 2u;
    const uint slot = index_base + quad * 6u;
    out.set_index (slot + 0u, corner + 0u);
    out.set_index (slot + 1u, corner + 1u);
    out.set_index (slot + 2u, corner + 3u);
    out.set_index (slot + 3u, corner + 0u);
    out.set_index (slot + 4u, corner + 3u);
    out.set_index (slot + 5u, corner + 2u);
  }
}

// ---- the fragment stage --------------------------------------------

fragment float4 undergrowth_fragment (UndergrowthVaryings in [[stage_in]],
                                      bool front_facing [[front_facing]],
                                      constant MoppeUndergrowthUniforms& u
                                      [[buffer (MOPPE_BUF_FRAME)]],
                                      depth2d<float> shadow_map
                                      [[texture (MOPPE_TEX_SHADOW)]]) {
  // A leaf has no back. Facing the normal at whoever is looking is what
  // stops half of every rosette reading as a hole cut in the ground.
  const float3 n = normalize (front_facing ? in.normal : -in.normal);
  const float3 l = u.sun_dir.xyz;
  const float3 to_frag = in.world_pos - u.camera_pos.xyz;
  const float dist = length (to_frag);
  const float fog = moppe_relief_haze (moppe_distance_fog (dist, u.fog_color.w),
                                       in.world_pos.y,
                                       u.relief.x,
                                       u.relief.y);
  const float sun_visibility =
    moppe_sun_visibility (in.world_pos,
                          n,
                          l,
                          fog,
                          u.light_matrix,
                          u.shadow.x,
                          u.shadow.y,
                          shadow_map) *
    moppe_cloud_transmission (in.world_pos, l, u.params.x, u.params.y);

  const float3 base = moppe_srgb (in.color);
  const float lambert = saturate ((dot (n, l) + 0.10) / 1.10);
  float3 color = base * (moppe_hemisphere_light (u.ambient.rgb, n) +
                         u.sun_diffuse.rgb * lambert * sun_visibility);

  // A frond is one leaf thick and glows when the sun is behind it, which is
  // most of what tells undergrowth apart from painted ground.
  const float leaf_back = pow (max (dot (-n, l), 0.0), 1.5);
  const float3 transmission_tint (1.16, 0.94, 0.58);
  color += base * u.sun_diffuse.rgb * transmission_tint * sun_visibility *
           leaf_back * (0.30 + 0.70 * in.exposure) * 0.95;

  const float3 fog_c =
    moppe_warmed_fog (u.fog_color.rgb, to_frag / max (dist, 1e-4), l);
  color = mix (color, fog_c, smoothstep (0.0, 0.9, fog));
  return float4 (color, 1.0);
}
