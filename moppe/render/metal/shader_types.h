// Uniform structs shared between C++ and MSL.  Every vector slot is
// a float4 and matrices are 64-byte column-major, so the layouts
// agree on both sides without packed-type tricks.  Keep scalars in
// groups of four.

#ifndef MOPPE_SHADER_TYPES_H
#define MOPPE_SHADER_TYPES_H

#ifdef __METAL_VERSION__
#include <metal_stdlib>
#define MOPPE_SHADER_ALIGN
typedef metal::float4x4 MoppeMat4;
typedef metal::float4 MoppeFloat4;
typedef metal::uint4 MoppeUint4;
#else
#include <cstdint>
#define MOPPE_SHADER_ALIGN alignas (16)
struct MOPPE_SHADER_ALIGN MoppeMat4 {
  float m[16];
};
struct MoppeFloat4 {
  float x, y, z, w;
};
struct MoppeUint4 {
  std::uint32_t x, y, z, w;
};
#endif

// Buffer indices (vertex stage).
#define MOPPE_BUF_VERTICES 0
#define MOPPE_BUF_FRAME 1
#define MOPPE_BUF_DRAW 2
#define MOPPE_BUF_CHUNK 3
#define MOPPE_BUF_PREVIOUS_VERTICES 4
#define MOPPE_BUF_FOREST 5

// Texture indices (fragment stage).
#define MOPPE_TEX_COLOR 0
#define MOPPE_TEX_GRASS 0
#define MOPPE_TEX_DIRT 1
#define MOPPE_TEX_SNOW 2
#define MOPPE_TEX_SHADOW 3
#define MOPPE_TEX_ROCK 4
#define MOPPE_TEX_TERRAIN_OVERLAY 6
#define MOPPE_TEX_TERRAIN_LANDSCAPE 7
#define MOPPE_TEX_TERRAIN_WATER 8
#define MOPPE_TEX_TERRAIN_GROUND 9
#define MOPPE_TEX_TERRAIN_NORMALS 10 /* fragment stage */
#define MOPPE_TEX_FOREST_CANOPY 11
#define MOPPE_TEX_FOREST_DENSITY 12
#define MOPPE_TEX_FOREST_LITTER 13
#define MOPPE_TEX_SCENE 0
#define MOPPE_TEX_BLOOM 1        /* post passes */
#define MOPPE_TEX_POST_DEPTH 2   /* light shafts: stored scene depth */
#define MOPPE_TEX_HEIGHTS 0      /* vertex stage */
#define MOPPE_TEX_NORMALS 1      /* vertex stage */
#define MOPPE_TEX_WATER_LEVELS 3 /* ocean vertex stage */
#define MOPPE_TEX_WATER_LEVELS_FRAGMENT 1
#define MOPPE_TEX_WATER_FLOW_FRAGMENT 2
#define MOPPE_TEX_WATER_GEOLOGY_FRAGMENT 4

// Buffer indices for the isolated reflection-geometry atelier.
#define MOPPE_BUF_REFLECTION_AS 0
#define MOPPE_BUF_REFLECTION_UNIFORMS 1
#define MOPPE_BUF_REFLECTION_OUTPUT 2
#define MOPPE_BUF_REFLECTION_VERTICES 3

// Texture indices for the Goal 1 water-reflection signal. Rendered water
// inputs and ray-query results stay as distinct images so later filtering can
// consume or reject each piece of evidence independently.
#define MOPPE_TEX_REFLECTION_ORIGIN 0
#define MOPPE_TEX_REFLECTION_OPTICAL_NORMAL 1
#define MOPPE_TEX_REFLECTION_RADIANCE 2
#define MOPPE_TEX_REFLECTION_HIT_NORMAL 3
#define MOPPE_TEX_REFLECTION_HIT_DISTANCE 4
#define MOPPE_TEX_REFLECTION_VALIDITY 5

struct MOPPE_SHADER_ALIGN MoppeFrameUniforms {
  MoppeMat4 view_proj;            // jittered current world -> clip
  MoppeMat4 unjittered_view_proj; // current world -> reference clip
  MoppeMat4 previous_view_proj;   // previous world -> reference clip
  MoppeMat4 light_matrix;         // world -> biased shadow uv/z
  MoppeFloat4 camera_pos;         // xyz; w unused
  MoppeFloat4 sun_dir;            // xyz world-space toward sun
  MoppeFloat4 sun_diffuse;        // rgb
  MoppeFloat4 sun_specular;       // rgb
  MoppeFloat4 ambient;            // rgb
  MoppeFloat4 fog_color;          // rgb; w = fog_scale
  MoppeFloat4 misc;               // x=time, y=cloudiness, z=sea, w=land relief
  MoppeFloat4 shadow;             // x=strength, y=shadow texel
  MoppeFloat4 temporal;           // xy=input pixels, z=previous time, w=enabled
};

