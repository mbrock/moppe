// Terrain: vertex-pulled from the height/normal textures (no vertex
// buffers), splat-textured by altitude and slope, PCF-shadowed.
// Port of shaders/test.vert + test.frag with explicit uniforms.
//
// The height texture is R32Float and is accessed via integer read().
// R32F is not linearly filterable on Apple GPUs before Apple9, so the
// subdivided near field performs its four-tap interpolation manually.

#include "grass_medium.h"

struct TerrainVaryings {
  float4 position [[position]];
  float3 world_pos;
  float3 normal; // world space
  float height;  // altitude in metres
  float fog;     // haze factor incl. valley mist
  float4 shadow_coord;
  float2 uv;
  float2 field_uv;
  float2 grid_coord;       // authoritative source-height lattice
  float2 mesh_coord;       // actual rendered lattice
  float lod_step [[flat]]; // source texels per rendered grid edge
  float2 motion [[center_no_perspective]];
};

// Catmull-Rom reconstruction and its derivative. The final 2D height is
// kept within the four corners of the source cell, so smoothing cannot grow a
// new peak or dig a new pit between authoritative height samples.
static inline float2
terrain_cubic (float p0, float p1, float p2, float p3, float t) {
  const float a = 0.5 * (-p0 + 3.0 * p1 - 3.0 * p2 + p3);
  const float b = 0.5 * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3);
  const float c = 0.5 * (-p0 + p2);
  return float2 (((a * t + b) * t + c) * t + p1,
                 (3.0 * a * t + 2.0 * b) * t + c);
}

static inline uint2 terrain_sample_position (int2 p, uint2 size) {
  const int2 period = int2 (size);
  p = (p % period + period) % period;
  return uint2 (p);
}

// Returns metre-valued elevation and derivatives with respect to grid x/z.
static inline float3
terrain_height_smooth (float2 grid,
                       constant MoppeTerrainUniforms& u,
                       texture2d<float, access::read> heights) {
  const uint2 size (heights.get_width (), heights.get_height ());
  grid -= floor (grid / float2 (size)) * float2 (size);

  const int2 cell = int2 (floor (grid));
  const float2 f = fract (grid);
  float row[4];
  float dx[4];
  for (int j = 0; j < 4; ++j) {
    float p[4];
    for (int i = 0; i < 4; ++i)
      p[i] =
        heights
          .read (terrain_sample_position (cell + int2 (i - 1, j - 1), size))
          .r;
    const float2 value = terrain_cubic (p[0], p[1], p[2], p[3], f.x);
    row[j] = value.x;
    dx[j] = value.y;
  }
  const float2 y = terrain_cubic (row[0], row[1], row[2], row[3], f.y);
  const float x = terrain_cubic (dx[0], dx[1], dx[2], dx[3], f.y).x;

  const float h00 = heights.read (terrain_sample_position (cell, size)).r;
  const float h10 =
    heights.read (terrain_sample_position (cell + int2 (1, 0), size)).r;
  const float h01 =
    heights.read (terrain_sample_position (cell + int2 (0, 1), size)).r;
  const float h11 =
    heights.read (terrain_sample_position (cell + int2 (1), size)).r;
  const float lo = min (min (h00, h10), min (h01, h11));
  const float hi = max (max (h00, h10), max (h01, h11));
  if (y.x < lo || y.x > hi) {
    const float h0 = mix (h00, h10, f.x);
    const float h1 = mix (h01, h11, f.x);
    const float bilinear_x = mix (h10 - h00, h11 - h01, f.y);
    return float3 (mix (h0, h1, f.y), bilinear_x, h1 - h0);
  }
  return float3 (y.x, x, y.y);
}

static inline float
terrain_height_bilinear (float2 grid, texture2d<float, access::read> heights) {
  const uint2 size (heights.get_width (), heights.get_height ());
  grid -= floor (grid / float2 (size)) * float2 (size);
  const uint2 p00 = uint2 (floor (grid)) % size;
  const uint2 p11 = (p00 + uint2 (1)) % size;
  const float2 f = fract (grid);
  const float h0 =
    mix (heights.read (p00).r, heights.read (uint2 (p11.x, p00.y)).r, f.x);
  const float h1 =
    mix (heights.read (uint2 (p00.x, p11.y)).r, heights.read (p11).r, f.x);
  return mix (h0, h1, f.y);
}

static inline float3 terrain_read_normal (uint2 p, texture2d<float> normals) {
  const float2 nxz = normals.read (p).rg;
  const float ny = sqrt (max (1.0 - dot (nxz, nxz), 0.0));
  return float3 (nxz.x, ny, nxz.y);
}

static inline float3 terrain_normal_filtered (float2 grid,
                                              texture2d<float> normals) {
  const uint2 size (normals.get_width (), normals.get_height ());
  constexpr sampler smp (coord::normalized, address::repeat, filter::linear);
  // A normalized sample addresses texel centers. The half-texel offset makes
  // an integral terrain coordinate land exactly on its authoritative normal,
  // matching the old four-read interpolation at a quarter of the fetches.
  const float2 uv = (grid + 0.5) / float2 (size);
  const float2 nxz = normals.sample (smp, uv).rg;
  const float ny = sqrt (max (1.0 - dot (nxz, nxz), 0.0));
  return float3 (nxz.x, ny, nxz.y);
}

