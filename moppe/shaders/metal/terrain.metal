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

// Ground relief below the lattice, delivered as a gradient because the
// shading only ever wants the slope.  Each octave has a world-space
// wavelength and retires once that wavelength falls toward the width of a
// screen pixel, so the ground holds its texture as the camera closes in and
// simply runs out of octaves as it pulls away.
//
// The relief this replaced was read out of the screen derivatives of the
// composed albedo.  That signal has no wavelength at all: its detail sits on
// the pixel grid at every distance, which is why near ground read as static
// and why it crawled whenever the camera moved.
//
// Amplitude falls with wavelength at the same rate here, so every octave
// contributes the same slope and the sum is an average rather than a
// runaway.  The result is normalized to unit RMS slope, which makes the
// caller's strength a micro-gradient it can reason about: 0.15 is a
// roughly eight-degree tilt, whatever wavelength carries it.
constant int TERRAIN_RELIEF_OCTAVES = 4;
constant float TERRAIN_RELIEF_LACUNARITY = 0.42;
constant float TERRAIN_RELIEF_RMS = 0.305;

static inline float2
terrain_relief_gradient (float2 plane, float pixel_m, float base_wavelength) {
  float2 gradient (0.0);
  float wavelength = base_wavelength;
  for (int octave = 0; octave < TERRAIN_RELIEF_OCTAVES; ++octave) {
    // An octave already down to about twice the pixel width has nothing
    // left to say and can only alias.  Wavelength shrinks monotonically,
    // so once one octave is gone every finer one is too.
    const float visible =
      1.0 - smoothstep (0.30 * wavelength, 0.85 * wavelength, pixel_m);
    if (visible <= 0.004)
      break;
    const float3 noise = moppe_value_noise_d (
      plane / wavelength + float2 (11.3, 27.1) * float (octave));
    gradient += noise.yz * visible;
    wavelength *= TERRAIN_RELIEF_LACUNARITY;
  }
  return gradient / (TERRAIN_RELIEF_OCTAVES * TERRAIN_RELIEF_RMS);
}

struct TerrainPebbleSample {
  float cap;
  float tone;
  float2 gradient;
};

// A small jittered cellular bed. The nearest site supplies one rounded cap;
// the gap to the second-nearest site opens a dark seam between stones. It is
// evaluated only on close, wet, gently sloping ground, where transparency
// makes centimetre-scale bed character worth its fragment cost.
static inline TerrainPebbleSample terrain_pebble_sample (float2 world_xz) {
  constexpr float frequency = 3.0;
  const float2 p = world_xz * frequency;
  const float2 cell = floor (p);
  const float2 local = fract (p);
  float nearest = 10.0;
  float second = 10.0;
  float2 nearest_gradient (0.0);
  float nearest_tone = 0.5;
  for (int z = -1; z <= 1; ++z)
    for (int x = -1; x <= 1; ++x) {
      const float2 offset (x, z);
      const float2 identity = cell + offset;
      const float2 jitter (moppe_hash12 (identity + float2 (19.1, 7.3)),
                           moppe_hash12 (identity + float2 (3.7, 31.9)));
      const float2 delta = local - (offset + float2 (0.18) + 0.64 * jitter);
      const float angle =
        6.2831853 * moppe_hash12 (identity + float2 (13.7, 41.3));
      const float2 major (cos (angle), sin (angle));
      const float2 minor (-major.y, major.x);
      const float radius =
        mix (0.42, 0.78, moppe_hash12 (identity + float2 (29.7, 11.5)));
      const float aspect =
        mix (0.76, 1.28, moppe_hash12 (identity + float2 (5.3, 61.7)));
      const float2 axes = radius * float2 (aspect, 1.0 / aspect);
      const float2 shaped (dot (delta, major) / axes.x,
                           dot (delta, minor) / axes.y);
      const float distance = length (shaped);
      const float2 distance_gradient =
        (major * shaped.x / axes.x + minor * shaped.y / axes.y) /
        max (distance, 1e-3);
      if (distance < nearest) {
        second = nearest;
        nearest = distance;
        nearest_gradient = distance_gradient;
        nearest_tone = moppe_hash12 (identity + float2 (47.3, 5.9));
      } else if (distance < second) {
        second = distance;
      }
    }

  const float round = saturate ((1.0 - nearest) / 0.62);
  const float cap = round * round * (3.0 - 2.0 * round);
  const float seam = smoothstep (0.035, 0.16, second - nearest);
  const float slope = -6.0 * round * (1.0 - round) / 0.62;
  const float2 gradient = frequency * slope * nearest_gradient;
  return { cap * seam, nearest_tone, gradient * seam };
}

