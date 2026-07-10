// Terrain: vertex-pulled from the height/normal textures (no vertex
// buffers), splat-textured by altitude and slope, PCF-shadowed.
// Port of shaders/test.vert + test.frag with explicit uniforms.
//
// The height texture is R32Float and is accessed via integer read().
// R32F is not linearly filterable on Apple GPUs before Apple9, so the
// subdivided near field performs its four-tap interpolation manually.

#include "common.h"

struct TerrainVaryings {
  float4 position [[position]];
  float3 world_pos;
  float3 normal;           // world space
  float height;            // altitude / height_scale, [0,1]
  float fog;               // haze factor incl. valley mist
  float4 shadow_coord;
  float2 uv;
};

static inline float
terrain_height_bilinear (float2 grid,
			 texture2d<float, access::read> heights)
{
  const uint2 limit (heights.get_width () - 1, heights.get_height () - 1);
  grid = clamp (grid, float2 (0.0), float2 (limit));
  const uint2 p00 = uint2 (floor (grid));
  const uint2 p11 = min (p00 + uint2 (1), limit);
  const float2 f = fract (grid);
  const float h0 = mix (heights.read (p00).r,
			heights.read (uint2 (p11.x, p00.y)).r, f.x);
  const float h1 = mix (heights.read (uint2 (p00.x, p11.y)).r,
			heights.read (p11).r, f.x);
  return mix (h0, h1, f.y);
}

static inline float3
terrain_read_normal (uint2 p,
		     texture2d<float, access::read> normals)
{
  const float2 nxz = normals.read (p).rg;
  const float ny = sqrt (max (1.0 - dot (nxz, nxz), 0.0));
  return float3 (nxz.x, ny, nxz.y);
}

static inline float3
terrain_normal_bilinear (float2 grid,
			 texture2d<float, access::read> normals)
{
  const uint2 limit (normals.get_width () - 1, normals.get_height () - 1);
  grid = clamp (grid, float2 (0.0), float2 (limit));
  const uint2 p00 = uint2 (floor (grid));
  const uint2 p11 = min (p00 + uint2 (1), limit);
  const float2 f = fract (grid);
  const float3 n0 = mix
    (terrain_read_normal (p00, normals),
     terrain_read_normal (uint2 (p11.x, p00.y), normals), f.x);
  const float3 n1 = mix
    (terrain_read_normal (uint2 (p00.x, p11.y), normals),
     terrain_read_normal (p11, normals), f.x);
  return mix (n0, n1, f.y);
}

// Height on the actual triangle surface produced by a coarser grid.
// The strip topology uses the bottom-left to top-right diagonal.
static inline float
terrain_height_on_lattice (float2 grid, float step,
			   texture2d<float, access::read> heights)
{
  const float2 limit (heights.get_width () - 1,
		      heights.get_height () - 1);
  const float2 cell = min (floor (grid / step) * step, limit - step);
  const float2 f = clamp ((grid - cell) / step, 0.0, 1.0);
  const uint stride = (uint) step;
  const uint2 p00 = uint2 (cell);
  const uint2 p10 = p00 + uint2 (stride, 0);
  const uint2 p01 = p00 + uint2 (0, stride);
  const uint2 p11 = p00 + uint2 (stride);
  const float h00 = heights.read (p00).r;
  const float h10 = heights.read (p10).r;
  const float h01 = heights.read (p01).r;
  const float h11 = heights.read (p11).r;
  if (f.x + f.y <= 1.0)
    return h00 + f.x * (h10 - h00) + f.y * (h01 - h00);
  return h11 + (1.0 - f.y) * (h10 - h11)
    + (1.0 - f.x) * (h01 - h11);
}

static inline float3
terrain_normal_on_lattice (float2 grid, float step,
			   texture2d<float, access::read> normals)
{
  const float2 limit (normals.get_width () - 1,
		      normals.get_height () - 1);
  const float2 cell = min (floor (grid / step) * step, limit - step);
  const float2 f = clamp ((grid - cell) / step, 0.0, 1.0);
  const uint stride = (uint) step;
  const uint2 p00 = uint2 (cell);
  const float3 n00 = terrain_read_normal (p00, normals);
  const float3 n10 = terrain_read_normal
    (p00 + uint2 (stride, 0), normals);
  const float3 n01 = terrain_read_normal
    (p00 + uint2 (0, stride), normals);
  const float3 n11 = terrain_read_normal
    (p00 + uint2 (stride), normals);
  if (f.x + f.y <= 1.0)
    return n00 + f.x * (n10 - n00) + f.y * (n01 - n00);
  return n11 + (1.0 - f.y) * (n10 - n11)
    + (1.0 - f.x) * (n01 - n11);
}