// Height on the actual triangle surface produced by a coarser grid.
// The strip topology uses the bottom-left to top-right diagonal.
static inline float terrain_height_on_lattice (
  float2 grid, float step, texture2d<float, access::read> heights) {
  const uint2 size (heights.get_width (), heights.get_height ());
  grid -= floor (grid / float2 (size)) * float2 (size);
  const float2 cell = floor (grid / step) * step;
  const float2 f = clamp ((grid - cell) / step, 0.0, 1.0);
  const int stride = (int)step;
  const int2 c = int2 (cell);
  const float h00 = heights.read (terrain_sample_position (c, size)).r;
  const float h10 =
    heights.read (terrain_sample_position (c + int2 (stride, 0), size)).r;
  const float h01 =
    heights.read (terrain_sample_position (c + int2 (0, stride), size)).r;
  const float h11 =
    heights.read (terrain_sample_position (c + int2 (stride), size)).r;
  if (f.x + f.y <= 1.0)
    return h00 + f.x * (h10 - h00) + f.y * (h01 - h00);
  return h11 + (1.0 - f.y) * (h10 - h11) + (1.0 - f.x) * (h01 - h11);
}

static inline float3
terrain_normal_on_lattice (float2 grid, float step, texture2d<float> normals) {
  const uint2 size (normals.get_width (), normals.get_height ());
  grid -= floor (grid / float2 (size)) * float2 (size);
  const float2 cell = floor (grid / step) * step;
  const float2 f = clamp ((grid - cell) / step, 0.0, 1.0);
  const int stride = (int)step;
  const int2 c = int2 (cell);
  const float3 n00 =
    terrain_read_normal (terrain_sample_position (c, size), normals);
  const float3 n10 = terrain_read_normal (
    terrain_sample_position (c + int2 (stride, 0), size), normals);
  const float3 n01 = terrain_read_normal (
    terrain_sample_position (c + int2 (0, stride), size), normals);
  const float3 n11 = terrain_read_normal (
    terrain_sample_position (c + int2 (stride), size), normals);
  if (f.x + f.y <= 1.0)
    return n00 + f.x * (n10 - n00) + f.y * (n01 - n00);
  return n11 + (1.0 - f.y) * (n10 - n11) + (1.0 - f.x) * (n01 - n11);
}

static inline float2 terrain_grid_pos (uint index,
                                       constant MoppeChunkUniforms& chunk) {
  const uint local_x = index % chunk.verts_per_row;
  const uint local_z = index / chunk.verts_per_row;
  return float2 (chunk.origin_x, chunk.origin_z) +
         float2 (local_x, local_z) * chunk.step;
}

static inline float3
terrain_world_pos (uint index,
                   constant MoppeChunkUniforms& chunk,
                   constant MoppeTerrainUniforms& u,
                   texture2d<float, access::read> heights) {
  const float2 grid = terrain_grid_pos (index, chunk);
  const float h = chunk.step < 1.0 ? terrain_height_bilinear (grid, heights)
                                   : heights.read (uint2 (grid)).r;

  return float3 (u.params0.x * grid.x, u.params0.y * h, u.params0.z * grid.y) +
         chunk.world_offset.xyz;
}