// A primary-ray camera and one RGBA16F diagnostic target. The target is split
// into normal, distance, primitive/barycentric, and hit-mask quadrants so the
// acceleration structure remains independently inspectable before water or
// temporal reconstruction depends on it.
struct MOPPE_SHADER_ALIGN MoppeReflectionGeometryUniforms {
  MoppeFloat4 camera;       // xyz=origin, w=max ray distance
  MoppeFloat4 camera_right; // xyz
  MoppeFloat4 camera_up;    // xyz
  MoppeFloat4 camera_back;  // xyz; camera forward is -back
  MoppeFloat4 projection;   // xy=perspective scale, zw=output dimensions
  MoppeFloat4 output;       // x=half4 row stride, yzw=reserved
};

// One sparse ray per valid low-resolution standing-water sample. The camera
// basis is unnecessary because the raster input already names the exact
// world-space origin and optical normal.
struct MOPPE_SHADER_ALIGN MoppeWaterReflectionUniforms {
  MoppeFloat4 camera;     // xyz=origin, w=max ray distance
  MoppeFloat4 sun_dir;    // xyz=toward sun
  MoppeFloat4 sun_colour; // rgb=linear diffuse radiance
  MoppeFloat4 ambient;    // rgb=linear ambient radiance
  MoppeFloat4 fog_colour; // rgb=linear sky/fog colour
  MoppeFloat4 dimensions; // xy=signal dimensions, zw=diagnostic dimensions
  MoppeFloat4 output;     // x=diagnostic half4 row stride, yzw=reserved
};

// Per-draw transform for retained meshes (identity for draw lists,
// whose vertices are already world space).
struct MOPPE_SHADER_ALIGN MoppeDrawUniforms {
  MoppeMat4 model;
  MoppeMat4 previous_model;
  MoppeFloat4 nrm0, nrm1, nrm2; // normal-matrix columns
  MoppeFloat4 temporal;         // x=previous vertex buffer, y=reactivity
};

struct MOPPE_SHADER_ALIGN MoppeTerrainUniforms {
  MoppeMat4 view_proj; // scene: reversed-Z; shadow pass: light NDC
  MoppeMat4 unjittered_view_proj;
  MoppeMat4 previous_view_proj;
  MoppeMat4 light_matrix; // world -> biased shadow uv/z
  MoppeFloat4 camera_pos;
  MoppeFloat4 sun_dir;
  MoppeFloat4 sun_diffuse;
  MoppeFloat4 sun_specular;
  MoppeFloat4 ambient;
  MoppeFloat4 fog_color; // rgb; w = fog_scale
  MoppeFloat4
    params0; // x=grid_step_x, y=height_scale_y, z=grid_step_z, w=tex_scale
  MoppeFloat4 params1;  // x=height_scale_norm, y=sea_level, z=shadow_strength,
                        // w=shadow_texel
  MoppeFloat4 params2;  // x=time, y=cloudiness
  MoppeFloat4 params3;  // xy=1/forest-period, z=canopy field, w=litter field
  MoppeFloat4 params4;  // x=overlay ramp + 1, y=min, z=max, w=opacity
  MoppeFloat4 params5;  // x=topology opacity, y=water, z=materials
  MoppeFloat4 params6;  // x=fragment normals, y=shore band metres
  MoppeFloat4 params7;  // x=filtered snow-support slope enabled,
                        // y=reserved,
                        // z=land relief above sea level in metres,
                        // w=grass cover boost (1 = habitat-driven)
  MoppeFloat4 temporal; // xy=input pixels, z=previous time, w=enabled
};

// Per-chunk terrain instance data.
struct MOPPE_SHADER_ALIGN MoppeChunkUniforms {
  int origin_x;
  int origin_z;
  float step; // source texels per rendered grid cell
  int verts_per_row;
  float morph_start; // horizontal world distance
  float morph_end;
  float parent_step; // next coarser source-texel step
  int pad;
  MoppeFloat4 world_offset; // x/z translated periodic image
};
#ifndef __METAL_VERSION__
static_assert (sizeof (MoppeChunkUniforms) == 48,
               "terrain chunk uniforms must match Metal layout");
