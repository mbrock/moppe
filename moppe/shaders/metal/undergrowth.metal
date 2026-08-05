// Grass and occasional ferns on the forest floor, generated rather than
// stored.
//
// Nothing here is a mesh. The object stage walks a window of ground tiles
// around the camera and keeps the ones the world's own fields say something
// grows on -- light and water, no trail worn across it, ground the plant could
// stand on. The mesh stage turns each surviving tile into shoots: a hash
// decides where each one sits and what it is, and the height and normal
// textures root it on the terrain by construction. The same gust function the
// trees use moves it. So undergrowth costs no memory,
// cannot drift out of step with the ground it grows on, and can change its
// count and its shape every frame, because nothing is kept to go stale.

#include "common.h"

// Each mesh thread grows one independently rooted grass blade or rare fern
// frond. Four cross-sections give that shoot a curved silhouette while the
// shared constants keep the meshlet exactly within Metal's 256-vertex limit.
#define UNDERGROWTH_LOD_TRANSITION 0.52
#define UNDERGROWTH_TILE_THREADS MOPPE_UNDERGROWTH_MESH_THREADS
#define UNDERGROWTH_OBJECT_THREADS 64

struct UndergrowthVaryings {
  float4 position [[position]];
  float3 world_pos;
  float3 normal;
  float3 color;
  float exposure;
  float2 motion [[center_no_perspective]];
};

struct UndergrowthTile {
  uint2 index;
  float wanted;
};

struct UndergrowthPayload {
  uint count;
  UndergrowthTile tiles[UNDERGROWTH_OBJECT_THREADS];
};

using UndergrowthMesh = metal::mesh<UndergrowthVaryings,
                                    void,
                                    MOPPE_UNDERGROWTH_MESH_VERTICES,
                                    MOPPE_UNDERGROWTH_MESH_PRIMITIVES,
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

// How much ground vegetation one patch carries. Grass is the substrate, so it
// also occupies open country; a closed canopy thins it rather than being the
// reason it exists. Soil water sets how lush it gets, a worn trail clears it,
// and a slope past what roots can hold sheds it entirely.
static inline float undergrowth_density (float2 world_xz,
                                         constant MoppeUndergrowthUniforms& u,
                                         texture2d<float> forest,
                                         texture2d<float> moisture,
                                         texture2d<float> paths,
                                         texture2d<float> snow_support,
                                         texture2d<float> water_levels,
                                         float ground_m,
                                         float3 ground_normal) {
  const float canopy = saturate (undergrowth_field (world_xz, u, forest).r);
  const float wet = saturate (undergrowth_field (world_xz, u, moisture).r);
  const float2 worn = saturate (undergrowth_field (world_xz, u, paths).rg);
  // Broad variation breaks up an evenly upholstered floor without opening the
  // large bare holes that made the former rosette layer look planted.
  const float clump =
    moppe_value_noise (world_xz * 0.085) * 0.55 +
    moppe_value_noise (world_xz * 0.021 + float2 (17.3, 4.1)) * 0.45;
  const float light = 1.0 - 0.30 * smoothstep (0.32, 0.92, canopy);
  const float damp = 0.75 + 0.25 * smoothstep (0.02, 0.48, wet);
  const float standable = smoothstep (0.52, 0.78, ground_normal.y);
  const float cleared = 1.0 - saturate (max (worn.x, worn.y) * 1.6);
  const float variation = 0.88 + 0.24 * smoothstep (0.18, 0.72, clump);
  // A generated blade must agree with the terrain material under it. The
  // filtered support field is the same broad hillside reading that retains
  // snow, while relative altitude removes grass gradually through the alpine
  // transition instead of drawing a hard contour. Standing water is a
  // physical exclusion rather than merely very damp habitat.
  const float support =
    u.relief.z > 0.5
      ? saturate (undergrowth_field (world_xz, u, snow_support).r)
      : ground_normal.y;
  const float relative_height = (ground_m - u.relief.x) / max (u.relief.y, 1.0);
  const float snow_habitat =
    smoothstep (0.55, 0.68, relative_height) * smoothstep (0.58, 0.78, support);
  const float alpine_survival = 1.0 - smoothstep (0.50, 0.67, relative_height);
  const float water_depth =
    u.relief.w > 0.5
      ? undergrowth_field (world_xz, u, water_levels).r - ground_m
      : 0.0;
  const float dry_ground = 1.0 - smoothstep (0.002, 0.030, water_depth);
  // A signed water level is also a habitat boundary. On the dry side of the
  // zero crossing, a narrow riparian band grows slightly denser; on the wet
  // side the same signal still excludes roots completely.
  const float shore =
    u.relief.w > 0.5 ? 1.0 - smoothstep (0.05, 1.35, abs (water_depth)) : 0.0;
  const float riparian =
    shore * dry_ground * smoothstep (0.52, 0.80, ground_normal.y);
  return saturate (light * damp * standable * cleared * variation *
                   alpine_survival * (1.0 - snow_habitat) * dry_ground *
                   (1.0 + 0.18 * riparian) * u.params.w);
}