vertex TerrainVaryings terrain_vertex (
  uint index [[vertex_id]],
  constant MoppeTerrainUniforms& u [[buffer (MOPPE_BUF_FRAME)]],
  constant MoppeChunkUniforms& chunk [[buffer (MOPPE_BUF_CHUNK)]],
  texture2d<float, access::read> heights [[texture (MOPPE_TEX_HEIGHTS)]],
  texture2d<float> normals [[texture (MOPPE_TEX_NORMALS)]]) {
  const float2 grid = terrain_grid_pos (index, chunk);
  float h;
  float3 normal;
  if (chunk.step < 1.0) {
    const float3 smooth = terrain_height_smooth (grid, u, heights);
    h = smooth.x;
    const float3 tangent_x (u.params0.x, smooth.y * u.params0.y, 0.0);
    const float3 tangent_z (0.0, smooth.z * u.params0.y, u.params0.z);
    normal = normalize (cross (tangent_z, tangent_x));
  } else {
    h = heights.read (uint2 (grid)).r;
    normal = terrain_read_normal (uint2 (grid), normals);
  }

  const float2 canonical_xz (u.params0.x * grid.x, u.params0.z * grid.y);
  const float2 world_xz = canonical_xz + chunk.world_offset.xz;
  if (chunk.parent_step > chunk.step && chunk.morph_end > chunk.morph_start) {
    const float dist = length (world_xz - u.camera_pos.xz);
    const float morph = smoothstep (chunk.morph_start, chunk.morph_end, dist);
    if (morph > 0.0) {
      float parent_h =
        terrain_height_on_lattice (grid, chunk.parent_step, heights);
      h = mix (h, parent_h, morph);
      const float3 parent_n =
        terrain_normal_on_lattice (grid, chunk.parent_step, normals);
      normal = mix (normal, parent_n, morph);
    }
  }

  const float3 world (world_xz.x, u.params0.y * h, world_xz.y);

  TerrainVaryings out;
  out.position = u.view_proj * float4 (world, 1.0);
  out.world_pos = world;
  out.normal = normal;
  out.height = h;

  // Distance haze plus valley mist that pools on low ground (the
  // mist term is terrain-exclusive by design).
  const float dist = length (world - u.camera_pos.xyz);
  float fog = 1.0 - exp (-pow (dist * u.fog_color.w, 1.5));
  // High ridges rise above the valley haze instead of dissolving into the
  // horizon with the lowlands. This keeps the world's distant relief legible
  // while nearby valleys retain the original atmospheric depth.
  fog = moppe_relief_haze (fog, world.y, u.params1.y, u.params7.z);
  if (u.fog_color.w > 0.0) {
    const float lowness = 1.0 - smoothstep (45.0, 170.0, world.y);
    fog += 0.3 * lowness * smoothstep (150.0, 1500.0, dist);
  }
  out.fog = saturate (fog);

  const float3 canonical_world (canonical_xz.x, world.y, canonical_xz.y);
  out.shadow_coord = u.light_matrix * float4 (canonical_world, 1.0);
  out.uv = world.xz * u.params0.w;
  out.field_uv = grid / float2 (heights.get_width (), heights.get_height ());
  out.grid_coord = grid;
  out.mesh_coord =
    (grid - float2 (chunk.origin_x, chunk.origin_z)) / chunk.step;
  out.lod_step = chunk.step;
  const float4 current_reference = u.unjittered_view_proj * float4 (world, 1.0);
  const float4 previous_reference = u.previous_view_proj * float4 (world, 1.0);
  out.motion =
    moppe_motion_vector (current_reference, previous_reference, u.temporal.xy);
  return out;
}

// Depth-only variant for the one-time shadow render; view_proj
// carries the light's NDC matrix.
vertex float4 terrain_shadow_vertex (uint index [[vertex_id]],
                                     constant MoppeTerrainUniforms& u
                                     [[buffer (MOPPE_BUF_FRAME)]],
                                     constant MoppeChunkUniforms& chunk
                                     [[buffer (MOPPE_BUF_CHUNK)]],
                                     texture2d<float, access::read> heights
                                     [[texture (MOPPE_TEX_HEIGHTS)]]) {
  const float3 world = terrain_world_pos (index, chunk, u, heights);
  return u.view_proj * float4 (world, 1.0);
}

static float terrain_shadow_factor (float4 shadow_coord,
                                    float fog,
                                    float3 n,
                                    float3 l,
                                    float shadow_strength,
                                    float shadow_texel,
                                    depth2d<float> shadow_map) {
  if (shadow_strength < 0.01)
    return 1.0;

  const float3 proj = shadow_coord.xyz / shadow_coord.w;
  if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0 ||
      proj.z < 0.0 || proj.z > 1.0)
    return 1.0;

  // Slope-scaled bias against acne on raking ground.
  const float bias = 0.0006 + 0.0025 * (1.0 - max (dot (n, l), 0.0));
  const float z = proj.z - bias;

  // Center + 4 diagonal taps; linear compare filtering gives free
  // 2x2 PCF inside each tap (Depth16Unorm filters on Apple3+).
  constexpr sampler shadow_smp (coord::normalized,
                                address::clamp_to_edge,
                                filter::linear,
                                compare_func::less_equal);

  float shadow = 0.4 * shadow_map.sample_compare (shadow_smp, proj.xy, z);
  for (float dy = -1.5; dy <= 1.5; dy += 3.0)
    for (float dx = -1.5; dx <= 1.5; dx += 3.0)
      shadow +=
        0.15 * shadow_map.sample_compare (
                 shadow_smp, proj.xy + float2 (dx, dy) * shadow_texel, z);
  shadow = pow (shadow, 1.3);

  // Fade shadows out into the haze.
  const float fade = saturate (2.5 * (1.0 - fog));
  return mix (1.0, shadow, shadow_strength * fade);
}

// Sample a splat layer at two scales and crossfade by distance:
// near ground keeps fine detail, far ground switches to a coarser,
// uncorrelated repeat so the tiling never shows.
static inline float3
terrain_layer (texture2d<float> tex, sampler smp, float2 tc, float far_blend) {
  const float3 near_c = tex.sample (smp, tc).rgb;
  const float3 far_c = tex.sample (smp, tc * 0.19 + float2 (0.13, 0.71)).rgb;
  return mix (near_c, far_c, far_blend);
}

// Dirt is sourced from a close photograph of loose gravel. Its centimetre
// contrast is useful as material evidence, but resolving every photographed
// grain makes an entire trail look like salt and pepper and gives HDR a field
// of isolated bright pixels to preserve. An additive mip bias integrates that
// source before lighting while the world-space fields below restore the
// metre-scale structure a rider should actually read.
static inline float3 terrain_layer_integrated (texture2d<float> tex,
                                               sampler smp,
                                               float2 tc,
                                               float far_blend) {
  const float3 near_c = tex.sample (smp, tc, bias (1.35)).rgb;
  const float3 far_c =
    tex.sample (smp, tc * 0.19 + float2 (0.13, 0.71), bias (1.10)).rgb;
  return mix (near_c, far_c, far_blend);
}

