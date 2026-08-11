// Animated translucent ocean -- port of shaders/ocean.vert +
// ocean.frag.  Three overlapping sine swells with Gerstner-style
// horizontal displacement and an analytic normal in the vertex
// stage; procedural ripples, Schlick fresnel and a GGX sun glint
// in the fragment stage.  Lighting runs in world space (the GLSL
// eye-space detour existed only for fixed-function GL lights).

#include "common.h"

struct OceanVaryings {
  float4 position [[position]];
  float3 world_pos;
  float3 normal;
  float fog;
  float2 motion [[center_no_perspective]];
};

// Bilinear grid sample under a point. Float textures must be read at
// integer coordinates, so filtering is explicit. This is shared by the
// R32F terrain heights, the RG32F standing-water grid, and the RG16F
// flow grid, so all three sheets agree on addressing.
static float2 ocean_grid_sample_raw (float2 world_xz,
                                     constant MoppeOceanUniforms& u,
                                     texture2d<float, access::read> grid) {
  const float period = u.shore.w;
  float gx = world_xz.x * u.shore.x;
  float gz = world_xz.y * u.shore.y;
  gx -= floor (gx / period) * period;
  gz -= floor (gz / period) * period;
  const uint2 i0 = uint2 ((uint)gx, (uint)gz);
  const uint2 i1 = (i0 + uint2 (1)) % uint (period);
  const float fx = gx - (float)i0.x;
  const float fz = gz - (float)i0.y;
  const float2 s00 = grid.read (i0).rg;
  const float2 s10 = grid.read (uint2 (i1.x, i0.y)).rg;
  const float2 s01 = grid.read (uint2 (i0.x, i1.y)).rg;
  const float2 s11 = grid.read (i1).rg;
  return mix (mix (s00, s10, fx), mix (s01, s11, fx), fz);
}

static float4 ocean_material_sample (float2 world_xz,
                                     constant MoppeOceanUniforms& u,
                                     texture2d<float> materials) {
  const float2 period (u.shore.w);
  float2 grid = world_xz * u.shore.xy;
  grid -= floor (grid / period) * period;
  constexpr sampler smp (coord::normalized, address::repeat, filter::linear);
  return materials.sample (smp, (grid + 0.5) / period);
}

// Height-bearing sheets: x is height in world meters, y carries the
// water grid's wave amplitude factor.
static float2 ocean_grid_sample (float2 world_xz,
                                 constant MoppeOceanUniforms& u,
                                 texture2d<float, access::read> grid) {
  float2 sample = ocean_grid_sample_raw (world_xz, u, grid);
  sample.x *= u.shore.z;
  return sample;
}

// One exact lattice sample: the wet probe walks tile corners.
static float2 ocean_grid_texel (int2 texel,
                                constant MoppeOceanUniforms& u,
                                texture2d<float, access::read> grid) {
  const int period = int (u.shore.w);
  texel = (texel % period + period) % period;
  return grid.read (uint2 (texel)).rg;
}

static float ocean_grid_height (float2 world_xz,
                                constant MoppeOceanUniforms& u,
                                texture2d<float, access::read> grid) {
  return ocean_grid_sample (world_xz, u, grid).x;
}

// Swell amplitude at a surface point, averaged over a small footprint:
// a point sample flickers cell-to-cell across shallow shelves and the
// whole surface reads as a checkerboard.
static float ocean_wave_scale (float2 world_xz,
                               constant MoppeOceanUniforms& u,
                               texture2d<float, access::read> heights,
                               texture2d<float, access::read> water_levels) {
  const float step = 3.0 / u.shore.x;
  const float2 taps[5] = { float2 (0, 0),
                           float2 (step, 0),
                           float2 (-step, 0),
                           float2 (0, step),
                           float2 (0, -step) };
  float scale = 0.0;
  for (int i = 0; i < 5; ++i) {
    const float2 water =
      ocean_grid_sample (world_xz + taps[i], u, water_levels);
    const float ground = ocean_grid_height (world_xz + taps[i], u, heights);
    scale += smoothstep (0.15, 6.0, water.x - ground) * water.y;
  }
  return 0.2 * scale;
}

// Swells, Gerstner crest displacement, analytic normal, and haze for a
// point of the water surface; shared by the coarse grid vertex stage
// and the lattice tile mesh stage so both surfaces move as one.
static OceanVaryings ocean_surface_point (float3 p,
                                          float wave_scale,
                                          constant MoppeOceanUniforms& u) {
  const float time = u.params.x;

  // Swells are near-field geometry.  A 300 m sine carrying meters of
  // amplitude reads fine underfoot, but from a ridge the whole body
  // visibly sloshes like a skipped rope, so the displacement fades
  // with distance and far water keeps its motion in the fragment
  // stage's ripple normals instead.
  const float cam_dist = length (p - u.camera_pos.xyz);
  wave_scale *= 1.0 - smoothstep (250.0, 900.0, cam_dist);

  // Three overlapping swells with different directions and speeds.
  const float a1 = p.x * 0.020 + time * 1.1;
  const float a2 = p.z * 0.023 - time * 0.9;
  const float a3 = (p.x + p.z) * 0.011 + time * 0.6;

  p.y += wave_scale * (1.2 * sin (a1) + 1.0 * sin (a2) + 1.8 * sin (a3));

  // Gerstner-style horizontal displacement sharpens the crests.
  p.x -= wave_scale * (0.8 * 1.2 * cos (a1) + 0.5 * 1.8 * cos (a3));
  p.z -= wave_scale * (0.8 * 1.0 * cos (a2) + 0.5 * 1.8 * cos (a3));

  // Analytic surface normal from the wave derivatives.
  const float dx =
    wave_scale * (1.2 * 0.020 * cos (a1) + 1.8 * 0.011 * cos (a3));
  const float dz =
    wave_scale * (1.0 * 0.023 * cos (a2) + 1.8 * 0.011 * cos (a3));

  OceanVaryings out;
  out.position = u.view_proj * float4 (p, 1.0);
  out.world_pos = p;
  out.normal = normalize (float3 (-dx, 1.0, -dz));

  // Same haze curve as the terrain shader, plus the sea mist that
  // pools over the water (matching the terrain's valley mist).
  const float dist = length (p - u.camera_pos.xyz);
  float fog = moppe_distance_fog (dist, u.fog_color.w);
  fog += 0.3 * smoothstep (150.0, 1500.0, dist);
  out.fog = saturate (fog);
  const float4 current_reference = u.unjittered_view_proj * float4 (p, 1.0);
  const float4 previous_reference = u.previous_view_proj * float4 (p, 1.0);
  out.motion =
    moppe_motion_vector (current_reference, previous_reference, u.temporal.xy);
  return out;
}