static_assert (alignof (MoppeChunkUniforms) == 16,
               "shader records require 16-byte GPU alignment");
#endif

struct MOPPE_SHADER_ALIGN MoppeSkyUniforms {
  MoppeMat4 view_proj; // rotation-only view * reversed-Z proj
  MoppeMat4 unjittered_view_proj;
  MoppeMat4 previous_view_proj;
  MoppeFloat4 sun_dir;
  MoppeFloat4 fog_color;
  MoppeFloat4 params;   // x=time, y=sun_height, z=cloudiness
  MoppeFloat4 temporal; // xy=input pixels, z=previous time, w=enabled
};

struct MOPPE_SHADER_ALIGN MoppeOceanUniforms {
  MoppeMat4 view_proj;
  MoppeMat4 unjittered_view_proj;
  MoppeMat4 previous_view_proj;
  MoppeMat4 light_matrix; // world -> biased shadow uv/z
  MoppeFloat4 camera_pos;
  MoppeFloat4 sun_dir;
  MoppeFloat4 sun_diffuse;
  MoppeFloat4 sun_specular;
  MoppeFloat4 ambient;
  MoppeFloat4 fog_color; // rgb; w = fog_scale
  MoppeFloat4 params;    // x=time, y=sea level, z=cloudiness,
                         // w=standing-water raster enabled
  MoppeFloat4 shore;     // x=1/step_x, y=1/step_z,
                         // z=height_scale, w=grid width (0=off)
  MoppeFloat4 world_offset;
  MoppeFloat4 shadow;   // x=strength, y=shadow texel
  MoppeFloat4 tiles;    // xy=origin tile indices, z=tiles per side,
                        // w=fine radius (+: coarse pass discards
                        // inside; -: lattice pass discards outside)
  MoppeFloat4 current;  // x=flow raster enabled, y=geology raster enabled
  MoppeFloat4 temporal; // xy=input pixels, z=previous time, w=enabled
};

// Undergrowth is generated, never stored. The object stage walks a window of
// ground tiles around the camera and keeps the ones whose fields say
// something grows there; the mesh stage turns each survivor into shoots. So
// what crosses this boundary is where the camera is and how the world's
// lattice is laid out -- never a plant. These derived counts are shared with
// the pipeline setup so a density change cannot silently exceed Metal's mesh
// output limits.
#define MOPPE_UNDERGROWTH_SHOOTS_PER_TILE 32
#define MOPPE_UNDERGROWTH_SECTIONS_PER_SHOOT 4
#define MOPPE_UNDERGROWTH_VERTICES_PER_SHOOT                                   \
  (MOPPE_UNDERGROWTH_SECTIONS_PER_SHOOT * 2)
#define MOPPE_UNDERGROWTH_PRIMITIVES_PER_SHOOT                                 \
  ((MOPPE_UNDERGROWTH_SECTIONS_PER_SHOOT - 1) * 2)
#define MOPPE_UNDERGROWTH_MESH_THREADS MOPPE_UNDERGROWTH_SHOOTS_PER_TILE
#define MOPPE_UNDERGROWTH_MESH_VERTICES                                        \
  (MOPPE_UNDERGROWTH_MESH_THREADS * MOPPE_UNDERGROWTH_VERTICES_PER_SHOOT)
#define MOPPE_UNDERGROWTH_MESH_PRIMITIVES                                      \
  (MOPPE_UNDERGROWTH_MESH_THREADS * MOPPE_UNDERGROWTH_PRIMITIVES_PER_SHOOT)

// The mesoscale sward mesh is the conservative top envelope of one
// terrain-following density field. Its fragment shader integrates the column
// beneath each entry point; this subdivision only bounds dispatch and does not
// create a population of grass proxies.
#define MOPPE_SWARD_CANOPY_CELLS 4
#define MOPPE_SWARD_ENSEMBLE_HEIGHT_METRES 0.42f
#define MOPPE_SWARD_CANOPY_VERTICES_PER_SIDE (MOPPE_SWARD_CANOPY_CELLS + 1)
#define MOPPE_SWARD_CANOPY_MESH_THREADS                                        \
  (MOPPE_SWARD_CANOPY_VERTICES_PER_SIDE * MOPPE_SWARD_CANOPY_VERTICES_PER_SIDE)