// Steep faces cannot use the ground's XZ projection without smearing the
// texture vertically. Blend three world-space projections by surface normal;
// squaring the weights keeps broad faces crisp while rounding transitions.
static inline float3 terrain_layer_triplanar (texture2d<float> tex,
                                              sampler smp,
                                              float3 world,
                                              float3 normal,
                                              float scale,
                                              float far_blend) {
  float3 w = abs (normalize (normal));
  w = w * w;
  w /= max (w.x + w.y + w.z, 1e-4);
  const float3 x = terrain_layer (tex, smp, world.zy * scale, far_blend);
  const float3 y = terrain_layer (tex, smp, world.xz * scale, far_blend);
  const float3 z = terrain_layer (tex, smp, world.xy * scale, far_blend);
  return x * w.x + y * w.y + z * w.z;
}

// R32F water levels are not linearly filterable on the oldest supported
// Apple GPUs, so that one physical field retains explicit four-tap filtering.
static inline float4
terrain_field_sample_read (float2 uv, texture2d<float, access::read> field) {
  const uint2 size (field.get_width (), field.get_height ());
  const float2 grid = fract (uv) * float2 (size);
  const uint2 p00 = uint2 (floor (grid)) % size;
  const uint2 p11 = (p00 + uint2 (1)) % size;
  const float2 f = fract (grid);
  const float4 a =
    mix (field.read (p00), field.read (uint2 (p11.x, p00.y)), f.x);
  const float4 b =
    mix (field.read (uint2 (p00.x, p11.y)), field.read (p11), f.x);
  return mix (a, b, f.y);
}

// Material readings use filterable half formats. Sampling them directly turns
// four explicit reads and three mixes into one texture operation, while the
// repeat mode preserves the world's periodic seam.
static inline float4 terrain_field_sample (float2 uv, texture2d<float> field) {
  const float2 size (field.get_width (), field.get_height ());
  constexpr sampler smp (coord::normalized, address::repeat, filter::linear);
  return field.sample (smp, uv + 0.5 / size);
}

static inline float3 terrain_heat_palette (float t) {
  const float3 cold (0.035, 0.12, 0.28);
  const float3 middle (0.05, 0.78, 0.58);
  const float3 hot (1.0, 0.72, 0.08);
  return t < 0.5 ? mix (cold, middle, t * 2.0)
                 : mix (middle, hot, (t - 0.5) * 2.0);
}

static inline float4 terrain_overlay_color (float value,
                                            constant MoppeTerrainUniforms& u) {
  const int ramp = int (u.params4.x) - 1;
  const float span = max (u.params4.z - u.params4.y, 1e-20);
  const float t = saturate ((value - u.params4.y) / span);
  const float opacity = u.params4.w;
  if (ramp == 1)
    return float4 (
      mix (float3 (0.01, 0.08, 0.18), float3 (0.15, 0.92, 1.0), sqrt (t)),
      opacity * (0.18 + 0.82 * sqrt (t)));
  if (ramp == 2)
    return float4 (float3 (0.08, 0.72, 1.0),
                   opacity * smoothstep (0.08, 0.16, t));
  if (ramp == 3) {
    const float hue = fract (value * 0.61803398875);
    const float3 color =
      0.48 + 0.48 * cos (6.2831853 * (hue + float3 (0.0, 0.33, 0.67)));
    return float4 (color, opacity);
  }
  if (ramp == 4) {
    const float signed_t =
      clamp (value / max (abs (u.params4.y), abs (u.params4.z)), -1.0, 1.0);
    const float3 negative (0.12, 0.48, 1.0);
    const float3 neutral (0.16, 0.18, 0.17);
    const float3 positive (1.0, 0.28, 0.08);
    const float change = abs (signed_t);
    return float4 (signed_t < 0.0 ? mix (neutral, negative, -signed_t)
                                  : mix (neutral, positive, signed_t),
                   opacity * smoothstep (0.08, 0.45, change));
  }
  if (ramp == 5)
    return float4 (float3 (1.0, 0.12, 0.75), opacity * t);
  if (ramp == 6) {
    if (value <= 1e-7)
      return float4 (0.0);
    const float3 deep (0.015, 0.10, 0.32);
    const float3 shallow (0.08, 0.78, 1.0);
    return float4 (mix (shallow, deep, sqrt (t)),
                   opacity * (0.45 + 0.55 * sqrt (t)));
  }
  if (ramp == 7) {
    if (value <= 1e-7)
      return float4 (0.0);
    const float halo = smoothstep (0.02, 0.34, t);
    const float core = smoothstep (0.34, 0.88, t);
    const float3 blue (0.02, 0.48, 0.92);
    const float3 whitewater (0.68, 0.98, 1.0);
    return float4 (mix (blue, whitewater, core),
                   opacity * (0.16 * halo + 0.78 * core));
  }
  return float4 (terrain_heat_palette (t), opacity);
}