vertex OceanVaryings ocean_vertex (uint vid [[vertex_id]],
                                   const device packed_float3* verts
                                   [[buffer (MOPPE_BUF_VERTICES)]],
                                   constant MoppeOceanUniforms& u
                                   [[buffer (MOPPE_BUF_FRAME)]],
                                   texture2d<float, access::read> heights
                                   [[texture (MOPPE_TEX_HEIGHTS)]],
                                   texture2d<float, access::read> water_levels
                                   [[texture (MOPPE_TEX_WATER_LEVELS)]]) {
  float3 p = float3 (verts[vid]);
  p += u.world_offset.xyz;

  float wave_scale = 1.0;
  if (u.params.w > 0.5) {
    // Waves vanish at lake and ocean shores instead of climbing dry
    // ground, and inland bodies only ripple with their per-body
    // amplitude: a tarn must not heave like the open sea.
    p.y = ocean_grid_sample (p.xz, u, water_levels).x;
    // ocean_surface_point fades the swell to nothing by 900 m and the
    // fragment stage's ripples take over. This grid spans the whole
    // world, so all but a couple of percent of its corners are past
    // that: reading the ten-tap shore filter for a swell about to be
    // multiplied by zero is the grid's largest avoidable cost. Same
    // number out, and the threshold is the fade's own.
    wave_scale = length (p - u.camera_pos.xyz) < 900.0
                   ? ocean_wave_scale (p.xz, u, heights, water_levels)
                   : 0.0;
  }
  return ocean_surface_point (p, wave_scale, u);
}

// ---- lattice water tiles (mesh pipeline) ---------------------------

// A tile is 8x8 terrain cells. Its 9x9 lattice corners plus every horizontal
// and vertical edge crossing fit in one 225-vertex meshlet. Boundary cells
// are clipped to the signed water-minus-ground zero set instead of moving a
// dry lattice corner and leaving its neighbouring triangles attached. That
// distinction matters on cliffs: moving the corner made a water-shaded flap;
// clipping makes a shoreline.
#define WATER_TILE_CELLS 8
#define WATER_TILE_SIDE (WATER_TILE_CELLS + 1)
#define WATER_TILE_CORNERS (WATER_TILE_SIDE * WATER_TILE_SIDE)
#define WATER_TILE_HORIZONTAL_EDGES (WATER_TILE_CELLS * WATER_TILE_SIDE)
#define WATER_TILE_VERTICAL_EDGES (WATER_TILE_SIDE * WATER_TILE_CELLS)
#define WATER_TILE_HORIZONTAL_BASE WATER_TILE_CORNERS
#define WATER_TILE_VERTICAL_BASE                                               \
  (WATER_TILE_HORIZONTAL_BASE + WATER_TILE_HORIZONTAL_EDGES)
#define WATER_TILE_VERTEX_COUNT                                                \
  (WATER_TILE_VERTICAL_BASE + WATER_TILE_VERTICAL_EDGES)
#define WATER_TILE_MAX_TRIANGLES (WATER_TILE_CELLS * WATER_TILE_CELLS * 4)
#define WATER_OBJECT_THREADS 64

struct WaterTilePayload {
  uint count;
  uint2 tiles[WATER_OBJECT_THREADS];
};

using WaterTileMesh = metal::mesh<OceanVaryings,
                                  void,
                                  256,
                                  WATER_TILE_MAX_TRIANGLES,
                                  metal::topology::triangle>;