// Relief lives on a plane, and a field laid over XZ smears into vertical
// streaks on a wall exactly as an XZ-projected texture does.  Compose three
// plane evaluations under the same squared-normal weights the splat
// triplanar uses.  Ground the rider actually drives on weighs almost
// entirely toward XZ and skips the other two.
static inline float3 terrain_relief_volume (float3 world,
                                            float3 normal,
                                            float pixel_m,
                                            float base_wavelength) {
  float3 w = abs (normal);
  w = w * w;
  w /= max (w.x + w.y + w.z, 1e-4);
  float3 gradient (0.0);
  if (w.y > 0.01) {
    const float2 g =
      terrain_relief_gradient (world.xz, pixel_m, base_wavelength);
    gradient += w.y * float3 (g.x, 0.0, g.y);
  }
  if (w.x > 0.01) {
    const float2 g =
      terrain_relief_gradient (world.zy, pixel_m, base_wavelength);
    gradient += w.x * float3 (0.0, g.y, g.x);
  }
  if (w.z > 0.01) {
    const float2 g =
      terrain_relief_gradient (world.xy, pixel_m, base_wavelength);
    gradient += w.z * float3 (g.x, g.y, 0.0);
  }
  return gradient;
}

// Tilt within the surface: only the tangential part of the relief gradient
// perturbs the normal, so a steep face is rolled along itself instead of
// being rotated through itself.
static inline float3
terrain_perturb_normal (float3 normal, float3 gradient, float strength) {
  const float3 tangential = gradient - normal * dot (gradient, normal);
  return normalize (normal - tangential * strength);
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
  float2 geology;
  float ground_up;
  float swash_zone;
  float damp;
  float2 intentional_ground;
  float forest_cover;
  float2 channel_flux;
  float channel_activity;
};

static inline TerrainSurfaceReadings
terrain_read_surface (thread const TerrainVaryings& in,
                      float3 normal,
                      constant MoppeTerrainUniforms& u,
                      texture2d<float> terrain_landscape,
                      texture2d<float, access::read> terrain_water,
                      texture2d<float> terrain_ground,
                      texture2d<float> terrain_channel_flux) {
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
  readings.geology = landscape.gb;
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
  readings.channel_flux =
    u.params7.y > 0.5
      ? terrain_field_sample (in.field_uv, terrain_channel_flux).rg
      : float2 (0.0);
  readings.channel_activity = min (length (readings.channel_flux), 1.0);
  return readings;
}

struct TerrainScaleMeasures {
  float far_blend;
  float ground_pixel_m;
  float fine_detail_visibility;
  float coarse_detail_visibility;
  float macro;
  float coarse;
  float jitter;
  float normalized_height;
};

static inline TerrainScaleMeasures
terrain_measure_scales (thread const TerrainVaryings& in,
                        float distance,
                        float sea_level,
                        constant MoppeTerrainUniforms& u,
                        texture2d<float> grass,
                        sampler smp) {
  TerrainScaleMeasures measures;
  measures.far_blend = smoothstep (40.0, 350.0, distance);
  measures.ground_pixel_m =
    max (length (dfdx (in.world_pos.xz)), length (dfdy (in.world_pos.xz)));
  measures.fine_detail_visibility =
    1.0 - smoothstep (0.18, 1.15, measures.ground_pixel_m);
  measures.coarse_detail_visibility =
    1.0 - smoothstep (0.75, 3.0, measures.ground_pixel_m);

  // Independent landscape-scale variation survives after source textures
  // have mipmapped away.
  measures.macro =
    0.65 * moppe_value_noise (in.world_pos.xz * 0.0027 + float2 (19.1, 7.3)) +
    0.35 * moppe_value_noise (in.world_pos.xz * 0.0091 + float2 (3.7, 31.9));
  measures.coarse =
    dot (grass.sample (smp, in.uv * 0.083 + float2 (0.37, 0.19)).rgb,
         float3 (0.299, 0.587, 0.114));
  measures.jitter = measures.coarse - 0.5;
  const float land_relief = max (u.params7.z, 1.0);
  measures.normalized_height = (in.height - sea_level) / land_relief +
                               0.045 * measures.jitter +
                               0.012 * (measures.macro - 0.5);
  return measures;
}