static inline float3 terrain_apply_analysis_overlay (
  float3 color,
  float2 field_uv,
  constant MoppeTerrainUniforms& u,
  texture2d<float, access::read> terrain_overlay) {
  if (u.params4.x <= 0.5)
    return color;

  const uint2 overlay_size (terrain_overlay.get_width (),
                            terrain_overlay.get_height ());
  const uint2 overlay_position =
    uint2 (round (fract (field_uv) * float2 (overlay_size))) % overlay_size;
  const float overlay_value = terrain_overlay.read (overlay_position).r;
  const float4 overlay = terrain_overlay_color (overlay_value, u);
  return mix (color, overlay.rgb, overlay.a);
}

static inline float3
terrain_apply_lattice_overlay (float3 color,
                               thread const TerrainVaryings& in,
                               float distance,
                               constant MoppeTerrainUniforms& u) {
  if (u.params5.x <= 0.0)
    return color;

  // Cyan is the actual vertex-pulled render lattice. Its quarter-cell near
  // field is allowed to disappear once it becomes sub-pixel instead of
  // turning into moire. Amber sites are the authoritative source samples:
  // one row of the materialized surface bundle per site.
  const float2 cell = fract (in.mesh_coord);
  const float2 axis_width = max (fwidth (in.mesh_coord), float2 (1e-4));
  const float axis_edge = min (min (cell.x, 1.0 - cell.x) / axis_width.x,
                               min (cell.y, 1.0 - cell.y) / axis_width.y);
  const float diagonal_width =
    max (fwidth (in.mesh_coord.x + in.mesh_coord.y), 1e-4);
  const float diagonal_edge = abs (cell.x + cell.y - 1.0) / diagonal_width;
  const float edge = min (axis_edge, diagonal_edge);
  const float line = 1.0 - smoothstep (0.45, 1.25, edge);
  const float mesh_spacing = 1.0 / max (max (axis_width.x, axis_width.y), 1e-4);
  const float mesh_visible = smoothstep (2.5, 5.0, mesh_spacing);
  const float2 vertex_distance =
    min (cell, 1.0 - cell) / max (axis_width, float2 (1e-4));
  const float mesh_vertex =
    1.0 - smoothstep (0.75, 1.7, length (vertex_distance));

  const float2 source_cell = fract (in.grid_coord);
  const float2 source_width = max (fwidth (in.grid_coord), float2 (1e-4));
  const float2 source_distance =
    min (source_cell, 1.0 - source_cell) / source_width;
  const float source_spacing =
    1.0 / max (max (source_width.x, source_width.y), 1e-4);
  const float source_visible = smoothstep (2.5, 5.0, source_spacing);
  const float source_vertex =
    (1.0 - smoothstep (1.1, 2.5, length (source_distance))) * source_visible;

  const float distance_fade = 1.0 - smoothstep (1400.0, 2200.0, distance);
  const float lod_band =
    clamp (log2 (max (in.lod_step, 0.25)) + 2.0, 0.0, 5.0) / 5.0;
  const float3 lod_tint =
    mix (float3 (0.82, 0.94, 1.0), float3 (1.0, 0.84, 0.68), lod_band);
  color = mix (color, color * lod_tint, 0.12 * distance_fade);
  const float wire = line * mesh_visible * distance_fade;
  color = mix (color, float3 (0.015, 0.16, 0.19), 0.82 * wire);
  color = mix (color,
               float3 (0.16, 0.95, 1.0),
               0.88 * mesh_vertex * mesh_visible * distance_fade);
  return mix (
    color, float3 (1.0, 0.63, 0.08), 0.96 * source_vertex * distance_fade);
}

struct TerrainSurfaceReadings {
  float moisture;
  float signed_water_depth;
  float submerged;
  float ground_up;
  float swash_zone;
  float damp;
  float2 intentional_ground;
  float forest_cover;
};

static inline TerrainSurfaceReadings
terrain_read_surface (thread const TerrainVaryings& in,
                      float3 normal,
                      constant MoppeTerrainUniforms& u,
                      texture2d<float> terrain_landscape,
                      texture2d<float, access::read> terrain_water,
                      texture2d<float> terrain_ground) {
  // The typed surface readings become two compact sheets only at the
  // presentation boundary. One filtered lookup supplies four fields which
  // share the same terrain coordinate and lifetime.
  const float4 landscape =
    u.params5.z > 0.5 ? terrain_field_sample (in.field_uv, terrain_landscape)
                      : float4 (0.0);
  const float4 ground = u.params5.z > 0.5
                          ? terrain_field_sample (in.field_uv, terrain_ground)
                          : float4 (1.0, normal.y, 0.0, 0.0);

  TerrainSurfaceReadings readings;
  readings.moisture = landscape.r;
  const float water_level =
    u.params5.y > 0.5 ? terrain_field_sample_read (in.field_uv, terrain_water).r
                      : -1.0;
  readings.signed_water_depth =
    u.params5.y > 0.5 ? (water_level - in.height) * u.params1.x : -100.0;
  const float water_depth = max (readings.signed_water_depth, 0.0);
  readings.submerged = smoothstep (0.015, 0.22, water_depth);
  readings.ground_up = ground.g;

  // Horizontal distance to the extracted waterline: the damp band hugs the
  // actual shoreline curve and fades on steep banks.
  const float shore_m = ground.r * u.params6.y;
  readings.swash_zone =
    (1.0 - smoothstep (0.3, 2.8, shore_m)) * smoothstep (0.42, 0.62, normal.y);
  readings.damp = max (max (readings.submerged, 0.92 * readings.swash_zone),
                       smoothstep (0.22, 0.82, readings.moisture));
  readings.intentional_ground = ground.ba;
  readings.forest_cover = landscape.a;
  return readings;
}