#define MOPPE_SWARD_CANOPY_MESH_VERTICES MOPPE_SWARD_CANOPY_MESH_THREADS
#define MOPPE_SWARD_CANOPY_MESH_PRIMITIVES                                     \
  (MOPPE_SWARD_CANOPY_CELLS * MOPPE_SWARD_CANOPY_CELLS * 2)

#ifndef __METAL_VERSION__
static_assert (MOPPE_UNDERGROWTH_MESH_VERTICES <= 256,
               "undergrowth meshlet exceeds Metal vertex limit");
static_assert (MOPPE_UNDERGROWTH_MESH_PRIMITIVES <= 512,
               "undergrowth meshlet exceeds Metal primitive limit");
static_assert (MOPPE_SWARD_CANOPY_MESH_VERTICES <= 256,
               "sward canopy meshlet exceeds Metal vertex limit");
static_assert (MOPPE_SWARD_CANOPY_MESH_PRIMITIVES <= 512,
               "sward canopy meshlet exceeds Metal primitive limit");
#endif

struct MOPPE_SHADER_ALIGN MoppeUndergrowthUniforms {
  MoppeMat4 view_proj;
  MoppeMat4 unjittered_view_proj;
  MoppeMat4 previous_view_proj;
  MoppeMat4 light_matrix; // world -> biased shadow uv/z
  MoppeFloat4 camera_pos;
  MoppeFloat4 previous_camera_pos;
  MoppeFloat4 sun_dir;
  MoppeFloat4 sun_diffuse;
  MoppeFloat4 sun_specular;
  MoppeFloat4 ambient;
  MoppeFloat4 fog_color;   // rgb; w = fog_scale
  MoppeFloat4 lattice;     // x=1/step_x, y=1/step_z, z=height_scale,
                           // w=lattice width in samples
  MoppeFloat4 tiles;       // xy=origin tile indices, z=tiles per side,
                           // w=tile side in metres
  MoppeFloat4 params;      // x=time, y=cloudiness, z=terrain texture scale,
                           // w=density scale
  MoppeFloat4 interaction; // xyz=current mover,
                           // w=parting radius in metres
  MoppeFloat4 shadow;      // x=strength, y=shadow texel
  MoppeFloat4 relief;      // x=sea level, y=land relief,
                           // z=snow support available,
                           // w=standing-water levels available
  MoppeFloat4 temporal;    // xy=input pixels, z=previous time, w=enabled
  MoppeFloat4 lod;         // x=shoot reach, y=sward reach,
                           // z=actual canopy field available,
                           // w=leaf-litter field available
};

// One aggregate meshlet carries one height stratum of a 24-metre population
// patch. Projected error selects a four- or eight-metre world cell; during the
// transition one meshlet carries both complete nested partitions and
// allocates optical depth between them. A soft ellipsoid impostor needs four
// vertices and two faces. Four vertical density slices preserve crown volume
// after individual triangles become unrepeatable.
#define MOPPE_FOREST_MEAN_CROWN_DIAMETER_METRES 6.0f
#define MOPPE_FOREST_CANOPY_HEIGHT_RANGE_METRES 32.0f
#define MOPPE_FOREST_STAND_SUPPORT_METRES 24.0f
// Stand support rises over this 24-metre closure interval, and individual
// identity transfers to the stand quotient over this projected-crown one. The
// renderer's candidate filter evaluates the same rule as the shaders.
#define MOPPE_FOREST_STAND_OPEN_CLOSURE 0.12f
#define MOPPE_FOREST_STAND_CLOSED_CLOSURE 0.42f
#define MOPPE_FOREST_TRANSFER_END_CROWN_PIXELS 8.0f
#define MOPPE_FOREST_TRANSFER_START_CROWN_PIXELS 32.0f
#define MOPPE_FOREST_CANOPY_OBJECT_THREADS 64
#define MOPPE_FOREST_CANOPY_GRID_CELLS 6
#define MOPPE_FOREST_CANOPY_SAMPLE_STEP_METRES 4.0f
#define MOPPE_FOREST_CANOPY_PATCH_METRES                                       \
  (MOPPE_FOREST_CANOPY_GRID_CELLS * MOPPE_FOREST_CANOPY_SAMPLE_STEP_METRES)