// Each world tile owns one phase for thinning its ordered shoots. The
// phase must be addressed by the world cell, never by the tile's temporary
// slot in the moving camera window: using the latter makes the whole floor
// choose new counts whenever the window crosses one tile boundary.
static inline float undergrowth_lod_phase (uint2 cell) {
  return undergrowth_hash (cell, 0x51a7u);
}

static inline uint undergrowth_lod_shoots (float wanted, uint2 cell) {
  const float phase = undergrowth_lod_phase (cell);
  const float begun = max (wanted - phase + UNDERGROWTH_LOD_TRANSITION, 0.0);
  return min (uint (ceil (begun)), uint (MOPPE_UNDERGROWTH_SHOOTS_PER_TILE));
}

static inline float
undergrowth_lod_presence (float wanted, uint shoot, uint2 cell) {
  const float threshold = float (shoot) + undergrowth_lod_phase (cell);
  const float presence = smoothstep (threshold - UNDERGROWTH_LOD_TRANSITION,
                                     threshold + UNDERGROWTH_LOD_TRANSITION,
                                     wanted);
  // An unusually early phase must still leave truly barren ground empty.
  return presence * smoothstep (0.0, UNDERGROWTH_LOD_TRANSITION, wanted);
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
  texture2d<float> paths [[texture (MOPPE_TEX_TERRAIN_PATHS)]],
  texture2d<float> snow_support [[texture (MOPPE_TEX_TERRAIN_SNOW_SUPPORT)]],
  texture2d<float> water_levels [[texture (MOPPE_TEX_TERRAIN_WATER)]]) {
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
  float wanted = 0.0;
  float ground = 0.0;
  uint shoots = 0u;

  if (valid) {
    const int2 cell = int2 (u.tiles.xy) + int2 (tile_x, tile_z);
    const float2 base = float2 (cell) * tile_world;
    const float2 center = base + 0.5 * tile_world;
    const float distance = length (center - u.camera_pos.xz);
    const float reach = u.params.z;
    valid = distance < reach + 0.75 * tile_world;

    if (valid) {
      const float3 ground_normal =
        undergrowth_ground_normal (center, u, normals);
      ground = undergrowth_ground (center, u, heights);
      const float density = undergrowth_density (center,
                                                 u,
                                                 forest,
                                                 moisture,
                                                 paths,
                                                 snow_support,
                                                 water_levels,
                                                 ground,
                                                 ground_normal);
      // The budget is the level of detail. Shoots grow down continuously with
      // distance, and the mesh stage widens the remaining coverage as the
      // budget recedes.
      const float near_share = 1.0 - smoothstep (0.62 * reach, reach, distance);
      wanted = density * near_share * float (MOPPE_UNDERGROWTH_SHOOTS_PER_TILE);
      shoots = undergrowth_lod_shoots (wanted, uint2 (cell));
      valid = shoots > 0u;
    }

    if (valid) {
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
      payload.tiles[slot].wanted = wanted;
    }
  }

  threadgroup_barrier (metal::mem_flags::mem_threadgroup);
  if (thread_id == 0u) {
    payload.count =
      atomic_load_explicit (&survivors, metal::memory_order_relaxed);
    mesh_grid.set_threadgroups_per_grid (uint3 (payload.count, 1, 1));
  }
}

// ---- the mesh stage: what a shoot is -------------------------------