[[object]] void
water_tile_object (object_data WaterTilePayload& payload [[payload]],
                   metal::mesh_grid_properties mesh_grid,
                   uint thread_id [[thread_index_in_threadgroup]],
                   uint3 grid_pos [[thread_position_in_grid]],
                   constant MoppeOceanUniforms& u [[buffer (MOPPE_BUF_FRAME)]],
                   texture2d<float, access::read> heights
                   [[texture (MOPPE_TEX_HEIGHTS)]],
                   texture2d<float, access::read> water_levels
                   [[texture (MOPPE_TEX_WATER_LEVELS)]]) {
  threadgroup atomic_uint survivors;
  if (thread_id == 0u)
    atomic_store_explicit (&survivors, 0u, metal::memory_order_relaxed);
  threadgroup_barrier (metal::mem_flags::mem_threadgroup);

  const uint tiles_side = uint (u.tiles.z);
  const uint index = grid_pos.x;
  bool valid = index < tiles_side * tiles_side;
  const uint tile_x = index % max (tiles_side, 1u);
  const uint tile_z = index / max (tiles_side, 1u);

  if (valid) {
    const float spacing = 1.0 / u.shore.x;
    const float tile_world = WATER_TILE_CELLS * spacing;
    const float2 base =
      (float2 (int2 (u.tiles.xy)) + float2 (tile_x, tile_z)) * tile_world;
    const float2 center = base + 0.5 * tile_world;
    const float fine_radius = -u.tiles.w;
    const float distance = length (center - u.camera_pos.xz);
    valid = distance < fine_radius + 0.75 * tile_world;

    if (valid) {
      // Wet probe on every lattice corner of the tile. Water minus
      // ground is bilinear inside each cell, so its maximum sits on a
      // corner: this probe is exact, and a painted river a cell and a
      // half wide cannot slip between the samples.
      //
      // A dry tile -- most of the window, most frames -- reads all 81
      // corners, and there is only one thread per tile, so this loop has
      // very little else to hide its memory latency behind. Two things
      // therefore matter more here than the early exit does.
      //
      // The wrap is hoisted out: the tile window is tiny against a lattice
      // thousands of cells across, so one conditional subtract
      // stands in for the modulo pair, and the loop does no integer
      // division at all. The row runs to its end instead of testing after
      // every corner, leaving its reads mutually independent.
      const int period = int (u.shore.w);
      const int2 origin =
        (int2 (u.tiles.xy) + int2 (tile_x, tile_z)) * WATER_TILE_CELLS;
      const int2 base = ((origin % period) + period) % period;
      const float wet_epsilon = 0.05 / u.shore.z;
      bool wet = false;
      float level = 0.0;
      for (uint row = 0; row < WATER_TILE_SIDE && !wet; ++row) {
        const int wrapped_z = base.y + int (row);
        const uint tz =
          uint (wrapped_z >= period ? wrapped_z - period : wrapped_z);
        for (uint col = 0; col < WATER_TILE_SIDE; ++col) {
          const int wrapped_x = base.x + int (col);
          const uint2 texel (
            uint (wrapped_x >= period ? wrapped_x - period : wrapped_x), tz);
          const float water = water_levels.read (texel).r;
          if (water - heights.read (texel).r > wet_epsilon) {
            wet = true;
            level = water * u.shore.z;
          }
        }
      }
      valid = wet;

      if (valid) {
        const float4 clip =
          u.view_proj * float4 (center.x, level, center.y, 1.0);
        const float margin = 1.35 * clip.w + 2.5 * tile_world;
        valid = clip.w > -tile_world && abs (clip.x) < margin &&
                abs (clip.y) < margin;
      }
    }

    if (valid) {
      const uint slot =
        atomic_fetch_add_explicit (&survivors, 1u, metal::memory_order_relaxed);
      payload.tiles[slot] = uint2 (tile_x, tile_z);
    }
  }

  threadgroup_barrier (metal::mem_flags::mem_threadgroup);
  if (thread_id == 0u) {
    payload.count =
      atomic_load_explicit (&survivors, metal::memory_order_relaxed);
    mesh_grid.set_threadgroups_per_grid (uint3 (payload.count, 1, 1));
  }
}