#define MOPPE_FOREST_CANOPY_CELL_COUNT                                         \
  (MOPPE_FOREST_CANOPY_GRID_CELLS * MOPPE_FOREST_CANOPY_GRID_CELLS)
#define MOPPE_FOREST_CANOPY_SECOND_CELL_COUNT 9
#define MOPPE_FOREST_CANOPY_MESH_VERTICES                                      \
  (4 * (MOPPE_FOREST_CANOPY_CELL_COUNT + MOPPE_FOREST_CANOPY_SECOND_CELL_COUNT))
#define MOPPE_FOREST_CANOPY_MESH_PRIMITIVES                                    \
  (2 * (MOPPE_FOREST_CANOPY_CELL_COUNT + MOPPE_FOREST_CANOPY_SECOND_CELL_COUNT))
#define MOPPE_FOREST_CANOPY_MESH_THREADS 128
#define MOPPE_FOREST_CANOPY_DENSITY_SLICES 4
#define MOPPE_FOREST_CANOPY_STRATUM_DEPTH_RANGE 4.0f

struct MOPPE_SHADER_ALIGN MoppeForestInstance {
  MoppeFloat4 root_height; // xyz=root in metres, w=height in metres
  MoppeFloat4 up_radius;   // xyz=ground normal, w=crown radius in metres
  MoppeFloat4 ecology;     // x=cover, y=moisture, z=stand closure, w=autumn
  MoppeUint4 identity;     // x=seed, y=species, z=age, w=reserved
};

struct MOPPE_SHADER_ALIGN MoppeForestCandidate {
  uint tree;
  float pixels;
  float crown_pixels;
  uint copy; // a shadow caster's periodic image, 0..8 with 4 the centre
};

// The trunk forest's detail is a projected-size tier, and an individual's
// topology -- facet and mass counts, and which vertices each triangle joins
// -- is a function of its species and tier alone. The shaders place the
// vertices; the vertex-pulled path draws each (species, tier) class from an
// index buffer the CPU fills from these same functions, so the two paths
// cannot disagree about a tree's faces.
#define MOPPE_FOREST_TRUNK_TIERS 6u

static inline unsigned int moppe_forest_trunk_tier (float pixels) {
  return (pixels > 20.0f ? 1u : 0u) + (pixels > 30.0f ? 1u : 0u) +
         (pixels > 40.0f ? 1u : 0u) + (pixels > 60.0f ? 1u : 0u) +
         (pixels > 90.0f ? 1u : 0u);
}

struct MoppeTrunkTopology {
  unsigned int sides;       // trunk facets
  unsigned int crown_sides; // crown facets
  unsigned int masses;      // cones, or a birch's leaf clumps
  bool conifer;
  bool branches; // whether each clump hangs from a visible branch
};

static inline struct MoppeTrunkTopology
moppe_trunk_topology (bool conifer, unsigned int tier) {
  struct MoppeTrunkTopology t;
  t.conifer = conifer;
  t.sides = tier >= 5u ? 10u : tier >= 2u ? 7u : 5u;
  t.crown_sides = t.sides == 10u ? 9u : t.sides;
  t.masses = conifer ? (tier >= 4u   ? 5u
                        : tier >= 1u ? 4u
                                     : 3u)
                     : (tier >= 4u   ? 10u
                        : tier >= 1u ? 7u
                                     : 4u);
  t.branches = !conifer && tier >= 3u;
  return t;
}

static inline unsigned int
moppe_trunk_mass_vertex_count (struct MoppeTrunkTopology t) {
  // A cone is a ring, an apex, and a centre closing it from below. A leaf
  // clump is an irregular hexagonal bipyramid, its branch a three-sided
  // prism.
  return t.conifer ? t.crown_sides + 2u : 8u + (t.branches ? 6u : 0u);
}
static inline unsigned int
moppe_trunk_mass_primitive_count (struct MoppeTrunkTopology t) {
  return t.conifer ? 2u * t.crown_sides : 12u + (t.branches ? 6u : 0u);
}
static inline unsigned int
moppe_trunk_vertex_count (struct MoppeTrunkTopology t) {
  return t.sides * 4u + t.masses * moppe_trunk_mass_vertex_count (t);
}
static inline unsigned int
moppe_trunk_primitive_count (struct MoppeTrunkTopology t) {
  return t.sides * 6u + t.masses * moppe_trunk_mass_primitive_count (t);
}