struct TerrainMaterialBands {
  float cliff;
  float scree;
  float snow;
  float beach;
  MoppeGrassMedium grass_medium;
  float grass_integrated;
};

static inline TerrainMaterialBands
terrain_classify_material (thread const TerrainVaryings& in,
                           float3 normal,
                           float distance,
                           float sea_level,
                           thread const TerrainSurfaceReadings& readings,
                           thread const TerrainScaleMeasures& scales,
                           constant MoppeTerrainUniforms& u) {
  TerrainMaterialBands bands;
  bands.cliff = 1.0 - smoothstep (0.60, 0.80, normal.y + 0.06 * scales.jitter);
  bands.scree = smoothstep (0.38, 0.58, scales.normalized_height);

  const float snow_support_up =
    u.params7.x > 0.5 ? readings.ground_up : normal.y;
  bands.snow = smoothstep (0.55, 0.68, scales.normalized_height) *
               smoothstep (0.58, 0.78, snow_support_up);

  const float jittered_height =
    in.height + 0.045 * scales.jitter + 0.012 * (scales.macro - 0.5);
  const float beach_low = sea_level + 0.5 / u.params1.x;
  const float beach_high = sea_level + 3.0 / u.params1.x;
  bands.beach = (1.0 - smoothstep (beach_low, beach_high, jittered_height)) *
                smoothstep (0.55, 0.75, normal.y);

  const float land_relief = max (u.params7.z, 1.0);
  const float grass_relative_height = (in.height - sea_level) / land_relief;
  bands.grass_medium = moppe_grass_medium (in.world_pos.xz,
                                           readings.moisture,
                                           readings.forest_cover,
                                           readings.intentional_ground,
                                           normal.y,
                                           snow_support_up,
                                           grass_relative_height,
                                           readings.signed_water_depth,
                                           u.params7.w);
  const float grass_focal_pixels = abs (u.view_proj[1][1]) * 0.5 * u.temporal.y;
  const float grass_blade_pixels =
    moppe_grass_blade_pixels (grass_focal_pixels, distance);
  bands.grass_integrated = moppe_grass_integrated_fraction (grass_blade_pixels);
  return bands;
}

struct TerrainPalette {
  float3 grass;
  float3 scree;
  float3 cliff;
  float3 snow;
  float scree_value;
  float sand_value;
  float3 blade_mean;
  float3 patch_hue;
  float turf_crowd;
  float turf_tuft;
  float turf_cover;
};