struct TerrainMaterialBands {
  float cliff;
  float scree;
  float snow;
  float beach;
  float grass;
  float3 sward_tint; // display-space ensemble blade tint, drift folded in
};

static inline TerrainMaterialBands
terrain_classify_material (thread const TerrainVaryings& in,
                           float3 normal,
                           float sea_level,
                           thread const TerrainSurfaceReadings& readings,
                           constant MoppeTerrainUniforms& u) {
  TerrainMaterialBands bands;
  const float land_relief = max (u.params7.z, 1.0);
  const float normalized_height = (in.height - sea_level) / land_relief;
  bands.cliff = 1.0 - smoothstep (0.60, 0.80, normal.y);
  bands.scree = smoothstep (0.38, 0.58, normalized_height);

  const float snow_support_up =
    u.params7.x > 0.5 ? readings.ground_up : normal.y;
  bands.snow = smoothstep (0.55, 0.68, normalized_height) *
               smoothstep (0.58, 0.78, snow_support_up);

  const float beach_low = sea_level + 0.5 / u.params1.x;
  const float beach_high = sea_level + 3.0 / u.params1.x;
  bands.beach = (1.0 - smoothstep (beach_low, beach_high, in.height)) *
                smoothstep (0.55, 0.75, normal.y);

  const MoppeGrassMedium medium =
    moppe_grass_medium (in.world_pos.xz,
                        readings.moisture,
                        readings.forest_cover,
                        readings.intentional_ground,
                        normal.y,
                        snow_support_up,
                        normalized_height,
                        readings.signed_water_depth,
                        u.params7.w);
  // Habitat owns the substrate colour continuously. Resolved blades add
  // silhouette and motion, but turning their narrow ribbons edge-on must not
  // reveal a distance-dependent rocky ground material underneath them.
  bands.grass = medium.cover;

  // The tint the sward ensemble presents: the medium's own blade tint,
  // with the flowering drift's wash chromaticity folded in where a drift
  // stands, so a hillside of retired heads still reads as flowering.
  const MoppeFlowerDrift drift = moppe_flower_drift (in.world_pos.xz,
                                                     readings.moisture,
                                                     readings.forest_cover,
                                                     medium.leaf_area);
  bands.sward_tint = mix (medium.blade_tint,
                          moppe_flower_wash_tint (drift.tint) * 0.60,
                          min (0.32, 0.32 * drift.presence));
  return bands;
}

struct TerrainPalette {
  float3 grass;
  float3 soil;
  float3 cliff;
  float3 snow;
};

static inline TerrainPalette
terrain_build_palette (thread const TerrainVaryings& in,
                       float3 normal,
                       thread const TerrainMaterialBands& bands,
                       texture2d<float> grass,
                       texture2d<float> dirt,
                       texture2d<float> snow,
                       texture2d<float> rock,
                       constant MoppeTerrainUniforms& u,
                       sampler smp) {
  TerrainPalette palette;
  const float far_blend =
    smoothstep (40.0, 350.0, length (in.world_pos - u.camera_pos.xyz));
  palette.grass = terrain_layer (grass, smp, in.uv, far_blend);
  palette.soil = terrain_layer_integrated (dirt, smp, in.uv, far_blend);

  palette.cliff = palette.soil;
  if (bands.cliff > 0.01) {
    palette.cliff = terrain_layer_triplanar (
      rock, smp, in.world_pos, normal, u.params0.w * 1.7, far_blend);
  }

  palette.snow = palette.soil;
  if (bands.snow > 0.01) {
    palette.snow = terrain_layer (snow, smp, in.uv, far_blend);
  }
  return palette;
}

struct TerrainMaterial {
  float3 albedo;
  float3 sward;       // display-space ensemble blade tint
  float sward_detail; // photo-texture luma keeping fine ground variation
  float grass;
  float trail;
  float base;
  float forest;
  float wetness;
};