// One triangle of an index buffer that the CPU fills from the same function
// the shaders' vertex numbering follows.
struct MoppeTriangle {
  unsigned int a, b, c;
};

static inline struct MoppeTriangle
moppe_trunk_triangle (struct MoppeTrunkTopology t, unsigned int primitive) {
  struct MoppeTriangle tri;
  const unsigned int trunk_primitives = t.sides * 6u;
  if (primitive < trunk_primitives) {
    // Three bands of quads between the trunk's four rings.
    const unsigned int band = primitive / (2u * t.sides);
    const unsigned int quad = (primitive / 2u) % t.sides;
    const unsigned int next = (quad + 1u) % t.sides;
    const unsigned int a = band * t.sides + quad, b = band * t.sides + next;
    const unsigned int c = a + t.sides, d = b + t.sides;
    tri.a = (primitive & 1u) ? b : a;
    tri.b = (primitive & 1u) ? d : b;
    tri.c = c;
    return tri;
  }
  const unsigned int local = primitive - trunk_primitives;
  const unsigned int per_mass = moppe_trunk_mass_primitive_count (t);
  const unsigned int mass = local / per_mass;
  const unsigned int p = local % per_mass;
  const unsigned int first =
    t.sides * 4u + mass * moppe_trunk_mass_vertex_count (t);
  const unsigned int n = t.crown_sides;
  if (t.conifer) {
    const unsigned int side = p % n;
    tri.a = first + side;
    tri.b = first + (side + 1u) % n;
    tri.c = first + (p < n ? n : n + 1u); // apex, then the closing centre
    return tri;
  }
  if (p < 12u) {
    // Bipyramid: six faces about the top tip, six about the bottom.
    const unsigned int k = p % 6u;
    const unsigned int e0 = first + 2u + k, e1 = first + 2u + (k + 1u) % 6u;
    tri.a = p < 6u ? first : first + 1u;
    tri.b = p < 6u ? e0 : e1;
    tri.c = p < 6u ? e1 : e0;
    return tri;
  }
  const unsigned int q = p - 12u;
  const unsigned int k = q / 2u;
  const unsigned int a = first + 8u + k, b = first + 8u + (k + 1u) % 3u;
  tri.a = (q & 1u) ? b : a;
  tri.b = (q & 1u) ? b + 3u : b;
  tri.c = a + 3u;
  return tri;
}

// Loose rocks: each is shaped from its record. Near rocks subdivide every
// icosahedron face into four; distant ones and shadows use the bare
// icosahedron. Boulder draws bind their records at the population slot the
// forest uses, since the two never draw together, and share the forest's
// frame uniforms.
#define MOPPE_BUF_BOULDERS MOPPE_BUF_FOREST
#define MOPPE_BOULDER_FINE_PIXELS 14.0f
#define MOPPE_BOULDER_COARSE_VERTICES 12u
#define MOPPE_BOULDER_COARSE_PRIMITIVES 20u
#define MOPPE_BOULDER_FINE_VERTICES 120u
#define MOPPE_BOULDER_FINE_PRIMITIVES 80u
// The icosahedron's twenty faces, as corner triples.
#define MOPPE_BOULDER_FACE_CORNERS                                             \
  0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10,  \
    2, 10, 7, 6, 7, 1, 8, 3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9,   \
    5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1

// A subdivided face has six vertices -- corners A, B, C in slots 0..2, edge
// midpoints AB, BC, CA in 3..5 -- and four triangles.
static inline struct MoppeTriangle
moppe_boulder_fine_triangle (unsigned int primitive) {
  const unsigned int first = (primitive / 4u) * 6u;
  struct MoppeTriangle tri;
  switch (primitive % 4u) {
  case 0u:
    tri.a = 0u, tri.b = 3u, tri.c = 5u;
    break;
  case 1u:
    tri.a = 3u, tri.b = 1u, tri.c = 4u;
    break;
  case 2u:
    tri.a = 5u, tri.b = 4u, tri.c = 2u;
    break;
  default:
    tri.a = 3u, tri.b = 4u, tri.c = 5u;
    break;
  }
  tri.a += first, tri.b += first, tri.c += first;
  return tri;
}