static inline TerrainPalette
terrain_build_palette (thread const TerrainVaryings& in,
                       float3 normal,
                       float damp,
                       thread const TerrainScaleMeasures& scales,
                       thread const TerrainMaterialBands& bands,
                       texture2d<float> grass,
                       texture2d<float> dirt,
                       texture2d<float> snow,
                       texture2d<float> rock,
                       constant MoppeTerrainUniforms& u,
                       sampler smp) {
  TerrainPalette palette;
  palette.grass = terrain_layer (grass, smp, in.uv, scales.far_blend);
  const float grass_value = dot (palette.grass, float3 (0.299, 0.587, 0.114));
  const float3 grass_palette =
    grass_value * moppe_srgb (float3 (0.68, 1.00, 0.52));
  palette.grass = mix (palette.grass, grass_palette, 0.34);
  palette.grass *= 1.36;
  palette.grass *= 0.88 + 0.55 * scales.coarse;
  palette.grass *=
    mix (float3 (0.84, 0.95, 0.76), float3 (1.10, 1.06, 0.92), scales.macro);
  palette.grass *= mix (float3 (1.04, 1.00, 0.90),
                        float3 (0.96, 1.02, 0.92),
                        smoothstep (0.08, 0.30, in.height));
  palette.grass *= mix (float3 (1.0), float3 (0.76, 0.91, 0.70), damp * 0.55);

  palette.turf_crowd = bands.grass_medium.clump;
  const float tuft_visibility =
    1.0 - smoothstep (0.12, 0.60, scales.ground_pixel_m);
  palette.turf_tuft =
    mix (0.5,
         moppe_value_noise (in.world_pos.xz * 0.85 + float2 (7.7, 3.1)),
         tuft_visibility);
  palette.turf_cover = bands.grass_medium.cover;
  palette.blade_mean = moppe_srgb (bands.grass_medium.blade_tint * 0.85);
  palette.patch_hue = float3 (1.0 + 0.14 * (palette.turf_crowd - 0.5),
                              1.0,
                              1.0 - 0.12 * (palette.turf_crowd - 0.5));

  palette.scree = terrain_layer_integrated (dirt, smp, in.uv, scales.far_blend);
  palette.scree *=
    0.82 + 0.36 * dirt.sample (smp, in.uv * 0.061, bias (1.50)).r;
  palette.scree_value = dot (palette.scree, float3 (0.299, 0.587, 0.114));
  palette.scree =
    mix (palette.scree, palette.scree_value * float3 (1.00, 0.96, 0.86), 0.72);

  palette.cliff = palette.scree;
  if (bands.cliff > 0.0) {
    palette.cliff = terrain_layer_triplanar (
      rock, smp, in.world_pos, normal, u.params0.w * 1.7, scales.far_blend);
    const float cliff_value = dot (palette.cliff, float3 (0.299, 0.587, 0.114));
    const float strata =
      0.5 + 0.5 * sin (in.world_pos.y * 0.075 +
                       3.5 * moppe_value_noise (in.world_pos.xz * 0.006));
    const float3 strata_tint =
      mix (float3 (0.72, 0.77, 0.80), float3 (0.92, 0.83, 0.68), 0.22 * strata);
    palette.cliff = mix (palette.cliff, cliff_value * strata_tint, 0.92);
    palette.cliff *= 0.82 + 0.42 * scales.coarse + 0.10 * strata;
  }

  palette.snow = palette.scree;
  if (bands.snow > 0.0) {
    palette.snow = terrain_layer (snow, smp, in.uv, scales.far_blend);
    palette.snow *=
      0.75 + 0.5 * snow.sample (smp, in.uv * 0.053 + float2 (0.21, 0.43)).r;
  }
  palette.sand_value = dot (palette.scree, float3 (0.299, 0.587, 0.114));
  return palette;
}

struct TerrainMaterial {
  float3 albedo;
  float shore;
  float wash;
  float grass;
  float pebble;
  TerrainPebbleSample pebble_sample;
  float trail;
  float close_trail;
  float base;
  float forest_presence;
  float canopy_grain;
  float forest_distance;
  float forest;
  float wetness;
};

static inline TerrainMaterial
terrain_compose_substrate (thread const TerrainVaryings& in,
                           float3 normal,
                           thread const TerrainSurfaceReadings& readings,
                           thread const TerrainScaleMeasures& scales,
                           thread const TerrainMaterialBands& bands,
                           thread const TerrainPalette& palette,
                           constant MoppeTerrainUniforms& u) {
  TerrainMaterial material;
  material.albedo = palette.grass;
  material.albedo = mix (material.albedo, palette.scree, bands.scree);
  material.albedo = mix (material.albedo, palette.cliff, bands.cliff);
  material.albedo = mix (material.albedo, palette.snow, bands.snow);

  const float3 sand =
    mix (palette.scree, palette.sand_value * float3 (1.12, 1.03, 0.82), 0.82);
  material.shore = max (bands.beach, 0.78 * readings.swash_zone) *
                   (1.0 - readings.submerged) * (1.0 - bands.snow) *
                   smoothstep (0.48, 0.74, normal.y);
  material.albedo = mix (material.albedo, sand, material.shore);

  // Fresh cuts expose regolith; deposition builds pale alluvium.
  if (u.params5.z > 0.5) {
    const float cut = smoothstep (0.12, 0.72, readings.geology.r);
    const float fill = smoothstep (0.12, 0.72, readings.geology.g);
    const float3 cut_color = mix (
      palette.scree, palette.scree_value * float3 (0.94, 0.84, 0.72), 0.55);
    material.albedo =
      mix (material.albedo, cut_color, cut * (1.0 - bands.snow) * 0.42);
    const float3 alluvium =
      mix (palette.scree, palette.sand_value * float3 (1.05, 1.00, 0.88), 0.70);
    const float fill_flat = smoothstep (0.78, 0.93, normal.y);
    material.albedo = mix (
      material.albedo, alluvium, fill * fill_flat * (1.0 - bands.snow) * 0.5);
  }

  material.wash =
    smoothstep (0.30, 0.85, readings.channel_activity + 0.10 * scales.jitter) *
    (1.0 - bands.snow) * (1.0 - readings.submerged);
  const float3 wash_color =
    mix (palette.scree, palette.scree_value * float3 (0.88, 0.84, 0.76), 0.55) *
    (0.82 + 0.30 * scales.coarse);
  material.albedo = mix (material.albedo, wash_color, 0.42 * material.wash);

  material.grass = 0.0;
  material.pebble = 0.0;
  material.pebble_sample = { 0.0, 0.5, float2 (0.0) };
  material.trail = 0.0;
  material.close_trail = 0.0;
  material.base = 0.0;
  material.forest_presence = 0.0;
  material.canopy_grain = 0.0;
  material.forest_distance = 0.0;
  material.forest = 0.0;
  material.wetness = 0.0;
  return material;
}