static inline TerrainMaterial
terrain_compose_material (float3 normal,
                          thread const TerrainSurfaceReadings& readings,
                          thread const TerrainMaterialBands& bands,
                          thread const TerrainPalette& palette) {
  TerrainMaterial material;
  material.albedo = palette.grass;
  material.albedo = mix (material.albedo, palette.soil, bands.scree);
  material.albedo = mix (material.albedo, palette.cliff, bands.cliff);
  material.albedo = mix (material.albedo, palette.snow, bands.snow);

  const float shore = max (bands.beach, 0.78 * readings.swash_zone) *
                      (1.0 - readings.submerged) * (1.0 - bands.snow) *
                      smoothstep (0.48, 0.74, normal.y);
  const float soil_value = dot (palette.soil, float3 (0.299, 0.587, 0.114));
  const float3 sand = soil_value * float3 (1.12, 1.03, 0.82);
  material.albedo = mix (material.albedo, sand, shore);

  // The grass band no longer tints the ground texture: grassy ground is
  // lit as the sward ensemble in terrain_light, and the albedo composed
  // here is the soil that shows wherever cover thins. The photo texture
  // survives as luma detail riding on the ensemble colour.
  material.grass = bands.grass;
  material.sward = bands.sward_tint;
  material.sward_detail =
    clamp (dot (palette.grass, float3 (0.299, 0.587, 0.114)) / 0.40, 0.6, 1.6);
  material.trail = 0.0;
  material.base = 0.0;
  material.forest = 0.0;
  material.wetness = 0.0;
  return material;
}

static inline void
terrain_compose_trail_and_base (thread TerrainMaterial& material,
                                thread const TerrainSurfaceReadings& readings,
                                thread const TerrainMaterialBands& bands,
                                thread const TerrainPalette& palette) {
  const float trail = readings.intentional_ground.r;
  const float trail_cover = (1.0 - bands.snow) * (1.0 - readings.submerged);
  const float trail_footprint = smoothstep (0.025, 0.32, trail);
  material.trail = trail_cover * trail_footprint;
  const float trail_value = dot (palette.soil, float3 (0.299, 0.587, 0.114));
  const float3 trail_color = trail_value * float3 (0.72, 0.49, 0.28);
  material.albedo = mix (material.albedo, trail_color, material.trail);

  const float home_base = readings.intentional_ground.g;
  material.base = smoothstep (0.03, 0.72, home_base) *
                  (1.0 - readings.submerged) * (1.0 - bands.snow);
  const float3 base_color =
    trail_value * mix (float3 (0.70, 0.48, 0.22),
                       float3 (1.05, 0.88, 0.52),
                       smoothstep (0.70, 1.0, home_base));
  material.albedo = mix (material.albedo, base_color, 0.92 * material.base);
}

static inline void terrain_compose_forest_and_wetness (
  thread TerrainMaterial& material,
  thread const TerrainSurfaceReadings& readings) {
  const float ground_value =
    dot (material.albedo, float3 (0.299, 0.587, 0.114));
  const float3 forest_color = ground_value * float3 (0.48, 0.72, 0.34);
  material.forest = smoothstep (0.035, 0.72, readings.forest_cover) *
                    (1.0 - material.base) * (1.0 - material.trail) *
                    (1.0 - readings.submerged);
  material.albedo = mix (material.albedo, forest_color, material.forest);

  material.wetness = max (0.62 * readings.damp, readings.submerged);
  const float wet_luma = dot (material.albedo, float3 (0.299, 0.587, 0.114));
  material.albedo = mix (
    material.albedo,
    mix (material.albedo, float3 (wet_luma), 0.20) * float3 (0.52, 0.58, 0.60),
    material.wetness * 0.58 * (1.0 - 0.85 * material.grass));
}

struct TerrainLighting {
  float3 color;
};

static inline TerrainLighting
terrain_light (float3 albedo,
               float3 normal,
               float3 view_dir,
               thread const TerrainVaryings& in,
               thread const TerrainMaterial& material,
               constant MoppeTerrainUniforms& u,
               depth2d<float> shadow_map) {
  TerrainLighting lighting;
  const float3 light = u.sun_dir.xyz;
  const float shadow = terrain_shadow_factor (in.shadow_coord,
                                              in.fog,
                                              normal,
                                              light,
                                              u.params1.z,
                                              u.params1.w,
                                              shadow_map);
  const float direct_visibility =
    shadow *
    moppe_cloud_transmission (in.world_pos, light, u.params2.x, u.params2.y);
  const float canopy_direct = mix (1.0, 0.68, material.forest);
  const float canopy_ambient = mix (1.0, 0.82, material.forest);
  const float intensity = saturate ((dot (light, normal) + 0.08) / 1.08);
  const float3 shade_fill =
    mix (float3 (0.80, 0.92, 1.14), float3 (1.0), shadow);
  const float3 diffuse_light =
    intensity * direct_visibility * canopy_direct * 0.9 * u.sun_diffuse.rgb +
    canopy_ambient * shade_fill *
      moppe_hemisphere_light (u.ambient.rgb, normal);
  lighting.color = albedo * diffuse_light;

  // Grass-covered ground is the blade shader's own ensemble limit, not a
  // tinted soil texture: near and far grass are one formula, so the
  // geometry's retirement can have no colour seam to find. Two octaves
  // of world-anchored grain keep the habitat's broad tint gradients from
  // reading as flat vinyl blobs: the fine octave yields as its cells go
  // subpixel, the coarse one carries the mottle to the horizon.
  if (material.grass > 0.001) {
    const float2 ground_xz = in.world_pos.xz;
    // The dynamics moment: blades gust, so their ensemble must too. The
    // same shared gust function tilts the sward's shading normal, and a
    // far field carries travelling sheen waves instead of sitting still
    // like paint.
    const float gust = moppe_grass_gust (ground_xz, u.params2.x);
    const float3 swayed = normalize (normal + float3 (0.79, 0.0, 0.53) *
                                                (0.10 * gust * material.grass));
    const float3 sward =
      moppe_sward_ensemble_light (material.sward,
                                  swayed,
                                  light,
                                  view_dir,
                                  u.sun_diffuse.rgb,
                                  u.ambient.rgb,
                                  shadow,
                                  direct_visibility * canopy_direct);
    const float footprint = length (in.world_pos - u.camera_pos.xyz);
    const float fine_visible = 1.0 - smoothstep (30.0, 110.0, footprint);
    const float fine = moppe_value_noise (ground_xz * 1.9);
    const float coarse =
      moppe_value_noise (ground_xz * 0.16 + float2 (31.7, 8.3));
    const float grain = (1.0 + 0.22 * (coarse - 0.5) * 2.0) *
                        (1.0 + 0.18 * (fine - 0.5) * 2.0 * fine_visible);
    lighting.color = mix (
      lighting.color, sward * material.sward_detail * grain, material.grass);
  }

  const float3 half_vector = normalize (light - view_dir);
  const float wet_spec =
    material.wetness * pow (max (dot (normal, half_vector), 0.0), 32.0);
  lighting.color +=
    u.sun_specular.rgb * direct_visibility * canopy_direct * wet_spec * 0.055;
  return lighting;
}