// Everything one shoot needs to exist, resolved from a hash and the ground.
// Reach and climb are kept apart because they are what tell the two forms
// apart: a fern throws its fronds outward while a grass blade rises.
struct UndergrowthShoot {
  float3 root;
  float3 up;
  float3 out;  // horizontal direction the shoot reaches along
  float3 tint; // display-space colour at the lit end
  float reach; // metres out from the root
  float climb; // metres of rise at the shoot's highest
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
  texture2d<float> moisture [[texture (MOPPE_TEX_TERRAIN_MOISTURE)]],
  texture2d<float> paths [[texture (MOPPE_TEX_TERRAIN_PATHS)]],
  texture2d<float> water_levels [[texture (MOPPE_TEX_TERRAIN_WATER)]]) {
  const UndergrowthTile tile = payload.tiles[min (mesh_id, payload.count - 1u)];
  const uint2 cell = uint2 (int2 (u.tiles.xy) + int2 (tile.index));
  const uint shoots = max (undergrowth_lod_shoots (tile.wanted, cell), 1u);
  if (thread_id == 0u) {
    out.set_primitive_count (shoots * MOPPE_UNDERGROWTH_PRIMITIVES_PER_SHOOT);
  }

  const uint shoot = thread_id;
  if (shoot >= shoots)
    return;

  const float tile_world = u.tiles.w;
  const uint2 identity =
    uint2 (cell.x * 73856093u + shoot, cell.y * 19349663u + shoot * 83492791u);

  // Where the shoot stands inside its tile. Jitter inside the edge so a
  // tile's shoots stay its own: a root that wanders across the boundary
  // would pop when its own tile fails a cull its neighbour passed.
  const float2 base =
    (float2 (int2 (u.tiles.xy)) + float2 (tile.index)) * tile_world;
  const float2 root_xz =
    base + tile_world * float2 (0.03 + 0.94 * undergrowth_hash (identity, 1u),
                                0.03 + 0.94 * undergrowth_hash (identity, 2u));

  const float3 ground_normal = undergrowth_ground_normal (root_xz, u, normals);
  const float ground = undergrowth_ground (root_xz, u, heights);
  const float3 root = float3 (root_xz.x, ground, root_xz.y);

  const float canopy = saturate (undergrowth_field (root_xz, u, forest).r);
  const float wet = saturate (undergrowth_field (root_xz, u, moisture).r);
  const float2 worn = saturate (undergrowth_field (root_xz, u, paths).rg);
  const float root_clear = 1.0 - saturate (max (worn.x, worn.y) * 1.6);
  const float root_water_depth =
    u.relief.w > 0.5 ? undergrowth_field (root_xz, u, water_levels).r - ground
                     : 0.0;
  const float root_dry = 1.0 - smoothstep (0.002, 0.030, root_water_depth);
  const float root_shore =
    u.relief.w > 0.5 ? 1.0 - smoothstep (0.05, 1.35, abs (root_water_depth))
                     : 0.0;
  const float riparian =
    root_shore * root_dry * smoothstep (0.52, 0.80, ground_normal.y);
  // Grass is the ordinary answer. Ferns are an accent reserved for damp shade,
  // not a second carpet competing with it.
  const float fern_habitat =
    smoothstep (0.28, 0.86, canopy) * smoothstep (0.18, 0.74, wet);
  const float fern_odds = 0.012 + 0.06 * fern_habitat;
  const bool fern = undergrowth_hash (identity, 3u) < fern_odds;

  // A shoot straddles its LOD threshold by growing into or out of the ground.
  // The short transition keeps motion continuous without turning the whole
  // layer translucent and giving depth ownership to stochastic fragments.
  const float presence = undergrowth_lod_presence (tile.wanted, shoot, cell);
  // Shoots thinned out by distance leave gaps, so the survivors take on the
  // projected width continuously. Height remains an ecological property:
  // making survivors taller as they recede is a conspicuous LOD tell.
  const float thinning =
    sqrt (float (MOPPE_UNDERGROWTH_SHOOTS_PER_TILE) / max (tile.wanted, 1.0));
  const float draw = undergrowth_hash (identity, 4u);
  const float scale = sqrt (presence * root_clear * root_dry) *
                      (0.60 + 0.35 * wet + 0.08 * (1.0 - canopy)) *
                      (0.65 + 0.65 * draw * draw) * (1.0 + 0.18 * riparian);
  const float coverage = mix (1.0, min (thinning, 1.5), fern ? 0.45 : 0.72);

  UndergrowthShoot s;
  s.root = root;
  // Fronds follow the ground; grass gravitropism makes its blades mostly
  // upright even when their roots are on a bank.
  s.up = normalize (mix (ground_normal, float3 (0, 1, 0), fern ? 0.38 : 0.72));
  const float turn = 6.2831853 * undergrowth_hash (identity, 5u) +
                     0.55 * (undergrowth_hash (identity, 6u) - 0.5);
  const float3 across =
    normalize (cross (s.up, float3 (0.0, 0.0, 1.0)) + float3 (0.001, 0.0, 0.0));
  const float3 along = normalize (cross (across, s.up));
  s.out = normalize (across * cos (turn) + along * sin (turn));

  const float spread = 0.80 + 0.45 * undergrowth_hash (identity, 11u);
  if (fern) {
    s.reach = scale * 0.48 * spread * coverage;
    s.climb = scale * 0.62 * spread;
    s.width = scale * 0.105 * coverage;
    s.lift = 1.88;
    s.arch = 1.20;
    s.lobed = 0.38;
    s.tint = float3 (0.108, 0.262, 0.082);
  } else {
    // One strip is one blade. Density now supplies the field's visual mass,
    // letting the individual silhouette remain convincingly narrow.
    s.reach = scale * 0.16 * spread;
    s.climb = scale * 0.65 * spread;
    s.width = scale * 0.018 * coverage;
    s.lift = 1.22;
    s.arch = 0.20;
    s.lobed = 0.0;
    s.tint = float3 (0.185, 0.315, 0.112);
    // Bank grasses trade their meadow spread for a taller upright profile.
    // This is a continuous habitat response, not a separately scattered row
    // of reeds that could drift away from the waterline.
    s.reach *= 1.0 - 0.18 * riparian;
    s.climb *= 1.0 + 0.28 * riparian;
    s.width *= 1.0 - 0.08 * riparian;
  }
  // Damp grass is deeper and greener; dry blades run straw-olive without
  // becoming a second ground texture.
  s.tint *= float3 (1.12 - 0.24 * wet, 0.84 + 0.30 * wet, 0.82 + 0.22 * wet);
  s.tint *= mix (float3 (1.0), float3 (0.82, 1.10, 0.88), riparian);
  // Keep blade-to-blade variation subordinate to the continuous habitat
  // fields. High-contrast salt and pepper reads as glitter once the blades
  // become subpixel, even though every blade has stable identity.
  const float olive = undergrowth_hash (identity, 17u) - 0.5;
  s.tint *= float3 (1.0 + 0.18 * olive, 1.0, 1.0 - 0.16 * olive);
  s.tint *= 0.96 + 0.11 * undergrowth_hash (identity, 19u);

  const uint vertex_base = thread_id * MOPPE_UNDERGROWTH_VERTICES_PER_SHOOT;
  const uint primitive_base =
    thread_id * MOPPE_UNDERGROWTH_PRIMITIVES_PER_SHOOT;
  const uint index_base = primitive_base * 3u;

  // Fine-scale motion is meaningful only while a blade spans several pixels.
  // Beyond that, the coherent gust remains but the fast flick fades before it
  // can turn a distant field into temporal sparkle.
  const float camera_distance = length (root_xz - u.camera_pos.xz);
  const float micro_detail =
    1.0 - smoothstep (0.28 * u.params.z, 0.72 * u.params.z, camera_distance);

  // The active mover parts the field without retaining or rewriting a single
  // blade. Roots stay fixed; upper sections lean away and lie down toward the
  // centre of the footprint, then recover automatically as it passes.
  const float2 from_mover = root_xz - u.interaction.xz;
  const float mover_distance = length (from_mover);
  const float response =
    u.interaction.w > 0.0
      ? 1.0 -
          smoothstep (0.18 * u.interaction.w, u.interaction.w, mover_distance)
      : 0.0;
  const float2 away = from_mover / max (mover_distance, 0.08);

  // The shoot's spine: it leaves the root steeply, then falls away. The
  // last cross-section is closed to a point, so a frond ends in a tip
  // rather than in a cut edge.
  for (uint step = 0; step < MOPPE_UNDERGROWTH_SECTIONS_PER_SHOOT; ++step) {
    const float t =
      float (step) / float (MOPPE_UNDERGROWTH_SECTIONS_PER_SHOOT - 1);
    const float rise = s.lift * t - s.arch * t * t;
    float3 spine = s.root + s.out * (s.reach * t) + s.up * (s.climb * rise);
    const float upper = smoothstep (0.0, 0.58, t) * response;
    spine += float3 (away.x, 0.0, away.y) * (0.42 * upper);
    spine -= s.up * (s.climb * rise * 0.72 * response);
    // Grass keeps nearly one width until its pointed tip; a fern broadens
    // through the middle and carries lobes on that profile.
    const float taper =
      fern ? 0.42 + 1.25 * t - 1.35 * t * t : 0.72 + 0.28 * sin (3.1415927 * t);
    const float lobes = 1.0 + s.lobed * cos (12.566371 * t);
    const float half_width = s.width * (t >= 0.999 ? 0.0 : taper * lobes);
    const float3 side = normalize (cross (s.out, s.up));
    // A little twist keeps neighbouring blades from showing identical faces.
    // Ferns twist more strongly as their broad fronds fall.
    const float3 face = normalize (
      s.up + s.out * (0.55 * rise) +
      side * ((fern ? 0.30 : 0.12) *
              sin (6.2831853 * undergrowth_hash (identity, 23u) + 2.2 * t)));
    const float3 edge = normalize (cross (face, s.out));

    // Fine grass edges scintillate easily, so the fast flick remains
    // subordinate to the shared low-frequency bend and disappears at range.
    const float bend = 0.18 + 0.50 * t;
    const float flutter = (0.06 + 0.18 * t) * micro_detail;
    const float3 left_base = spine - edge * half_width;
    const float3 right_base = spine + edge * half_width;
    const float3 left = moppe_wind (left_base, bend, flutter, u.params.x);
    const float3 right = moppe_wind (right_base, bend, flutter, u.params.x);
    const float3 previous_left =
      moppe_wind (left_base, bend, flutter, u.temporal.z);
    const float3 previous_right =
      moppe_wind (right_base, bend, flutter, u.temporal.z);

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
    v.motion =
      moppe_motion_vector (u.unjittered_view_proj * float4 (left, 1.0),
                           u.previous_view_proj * float4 (previous_left, 1.0),
                           u.temporal.xy);
    out.set_vertex (vertex_base + step * 2u, v);

    v.world_pos = right;
    v.position = u.view_proj * float4 (right, 1.0);
    v.motion =
      moppe_motion_vector (u.unjittered_view_proj * float4 (right, 1.0),
                           u.previous_view_proj * float4 (previous_right, 1.0),
                           u.temporal.xy);
    out.set_vertex (vertex_base + step * 2u + 1u, v);
  }

  for (uint quad = 0; quad + 1u < MOPPE_UNDERGROWTH_SECTIONS_PER_SHOOT;
       ++quad) {
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

fragment MoppeTemporalOutput undergrowth_fragment (
  UndergrowthVaryings in [[stage_in]],
  bool front_facing [[front_facing]],
  constant MoppeUndergrowthUniforms& u [[buffer (MOPPE_BUF_FRAME)]],
  depth2d<float> shadow_map [[texture (MOPPE_TEX_SHADOW)]]) {
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
  const float cast_light = moppe_sun_visibility (in.world_pos,
                                                 n,
                                                 l,
                                                 fog,
                                                 u.light_matrix,
                                                 u.shadow.x,
                                                 u.shadow.y,
                                                 shadow_map);
  const float sun_visibility =
    cast_light *
    moppe_cloud_transmission (in.world_pos, l, u.params.x, u.params.y);

  const float3 base = moppe_srgb (in.color);
  const float lambert = saturate ((dot (n, l) + 0.10) / 1.10);
  // Cast shadow cools the fill to match the terrain beneath: shaded blades
  // are skylit only.
  const float3 shade_fill =
    mix (float3 (0.80, 0.92, 1.14), float3 (1.0), cast_light);
  float3 color =
    base * (shade_fill * moppe_hemisphere_light (u.ambient.rgb, n) +
            u.sun_diffuse.rgb * lambert * sun_visibility);

  // A blade is one leaf thick and glows when the sun is behind it, which is
  // most of what tells this layer apart from painted ground.
  const float leaf_back = pow (max (dot (-n, l), 0.0), 1.8);
  const float3 transmission_tint (0.96, 0.88, 0.62);
  color += base * u.sun_diffuse.rgb * transmission_tint * sun_visibility *
           leaf_back * (0.30 + 0.70 * in.exposure) * 0.52;

  const float3 fog_c =
    moppe_warmed_fog (u.fog_color.rgb, to_frag / max (dist, 1e-4), l);
  color = mix (color, fog_c, smoothstep (0.0, 0.9, fog));
  return moppe_temporal_output (float4 (color, 1.0), in.motion, 0.55);
}