static inline void
terrain_compose_grass (thread TerrainMaterial& material,
                       thread const TerrainMaterialBands& bands,
                       thread const TerrainPalette& palette) {
  const float3 sward =
    palette.blade_mean * palette.patch_hue *
    (0.72 + 0.56 * palette.turf_crowd + 0.20 * (palette.turf_tuft - 0.5));
  const float grass_floor_shadow =
    palette.turf_cover * mix (0.24, 0.42, 1.0 - bands.grass_integrated);
  material.albedo = mix (material.albedo,
                         material.albedo * float3 (0.54, 0.66, 0.43),
                         grass_floor_shadow);
  material.grass = palette.turf_cover * bands.grass_integrated;
  material.albedo = mix (material.albedo, sward, material.grass);
}

static inline void
terrain_compose_pebbles (thread TerrainMaterial& material,
                         thread const TerrainVaryings& in,
                         float3 normal,
                         float distance,
                         thread const TerrainSurfaceReadings& readings,
                         thread const TerrainScaleMeasures& scales,
                         thread const TerrainMaterialBands& bands) {
  material.pebble = max (readings.submerged, 0.52 * readings.swash_zone) *
                    smoothstep (0.48, 0.90, normal.y) * (1.0 - bands.snow) *
                    scales.fine_detail_visibility *
                    (1.0 - smoothstep (18.0, 55.0, distance));
  if (material.pebble <= 0.002)
    return;

  material.pebble_sample = terrain_pebble_sample (in.world_pos.xz);
  const float3 cool_stone (0.28, 0.31, 0.32);
  const float3 warm_stone (0.39, 0.34, 0.29);
  const float3 pale_stone (0.47, 0.46, 0.42);
  float3 stone_srgb =
    mix (cool_stone,
         warm_stone,
         smoothstep (0.18, 0.78, material.pebble_sample.tone));
  stone_srgb = mix (stone_srgb,
                    pale_stone,
                    smoothstep (0.76, 0.97, material.pebble_sample.tone));
  const float3 stone =
    moppe_srgb (stone_srgb) * (0.72 + 0.36 * material.pebble_sample.cap);
  const float3 interstitial = moppe_srgb (float3 (0.10, 0.11, 0.105));
  const float3 pebble_color =
    mix (interstitial, stone, material.pebble_sample.cap);
  material.albedo =
    mix (material.albedo,
         pebble_color,
         material.pebble * (0.52 + 0.48 * material.pebble_sample.cap));
}

