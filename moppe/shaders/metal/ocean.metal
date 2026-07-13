// Animated translucent ocean -- port of shaders/ocean.vert +
// ocean.frag.  Three overlapping sine swells with Gerstner-style
// horizontal displacement and an analytic normal in the vertex
// stage; procedural ripples, Schlick fresnel and a Blinn sun glint
// in the fragment stage.  Lighting runs in world space (the GLSL
// eye-space detour existed only for fixed-function GL lights).

#include "common.h"

struct OceanVaryings {
  float4 position [[position]];
  float3 world_pos;
  float3 normal;
  float fog;
};

// Bilinear grid sample under a point. Float textures must be read at
// integer coordinates, so filtering is explicit. This is shared by the
// R32F terrain heights, the RG32F standing-water grid, and the RG16F
// flow grid, so all three sheets agree on addressing.
static float2 ocean_grid_sample_raw (float2 world_xz,
                                     constant MoppeOceanUniforms& u,
                                     texture2d<float, access::read> grid) {
  const float sample_edge = u.shore.w - 2.0;
  const float period = u.shore.w - 1.0;
  float gx = world_xz.x * u.shore.x;
  float gz = world_xz.y * u.shore.y;
  if (u.params.z > 0.5) {
    gx -= floor (gx / period) * period;
    gz -= floor (gz / period) * period;
  } else {
    gx = clamp (gx, 0.0, sample_edge);
    gz = clamp (gz, 0.0, sample_edge);
  }
  const uint2 i = uint2 ((uint)gx, (uint)gz);
  const float fx = gx - (float)i.x;
  const float fz = gz - (float)i.y;
  const float2 s00 = grid.read (i).rg;
  const float2 s10 = grid.read (i + uint2 (1, 0)).rg;
  const float2 s01 = grid.read (i + uint2 (0, 1)).rg;
  const float2 s11 = grid.read (i + uint2 (1, 1)).rg;
  return mix (mix (s00, s10, fx), mix (s01, s11, fx), fz);
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

// One exact lattice sample: the wet probe walks tile corners, and a
// corner read must not smear across the duplicated periodic seam.
static float2 ocean_grid_texel (int2 texel,
                                constant MoppeOceanUniforms& u,
                                texture2d<float, access::read> grid) {
  const int period = int (u.shore.w) - 1;
  if (u.params.z > 0.5)
    texel = (texel % period + period) % period;
  else
    texel = clamp (texel, 0, period);
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
    wave_scale = ocean_wave_scale (p.xz, u, heights, water_levels);
  }
  return ocean_surface_point (p, wave_scale, u);
}

// ---- lattice water tiles (mesh pipeline) ---------------------------

// A tile is 15x15 terrain cells: a 16x16 vertex lattice (256, the
// meshlet limit) and 450 triangles. The object stage walks a window of
// tiles around the camera and keeps only wet, visible ones; the mesh
// stage emits the water surface on the terrain's own sample lattice,
// so near shorelines resolve at terrain resolution instead of the
// coarse grid's.
#define WATER_TILE_CELLS 15
#define WATER_OBJECT_THREADS 64

struct WaterTilePayload {
  uint count;
  uint2 tiles[WATER_OBJECT_THREADS];
};