[[mesh]] void water_tile_mesh (WaterTileMesh out,
                               object_data const WaterTilePayload& payload
                               [[payload]],
                               uint mesh_id [[threadgroup_position_in_grid]],
                               uint thread_id [[thread_index_in_threadgroup]],
                               constant MoppeOceanUniforms& u
                               [[buffer (MOPPE_BUF_FRAME)]],
                               texture2d<float, access::read> heights
                               [[texture (MOPPE_TEX_HEIGHTS)]],
                               texture2d<float, access::read> water_levels
                               [[texture (MOPPE_TEX_WATER_LEVELS)]]) {
  constexpr uint side = WATER_TILE_SIDE;
  const uint2 tile = payload.tiles[min (mesh_id, payload.count - 1u)];
  const float spacing = 1.0 / u.shore.x;
  const int2 origin = (int2 (u.tiles.xy) + int2 (tile)) * WATER_TILE_CELLS;
  const float2 world_origin = float2 (origin) * spacing;

  // First publish the signed water depth at every lattice corner. A later
  // thread owns each possible edge crossing, so adjacent cells and adjacent
  // tiles name the same geometric point instead of independently moving one
  // of their dry corners.
  threadgroup float corner_depth[WATER_TILE_CORNERS];
  if (thread_id < WATER_TILE_CORNERS) {
    const uint vx = thread_id % side;
    const uint vz = thread_id / side;
    const int2 texel = origin + int2 (vx, vz);
    const float ground = ocean_grid_texel (texel, u, heights).x;
    const float water = ocean_grid_texel (texel, u, water_levels).x;
    corner_depth[thread_id] = water - ground;
    if (water > ground) {
      const float2 world_xz = world_origin + float2 (vx, vz) * spacing;
      const float wave_scale =
        ocean_wave_scale (world_xz, u, heights, water_levels);
      out.set_vertex (
        thread_id,
        ocean_surface_point (
          float3 (world_xz.x, water * u.shore.z, world_xz.y), wave_scale, u));
    }
  }
  threadgroup_barrier (metal::mem_flags::mem_threadgroup);

  // Horizontal edge intersections.
  if (thread_id >= WATER_TILE_HORIZONTAL_BASE &&
      thread_id < WATER_TILE_VERTICAL_BASE) {
    const uint edge = thread_id - WATER_TILE_HORIZONTAL_BASE;
    const uint vx = edge % WATER_TILE_CELLS;
    const uint vz = edge / WATER_TILE_CELLS;
    const uint a = vz * side + vx;
    const uint b = a + 1u;
    const float da = corner_depth[a];
    const float db = corner_depth[b];
    if ((da > 0.0) != (db > 0.0)) {
      const float t = clamp (da / (da - db), 0.0, 1.0);
      const float2 world_xz =
        world_origin + (float2 (vx, vz) + float2 (t, 0.0)) * spacing;
      const float water = ocean_grid_sample (world_xz, u, water_levels).x;
      const float wave_scale =
        ocean_wave_scale (world_xz, u, heights, water_levels);
      out.set_vertex (thread_id,
                      ocean_surface_point (
                        float3 (world_xz.x, water, world_xz.y), wave_scale, u));
    }
  }

  // Vertical edge intersections.
  if (thread_id >= WATER_TILE_VERTICAL_BASE &&
      thread_id < WATER_TILE_VERTEX_COUNT) {
    const uint edge = thread_id - WATER_TILE_VERTICAL_BASE;
    const uint vx = edge % side;
    const uint vz = edge / side;
    const uint a = vz * side + vx;
    const uint b = a + side;
    const float da = corner_depth[a];
    const float db = corner_depth[b];
    if ((da > 0.0) != (db > 0.0)) {
      const float t = clamp (da / (da - db), 0.0, 1.0);
      const float2 world_xz =
        world_origin + (float2 (vx, vz) + float2 (0.0, t)) * spacing;
      const float water = ocean_grid_sample (world_xz, u, water_levels).x;
      const float wave_scale =
        ocean_wave_scale (world_xz, u, heights, water_levels);
      out.set_vertex (thread_id,
                      ocean_surface_point (
                        float3 (world_xz.x, water, world_xz.y), wave_scale, u));
    }
  }

  // Each cell contributes a clipped perimeter polygon. The two saddle cases
  // use the bilinear determinant as an asymptotic decider: either two separate
  // wet corners, or the same corners joined by a four-triangle neck.
  threadgroup atomic_uint kept_triangles;
  if (thread_id == 0u)
    atomic_store_explicit (&kept_triangles, 0u, metal::memory_order_relaxed);
  threadgroup_barrier (metal::mem_flags::mem_threadgroup);

  uint triangle_indices[12];
  uint triangle_count = 0u;
  if (thread_id < WATER_TILE_CELLS * WATER_TILE_CELLS) {
    const uint cx = thread_id % WATER_TILE_CELLS;
    const uint cz = thread_id / WATER_TILE_CELLS;
    const uint corners[4] = { cz * side + cx,
                              cz * side + cx + 1u,
                              (cz + 1u) * side + cx + 1u,
                              (cz + 1u) * side + cx };
    const uint edges[4] = {
      WATER_TILE_HORIZONTAL_BASE + cz * WATER_TILE_CELLS + cx,
      WATER_TILE_VERTICAL_BASE + cz * side + cx + 1u,
      WATER_TILE_HORIZONTAL_BASE + (cz + 1u) * WATER_TILE_CELLS + cx,
      WATER_TILE_VERTICAL_BASE + cz * side + cx
    };
    uint mask = 0u;
    for (uint corner = 0u; corner < 4u; ++corner)
      if (corner_depth[corners[corner]] > 0.0)
        mask |= 1u << corner;

    if (mask == 5u || mask == 10u) {
      // Nielson's asymptotic decider for the bilinear patch. Sampling the
      // cell center is only an approximation and can flip the connection
      // when the opposite corners have very different magnitudes.
      const float determinant =
        corner_depth[corners[0]] * corner_depth[corners[2]] -
        corner_depth[corners[1]] * corner_depth[corners[3]];
      const float center = corner_depth[corners[0]] + corner_depth[corners[1]] +
                           corner_depth[corners[2]] + corner_depth[corners[3]];
      const bool center_wet =
        mask == 5u ? determinant > 0.0 || (determinant == 0.0 && center > 0.0)
                   : determinant < 0.0 || (determinant == 0.0 && center > 0.0);
      const uint first = mask == 5u ? 0u : 1u;
      const uint second = first + 2u;
      const uint before_first = (first + 3u) % 4u;
      const uint before_second = (second + 3u) % 4u;
      triangle_indices[0] = corners[first];
      triangle_indices[1] = edges[first];
      triangle_indices[2] = edges[before_first];
      triangle_indices[3] = corners[second];
      triangle_indices[4] = edges[second];
      triangle_indices[5] = edges[before_second];
      triangle_count = 2u;
      if (center_wet) {
        triangle_indices[6] = edges[first];
        triangle_indices[7] = edges[second];
        triangle_indices[8] = edges[before_first];
        triangle_indices[9] = edges[before_first];
        triangle_indices[10] = edges[second];
        triangle_indices[11] = edges[before_second];
        triangle_count = 4u;
      }
    } else if (mask != 0u) {
      uint polygon[8];
      uint polygon_count = 0u;
      for (uint corner = 0u; corner < 4u; ++corner) {
        const uint next = (corner + 1u) % 4u;
        const bool wet = (mask & (1u << corner)) != 0u;
        const bool next_wet = (mask & (1u << next)) != 0u;
        if (wet)
          polygon[polygon_count++] = corners[corner];
        if (wet != next_wet)
          polygon[polygon_count++] = edges[corner];
      }
      for (uint triangle = 0u; triangle + 2u < polygon_count; ++triangle) {
        triangle_indices[3u * triangle] = polygon[0];
        triangle_indices[3u * triangle + 1u] = polygon[triangle + 1u];
        triangle_indices[3u * triangle + 2u] = polygon[triangle + 2u];
      }
      triangle_count = polygon_count >= 3u ? polygon_count - 2u : 0u;
    }
  }

  uint triangle_slot = 0u;
  if (triangle_count > 0u)
    triangle_slot = atomic_fetch_add_explicit (
      &kept_triangles, triangle_count, metal::memory_order_relaxed);
  threadgroup_barrier (metal::mem_flags::mem_threadgroup);

  if (thread_id == 0u)
    out.set_primitive_count (
      atomic_load_explicit (&kept_triangles, metal::memory_order_relaxed));
  for (uint index = 0u; index < 3u * triangle_count; ++index)
    out.set_index (3u * triangle_slot + index, triangle_indices[index]);
}