struct MOPPE_SHADER_ALIGN MoppeBoulderInstance {
  MoppeFloat4 centre_radius; // xyz=body centre in metres, w=radius in metres
  MoppeFloat4 up_moisture;   // xyz=ground normal, w=surface moisture
  MoppeUint4 identity;       // x=seed, yzw=reserved
};

struct MOPPE_SHADER_ALIGN MoppeBoulderCandidate {
  uint boulder;
  float pixels; // projected radius in scene pixels
  uint copy;    // a shadow caster's periodic image, 0..8 with 4 the centre
  uint reserved;
};

// Sun-shaft raymarch: rays come from a camera basis with the frustum
// half-extents folded in, and occlusion comes from projecting each march
// sample forward through the scene and light matrices — no inverse anywhere.
struct MOPPE_SHADER_ALIGN MoppeShaftUniforms {
  MoppeMat4 view_proj;     // unjittered scene projection
  MoppeMat4 light_matrix;  // biased shadow projection
  MoppeFloat4 camera_pos;  // xyz
  MoppeFloat4 ray_forward; // xyz unit view direction
  MoppeFloat4 ray_right;   // xyz right * tan(fov/2) * aspect
  MoppeFloat4 ray_up;      // xyz up * tan(fov/2)
  MoppeFloat4 sun_dir;     // xyz toward the sun
  MoppeFloat4 sun_color;   // rgb linear; w = strength
  MoppeFloat4 params;      // x=max distance m, y=extinction /m, z=steps
};

// Screen-space ambient occlusion over the stored scene depth. Positions
// reconstruct through the same camera-ray basis as the sun shafts; normals
// come from screen derivatives of those positions.
struct MOPPE_SHADER_ALIGN MoppeGtaoUniforms {
  MoppeFloat4 camera_pos;  // xyz
  MoppeFloat4 ray_forward; // xyz unit view direction
  MoppeFloat4 ray_right;   // xyz right * tan(fov/2) * aspect
  MoppeFloat4 ray_up;      // xyz up * tan(fov/2)
  MoppeFloat4 params;      // x=world radius m, y=strength, z=near m, w=far m
  MoppeFloat4 blur;        // xy=blur step in uv
};

struct MOPPE_SHADER_ALIGN MoppeForestUniforms {
  MoppeMat4 view_proj;
  MoppeMat4 unjittered_view_proj;
  MoppeMat4 previous_view_proj;
  MoppeMat4 light_matrix;
  MoppeFloat4 camera_pos;
  MoppeFloat4 sun_dir;
  MoppeFloat4 sun_diffuse;
  MoppeFloat4 sun_specular;
  MoppeFloat4 ambient;
  MoppeFloat4 fog_color; // rgb; w=fog scale
  MoppeFloat4 world;     // x=period x, y=period z, z=tree count
  MoppeFloat4 params;    // x=time, y=cloudiness, z=sea, w=land relief
  MoppeFloat4 shadow;    // x=strength, y=shadow texel
  MoppeFloat4 temporal;  // xy=input pixels, z=previous time, w=enabled
};

// Falling leaves: one leaf per cell of a world-anchored lattice around the
// camera, a block of cells per mesh threadgroup.
#define MOPPE_LEAF_FALL_BLOCK_CELLS 8
#define MOPPE_LEAF_FALL_THREADS 64
#define MOPPE_LEAF_FALL_GRID_BLOCKS 8

struct MOPPE_SHADER_ALIGN MoppeLeafFallUniforms {
  MoppeMat4 view_proj;
  MoppeMat4 unjittered_view_proj;
  MoppeMat4 previous_view_proj;
  MoppeMat4 light_matrix;
  MoppeFloat4 camera_pos;
  MoppeFloat4 sun_dir;
  MoppeFloat4 sun_diffuse;
  MoppeFloat4 ambient;
  MoppeFloat4 fog_color; // rgb; w=fog scale
  MoppeFloat4 lattice;   // x=1/step_x, y=1/step_z, z=height scale,
                         // w=lattice width in samples
  MoppeFloat4 field;     // xy=1/forest period
  MoppeFloat4 grid;      // xy=first cell index, z=cell metres, w=reach metres
  MoppeFloat4 params;    // x=time, y=cloudiness, z=sea, w=land relief
  MoppeFloat4 shadow;    // x=strength, y=shadow texel
  MoppeFloat4 temporal;  // xy=input pixels, z=previous time, w=enabled
};