static inline void
terrain_compose_trail_and_base (thread TerrainMaterial& material,
                                thread const TerrainVaryings& in,
                                float distance,
                                thread const TerrainSurfaceReadings& readings,
                                thread const TerrainScaleMeasures& scales,
                                thread const TerrainMaterialBands& bands,
                                thread const TerrainPalette& palette) {
  const float trail = readings.intentional_ground.r;
  const float trail_cover = (1.0 - bands.snow) * (1.0 - readings.submerged);
  const float trail_footprint = smoothstep (0.025, 0.32, trail);
  const float trail_core = smoothstep (0.42, 0.88, trail);
  const float trail_shoulder =
    smoothstep (0.04, 0.22, trail) * (1.0 - smoothstep (0.38, 0.64, trail));
  material.close_trail = 1.0 - smoothstep (55.0, 240.0, distance);
  const float coarse_visibility =
    1.0 - smoothstep (0.64, 1.78, scales.ground_pixel_m);
  const float fine_visibility =
    1.0 - smoothstep (0.16, 0.43, scales.ground_pixel_m);
  const float gravel_coarse =
    mix (0.5,
         moppe_value_noise (in.world_pos.xz * 0.31 + float2 (11.7, 29.1)),
         coarse_visibility);
  const float gravel_fine =
    mix (0.5,
         moppe_value_noise (in.world_pos.xz * 1.27 + float2 (47.3, 5.9)),
         fine_visibility);
  const float gravel = 0.72 * gravel_coarse + 0.28 * gravel_fine;
  const float worn_bands =
    (1.0 - smoothstep (0.055, 0.15, abs (trail - 0.64))) *
    material.close_trail * trail_footprint;

  const float trail_value =
    mix (0.34, clamp (palette.sand_value, 0.24, 0.56), 0.38);
  float3 trail_color =
    mix (palette.scree, trail_value * float3 (0.86, 0.57, 0.31), 0.94);
  trail_color *= 0.93 + 0.14 * gravel;
  const float pale_grit = smoothstep (0.66, 0.84, gravel_coarse);
  trail_color = mix (trail_color,
                     palette.sand_value * float3 (0.98, 0.82, 0.58),
                     0.055 * pale_grit * coarse_visibility * trail_core);
  trail_color *= 1.0 - 0.24 * trail_shoulder;
  trail_color *= 1.0 - 0.16 * worn_bands;
  trail_color *= 1.0 + 0.08 * trail_core * material.close_trail;

  material.trail = trail_cover * trail_footprint * (0.60 + 0.36 * trail_core);
  material.albedo = mix (material.albedo, trail_color, material.trail);

  const float home_base = readings.intentional_ground.g;
  material.base = smoothstep (0.03, 0.72, home_base) *
                  (1.0 - readings.submerged) * (1.0 - bands.snow);
  const float3 base_color =
    palette.sand_value * mix (float3 (0.70, 0.48, 0.22),
                              float3 (1.05, 0.88, 0.52),
                              smoothstep (0.70, 1.0, home_base));
  material.albedo = mix (material.albedo, base_color, 0.92 * material.base);
}

static inline void terrain_compose_forest_and_wetness (
  thread TerrainMaterial& material,
  thread const TerrainVaryings& in,
  float distance,
  thread const TerrainSurfaceReadings& readings) {
  material.forest_presence = smoothstep (0.035, 0.72, readings.forest_cover);
  material.canopy_grain =
    0.68 * moppe_value_noise (in.world_pos.xz * 0.018 + float2 (17.3, 5.7)) +
    0.32 * moppe_value_noise (in.world_pos.xz * 0.061 + float2 (2.1, 41.9));
  const float ground_value =
    dot (material.albedo, float3 (0.299, 0.587, 0.114));
  const float3 forest_color = ground_value * float3 (0.55, 0.82, 0.40) *
                              (0.82 + 0.30 * material.canopy_grain);
  material.forest_distance = smoothstep (280.0, 1450.0, distance);
  material.forest = material.forest_presence *
                    mix (0.20, 0.62, material.forest_distance) *
                    (1.0 - material.base) * (1.0 - material.trail);
  material.albedo = mix (material.albedo, forest_color, material.forest);

  material.wetness = max (0.62 * readings.damp, readings.submerged);
  const float wet_luma = dot (material.albedo, float3 (0.299, 0.587, 0.114));
  material.albedo = mix (
    material.albedo,
    mix (material.albedo, float3 (wet_luma), 0.20) * float3 (0.52, 0.58, 0.60),
    material.wetness * 0.58 * (1.0 - 0.85 * material.grass));
}