using WaterTileMesh = metal::mesh<OceanVaryings,
                                  void,
                                  256,
                                  WATER_TILE_CELLS * WATER_TILE_CELLS * 2,
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
      bool wet = false;
      float level = 0.0;
      const int2 origin =
        (int2 (u.tiles.xy) + int2 (tile_x, tile_z)) * WATER_TILE_CELLS;
      for (uint probe = 0; probe < 256u && !wet; ++probe) {
        const int2 texel = origin + int2 (probe % 16u, probe / 16u);
        const float water = ocean_grid_texel (texel, u, water_levels).x;
        const float ground = ocean_grid_texel (texel, u, heights).x;
        if (water > ground + 0.05 / u.shore.z) {
          wet = true;
          level = water * u.shore.z;
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
  constexpr uint side = WATER_TILE_CELLS + 1;
  if (thread_id == 0u)
    out.set_primitive_count (WATER_TILE_CELLS * WATER_TILE_CELLS * 2);

  const uint2 tile = payload.tiles[min (mesh_id, payload.count - 1u)];
  const float spacing = 1.0 / u.shore.x;
  const uint vx = thread_id % side;
  const uint vz = thread_id / side;
  const int2 texel =
    (int2 (u.tiles.xy) + int2 (tile)) * WATER_TILE_CELLS + int2 (vx, vz);
  float2 world_xz =
    ((float2 (int2 (u.tiles.xy)) + float2 (tile)) * float (WATER_TILE_CELLS) +
     float2 (vx, vz)) *
    spacing;

  // Waterline conformance: a dry lattice vertex bordering wet cells
  // slides onto the wet/dry crossing of its lattice edges (the zero of
  // the bilinear water-minus-ground, exact per edge), so the mesh edge
  // lands on the true waterline instead of ending at a discard
  // staircase.  Neighboring tiles compute the same texels, so the seam
  // stays crack-free.
  const float wet_self = ocean_grid_texel (texel, u, water_levels).x -
                         ocean_grid_texel (texel, u, heights).x;
  if (wet_self <= 0.0) {
    constexpr int2 lattice_edges[4] = {
      int2 (1, 0), int2 (-1, 0), int2 (0, 1), int2 (0, -1)
    };
    float2 shift = float2 (0.0);
    float crossings = 0.0;
    for (int k = 0; k < 4; ++k) {
      const int2 neighbor = texel + lattice_edges[k];
      const float wet_neighbor =
        ocean_grid_texel (neighbor, u, water_levels).x -
        ocean_grid_texel (neighbor, u, heights).x;
      if (wet_neighbor > 0.0) {
        const float s = wet_self / (wet_self - wet_neighbor);
        shift += float2 (lattice_edges[k]) * s;
        crossings += 1.0;
      }
    }
    if (crossings > 0.0)
      world_xz += spacing * (shift / crossings);
  }

  const float2 water = ocean_grid_sample (world_xz, u, water_levels);
  const float wave_scale =
    ocean_wave_scale (world_xz, u, heights, water_levels);
  out.set_vertex (thread_id,
                  ocean_surface_point (
                    float3 (world_xz.x, water.x, world_xz.y), wave_scale, u));

  if (vx < WATER_TILE_CELLS && vz < WATER_TILE_CELLS) {
    const uint cell = vz * WATER_TILE_CELLS + vx;
    const uint v0 = vz * side + vx;
    const uint base = 6u * cell;
    out.set_index (base + 0u, v0);
    out.set_index (base + 1u, v0 + side);
    out.set_index (base + 2u, v0 + 1u);
    out.set_index (base + 3u, v0 + 1u);
    out.set_index (base + 4u, v0 + side);
    out.set_index (base + 5u, v0 + side + 1u);
  }
}

// Bilinear ground height under a point, in world meters.  R32F
// must be read() at integer coords, so filter by hand.
fragment float4 ocean_fragment (OceanVaryings in [[stage_in]],
                                constant MoppeOceanUniforms& u
                                [[buffer (MOPPE_BUF_FRAME)]],
                                texture2d<float, access::read> heights
                                [[texture (MOPPE_TEX_HEIGHTS)]],
                                texture2d<float, access::read> water_levels
                                [[texture (MOPPE_TEX_WATER_LEVELS_FRAGMENT)]],
                                texture2d<float, access::read> water_flow
                                [[texture (MOPPE_TEX_WATER_FLOW_FRAGMENT)]],
                                depth2d<float> shadow_map
                                [[texture (MOPPE_TEX_SHADOW)]]) {
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
  const float surface = u.params.w > 0.5
                          ? ocean_grid_height (in.world_pos.xz, u, water_levels)
                          : u.params.y;
  const float still_depth = surface - ground;

  // The flow sheet: which way the water moves and how fast, painted
  // per terrain cell. Rivers carry strong arrows, lakes almost none,
  // and at a confluence the painted arrows already blend, so the same
  // shading swirls two currents into one with no seam to sew.
  float flowing = 0.0;
  float rapid = 0.0;
  float flow_detail = 0.5;
  float2 flow_grad = float2 (0.0);
  const float flow_fade = exp (-dist * 0.0012);
  if (u.current.x > 0.5 && u.shore.w > 0.5) {
    const float2 flow = ocean_grid_sample_raw (in.world_pos.xz, u, water_flow);
    const float speed = length (flow);
    flowing = smoothstep (0.25, 1.0, speed) * flow_fade;
    rapid = smoothstep (4.0, 7.5, speed);
    if (flowing > 1e-3) {
      // Flow map, two phases: a copy of the surface detail drifts
      // with the current for one cycle and hands over to a fresh copy
      // before it shears apart. Foam on real water is just as
      // short-lived, so the handover reads as nature, not a loop.
      const float cycle = 1.7;
      const float t0 = fract (time / cycle);
      const float t1 = fract (t0 + 0.5);
      const float blend = abs (2.0 * t0 - 1.0);
      const float2 base_uv = in.world_pos.xz * 0.85;
      const float2 drift = flow * (0.85 * cycle);
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
      flow_grad = mix (g0, g1, blend);
    }
  }

  // A small lap at the waterline. Reuse the water sheet's body-scale motion
  // factor so lakes and ponds remain nearly still instead of receiving the
  // ocean's shoreline motion. Running water keeps its fixed edge and churns
  // through the flow treatment below.
  const float shore_band = 1.0 - smoothstep (0.0, 1.5, still_depth);
  const float swash_phase =
    sin (time * 1.15 + 6.28318 * moppe_value_noise (in.world_pos.xz * 0.045));
  const float body_motion =
    u.params.w > 0.5
      ? saturate (ocean_grid_sample_raw (in.world_pos.xz, u, water_levels).y)
      : 1.0;
  const float swash =
    shore_band * 0.03 * body_motion * (1.0 + swash_phase) * (1.0 - flowing);
  const float depth_m = max (still_depth - swash, 0.0);
  if (u.params.w > 0.5 && still_depth <= 0.005)
    discard_fragment ();

  float3 n = normalize (in.normal);

  // Small-scale ripples sparkling on top of the big swells, faded
  // with distance so the horizon doesn't shimmer with aliasing.
  // Drifting value noise: crossed plane-wave sines interfere into a
  // checkerboard across the whole surface at grazing angles.  Where
  // the water flows, the directionless drift yields to the advected
  // detail riding the current.
  const float ripple_fade = exp (-dist * 0.002) * (1.0 - 0.75 * flowing);
  const float2 ripple1 =
    in.world_pos.xz * 0.35 + float2 (time * 0.9, -time * 0.7);
  const float2 ripple2 =
    in.world_pos.xz * 0.11 - float2 (time * 0.4, time * 0.5);
  n.x += ripple_fade * (0.16 * (moppe_value_noise (ripple1) - 0.5) +
                        0.12 * (moppe_value_noise (ripple2) - 0.5));
  n.z += ripple_fade * (0.16 * (moppe_value_noise (ripple1 + 7.3) - 0.5) +
                        0.12 * (moppe_value_noise (ripple2 + 3.1) - 0.5));
  const float flow_bump = flowing * (0.14 + 0.20 * rapid);
  n.x += flow_bump * flow_grad.x;
  n.z += flow_bump * flow_grad.y;
  n = normalize (n);

  const float3 v = normalize (-to_frag);
  const float3 reflection_dir = reflect (-v, n);
  const float sun_visibility = moppe_sun_visibility (in.world_pos,
                                                     n,
                                                     u.sun_dir.xyz,
                                                     in.fog,
                                                     u.light_matrix,
                                                     u.shadow.x,
                                                     u.shadow.y,
                                                     shadow_map);

  // Schlick fresnel with a proper F0 floor.
  const float fresnel = 0.02 + 0.98 * pow (1.0 - max (dot (n, v), 0.0), 5.0);

  // Sun glint, dimmed by haze so it doesn't ghost through the fog.
  const float3 sun = normalize (u.sun_dir.xyz);
  const float3 h = normalize (sun + v);
  const float nh = max (dot (n, h), 0.0);
  // The sharp glint deliberately exceeds display white: HDR keeps
  // it for the bloom pass and the tonemapper's shoulder.
  const float sharp_glint = pow (nh, 120.0) * 1.9;
  const float broad_glint = pow (nh, 24.0) * 0.14;
  const float spec = (sharp_glint + broad_glint) * (1.0 - in.fog);

  const float daylight = smoothstep (-0.08, 0.18, sun.y);
  const float golden = daylight * (1.0 - smoothstep (0.15, 0.65, sun.y));
  const float3 glint_color = mix (moppe_srgb (float3 (0.92, 0.96, 1.0)),
                                  moppe_srgb (float3 (1.0, 0.67, 0.34)),
                                  golden);

  const float3 deep = moppe_srgb (float3 (0.035, 0.17, 0.28));
  const float3 shallow = moppe_srgb (float3 (0.10, 0.42, 0.55));
  float3 water = mix (deep, shallow, 0.25 + 0.5 * fresnel);
  float alpha = mix (0.78, 0.95, fresnel);

  // Shoreline: the seabed shows through glassy turquoise shallows,
  // and a band of animated foam hugs the waterline.
  if (u.shore.w > 0.5) {
    // Clarity by water column: tropical turquoise over the sand.
    // Rivers run murkier than the sea — silt rides the current — so
    // flow shortens the extinction length and a metre of moving
    // water reads as a body instead of glass over gravel.
    const float clarity = exp (-depth_m * mix (0.14, 0.55, flowing));
    water = mix (water, moppe_srgb (float3 (0.13, 0.52, 0.50)), 0.6 * clarity);
    alpha = mix (alpha, 0.35, clarity * (1.0 - 0.5 * fresnel));

    // Foam: hugs the waterline (the pow sharpens the band so the
    // wide shallow shelf doesn't stripe), pulsing gently, broken up
    // by drifting value noise (plane-wave products read as plaid
    // moire from the air).
    // Foam surges with the swash: brightest as the water runs up the
    // beach, thinning as it drains back.
    const float surge = 0.5 + 0.5 * swash_phase;
    float foam = pow (1.0 - smoothstep (0.0, 1.2, depth_m), 1.7) *
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
    const float3 foam_light = moppe_hemisphere_light (u.ambient.rgb, n) +
                              u.sun_diffuse.rgb *
                                (0.35 + 0.65 * max (dot (n, sun), 0.0)) *
                                sun_visibility;
    water = mix (water, foam_albedo * foam_light, foam);
    alpha = max (alpha, foam * 0.9);
  }

  // Aerial perspective: same sun-warmed haze as the terrain.
  const float3 fog_c =
    moppe_warmed_fog (u.fog_color.rgb, to_frag / max (dist, 1e-4), sun);
  const float3 sky_reflection =
    moppe_sky_radiance (u.fog_color.rgb, reflection_dir, sun);

  const float3 color = mix (water, sky_reflection, 0.55 * fresnel) +
                       glint_color * spec * daylight * sun_visibility;

  // Feathered waterline: alpha fades out over the last few centimeters
  // of water column instead of ending at a discard cliff.
  if (u.params.w > 0.5)
    alpha *= smoothstep (0.0, 0.12, depth_m);

  // Identical fog curve to the terrain so shorelines match.
  const float ff = smoothstep (0.0, 0.9, in.fog);
  return float4 (mix (color, fog_c, ff), alpha * (1.0 - 0.4 * ff));
}