static inline float2
terrain_grid_pos (uint index, constant MoppeChunkUniforms& chunk)
{
  const uint local_x = index % chunk.verts_per_row;
  const uint local_z = index / chunk.verts_per_row;
  return float2 (chunk.origin_x, chunk.origin_z)
    + float2 (local_x, local_z) * chunk.step;
}

static inline float3
terrain_world_pos (uint index,
		   constant MoppeChunkUniforms& chunk,
		   constant MoppeTerrainUniforms& u,
		   texture2d<float, access::read> heights)
{
  const float2 grid = terrain_grid_pos (index, chunk);
  const float h = chunk.step < 1.0
    ? terrain_height_bilinear (grid, heights)
    : heights.read (uint2 (grid)).r;

  return float3 (u.params0.x * grid.x,
		 u.params0.y * h,
		 u.params0.z * grid.y) + chunk.world_offset.xyz;
}

vertex TerrainVaryings
terrain_vertex (uint index [[vertex_id]],
		constant MoppeTerrainUniforms& u [[buffer(MOPPE_BUF_FRAME)]],
		constant MoppeChunkUniforms& chunk [[buffer(MOPPE_BUF_CHUNK)]],
		texture2d<float, access::read> heights [[texture(MOPPE_TEX_HEIGHTS)]],
		texture2d<float, access::read> normals [[texture(MOPPE_TEX_NORMALS)]])
{
  const float2 grid = terrain_grid_pos (index, chunk);
  float h;
  float3 normal;
  if (chunk.step < 1.0) {
    h = terrain_height_bilinear (grid, heights);
    normal = terrain_normal_bilinear (grid, normals);
  } else {
    h = heights.read (uint2 (grid)).r;
    normal = terrain_read_normal (uint2 (grid), normals);
  }

  const float2 canonical_xz (u.params0.x * grid.x,
			     u.params0.z * grid.y);
  const float2 world_xz = canonical_xz + chunk.world_offset.xz;
  if (chunk.parent_step > chunk.step &&
      chunk.morph_end > chunk.morph_start) {
    const float dist = length (world_xz - u.camera_pos.xz);
    const float morph = smoothstep
      (chunk.morph_start, chunk.morph_end, dist);
    if (morph > 0.0) {
      const float parent_h = terrain_height_on_lattice
	(grid, chunk.parent_step, heights);
      const float3 parent_n = terrain_normal_on_lattice
	(grid, chunk.parent_step, normals);
      h = mix (h, parent_h, morph);
      normal = mix (normal, parent_n, morph);
    }
  }

  float3 world;
  if (u.params2.x > 0.5) {
    constexpr float tau = 6.28318530718;
    const float theta = tau * grid.x / (heights.get_width () - 1);
    const float phi = tau * grid.y / (heights.get_height () - 1);
    const float ct = cos (theta), st = sin (theta);
    const float cp = cos (phi), sp = sin (phi);
    const float tube_radius = u.params2.z
      + u.params0.y * h * u.params2.w;
    const float ring_radius = u.params2.y + tube_radius * cp;
    const float2 center
      (u.params0.x * (heights.get_width () - 1) * 0.5,
       u.params0.z * (heights.get_height () - 1) * 0.5);
    world = float3 (center.x + ring_radius * ct,
		    tube_radius * sp,
		    center.y + ring_radius * st);

    const float3 tangent_u (-st, 0.0, ct);
    const float3 outward (ct * cp, sp, st * cp);
    const float3 tangent_v (-ct * sp, cp, -st * sp);
    normal = normalize (normal.x * tangent_u
			+ normal.y * outward
			+ normal.z * tangent_v);
  } else {
    world = float3 (world_xz.x, u.params0.y * h, world_xz.y);
  }

  TerrainVaryings out;
  out.position = u.view_proj * float4 (world, 1.0);
  out.world_pos = world;
  out.normal = normal;
  out.height = h;

  // Distance haze plus valley mist that pools on low ground (the
  // mist term is terrain-exclusive by design).
  const float dist = length (world - u.camera_pos.xyz);
  float fog = 1.0 - exp (-pow (dist * u.fog_color.w, 1.5));
  if (u.fog_color.w > 0.0) {
    const float lowness = 1.0 - smoothstep (45.0, 170.0, world.y);
    fog += 0.3 * lowness * smoothstep (150.0, 1500.0, dist);
  }
  out.fog = saturate (fog);

  const float3 canonical_world
    (canonical_xz.x, world.y, canonical_xz.y);
  out.shadow_coord = u.light_matrix * float4 (canonical_world, 1.0);
  out.uv = (u.params2.x > 0.5 ? canonical_xz : world.xz)
    * u.params0.w;
  return out;
}

