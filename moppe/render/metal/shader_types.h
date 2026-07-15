// Uniform structs shared between C++ and MSL.  Every vector slot is
// a float4 and matrices are 64-byte column-major, so the layouts
// agree on both sides without packed-type tricks.  Keep scalars in
// groups of four.

#ifndef MOPPE_SHADER_TYPES_H
#define MOPPE_SHADER_TYPES_H

#ifdef __METAL_VERSION__
#include <metal_stdlib>
typedef metal::float4x4 MoppeMat4;
typedef metal::float4 MoppeFloat4;
#else
#include <cstdint>
struct MoppeMat4 {
  float m[16];
};
struct MoppeFloat4 {
  float x, y, z, w;
};
#endif

// Buffer indices (vertex stage).
#define MOPPE_BUF_VERTICES 0
#define MOPPE_BUF_FRAME 1
#define MOPPE_BUF_DRAW 2
#define MOPPE_BUF_CHUNK 3

// Texture indices (fragment stage).
#define MOPPE_TEX_COLOR 0
#define MOPPE_TEX_GRASS 0
#define MOPPE_TEX_DIRT 1
#define MOPPE_TEX_SNOW 2
#define MOPPE_TEX_SHADOW 3
#define MOPPE_TEX_ROCK 4
#define MOPPE_TEX_PREVIOUS_SHADOW 5
#define MOPPE_TEX_TERRAIN_OVERLAY 6
#define MOPPE_TEX_TERRAIN_MOISTURE 7
#define MOPPE_TEX_TERRAIN_WATER 8
#define MOPPE_TEX_TERRAIN_GEOLOGY 9
#define MOPPE_TEX_TERRAIN_NORMALS 10 /* fragment stage */
#define MOPPE_TEX_TERRAIN_SHORE 11
#define MOPPE_TEX_TERRAIN_PATHS 12
#define MOPPE_TEX_TERRAIN_FOREST 13
#define MOPPE_TEX_SCENE 0
#define MOPPE_TEX_BLOOM 1            /* post passes */
#define MOPPE_TEX_HEIGHTS 0          /* vertex stage */
#define MOPPE_TEX_NORMALS 1          /* vertex stage */
#define MOPPE_TEX_PREVIOUS_HEIGHTS 2 /* Terrain Lab transition */
#define MOPPE_TEX_WATER_LEVELS 3     /* ocean vertex stage */
#define MOPPE_TEX_WATER_LEVELS_FRAGMENT 1
#define MOPPE_TEX_WATER_FLOW_FRAGMENT 2
#define MOPPE_TEX_WATER_GEOLOGY_FRAGMENT 4

struct MoppeFrameUniforms {
  MoppeMat4 view_proj;
  MoppeMat4 light_matrix;   // world -> biased shadow uv/z
  MoppeFloat4 camera_pos;   // xyz; w unused
  MoppeFloat4 sun_dir;      // xyz world-space toward sun
  MoppeFloat4 sun_diffuse;  // rgb
  MoppeFloat4 sun_specular; // rgb
  MoppeFloat4 ambient;      // rgb
  MoppeFloat4 fog_color;    // rgb; w = fog_scale
  MoppeFloat4 misc;         // x = time
  MoppeFloat4 shadow;       // x=strength, y=shadow texel
};

// Per-draw transform for retained meshes (identity for draw lists,
// whose vertices are already world space).
struct MoppeDrawUniforms {
  MoppeMat4 model;
  MoppeFloat4 nrm0, nrm1, nrm2; // normal-matrix columns
};

struct MoppeTerrainUniforms {
  MoppeMat4 view_proj;    // scene: reversed-Z; shadow pass: light NDC
  MoppeMat4 light_matrix; // world -> biased shadow uv/z
  MoppeFloat4 camera_pos;
  MoppeFloat4 sun_dir;
  MoppeFloat4 sun_diffuse;
  MoppeFloat4 sun_specular;
  MoppeFloat4 ambient;
  MoppeFloat4 fog_color; // rgb; w = fog_scale
  MoppeFloat4
    params0; // x=grid_step_x, y=height_scale_y, z=grid_step_z, w=tex_scale
  MoppeFloat4 params1; // x=height_scale_norm, y=sea_level, z=shadow_strength,
                       // w=shadow_texel
  MoppeFloat4 params2; // x=projection, y=torus R, z=torus r, w=height scale
  MoppeFloat4 params3; // x=derive normals, y=periodic
                       // z=height blend, w=previous shadow texel
  MoppeFloat4 params4; // x=overlay ramp + 1, y=min, z=max, w=opacity
  MoppeFloat4 params5; // x=topology opacity, y=water, z=moisture,
                       // w=geology
  MoppeFloat4 params6; // x=fragment normals, y=shore, z=trail influence,
                       // w=forest cover
};

// Per-chunk terrain instance data.
struct MoppeChunkUniforms {
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
#endif

struct MoppeSkyUniforms {
  MoppeMat4 view_proj; // rotation-only view * reversed-Z proj
  MoppeFloat4 sun_dir;
  MoppeFloat4 fog_color;
  MoppeFloat4 params; // x=time, y=sun_height, z=cloudiness
};

struct MoppeOceanUniforms {
  MoppeMat4 view_proj;
  MoppeMat4 light_matrix; // world -> biased shadow uv/z
  MoppeFloat4 camera_pos;
  MoppeFloat4 sun_dir;
  MoppeFloat4 sun_diffuse;
  MoppeFloat4 sun_specular;
  MoppeFloat4 ambient;
  MoppeFloat4 fog_color; // rgb; w = fog_scale
  MoppeFloat4 params;    // x=time, y=sea level, z=periodic,
                         // w=standing-water raster enabled
  MoppeFloat4 shore;     // x=1/step_x, y=1/step_z,
                         // z=height_scale, w=grid width (0=off)
  MoppeFloat4 world_offset;
  MoppeFloat4 shadow;  // x=strength, y=shadow texel
  MoppeFloat4 tiles;   // xy=origin tile indices, z=tiles per side,
                       // w=fine radius (+: coarse pass discards
                       // inside; -: lattice pass discards outside)
  MoppeFloat4 current; // x=flow raster enabled, y=geology raster enabled
};

struct MoppeDustEmission {
  MoppeFloat4 position_birth; // xyz position, w birth time
  MoppeFloat4 velocity_count; // xyz base velocity, w particle count
  MoppeFloat4 color_id;       // rgb display-space color, w emission id
  MoppeFloat4 style;          // size, life, gravity, spread
};

struct MoppeDustUniforms {
  MoppeFloat4 camera_right; // xyz
  MoppeFloat4 camera_up;    // xyz
  MoppeFloat4 params;       // x logical time
};

// Fullscreen quad passes: present, motion-blur ghosts, underwater,
// bloom bright/blur.
struct MoppeQuadUniforms {
  MoppeFloat4 tint;   // rgb * alpha blend factor
  MoppeFloat4 params; // x=uv zoom, y=time, zw=blur texel step
  MoppeFloat4 sun;    // xy=sun screen uv, z=flare strength,
                      // w=aspect (present pass only)
};

struct MoppeHudUniforms {
  MoppeMat4 proj;     // point coords, y-down
  MoppeFloat4 params; // x=extended-linear output
};

#endif