struct MOPPE_SHADER_ALIGN MoppeForestCanopyUniforms {
  MoppeMat4 view_proj;
  MoppeMat4 unjittered_view_proj;
  MoppeMat4 previous_view_proj;
  MoppeFloat4 camera_pos;
  MoppeFloat4 previous_camera_pos;
  MoppeFloat4 sun_dir;
  MoppeFloat4 sun_diffuse;
  MoppeFloat4 ambient;
  MoppeFloat4 fog_color;
  MoppeFloat4 terrain;  // xy=terrain samples/metre, z=height scale, w=width
  MoppeFloat4 field;    // xy=1/world period, z=stored height range, w=litter
  MoppeFloat4 tiles;    // xy=world patch origin, z=patches/side, w=patch side
  MoppeFloat4 params;   // x=time, y=cloudiness, z=sea, w=land relief
  MoppeFloat4 temporal; // xy=input pixels, z=previous time, w=enabled
  MoppeFloat4 lod;      // x=window reach, y=cell side
};

#ifndef __METAL_VERSION__
static_assert (sizeof (MoppeMat4) == 64,
               "shader matrices must remain four float4 columns");
static_assert (alignof (MoppeMat4) == 16,
               "shader matrices require GPU alignment");
static_assert (sizeof (MoppeForestInstance) == 64,
               "forest instance must remain one cache line");
static_assert (alignof (MoppeForestInstance) == 16,
               "forest instances require GPU alignment");
static_assert (sizeof (MoppeForestCandidate) == 16,
               "forest candidate must remain one SIMD lane");
static_assert (sizeof (MoppeBoulderInstance) == 48,
               "boulder instance layout must match the shader");
static_assert (sizeof (MoppeBoulderCandidate) == 16,
               "boulder candidate layout must match the shader");
static_assert (MOPPE_FOREST_CANOPY_MESH_VERTICES <= 256,
               "forest canopy meshlet exceeds Metal vertex limit");
static_assert (MOPPE_FOREST_CANOPY_MESH_PRIMITIVES <= 512,
               "forest canopy meshlet exceeds Metal primitive limit");
#endif

struct MOPPE_SHADER_ALIGN MoppeDustEmission {
  MoppeFloat4 position_birth; // xyz position, w birth time
  MoppeFloat4 velocity_count; // xyz base velocity, w particle count
  MoppeFloat4 color_id;       // rgb display-space color, w emission id
  MoppeFloat4 style;          // size, life, gravity, spread
  MoppeFloat4 shape;          // x=1 for a hard-edged flake that keeps its size
};

struct MOPPE_SHADER_ALIGN MoppeDustUniforms {
  MoppeFloat4 camera_right; // xyz
  MoppeFloat4 camera_up;    // xyz
  MoppeFloat4 params;       // x=current time, y=previous time
};

// Fullscreen quad passes: present, motion-blur ghosts, underwater,
// bloom bright/blur.
struct MOPPE_SHADER_ALIGN MoppeQuadUniforms {
  MoppeFloat4 tint;   // rgb * alpha blend factor
  MoppeFloat4 params; // x=uv zoom, y=time, zw=blur texel step
  MoppeFloat4 sun;    // xy=sun screen uv, z=flare strength,
                      // w=aspect (present pass only)
};

struct MOPPE_SHADER_ALIGN MoppeHudUniforms {
  MoppeMat4 proj;     // point coords, y-down
  MoppeFloat4 params; // x=extended-linear output, y=pixels per point
};

// Slug glyph instances (render::GlyphQuad) and their outline buffers. The
// instances are vertex-stage data; the curve and band buffers are read by
// the fragment stage only.
#define MOPPE_BUF_GLYPH_QUADS 0
#define MOPPE_BUF_GLYPH_CURVES 2
#define MOPPE_BUF_GLYPH_BANDS 3

struct MOPPE_SHADER_ALIGN MoppeGlyphQuad {
  MoppeFloat4 origin; // xyz=target position of em (0,0); w=filter pixels
  MoppeFloat4 axis_x; // xyz=target displacement per em along u
  MoppeFloat4 axis_y; // xyz=target displacement per em along v (font up)
  MoppeFloat4 bounds; // exact em bounds: min u, min v, max u, max v
  MoppeFloat4 color;  // straight-alpha display RGBA
  MoppeUint4 glyph;   // band offset, horizontal bands, vertical bands
};

#undef MOPPE_SHADER_ALIGN

#endif