// The directionless ripple field is optical evidence shared by ordinary
// water shading and the ray-query input pass. Flow deformation is layered on
// afterward by ocean_fragment; Goal 1 deliberately admits standing water
// only, so it calls this with full strength.
static float3 ocean_ripple_normal (OceanVaryings in,
                                   constant MoppeOceanUniforms& u,
                                   float flow_atten) {
  const float time = u.params.x;
  const float dist = length (in.world_pos - u.camera_pos.xyz);
  float3 n = normalize (in.normal);
  const float wavelet_fade = exp (-dist * 0.008);
  const float chop_fade = exp (-dist * 0.002);
  const float streak_fade = 1.0 - 0.6 * smoothstep (2000.0, 6000.0, dist);
  const float2 ripple1 =
    in.world_pos.xz * 0.35 + float2 (time * 0.9, -time * 0.7);
  const float2 ripple2 =
    in.world_pos.xz * 0.11 - float2 (time * 0.4, time * 0.5);
  const float2 ripple3 =
    in.world_pos.xz * 0.017 + float2 (time * 0.16, time * 0.11);
  n.x +=
    flow_atten * (0.16 * wavelet_fade * (moppe_value_noise (ripple1) - 0.5) +
                  0.12 * chop_fade * (moppe_value_noise (ripple2) - 0.5) +
                  0.11 * streak_fade * (moppe_value_noise (ripple3) - 0.5));
  n.z += flow_atten *
         (0.16 * wavelet_fade * (moppe_value_noise (ripple1 + 7.3) - 0.5) +
          0.12 * chop_fade * (moppe_value_noise (ripple2 + 3.1) - 0.5) +
          0.11 * streak_fade * (moppe_value_noise (ripple3 + 11.7) - 0.5));
  return normalize (n);
}

struct ReflectionWaterInput {
  float4 origin [[color (0)]];
  half4 optical_normal [[color (1)]];
};

// Rasterize the same clipped, displaced standing-water surface as the normal
// water pass. World positions remain float32 because half precision loses
// metres at Moppe's kilometre-scale periodic coordinates.
fragment ReflectionWaterInput reflection_water_input_fragment (
  OceanVaryings in [[stage_in]],
  constant MoppeOceanUniforms& u [[buffer (MOPPE_BUF_FRAME)]],
  texture2d<float, access::read> heights [[texture (MOPPE_TEX_HEIGHTS)]],
  texture2d<float, access::read> water_levels
  [[texture (MOPPE_TEX_WATER_LEVELS_FRAGMENT)]],
  texture2d<float, access::read> water_flow
  [[texture (MOPPE_TEX_WATER_FLOW_FRAGMENT)]]) {
  const float3 to_frag = in.world_pos - u.camera_pos.xyz;
  const float planar = length (in.world_pos.xz - u.camera_pos.xz);
  if (u.tiles.w > 0.5 && planar < u.tiles.w)
    discard_fragment ();
  if (u.tiles.w < -0.5 && planar > -u.tiles.w)
    discard_fragment ();

  const float ground = ocean_grid_height (in.world_pos.xz, u, heights);
  const float2 water_state =
    u.params.w > 0.5 ? ocean_grid_sample (in.world_pos.xz, u, water_levels)
                     : float2 (u.params.y, 1.0);
  const float still_depth = water_state.x - ground;
  if (u.params.w > 0.5 && still_depth <= 0.005)
    discard_fragment ();

  if (dot (normalize (in.normal), normalize (-to_frag)) <= 0.0 &&
      u.params.w > 0.5) {
    const float2 camera_water =
      ocean_grid_sample (u.camera_pos.xz, u, water_levels);
    const bool camera_in_water =
      camera_water.y > 0.5 && u.camera_pos.y < camera_water.x + 0.10;
    if (!camera_in_water)
      discard_fragment ();
  }

  float flow_speed = 0.0;
  if (u.current.x > 0.5 && u.shore.w > 0.5)
    flow_speed =
      length (ocean_grid_sample_raw (in.world_pos.xz, u, water_flow));
  const bool running_water =
    flow_speed > 0.25 && water_state.y < 0.01 && still_depth < 3.0;
  if (running_water)
    discard_fragment ();

  float3 normal = ocean_ripple_normal (in, u, 1.0);
  const float3 view = normalize (-to_frag);
  if (dot (normal, view) < 0.0)
    normal = -normal;
  ReflectionWaterInput out;
  out.origin = float4 (in.world_pos, 1.0);
  out.optical_normal = half4 (half3 (normal), 0.10h);
  return out;
}

