// The ground flora -- grass, fern rosettes, and flowering drifts --
// generated rather than stored.
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
//
// A family is a way of spending one shoot's fixed vertex allowance. Grass
// spends it as a narrow upright ribbon and is the substrate everywhere.
// The accents displace grass shoots where their habitat says so: a fern is
// one frond of a rosette whose crown a damp-shade lattice owns, and a
// flower is a stem whose last two cross-sections become a petal head. The
// families partition habitat -- deep damp shade to ferns, open sunlit
// meadow to flowers -- so no ground carries two accents at once.

#include "grass_medium.h"

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
  float petal; // 1 on flower-head fragments: petals are not chlorophyll
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
  return moppe_plant_hash (cell, lane);
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
static inline MoppeGrassMedium
undergrowth_medium (float2 world_xz,
                    constant MoppeUndergrowthUniforms& u,
                    texture2d<float> landscape_materials,
                    texture2d<float> ground_materials,
                    texture2d<float> water_levels,
                    float ground_m,
                    float3 ground_normal) {
  const float4 landscape = undergrowth_field (world_xz, u, landscape_materials);
  const float4 ground = undergrowth_field (world_xz, u, ground_materials);
  const float canopy = landscape.a;
  const float wet = landscape.r;
  const float2 worn = ground.ba;
  const float support = u.relief.z > 0.5 ? ground.g : ground_normal.y;
  const float relative_height = (ground_m - u.relief.x) / max (u.relief.y, 1.0);
  const float signed_water_depth =
    u.relief.w > 0.5
      ? undergrowth_field (world_xz, u, water_levels).r - ground_m
      : -100.0;
  return moppe_grass_medium (world_xz,
                             wet,
                             canopy,
                             worn,
                             ground_normal.y,
                             support,
                             relative_height,
                             signed_water_depth,
                             u.params.w);
}