// Depth-only variant for the one-time shadow render; view_proj
// carries the light's NDC matrix.
vertex float4
terrain_shadow_vertex (uint index [[vertex_id]],
		       constant MoppeTerrainUniforms& u [[buffer(MOPPE_BUF_FRAME)]],
		       constant MoppeChunkUniforms& chunk [[buffer(MOPPE_BUF_CHUNK)]],
		       texture2d<float, access::read> heights [[texture(MOPPE_TEX_HEIGHTS)]])
{
  const float3 world = terrain_world_pos (index, chunk, u, heights);
  return u.view_proj * float4 (world, 1.0);
}

static float
terrain_shadow_factor (float4 shadow_coord, float fog, float3 n,
		       float3 l, float shadow_strength,
		       float shadow_texel,
		       depth2d<float> shadow_map)
{
  if (shadow_strength < 0.01)
    return 1.0;

  const float3 proj = shadow_coord.xyz / shadow_coord.w;
  if (proj.x < 0.0 || proj.x > 1.0 ||
      proj.y < 0.0 || proj.y > 1.0 ||
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
      shadow += 0.15 * shadow_map.sample_compare
	(shadow_smp, proj.xy + float2 (dx, dy) * shadow_texel, z);
  shadow = pow (shadow, 1.3);

  // Fade shadows out into the haze.
  const float fade = saturate (2.5 * (1.0 - fog));
  return mix (1.0, shadow, shadow_strength * fade);
}

// Sample a splat layer at two scales and crossfade by distance:
// near ground keeps fine detail, far ground switches to a coarser,
// uncorrelated repeat so the tiling never shows.
static inline float3
terrain_layer (texture2d<float> tex, sampler smp, float2 tc,
	       float far_blend)
{
  const float3 near_c = tex.sample (smp, tc).rgb;
  const float3 far_c = tex.sample (smp, tc * 0.19
				   + float2 (0.13, 0.71)).rgb;
  return mix (near_c, far_c, far_blend);
}

fragment float4
terrain_fragment (TerrainVaryings in [[stage_in]],
		  constant MoppeTerrainUniforms& u [[buffer(MOPPE_BUF_FRAME)]],
		  texture2d<float> grass [[texture(MOPPE_TEX_GRASS)]],
		  texture2d<float> dirt [[texture(MOPPE_TEX_DIRT)]],
		  texture2d<float> snow [[texture(MOPPE_TEX_SNOW)]],
		  texture2d<float> rock [[texture(MOPPE_TEX_ROCK)]],
		  depth2d<float> shadow_map [[texture(MOPPE_TEX_SHADOW)]],
		  sampler smp [[sampler(0)]])
{
  const float3 to_frag = in.world_pos - u.camera_pos.xyz;
  const float dist = length (to_frag);
  const float3 view_dir = to_frag / max (dist, 1e-4);
  const float3 l = u.sun_dir.xyz;

  const float3 fog_c = moppe_warmed_fog (u.fog_color.rgb, view_dir, l);

  // Fully fogged: skip all texture and shadow work.
  const float fog_factor = smoothstep (0.0, 0.9, in.fog);
  if (fog_factor >= 0.995)
    return float4 (fog_c, 1.0);

  const float3 n = normalize (in.normal);
  const float height = in.height;
  const float sea_level = u.params1.y;

  const float2 tc = in.uv;
  const float far_blend = smoothstep (40.0, 350.0, dist);

  // Large-scale variation shared by every layer; its luminance also
  // jitters the splat thresholds so band edges wander organically
  // instead of tracing contour lines.
  const float coarse = dot (grass.sample
			    (smp, tc * 0.083 + float2 (0.37, 0.19)).rgb,
			    float3 (0.299, 0.587, 0.114));
  const float jitter = coarse - 0.5;
  const float hj = height + 0.045 * jitter;

  // Texture bands: by altitude AND slope.  Stony cliffs break
  // through on steep faces, dry scree takes over up high, snow only
  // settles on flatter ground, sand stays off the cliffs.
  const float cliff_coef = 1.0 - smoothstep (0.60, 0.80,
					     n.y + 0.06 * jitter);
  const float scree_coef = smoothstep (0.38, 0.58, hj);
  const float snow_coef = smoothstep (0.55, 0.68, hj)
    * smoothstep (0.58, 0.78, n.y);
  const float beach_coef = (1.0 - smoothstep (sea_level + 0.004,
					      sea_level + 0.030, hj))
    * smoothstep (0.55, 0.75, n.y);

  // -- grass ------------------------------------------------------
  float3 grass_c = terrain_layer (grass, smp, tc, far_blend);

  // The source grass is an extremely saturated photographic green.
  // Pull it toward a sunlit emerald/olive palette before applying
  // large-scale variation and lighting.
  const float grass_value = dot (grass_c,
				 float3 (0.299, 0.587, 0.114));
  const float3 grass_palette = grass_value
    * moppe_srgb (float3 (0.70, 1.18, 0.50));
  grass_c = mix (grass_c, grass_palette, 0.58);
  grass_c *= 0.65 + 0.95 * coarse;

  // Altitude tint: sun-dried gold near the coast, lusher higher up.
  grass_c *= mix (float3 (1.10, 0.96, 0.78),
		  float3 (0.96, 1.00, 0.88),
		  smoothstep (0.08, 0.30, height));

  // -- scree / cliff / snow ---------------------------------------
  float3 scree_c = terrain_layer (dirt, smp, tc, far_blend);
  scree_c *= 0.7 + 0.6 * dirt.sample (smp, tc * 0.061).r;
  // Rein in the linear-space saturation of the pink gravel (ACES
  // pushes warm midtones hard).
  scree_c = mix (scree_c,
		 float3 (dot (scree_c, float3 (0.299, 0.587, 0.114))),
		 0.18);

  // The stones texture is olive; pull it toward a dry granite gray
  // and let the macro variation streak it.
  float3 cliff_c = terrain_layer (rock, smp, tc * 1.7, far_blend);
  const float cliff_value = dot (cliff_c,
				 float3 (0.299, 0.587, 0.114));
  cliff_c = mix (cliff_c,
		 cliff_value * moppe_srgb (float3 (1.01, 0.97, 0.94)),
		 0.65);
  cliff_c *= 0.75 + 0.7 * coarse;

  float3 snow_c = terrain_layer (snow, smp, tc, far_blend);
  snow_c *= 0.75 + 0.5 * snow.sample (smp, tc * 0.053
				      + float2 (0.21, 0.43)).r;

  // -- compose ----------------------------------------------------
  float3 texel = grass_c;
  texel = mix (texel, scree_c, scree_coef);
  texel = mix (texel, cliff_c, cliff_coef);
  texel = mix (texel, snow_c, snow_coef);
  // Gentler warm ratio than the display-era tint: multiplicative
  // hue shifts run stronger in linear light.
  const float3 sand_c = scree_c * float3 (1.28, 1.20, 0.98);
  texel = mix (texel, sand_c, beach_coef);

  // Per-pixel Lambert with real cast shadows.
  const float shadow = terrain_shadow_factor (in.shadow_coord, in.fog,
					      n, l, u.params1.z,
					      u.params1.w, shadow_map);
  // 0.9 is the old GL terrain material diffuse.
  const float intensity = saturate ((dot (l, n) + 0.08) / 1.08);
  float3 lit = intensity * shadow * 0.9 * u.sun_diffuse.rgb
    + moppe_hemisphere_light (u.ambient.rgb, n);

  // Snowfields sparkle in the sun (into HDR headroom, for bloom).
  const float3 h = normalize (l - view_dir);
  lit += snow_coef * shadow * pow (max (dot (n, h), 0.0), 32.0) * 0.65;

  const float3 color = texel * lit;
  return float4 (mix (color, fog_c, fog_factor), 1.0);
}
