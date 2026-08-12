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
  MoppeFloat4 params3;  // xy=1/forest-period, z=actual canopy field available
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
                           // z=actual canopy field available
};

// A forest crosses the renderer boundary as stable individuals, not baked
// vertices. The object stage selects projected detail and schedules reusable
// trunk/crown assemblies; the mesh stage expands only those assemblies.
#define MOPPE_FOREST_OBJECT_THREADS 8
#define MOPPE_FOREST_PARTS_PER_TREE 8
/* The nearest trees expand into pure bough assemblies: a trunk and nine
   whorls of seven feathered boughs, one meshlet each. No shell or cone
   primitive exists at this tier; the crown is the union of its branches. */
#define MOPPE_FOREST_HERO_PARTS 64
#define MOPPE_FOREST_PAYLOAD_PARTS                                             \
  (MOPPE_FOREST_OBJECT_THREADS * MOPPE_FOREST_PARTS_PER_TREE)
#define MOPPE_FOREST_MESH_THREADS 192
#define MOPPE_FOREST_MESH_VERTICES 128
#define MOPPE_FOREST_MESH_PRIMITIVES 128

// One aggregate meshlet carries one height stratum of a 24-metre population
// patch. Projected error selects a four- or eight-metre world cell; during the
// transition one meshlet carries both complete nested partitions and
// allocates optical depth between them. A soft ellipsoid impostor needs four
// vertices and two faces. Four vertical density slices preserve crown volume
// after individual triangles become unrepeatable.
#define MOPPE_FOREST_MEAN_CROWN_DIAMETER_METRES 6.0f
#define MOPPE_FOREST_CANOPY_HEIGHT_RANGE_METRES 32.0f
#define MOPPE_FOREST_STAND_SUPPORT_METRES 24.0f
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
  MoppeFloat4 ecology;     // x=cover, y=moisture, z=stand closure
  MoppeUint4 identity;     // x=seed, y=species, z=age, w=reserved
};

struct MOPPE_SHADER_ALIGN MoppeForestCandidate {
  uint tree;
  float pixels;
  float crown_pixels;
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
  MoppeFloat4 field;    // xy=1/world period, z=stored height range, w=reserved
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
static_assert (MOPPE_FOREST_MESH_VERTICES <= 256,
               "forest meshlet exceeds Metal vertex limit");
static_assert (MOPPE_FOREST_MESH_PRIMITIVES <= 512,
               "forest meshlet exceeds Metal primitive limit");
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
  MoppeFloat4 params; // x=extended-linear output
};

#undef MOPPE_SHADER_ALIGN

#endif