// Bracken belongs to edges and glades as much as to the deep stand:
// asking for a closed canopy kept every rosette in permanent shadow
// where nothing reads. Thin woodland already qualifies.
static inline float undergrowth_fern_habitat (float canopy, float wet) {
  return smoothstep (0.18, 0.55, canopy) * smoothstep (0.14, 0.55, wet);
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
  texture2d<float> landscape_materials
  [[texture (MOPPE_TEX_TERRAIN_LANDSCAPE)]],
  texture2d<float> ground_materials [[texture (MOPPE_TEX_TERRAIN_GROUND)]],
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
    const float horizontal_distance = length (center - u.camera_pos.xz);
    valid = horizontal_distance < u.lod.x + 0.75 * tile_world;

    if (valid) {
      const float3 ground_normal =
        undergrowth_ground_normal (center, u, normals);
      ground = undergrowth_ground (center, u, heights);
      const MoppeGrassMedium grass = undergrowth_medium (center,
                                                         u,
                                                         landscape_materials,
                                                         ground_materials,
                                                         water_levels,
                                                         ground,
                                                         ground_normal);
      const float focal_pixels = abs (u.view_proj[1][1]) * 0.5 * u.temporal.y;
      const float distance =
        length (float3 (center.x, ground, center.y) - u.camera_pos.xyz);

      // The tile asks for the largest of its families' own budgets. Each
      // family prices itself by its signature feature's projected size, so
      // a drift tile stays alive for its heads after its blades retire,
      // and a fern tile for its fronds farther still. The mesh stage
      // re-prices per shoot, so a family never spends another's budget.
      const float blade_pixels =
        moppe_grass_blade_pixels (focal_pixels, distance);
      // Grass counts fall on the gentler square-root curve: survivors
      // widen to carry the coverage of retired neighbours, so the field
      // can afford fewer, broader tufts much farther out.
      float budget =
        grass.leaf_area * sqrt (moppe_grass_resolved_fraction (blade_pixels));
      const MoppeFlowerDrift drift = moppe_flower_drift (
        center, grass.moisture, grass.forest_cover, grass.leaf_area);
      if (drift.presence > 0.02) {
        const float head_pixels =
          moppe_feature_pixels (2.0 * drift.head, focal_pixels, distance);
        budget =
          max (budget,
               grass.leaf_area * moppe_flower_resolved_fraction (head_pixels) *
                 smoothstep (0.02, 0.10, drift.presence));
      }
      const float ferny =
        undergrowth_fern_habitat (grass.forest_cover, grass.moisture);
      if (ferny > 0.05) {
        const float frond_pixels = moppe_feature_pixels (
          MOPPE_FERN_FROND_WIDTH_METRES, focal_pixels, distance);
        budget =
          max (budget,
               grass.leaf_area * moppe_fern_resolved_fraction (frond_pixels) *
                 smoothstep (0.05, 0.25, ferny));
      }
      wanted = budget * float (MOPPE_UNDERGROWTH_SHOOTS_PER_TILE);
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

#define UNDERGROWTH_GRASS 0u
#define UNDERGROWTH_FERN 1u
#define UNDERGROWTH_FLOWER 2u

// Everything one shoot needs to exist, resolved from a hash and the ground.
// Reach and climb are kept apart because they are what tell the forms
// apart: a fern throws its fronds outward while a grass blade rises and a
// flower stem carries its head above the sward.
struct UndergrowthShoot {
  uint family;
  float3 root;
  float3 up;
  float3 out;  // horizontal direction the shoot reaches along
  float3 tint; // display-space colour at the lit end
  float reach; // metres out from the root
  float climb; // metres of rise at the shoot's highest
  float width;
  float lift;       // how steeply it leaves the crown
  float arch;       // how far it falls away again before the tip
  float lobed;      // depth of the pinnae riding on the width profile
  float head;       // flower petal-head half-size in metres
  float3 head_tint; // display-space petal colour
};

// A fern is a rosette, not a blade. A coarse lattice proposes crown points
// in damp shade, and every shoot rooted near one becomes a frond radiating
// from it. The cell owns the crown, so the same rosette assembles from the
// same fronds no matter where the camera window falls; the crown radius
// stays inside the jittered centre's own cell so no frond is cut off at a
// lattice border it cannot see.
struct UndergrowthCrown {
  float2 center;
  bool member;
};

static inline UndergrowthCrown
undergrowth_fern_crown (float2 root_xz, float canopy, float wet) {
  const float cell_metres = 1.05;
  const float2 cell = floor (root_xz / cell_metres);
  const uint2 id = uint2 (int2 (cell));
  UndergrowthCrown crown;
  crown.center = (cell + float2 (0.32 + 0.36 * moppe_plant_hash (id, 61u),
                                 0.32 + 0.36 * moppe_plant_hash (id, 67u))) *
                 cell_metres;
  const float habitat = undergrowth_fern_habitat (canopy, wet);
  // Bracken spreads by rhizome, so it arrives as stands: a coarse noise
  // sampled at the crown's own centre keeps whole colonies together
  // without letting two fronds of one rosette disagree about existing.
  const float stand =
    smoothstep (0.30, 0.62, moppe_value_noise (crown.center * 0.09));
  const float odds = 0.50 * habitat * (0.25 + 0.75 * stand);
  crown.member = moppe_plant_hash (id, 71u) < odds &&
                 distance (root_xz, crown.center) < 0.30;
  return crown;
}

// ---- the mesh stage ------------------------------------------------

[[mesh]] void undergrowth_mesh (
  UndergrowthMesh out,
  object_data const UndergrowthPayload& payload [[payload]],
  uint mesh_id [[threadgroup_position_in_grid]],
  uint thread_id [[thread_index_in_threadgroup]],
  constant MoppeUndergrowthUniforms& u [[buffer (MOPPE_BUF_FRAME)]],
  texture2d<float, access::read> heights [[texture (MOPPE_TEX_HEIGHTS)]],
  texture2d<float> normals [[texture (MOPPE_TEX_TERRAIN_NORMALS)]],
  texture2d<float> landscape_materials
  [[texture (MOPPE_TEX_TERRAIN_LANDSCAPE)]],
  texture2d<float> ground_materials [[texture (MOPPE_TEX_TERRAIN_GROUND)]],
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
  float2 root_xz =
    base + tile_world * float2 (0.03 + 0.94 * undergrowth_hash (identity, 1u),
                                0.03 + 0.94 * undergrowth_hash (identity, 2u));

  float3 ground_normal = undergrowth_ground_normal (root_xz, u, normals);
  float ground = undergrowth_ground (root_xz, u, heights);

  MoppeGrassMedium grass = undergrowth_medium (root_xz,
                                               u,
                                               landscape_materials,
                                               ground_materials,
                                               water_levels,
                                               ground,
                                               ground_normal);
  const float canopy = grass.forest_cover;
  const float wet = grass.moisture;

  // Grass is the ordinary answer; an accent family takes the shoot only
  // where its habitat claims the ground. The claims cannot overlap, since
  // crowns want deep damp shade and drifts want open sky.
  const UndergrowthCrown crown = undergrowth_fern_crown (root_xz, canopy, wet);
  uint family = crown.member ? UNDERGROWTH_FERN : UNDERGROWTH_GRASS;
  MoppeFlowerDrift drift;
  drift.presence = 0.0;
  if (family == UNDERGROWTH_GRASS) {
    drift = moppe_flower_drift (root_xz, wet, canopy, grass.leaf_area);
    if (undergrowth_hash (identity, 3u) < 0.55 * drift.presence)
      family = UNDERGROWTH_FLOWER;
  }

  // A frond leaves from beside its crown, not from wherever its shoot's
  // hash happened to fall, so the rosette has a centre. The pulled root
  // re-reads the ground: on a bank the crown must not float.
  float2 radial = float2 (1.0, 0.0);
  if (family == UNDERGROWTH_FERN) {
    const float2 spoke = root_xz - crown.center;
    const float apart = max (length (spoke), 0.02);
    radial = spoke / apart;
    root_xz = crown.center + radial * (0.04 + 0.20 * apart);
    ground_normal = undergrowth_ground_normal (root_xz, u, normals);
    ground = undergrowth_ground (root_xz, u, heights);
    grass = undergrowth_medium (root_xz,
                                u,
                                landscape_materials,
                                ground_materials,
                                water_levels,
                                ground,
                                ground_normal);
  }
  const float3 root = float3 (root_xz.x, ground, root_xz.y);

  const float camera_distance = length (root - u.camera_pos.xyz);
  // Fine-scale motion is meaningful only while a blade spans several pixels.
  // Beyond that, the coherent gust remains but the fast flick fades before it
  // can turn a distant field into temporal sparkle.
  const float focal_pixels = abs (u.view_proj[1][1]) * 0.5 * u.temporal.y;
  const float blade_pixels =
    moppe_grass_blade_pixels (focal_pixels, camera_distance);
  const float micro_detail = smoothstep (1.5, 4.0, blade_pixels);

  // Each family prices its own presence by its signature feature against
  // the same per-cell phase: the sward thins blade by blade while the
  // drift above it keeps its heads and the rosette keeps its fronds. The
  // tile's budget was the largest of these, so a shoot never spends
  // another family's allowance.
  float resolved_feature;
  if (family == UNDERGROWTH_FERN) {
    resolved_feature = moppe_fern_resolved_fraction (moppe_feature_pixels (
      MOPPE_FERN_FROND_WIDTH_METRES, focal_pixels, camera_distance));
  } else if (family == UNDERGROWTH_FLOWER) {
    resolved_feature = moppe_flower_resolved_fraction (
      moppe_feature_pixels (2.0 * drift.head, focal_pixels, camera_distance));
  } else {
    // The square root matches the object stage: grass survivors widen to
    // carry retired neighbours' coverage, so the count affords to fall
    // more slowly than the blade resolves.
    resolved_feature = sqrt (moppe_grass_resolved_fraction (blade_pixels));
  }
  const float family_wanted = grass.leaf_area * resolved_feature *
                              float (MOPPE_UNDERGROWTH_SHOOTS_PER_TILE);

  // A shoot straddles its LOD threshold by growing into or out of the ground.
  // The short transition keeps motion continuous without turning the whole
  // layer translucent and giving depth ownership to stochastic fragments.
  // Retiring neighbours leave the stable habitat-coloured terrain substrate,
  // and a surviving shoot keeps its ecological height; only its width may
  // grow, bounded, to conserve the coverage its neighbours dropped.
  const float presence = undergrowth_lod_presence (family_wanted, shoot, cell);

  const float draw = undergrowth_hash (identity, 4u);
  float scale = sqrt (presence) * (0.60 + 0.35 * wet + 0.08 * (1.0 - canopy)) *
                (0.65 + 0.65 * draw * draw) * (1.0 + 0.18 * grass.riparian);
  // The starved shade sward is short as well as sparse, so the rosettes
  // and the litter-dark substrate own the forest floor's silhouette.
  if (family == UNDERGROWTH_GRASS)
    scale *= 1.0 - 0.32 * smoothstep (0.40, 0.95, canopy);

  UndergrowthShoot s;
  s.family = family;
  s.root = root;
  s.head = 0.0;
  s.head_tint = float3 (0.0);
  // Fronds follow the ground; gravitropism holds grass mostly upright even
  // on a bank, and flower stems more strongly still.
  const float upright = family == UNDERGROWTH_FERN     ? 0.38
                        : family == UNDERGROWTH_FLOWER ? 0.85
                                                       : 0.72;
  s.up = normalize (mix (ground_normal, float3 (0, 1, 0), upright));
  const float turn = 6.2831853 * undergrowth_hash (identity, 5u) +
                     0.55 * (undergrowth_hash (identity, 6u) - 0.5);
  const float3 across =
    normalize (cross (s.up, float3 (0.0, 0.0, 1.0)) + float3 (0.001, 0.0, 0.0));
  const float3 along = normalize (cross (across, s.up));
  if (family == UNDERGROWTH_FERN) {
    // Radially out of the rosette, with only a little swirl: the shared
    // centre is what makes eight fronds read as one plant.
    const float swirl = 0.45 * (undergrowth_hash (identity, 6u) - 0.5);
    const float2 spun =
      float2 (radial.x * cos (swirl) - radial.y * sin (swirl),
              radial.x * sin (swirl) + radial.y * cos (swirl));
    s.out = normalize (float3 (spun.x, 0.0, spun.y) -
                       s.up * dot (s.up, float3 (spun.x, 0.0, spun.y)));
  } else {
    s.out = normalize (across * cos (turn) + along * sin (turn));
  }

  const float spread = 0.80 + 0.45 * undergrowth_hash (identity, 11u);
  if (family == UNDERGROWTH_FERN) {
    // Fronds arc up and over the starved shade sward, so the rosette owns
    // its patch of floor in silhouette rather than hiding green-on-green.
    s.reach = scale * 0.66 * spread;
    s.climb = scale * 0.55 * spread;
    s.width = scale * 0.085;
    s.lift = 2.0;
    s.arch = 1.5;
    s.lobed = 0.42;
    // Lighter and bluer than the sward: the accent must survive shade,
    // where the litter substrate beneath it is nearly black.
    s.tint = float3 (0.155, 0.335, 0.108) * (0.88 + 0.24 * wet);
  } else if (family == UNDERGROWTH_FLOWER) {
    s.reach = scale * 0.07;
    s.climb =
      scale * drift.stem * (0.80 + 0.30 * undergrowth_hash (identity, 12u));
    // A stem's height was always relative to the sward around it. Once
    // that sward has retired into the substrate, the flower re-references
    // to the ground the substrate draws and crouches smoothly with the
    // blades' own resolved fraction — a far drift colours its hillside
    // instead of scattering skyline confetti over an invisible meadow.
    s.climb *= mix (0.30, 1.0, moppe_grass_resolved_fraction (blade_pixels));
    s.width = scale * 0.010;
    // A support must not vanish while what it carries is still resolved:
    // a head whose stem has gone subpixel reads as confetti floating over
    // the sward, so the stem holds the same minimum footprint the head
    // does. It stays a dark line, so it widens without brightening.
    const float stem_px =
      moppe_feature_pixels (2.0 * s.width, focal_pixels, camera_distance);
    s.width *= clamp (0.7 / max (stem_px, 0.05), 1.0, 3.5);
    s.lift = 1.0;
    s.arch = 0.0;
    s.lobed = 0.0;
    s.tint = grass.blade_tint * 0.88;
    // The head keeps species colour only while it spans enough pixels for
    // a jittered sample to revisit. Past that it descends the ladder's
    // middle rung: the disc widens to the smallest footprint a sample can
    // hit twice and dims in proportion, while its chroma collapses toward
    // the same wash chromaticity the terrain substrate carries -- so a
    // receding drift dissolves into its own wash with neither a seam nor
    // a scintilla.
    const float head_px =
      moppe_feature_pixels (2.0 * drift.head, focal_pixels, camera_distance);
    const float resolved_head = smoothstep (1.1, 3.2, head_px);
    const float footprint = clamp (1.35 / max (head_px, 0.05), 1.0, 2.4);
    const float3 wash = moppe_flower_wash_tint (drift.tint);
    const float3 retired_tint = mix (grass.blade_tint, wash, 0.85);
    s.head = drift.head * (0.80 + 0.35 * undergrowth_hash (identity, 13u)) *
             smoothstep (0.05, 0.50, presence) * footprint;
    s.head_tint = mix (retired_tint, drift.tint, resolved_head) *
                  (0.92 + 0.16 * undergrowth_hash (identity, 29u)) / footprint;
  } else {
    // One strip is one blade. Density now supplies the field's visual mass,
    // letting the individual silhouette remain convincingly narrow.
    s.reach = scale * 0.16 * spread;
    s.climb = scale * 0.65 * spread;
    s.width = scale * MOPPE_GRASS_BLADE_WIDTH_METRES;
    s.lift = 1.22;
    s.arch = 0.20;
    s.lobed = 0.0;
    s.tint = grass.blade_tint;
    // Bank grasses trade their meadow spread for a taller upright profile.
    // This is a continuous habitat response, not a separately scattered row
    // of reeds that could drift away from the waterline.
    s.reach *= 1.0 - 0.18 * grass.riparian;
    s.climb *= 1.0 + 0.28 * grass.riparian;
    s.width *= 1.0 - 0.08 * grass.riparian;
    // The ladder's middle rung for the sward itself: once a blade is too
    // narrow to resolve, the survivors widen -- bounded to a tuft, never
    // a ribbon -- and carry the projected leaf area their retired
    // neighbours dropped. The field then coarsens in place instead of
    // visibly assembling a few metres in front of the rider.
    s.width *= clamp (0.85 / max (blade_pixels, 0.05), 1.0, 4.0);
  }
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
  // rather than in a cut edge. A flower spends its sections differently:
  // the stem runs root to top across the first two, and the last two
  // become the lower and upper edges of the petal head.
  for (uint step = 0; step < MOPPE_UNDERGROWTH_SECTIONS_PER_SHOOT; ++step) {
    const bool head = s.family == UNDERGROWTH_FLOWER && step >= 2u;
    const float t =
      s.family == UNDERGROWTH_FLOWER
        ? min (float (step), 1.0)
        : float (step) / float (MOPPE_UNDERGROWTH_SECTIONS_PER_SHOOT - 1);
    const float rise = s.lift * t - s.arch * t * t;
    float3 spine = s.root + s.out * (s.reach * t) + s.up * (s.climb * rise);
    const float upper = smoothstep (0.0, 0.58, t) * response;
    spine += float3 (away.x, 0.0, away.y) * (0.42 * upper);
    spine -= s.up * (s.climb * rise * 0.72 * response);

    float3 face_normal;
    float3 edge;
    float half_width;
    float3 section_tint = s.tint;
    float petal = 0.0;
    if (head) {
      // The head is a disc tilted between its stem's up and the camera:
      // seen from above it faces the sky, from the saddle it shows its
      // colour, and the basis never degenerates because up rules the
      // vertical and the stem's own out-direction rules the horizontal.
      // Each head rolls to its own stable angle — camera-facing quads
      // that all share one orientation read as a fall of confetti — and
      // its lower edge shades darker, so the flat quad carries a form.
      const float3 to_cam = normalize (u.camera_pos.xyz - spine);
      const float3 facing = normalize (s.up + 1.25 * to_cam);
      const float roll = 6.2831853 * undergrowth_hash (identity, 41u);
      const float3 axis_r = normalize (cross (facing, s.out));
      const float3 axis_u = cross (axis_r, facing);
      const float3 disc_r = axis_r * cos (roll) + axis_u * sin (roll);
      const float3 disc_u = axis_u * cos (roll) - axis_r * sin (roll);
      spine += facing * 0.012 + s.up * (0.30 * s.head) +
               disc_u * (s.head * (step == 2u ? -0.62 : 0.66));
      face_normal = facing;
      edge = disc_r;
      half_width = s.head * (step == 2u ? 0.98 : 0.80);
      section_tint = s.head_tint * (step == 2u ? 0.84 : 1.05);
      petal = 1.0;
    } else {
      // Grass keeps nearly one width until its pointed tip; a fern
      // broadens through the middle and carries lobes on that profile.
      const float taper = s.family == UNDERGROWTH_FERN
                            ? 0.30 + 1.60 * t - 1.70 * t * t
                            : 0.72 + 0.28 * sin (3.1415927 * t);
      const float lobes = 1.0 + s.lobed * (0.5 - 0.5 * cos (6.2831853 * t));
      half_width = s.width * (t >= 0.999 ? 0.0 : taper * lobes);
      const float3 side = normalize (cross (s.out, s.up));
      // A little twist keeps neighbouring blades from showing identical
      // faces. Ferns twist more strongly as their broad fronds fall.
      const float3 face = normalize (
        s.up + s.out * (0.55 * rise) +
        side * ((s.family == UNDERGROWTH_FERN ? 0.30 : 0.12) *
                sin (6.2831853 * undergrowth_hash (identity, 23u) + 2.2 * t)));
      face_normal = face;
      edge = normalize (cross (face, s.out));
    }

    // Fine grass edges scintillate easily, so the fast flick remains
    // subordinate to the shared low-frequency bend and disappears at range.
    // A petal head keeps no flick at all: its four corners share one bend,
    // so the disc sways rigidly on its stem.
    const float bend = 0.18 + 0.50 * t;
    const float flutter = (0.06 + 0.18 * t) * micro_detail * (1.0 - petal);
    const float3 left_base = spine - edge * half_width;
    const float3 right_base = spine + edge * half_width;
    const float3 left = moppe_wind (left_base, bend, flutter, u.params.x);
    const float3 right = moppe_wind (right_base, bend, flutter, u.params.x);
    const float3 previous_left =
      moppe_wind (left_base, bend, flutter, u.temporal.z);
    const float3 previous_right =
      moppe_wind (right_base, bend, flutter, u.temporal.z);

    // Exposure along the shoot stands in for how much sky its part of the
    // plant sees: a blade rises out of its own litter shadow, a head above
    // the sward catches everything, and a frond arcs over the litter
    // rather than through it, so its whole length stays in the light the
    // canopy admits. A far blade's dark base against the bright substrate
    // is a visible tick that arrives and departs with the count, so as a
    // blade's own pixels run out its exposure settles to the field mean:
    // retiring geometry vacates pixels that already look like the ground
    // that replaces them.
    const float ramp =
      s.family == UNDERGROWTH_FERN ? 0.40 + 0.60 * t : 0.12 + 0.88 * t;
    const float settled = s.family == UNDERGROWTH_GRASS
                            ? smoothstep (0.35, 0.95, blade_pixels)
                            : 1.0;
    const float exposure = head ? 1.0 : mix (0.72, ramp, settled);
    const float3 colour = section_tint * (0.58 + 0.66 * exposure);

    UndergrowthVaryings v;
    v.normal = face_normal;
    v.color = colour;
    v.exposure = exposure;
    v.petal = petal;

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

  // How many input pixels this shoot's width spans here. This, not
  // distance, is the temporal-stability measure: a jittered single sample
  // can only revisit a feature it can resolve, so per-feature lighting
  // variance must not outlive the feature's own pixels. This is the same
  // physical blade width that decides whether explicit blade geometry remains
  // repeatable over the stable substrate.
  const float focal_px = abs (u.view_proj[1][1]) * 0.5 * u.temporal.y;
  const float blade_px = moppe_grass_blade_pixels (focal_px, dist);
  const float resolvable = smoothstep (1.5, 4.0, blade_px);

  // A blade is one leaf thick and glows when the sun is behind it, which is
  // most of what tells this layer apart from painted ground. Exposure along
  // the shoot doubles as optical depth: a tip is one leaf, the litter at
  // the base is a mat, so Beer-Lambert pins the glow to the thin lit
  // fringe, and sun visibility keeps shaded blades from glowing at all.
  // Once a blade is subpixel, its individual twist term collapses to the
  // ensemble mean: the field keeps its aggregate warm glow, but stops
  // carrying per-blade HDR spikes no sample can hit twice.
  const float leaf_back = moppe_grass_leaf_back (n, l, resolvable);
  const float thin = exp (-2.0 * (1.0 - in.exposure));
  const float trans = leaf_back * thin * sun_visibility;
  const float3 view = to_frag / max (dist, 1e-4);
  const float toward = saturate (dot (view, l));

  // Cast shadow cools the fill to match the terrain beneath: shaded blades
  // are skylit only. What a blade transmits it does not also reflect, so
  // the diffuse sun term yields as transmission rises.
  const float3 shade_fill =
    mix (float3 (0.80, 0.92, 1.14), float3 (1.0), cast_light);
  float3 color =
    base *
    (shade_fill * moppe_hemisphere_light (u.ambient.rgb, n) +
     u.sun_diffuse.rgb * lambert * sun_visibility * (1.0 - 0.45 * trans));

  // Chlorophyll transmits green-yellow and almost no blue. Against the
  // light the term spends real HDR headroom -- transmitted radiance on a
  // backlit blade exceeds the reflected scene around it -- and the bloom
  // bright-pass supplies the halo for free. Transmittance rides on
  // sqrt(base): a thin blade passes far more light than its dark diffuse
  // albedo reflects. A petal is not chlorophyll: backlit it glows warm in
  // roughly its own colour, and a little less fiercely than a blade.
  const float3 chlorophyll =
    mix (moppe_grass_chlorophyll (), float3 (1.0, 0.90, 0.74), in.petal);
  const float lobe = moppe_grass_toward_lobe (toward);
  color += sqrt (base) * u.sun_diffuse.rgb * chlorophyll * trans * lobe *
           mix (3.2, 2.1, in.petal);

  // Real backlit grass also glints: a blade is a waxy cylinder carrying an
  // anisotropic streak along its axis (Kajiya-Kay). The axis is near
  // vertical with a small per-blade wobble borrowed from the normal's
  // horizontal part, which breaks the sheen into shimmer.
  // The silver stays sun-gated like the glow: without the against-the-light
  // factor a whole cross-lit hillside turns chalk, and a field that always
  // shimmers is just a paler ground texture.
  //
  // Temporal discipline follows the same rule as blade flutter: fine
  // sparkle is meaningful only while a blade spans several pixels. As the
  // blade's pixels run out the streak widens (the highlight's footprint
  // must not shrink below what one jittered sample can hit twice) and the
  // per-blade wobble straightens, so unresolved blades glint coherently as
  // one sheen instead of scintillating individually.
  const float wobble = 0.25 * resolvable;
  const float3 axis = normalize (float3 (wobble * n.x, 1.0, wobble * n.z));
  const float3 half_dir = normalize (l - view);
  const float along = dot (axis, half_dir);
  const float streak = mix (10.0, 64.0, resolvable);
  const float glint = pow (sqrt (saturate (1.0 - along * along)), streak) *
                      (0.10 + 0.90 * toward * toward);
  const float steady = smoothstep (0.40, 2.0, blade_px);
  // Petals are matte; the waxy axial streak belongs to the blades.
  color += u.sun_specular.rgb * glint * sun_visibility * steady *
           mix (0.40, 1.0, resolvable) * (0.30 + 0.70 * in.exposure) * 2.4 *
           (1.0 - 0.78 * in.petal);

  const float3 fog_c =
    moppe_warmed_fog (u.fog_color.rgb, to_frag / max (dist, 1e-4), l);
  color = mix (color, fog_c, smoothstep (0.0, 0.9, fog));
  // The layer's flat reactive weight already assumes stochastic blade
  // fragments; raising it where the glow fires rejected exactly the
  // history that keeps a jittered single-sample field calm, and the
  // fringe shimmered. The glow rides on the same weight as the blades.
  return moppe_temporal_output (float4 (color, 1.0), in.motion, 0.55);
}