fragment MoppeTemporalOutput terrain_fragment (
  TerrainVaryings in [[stage_in]],
  constant MoppeTerrainUniforms& u [[buffer (MOPPE_BUF_FRAME)]],
  texture2d<float> grass [[texture (MOPPE_TEX_GRASS)]],
  texture2d<float> dirt [[texture (MOPPE_TEX_DIRT)]],
  texture2d<float> snow [[texture (MOPPE_TEX_SNOW)]],
  texture2d<float> rock [[texture (MOPPE_TEX_ROCK)]],
  depth2d<float> shadow_map [[texture (MOPPE_TEX_SHADOW)]],
  texture2d<float, access::read> terrain_overlay
  [[texture (MOPPE_TEX_TERRAIN_OVERLAY)]],
  texture2d<float> terrain_landscape [[texture (MOPPE_TEX_TERRAIN_LANDSCAPE)]],
  texture2d<float, access::read> terrain_water
  [[texture (MOPPE_TEX_TERRAIN_WATER)]],
  texture2d<float> terrain_ground [[texture (MOPPE_TEX_TERRAIN_GROUND)]],
  texture2d<float> normals [[texture (MOPPE_TEX_TERRAIN_NORMALS)]],
  sampler smp [[sampler (0)]]) {

  const float3 to_frag = in.world_pos - u.camera_pos.xyz;
  const float dist = length (to_frag);
  const float3 view_dir = to_frag / max (dist, 1e-4);
  const float3 l = u.sun_dir.xyz;

  const float3 fog_c = moppe_warmed_fog (u.fog_color.rgb, view_dir, l);

  // Fully fogged: skip all texture and shadow work.
  const float fog_factor = smoothstep (0.0, 0.9, in.fog);
  if (fog_factor >= 0.995)
    return moppe_temporal_output (float4 (fog_c, 1.0), in.motion, 0.0);

  // Native and coarser LODs light from the full-resolution normal
  // texture at fragment rate: a stride-8 silhouette carries full
  // shading detail, exactly as a normal-mapped mesh does.  The
  // subdivided near field keeps its analytic surface normals.
  float3 n = (u.params6.x > 0.5 && in.lod_step >= 1.0)
               ? normalize (terrain_normal_filtered (in.grid_coord, normals))
               : normalize (in.normal);
  const float sea_level = u.params1.y;

  const TerrainSurfaceReadings readings = terrain_read_surface (
    in, n, u, terrain_landscape, terrain_water, terrain_ground);
  const TerrainMaterialBands bands =
    terrain_classify_material (in, n, sea_level, readings, u);

  const TerrainPalette palette =
    terrain_build_palette (in, n, bands, grass, dirt, snow, rock, u, smp);

  TerrainMaterial material =
    terrain_compose_material (n, readings, bands, palette);
  terrain_compose_trail_and_base (material, readings, bands, palette);
  terrain_compose_forest_and_wetness (material, readings);
  float3 texel = material.albedo;
  texel =
    terrain_apply_analysis_overlay (texel, in.field_uv, u, terrain_overlay);

  const TerrainLighting lighting =
    terrain_light (texel, n, view_dir, in, material, u, shadow_map);
  float3 color = lighting.color;
  color = terrain_apply_lattice_overlay (color, in, dist, u);
  return moppe_temporal_output (
    float4 (mix (color, fog_c, fog_factor), 1.0), in.motion, 0.0);
}