static inline float3
terrain_apply_micro_relief (float3 normal,
                            thread const TerrainVaryings& in,
                            thread const TerrainSurfaceReadings& readings,
                            thread const TerrainMaterialBands& bands,
                            thread const TerrainMaterial& material) {
  // Relief is measured against the world-space width of a pixel so each
  // octave retires before it aliases.
  const float pixel_m =
    max (length (dfdx (in.world_pos)), length (dfdy (in.world_pos)));
  const float cut_relief = smoothstep (0.10, 0.65, readings.geology.r);
  const float fill_relief = smoothstep (0.10, 0.65, readings.geology.g);
  float relief_wavelength =
    mix (1.05, 2.90, saturate (bands.cliff + 0.45 * bands.scree));
  relief_wavelength *= mix (1.0, 1.45, material.trail);
  const float detail_strength =
    (0.13 + 0.42 * bands.cliff + 0.11 * bands.scree + 0.20 * cut_relief +
     0.035 * material.trail * material.close_trail) *
    (1.0 - 0.55 * fill_relief) * (1.0 - 0.85 * bands.snow) *
    mix (1.0, 0.62, material.trail);
  normal = terrain_perturb_normal (
    normal,
    terrain_relief_volume (in.world_pos, normal, pixel_m, relief_wavelength),
    detail_strength);

  const float pebble_strength =
    material.pebble * (0.06 + 0.08 * material.pebble_sample.cap);
  if (pebble_strength > 0.002)
    normal = terrain_perturb_normal (normal,
                                     float3 (material.pebble_sample.gradient.x,
                                             0.0,
                                             material.pebble_sample.gradient.y),
                                     pebble_strength);

  const float rill_strength =
    smoothstep (0.35, 0.90, readings.channel_activity) *
    (1.0 - 0.85 * bands.snow) * (1.0 - readings.submerged * 0.5) * 0.26;
  if (rill_strength > 0.002) {
    const float2 flow_dir =
      readings.channel_flux / max (readings.channel_activity, 1e-4);
    const float2 gradient = terrain_relief_gradient (
      in.world_pos.xz + float2 (13.7, 41.3), pixel_m, 1.15);
    const float2 across = gradient - flow_dir * dot (gradient, flow_dir);
    normal = terrain_perturb_normal (
      normal, float3 (across.x, 0.0, across.y), rill_strength);
  }
  return normal;
}

struct TerrainLighting {
  float3 color;
  float direct_visibility;
  float canopy_direct;
  float intensity;
  float3 half_vector;
};

static inline TerrainLighting
terrain_light_ground (float3 albedo,
                      float3 normal,
                      float3 view_dir,
                      thread const TerrainVaryings& in,
                      thread const TerrainSurfaceReadings& readings,
                      thread const TerrainScaleMeasures& scales,
                      thread const TerrainMaterialBands& bands,
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
  lighting.direct_visibility =
    shadow *
    moppe_cloud_transmission (in.world_pos, light, u.params2.x, u.params2.y);

  const float canopy_footprint =
    material.forest_presence * (1.0 - material.base) * (1.0 - material.trail) *
    (1.0 - bands.snow) * (1.0 - readings.submerged);
  const float dapple = smoothstep (0.36, 0.72, material.canopy_grain);
  const float near_canopy_sun = mix (0.42, 1.0, dapple);
  const float canopy_sun =
    mix (near_canopy_sun, 0.62, material.forest_distance);
  lighting.canopy_direct = mix (1.0, canopy_sun, canopy_footprint);
  const float canopy_ambient = mix (1.0, 0.76, canopy_footprint);
  lighting.intensity = saturate ((dot (light, normal) + 0.08) / 1.08);
  const float3 shade_fill =
    mix (float3 (0.80, 0.92, 1.14), float3 (1.0), shadow);
  const float3 diffuse_light = lighting.intensity * lighting.direct_visibility *
                                 lighting.canopy_direct * 0.9 *
                                 u.sun_diffuse.rgb +
                               canopy_ambient * shade_fill *
                                 moppe_hemisphere_light (u.ambient.rgb, normal);
  lighting.color = albedo * diffuse_light;

  lighting.half_vector = normalize (light - view_dir);
  const float roughness = mix (0.92, 0.68, bands.cliff);
  const float material_spec =
    (0.012 + 0.035 * (1.0 - roughness)) *
    pow (max (dot (normal, lighting.half_vector), 0.0),
         mix (18.0, 72.0, 1.0 - roughness));
  lighting.color += u.sun_specular.rgb * lighting.direct_visibility *
                    lighting.canopy_direct * material_spec;
  lighting.color += bands.snow * u.sun_specular.rgb *
                    lighting.direct_visibility *
                    pow (max (dot (normal, lighting.half_vector), 0.0), 32.0) *
                    mix (0.06, 0.24, scales.coarse_detail_visibility);
  const float wet_spec =
    material.wetness * pow (max (dot (normal, lighting.half_vector), 0.0),
                            mix (18.0, 52.0, readings.submerged));
  lighting.color += u.sun_specular.rgb * lighting.direct_visibility *
                    lighting.canopy_direct * wet_spec *
                    mix (0.025, 0.075, readings.submerged);
  return lighting;
}