// Bilinear ground height under a point, in world meters.  R32F
// must be read() at integer coords, so filter by hand.
fragment MoppeTemporalOutput ocean_fragment (
  OceanVaryings in [[stage_in]],
  constant MoppeOceanUniforms& u [[buffer (MOPPE_BUF_FRAME)]],
  texture2d<float, access::read> heights [[texture (MOPPE_TEX_HEIGHTS)]],
  texture2d<float, access::read> water_levels
  [[texture (MOPPE_TEX_WATER_LEVELS_FRAGMENT)]],
  texture2d<float, access::read> water_flow
  [[texture (MOPPE_TEX_WATER_FLOW_FRAGMENT)]],
  texture2d<float> geology [[texture (MOPPE_TEX_WATER_GEOLOGY_FRAGMENT)]],
  depth2d<float> shadow_map [[texture (MOPPE_TEX_SHADOW)]]) {
  const float time = u.params.x;
  const float3 to_frag = in.world_pos - u.camera_pos.xyz;
  const float dist = length (to_frag);

  // The lattice tiles own the water inside the fine radius and the
  // coarse grid owns everything beyond it; both discard on the same
  // predicate so the two surfaces partition exactly.
  const float planar = length (in.world_pos.xz - u.camera_pos.xz);
  if (u.tiles.w > 0.5 && planar < u.tiles.w)
    discard_fragment ();
  if (u.tiles.w < -0.5 && planar > -u.tiles.w)
    discard_fragment ();

  const float ground = ocean_grid_height (in.world_pos.xz, u, heights);
  const float2 water_state =
    u.params.w > 0.5 ? ocean_grid_sample (in.world_pos.xz, u, water_levels)
                     : float2 (u.params.y, 1.0);
  const float surface = water_state.x;
  const float still_depth = surface - ground;

  // Keep the surface two-sided only while the camera is actually submerged
  // in this water field. Without this test a dry ravine below an elevated
  // lake sees the lake's underside as a bright blue ceiling. True underwater
  // views remain intact because the camera probe is wet and below its local
  // surface.
  if (dot (normalize (in.normal), normalize (-to_frag)) <= 0.0 &&
      u.params.w > 0.5) {
    const float2 camera_water =
      ocean_grid_sample (u.camera_pos.xz, u, water_levels);
    // The gameplay underwater pass currently belongs to the sea. Inland
    // lake sheets may bridge very steep banks in the finite terrain field;
    // treating a dry camera below one as submerged recreates the blue-roof
    // glitch this test exists to reject.
    const bool camera_in_water =
      camera_water.y > 0.5 && u.camera_pos.y < camera_water.x + 0.10;
    if (!camera_in_water)
      discard_fragment ();
  }

  // The flow sheet: which way the water moves and how fast, painted
  // per terrain cell. Rivers carry strong arrows, lakes almost none,
  // and at a confluence the painted arrows already blend, so the same
  // shading swirls two currents into one with no seam to sew.
  float flowing = 0.0;
  float rapid = 0.0;
  float flow_detail = 0.5;
  float2 flow_grad = float2 (0.0);
  float flow_speed = 0.0;
  float current_character = 0.0;
  const float flow_fade = exp (-dist * 0.0012);
  if (u.current.x > 0.5 && u.shore.w > 0.5) {
    const float2 flow = ocean_grid_sample_raw (in.world_pos.xz, u, water_flow);
    flow_speed = length (flow);
    // Keep the material identity independent of camera distance. Only the
    // animated detail retires into the distance; otherwise a dark stream
    // would turn back into a bright sky ribbon as the camera pulled away.
    current_character = smoothstep (0.25, 1.0, flow_speed);
    flowing = current_character * flow_fade;
    rapid = smoothstep (4.0, 7.5, flow_speed);
    if (flowing > 1e-3) {
      // Flow map, two phases: a copy of the surface detail drifts
      // with the current for one cycle and hands over to a fresh copy
      // before it shears apart. Foam on real water is just as
      // short-lived, so the handover reads as nature, not a loop.
      const float cycle = 1.7;
      const float t0 = fract (time / cycle);
      const float t1 = fract (t0 + 0.5);
      const float blend = abs (2.0 * t0 - 1.0);
      // The texture frame follows the current: a dense cross-stream axis and
      // a longer along-stream axis make ripples stretch with flow through
      // bends. At confluences the painted vector already blends the two
      // incoming directions, so this basis rotates continuously too.
      const float2 direction = flow / max (flow_speed, 1e-4);
      const float2 across = float2 (-direction.y, direction.x);
      const float2 base_uv = float2 (dot (in.world_pos.xz, across) * 1.15,
                                     dot (in.world_pos.xz, direction) * 0.32);
      const float2 drift = float2 (0.0, flow_speed * 0.32 * cycle);
      const float2 uv0 = base_uv - drift * (t0 - 0.5);
      const float2 uv1 = base_uv - drift * (t1 - 0.5);
      const float n0 = moppe_value_noise (uv0);
      const float n1 = moppe_value_noise (uv1);
      const float2 g0 =
        float2 (moppe_value_noise (uv0 + float2 (0.31, 0.0)) - n0,
                moppe_value_noise (uv0 + float2 (0.0, 0.29)) - n0);
      const float2 g1 =
        float2 (moppe_value_noise (uv1 + float2 (0.31, 0.0)) - n1,
                moppe_value_noise (uv1 + float2 (0.0, 0.29)) - n1);
      flow_detail = mix (n0, n1, blend);
      const float2 texture_grad = mix (g0, g1, blend);
      flow_grad = across * texture_grad.x + direction * (0.32 * texture_grad.y);
    }
  }

  // A narrow channel cannot survive resampling by the whole-world coarse
  // grid. The exact lattice owns running water nearby; standing bodies
  // continue seamlessly onto the coarse surface.
  const bool running_water =
    flow_speed > 0.25 && water_state.y < 0.01 && still_depth < 3.0;
  if (u.tiles.w > 0.5 && running_water)
    discard_fragment ();
  const float running_lod =
    u.tiles.w < -0.5 && running_water
      ? 1.0 - smoothstep (0.80 * -u.tiles.w, -u.tiles.w, planar)
      : 1.0;

  // A small lap at the waterline. Reuse the water sheet's body-scale motion
  // factor so lakes and ponds remain nearly still instead of receiving the
  // ocean's shoreline motion. Running water keeps its fixed edge and churns
  // through the flow treatment below.
  const float shore_band = 1.0 - smoothstep (0.0, 1.5, still_depth);
  const float swash_phase =
    sin (time * 1.15 + 6.28318 * moppe_value_noise (in.world_pos.xz * 0.045));
  const float body_motion = saturate (water_state.y);
  const float swash =
    shore_band * 0.03 * body_motion * (1.0 + swash_phase) * (1.0 - flowing);
  const float depth_m = max (still_depth - swash, 0.0);
  if (u.params.w > 0.5 && still_depth <= 0.005)
    discard_fragment ();

  // Small-scale ripples on top of the swells.  Three drifting
  // value-noise octaves hand over with distance -- fine wavelets
  // underfoot, broad wind streaks far out -- so each keeps roughly
  // its screen-space frequency and the surface is never a perfect
  // mirror: a flat normal under a bright horizon sky reads as a
  // sheet of milk.  Where the water flows, the directionless drift
  // yields to the advected detail riding the current.
  const float flow_atten = 1.0 - 0.75 * flowing;
  float3 n = ocean_ripple_normal (in, u, flow_atten);
  const float flow_bump = flowing * (0.36 + 0.34 * rapid);
  n.x += flow_bump * flow_grad.x;
  n.z += flow_bump * flow_grad.y;
  n = normalize (n);

  const float3 v = normalize (-to_frag);
  // Keep the optical normal facing the viewer. The same sheet is visible
  // from below while swimming, but Fresnel is defined against the interface
  // normal on the viewer's side rather than against a signed dot product.
  if (dot (n, v) < 0.0)
    n = -n;
  const float3 reflection_dir = reflect (-v, n);
  const float sun_visibility =
    moppe_sun_visibility (in.world_pos,
                          n,
                          u.sun_dir.xyz,
                          in.fog,
                          u.light_matrix,
                          u.shadow.x,
                          u.shadow.y,
                          shadow_map) *
    moppe_cloud_transmission (
      in.world_pos, u.sun_dir.xyz, u.params.x, u.params.z);

  // Air-to-water Schlick Fresnel. F0 follows from an IOR of 1.333: water is
  // almost transparent head-on but becomes a proper mirror at grazing view.
  const float water_f0 = 0.02037;
  const float nv = max (dot (n, v), 1e-4);
  const float fresnel = water_f0 + (1.0 - water_f0) * pow (1.0 - nv, 5.0);

  // A compact GGX sun lobe. Flow and rapids broaden the microfacet
  // distribution instead of adding an unrelated white highlight.
  const float3 sun = normalize (u.sun_dir.xyz);
  const float3 h = normalize (sun + v);
  const float nl = max (dot (n, sun), 0.0);
  const float nh = max (dot (n, h), 0.0);
  const float vh = max (dot (v, h), 0.0);
  const float roughness = 0.10 + 0.10 * current_character + 0.06 * rapid;
  const float alpha_roughness = roughness * roughness;
  const float alpha2 = alpha_roughness * alpha_roughness;
  const float denominator = nh * nh * (alpha2 - 1.0) + 1.0;
  const float distribution =
    alpha2 / max (3.14159265 * denominator * denominator, 1e-5);
  const float geometry_k = 0.5 * alpha_roughness;
  const float geometry_v = nv / (nv * (1.0 - geometry_k) + geometry_k);
  const float geometry_l =
    nl / max (nl * (1.0 - geometry_k) + geometry_k, 1e-4);
  const float sun_fresnel = water_f0 + (1.0 - water_f0) * pow (1.0 - vh, 5.0);
  const float spec = nl > 0.0 ? min (distribution * geometry_v * geometry_l *
                                       sun_fresnel / max (4.0 * nv, 1e-4),
                                     6.0) *
                                  (1.0 - in.fog)
                              : 0.0;

  const float daylight = smoothstep (-0.08, 0.18, sun.y);
  const float golden = daylight * (1.0 - smoothstep (0.15, 0.65, sun.y));
  const float3 glint_color = mix (moppe_srgb (float3 (0.92, 0.96, 1.0)),
                                  moppe_srgb (float3 (1.0, 0.67, 0.34)),
                                  golden);

  // Silt from the sediment ledger clouds the water: a lake fed by
  // depositing gullies reads murky green-brown while a rock-bound
  // basin stays clear.  (The ledger's deposited column rides in the
  // geology raster's green channel, on the same lattice as the
  // heights.)
  const float turbidity =
    (u.current.y > 0.5 && u.shore.w > 0.5)
      ? saturate (1.4 * ocean_material_sample (in.world_pos.xz, u, geology).b)
      : 0.0;

  float foam = 0.0;
  float3 foam_radiance (0.0);
  // Shoreline foam and running-water churn are an opaque material laid over
  // the optical surface. Keep their coverage separate until the water column
  // has established reflection and transmission below.
  if (u.shore.w > 0.5) {
    // Foam: hugs the waterline (the pow sharpens the band so the
    // wide shallow shelf doesn't stripe), pulsing gently, broken up
    // by drifting value noise (plane-wave products read as plaid
    // moire from the air).
    // Foam surges with the swash: brightest as the water runs up the
    // beach, thinning as it drains back.
    const float surge = 0.5 + 0.5 * swash_phase;
    foam = pow (1.0 - smoothstep (0.0, 1.2, depth_m), 1.7) *
           (0.65 + 0.45 * surge) * (1.0 - flowing);
    // The breakup noise only runs inside the shore band; deep water
    // keeps its zero foam without paying for it.
    if (foam > 1e-3) {
      const float b1 = moppe_value_noise (in.world_pos.xz * 0.22 +
                                          float2 (time * 0.10, -time * 0.07));
      const float b2 = moppe_value_noise (in.world_pos.xz * 0.55 -
                                          float2 (time * 0.05, time * 0.09));
      foam *= 0.25 + 0.75 * smoothstep (0.25, 0.80, 0.55 * b1 + 0.45 * b2);
    }
    // Flowing water trades the tidal foam for churn: broken water
    // rides the advected detail wherever the arrows run fast — which
    // is exactly the steep, quick squares where the land makes
    // rapids. Calm shallow streams get none at all, so they stay
    // clear water instead of white tubes.
    const float churn = flowing * (0.10 + 0.90 * rapid) *
                        smoothstep (0.55, 0.92, flow_detail + 0.30 * rapid);
    foam = max (foam, saturate (churn));
    foam = saturate (foam) * (0.35 + 0.65 * daylight);

    const float3 foam_albedo = moppe_srgb (float3 (0.93, 0.97, 1.0));
    const float3 foam_light =
      moppe_hemisphere_light (u.ambient.rgb, n) +
      u.sun_diffuse.rgb * (0.35 + 0.65 * nl) * sun_visibility;
    foam_radiance = foam_albedo * foam_light;
  }

  // Aerial perspective: same sun-warmed haze as the terrain.
  const float3 fog_c =
    moppe_warmed_fog (u.fog_color.rgb, to_frag / max (dist, 1e-4), sun);
  float3 sky_reflection =
    moppe_sky_radiance (u.fog_color.rgb, reflection_dir, sun);

  // The cheap environment map contains sky but no terrain. That is a fair
  // approximation on an open lake; in a shallow channel it reflects a bright
  // horizon where the real ray would usually meet a bank, reeds, or canopy.
  // Depth and current are the material evidence available here: they leave
  // standing/deep water alone and replace only the grazing environment of small
  // running water with a restrained earth-and-vegetation response. Fresnel
  // itself stays physical -- this changes what the surface sees, not how much
  // of it the interface reflects.
  float shallow_channel = 0.0;
  if (current_character > 0.001) {
    shallow_channel =
      current_character * (1.0 - smoothstep (0.35, 1.80, depth_m));
    const float horizon_reflection =
      1.0 - smoothstep (0.16, 0.82, saturate (reflection_dir.y));
    const float channel_enclosure =
      shallow_channel * mix (0.34, 1.0, horizon_reflection);
    const float3 green_bank = moppe_srgb (float3 (0.15, 0.20, 0.12));
    const float3 earth_bank = moppe_srgb (float3 (0.24, 0.20, 0.12));
    const float3 bank_albedo = mix (green_bank, earth_bank, turbidity);
    const float bank_light =
      0.58 + 0.26 * sun_visibility +
      0.45 * dot (moppe_hemisphere_light (u.ambient.rgb, n),
                  float3 (0.299, 0.587, 0.114));
    const float3 bank_reflection = bank_albedo * bank_light;
    sky_reflection =
      mix (sky_reflection, bank_reflection, 0.78 * channel_enclosure);
  }

  // Beer-Lambert extinction carries the transparency. Red is absorbed first;
  // sediment and current-borne particles shorten all three mean free paths.
  // The destination framebuffer already contains the lit bed, so alpha is
  // chosen to leave exactly the surviving scalar share of that bed visible.
  float3 extinction = float3 (0.34, 0.10, 0.045);
  extinction += turbidity * float3 (1.25, 0.92, 0.62);
  extinction += flowing * float3 (0.10, 0.14, 0.16);
  extinction *= mix (1.0, 1.10 - 0.16 * (flow_detail - 0.5), flowing);
  const float3 transmission = exp (-extinction * depth_m);
  const float bed_visibility = dot (transmission, float3 (0.299, 0.587, 0.114));

  // Light removed from the bed path becomes colored in-scattering from the
  // water column. This is what gives deep water body without painting a cyan
  // layer over a five-centimetre stream.
  const float3 clear_scatter = moppe_srgb (float3 (0.035, 0.34, 0.43));
  const float3 silt_scatter = moppe_srgb (float3 (0.28, 0.30, 0.13));
  const float3 stream_scatter = moppe_srgb (float3 (0.10, 0.22, 0.16));
  float3 scatter_tint = mix (clear_scatter, silt_scatter, turbidity);
  scatter_tint = mix (scatter_tint, stream_scatter, 0.62 * shallow_channel);
  const float3 column_light = float3 (0.44) +
                              0.72 * moppe_hemisphere_light (u.ambient.rgb, n) +
                              0.22 * u.sun_diffuse.rgb * nl * sun_visibility;
  const float3 column_radiance =
    (1.0 - fresnel) * scatter_tint * (1.0 - transmission) * column_light;
  const float3 glint_radiance =
    glint_color * u.sun_specular.rgb * spec * daylight * sun_visibility;

  // Standard alpha blending can reproduce reflection + volume + transmitted
  // bed when the source is normalized by the coverage it contributes.
  float alpha = saturate (1.0 - (1.0 - fresnel) * saturate (bed_visibility));
  float3 color = (fresnel * sky_reflection + column_radiance + glint_radiance) /
                 max (alpha, 0.02);

  color = mix (color, foam_radiance, foam);
  alpha = mix (alpha, 0.96, foam);

  // Feathered waterline: alpha fades out over the last few centimeters
  // of water column instead of ending at a discard cliff.
  if (u.params.w > 0.5)
    alpha *= smoothstep (0.0, 0.12, depth_m);
  alpha *= running_lod;

  // Identical fog curve to the terrain so shorelines match.
  const float ff = smoothstep (0.0, 0.9, in.fog);
  return moppe_temporal_output (
    float4 (mix (color, fog_c, ff), alpha * (1.0 - 0.4 * ff)), in.motion, 0.78);
}