static inline float3
terrain_light_grass_canopy (float3 color,
                            float3 albedo,
                            float3 normal,
                            float3 view_dir,
                            thread const TerrainVaryings& in,
                            thread const TerrainSurfaceReadings& readings,
                            thread const TerrainMaterialBands& bands,
                            thread const TerrainPalette& palette,
                            thread const TerrainMaterial& material,
                            thread const TerrainLighting& lighting,
                            constant MoppeTerrainUniforms& u) {
  const float turf = (1.0 - bands.scree) * (1.0 - bands.cliff) *
                     (1.0 - bands.snow) * (1.0 - material.shore) *
                     (1.0 - material.trail) * (1.0 - 0.92 * material.base) *
                     (1.0 - readings.submerged) * (1.0 - 0.42 * material.wash) *
                     (1.0 - material.forest) *
                     smoothstep (0.52, 0.78, normal.y);
  const float canopy_grass = turf * bands.grass_integrated * palette.turf_cover;
  if (canopy_grass <= 0.002)
    return color;

  const float3 light = u.sun_dir.xyz;
  const float gust = moppe_grass_gust (in.world_pos.xz, u.params2.x);
  const float wave = 1.0 + 0.11 * gust;
  const float3 canopy_light =
    u.sun_diffuse.rgb * lighting.direct_visibility * lighting.canopy_direct;
  const float toward = saturate (dot (view_dir, light));
  const float lobe = moppe_grass_toward_lobe (toward);
  color += sqrt (albedo) * canopy_light * moppe_grass_chlorophyll () * lobe *
           wave * 0.50 * canopy_grass;

  const float3 canopy_axis =
    moppe_grass_ensemble_axis (in.world_pos.xz, u.params2.x);
  const float along_sun = dot (canopy_axis, light);
  const float blade_diffuse =
    0.52 * sqrt (saturate (1.0 - along_sun * along_sun));
  color += albedo * u.sun_diffuse.rgb * lighting.direct_visibility *
           lighting.canopy_direct * (blade_diffuse - lighting.intensity) * 0.9 *
           wave * canopy_grass;
  const float along = dot (canopy_axis, lighting.half_vector);
  const float sheen = pow (sqrt (saturate (1.0 - along * along)), 8.0) *
                      (0.10 + 0.90 * toward * toward);
  color += u.sun_specular.rgb * lighting.direct_visibility *
           lighting.canopy_direct * sheen * wave * 0.35 * canopy_grass;
  const float retro = pow (saturate (-dot (view_dir, light)), 4.0);
  return color + albedo * canopy_light * retro * 0.35 * canopy_grass;
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
  texture2d<float> terrain_channel_flux
  [[texture (MOPPE_TEX_TERRAIN_CHANNEL_FLUX)]],
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

  const TerrainSurfaceReadings readings =
    terrain_read_surface (in,
                          n,
                          u,
                          terrain_landscape,
                          terrain_water,
                          terrain_ground,
                          terrain_channel_flux);
  const float damp = readings.damp;

  const TerrainScaleMeasures scales =
    terrain_measure_scales (in, dist, sea_level, u, grass, smp);
  const TerrainMaterialBands bands =
    terrain_classify_material (in, n, dist, sea_level, readings, scales, u);

  const TerrainPalette palette = terrain_build_palette (
    in, n, damp, scales, bands, grass, dirt, snow, rock, u, smp);

  // These calls are deliberate experiment boundaries. Each stage can be
  // replaced by a trivial implementation without editing the stages around it.
  TerrainMaterial material =
    terrain_compose_substrate (in, n, readings, scales, bands, palette, u);
  terrain_compose_grass (material, bands, palette);
  terrain_compose_pebbles (material, in, n, dist, readings, scales, bands);
  terrain_compose_trail_and_base (
    material, in, dist, readings, scales, bands, palette);
  terrain_compose_forest_and_wetness (material, in, dist, readings);
  float3 texel = material.albedo;
  texel =
    terrain_apply_analysis_overlay (texel, in.field_uv, u, terrain_overlay);

  n = terrain_apply_micro_relief (n, in, readings, bands, material);

  const TerrainLighting lighting = terrain_light_ground (
    texel, n, view_dir, in, readings, scales, bands, material, u, shadow_map);
  float3 color = terrain_light_grass_canopy (lighting.color,
                                             texel,
                                             n,
                                             view_dir,
                                             in,
                                             readings,
                                             bands,
                                             palette,
                                             material,
                                             lighting,
                                             u);
  color = terrain_apply_lattice_overlay (color, in, dist, u);
  return moppe_temporal_output (
    float4 (mix (color, fog_c, fog_factor), 1.0), in.motion, 0.0);
}
