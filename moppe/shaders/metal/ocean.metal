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
// R32F terrain heights and the RG32F standing-water grid; x is height
// in world meters, y carries the water grid's wave amplitude factor.
static float2
ocean_grid_sample (float2 world_xz,
		   constant MoppeOceanUniforms& u,
		   texture2d<float, access::read> grid)
{
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
  const uint2 i = uint2 ((uint) gx, (uint) gz);
  const float fx = gx - (float) i.x;
  const float fz = gz - (float) i.y;
  const float2 s00 = grid.read (i).rg;
  const float2 s10 = grid.read (i + uint2 (1, 0)).rg;
  const float2 s01 = grid.read (i + uint2 (0, 1)).rg;
  const float2 s11 = grid.read (i + uint2 (1, 1)).rg;
  float2 sample = mix (mix (s00, s10, fx), mix (s01, s11, fx), fz);
  sample.x *= u.shore.z;
  return sample;
}

static float
ocean_grid_height (float2 world_xz,
		   constant MoppeOceanUniforms& u,
		   texture2d<float, access::read> grid)
{
  return ocean_grid_sample (world_xz, u, grid).x;
}

vertex OceanVaryings
ocean_vertex (uint vid [[vertex_id]],
	      const device packed_float3* verts [[buffer(MOPPE_BUF_VERTICES)]],
	      constant MoppeOceanUniforms& u [[buffer(MOPPE_BUF_FRAME)]],
	      texture2d<float, access::read> heights
		[[texture(MOPPE_TEX_HEIGHTS)]],
	      texture2d<float, access::read> water_levels
		[[texture(MOPPE_TEX_WATER_LEVELS)]])
{
  float3 p = float3 (verts[vid]);
  p += u.world_offset.xyz;
  const float time = u.params.x;

  float wave_scale = 1.0;
  if (u.params.w > 0.5) {
    const float ground = ocean_grid_height (p.xz, u, heights);
    const float2 water = ocean_grid_sample (p.xz, u, water_levels);
    p.y = water.x;
    // Waves vanish at lake and ocean shores instead of climbing dry
    // ground, and inland bodies only ripple with their per-body
    // amplitude: a tarn must not heave like the open sea.
    wave_scale = smoothstep (0.15, 6.0, p.y - ground) * water.y;
  }

  // Three overlapping swells with different directions and speeds.
  const float a1 = p.x * 0.020 + time * 1.1;
  const float a2 = p.z * 0.023 - time * 0.9;
  const float a3 = (p.x + p.z) * 0.011 + time * 0.6;

  p.y += wave_scale
    * (1.2 * sin (a1) + 1.0 * sin (a2) + 1.8 * sin (a3));

  // Gerstner-style horizontal displacement sharpens the crests.
  p.x -= wave_scale
    * (0.8 * 1.2 * cos (a1) + 0.5 * 1.8 * cos (a3));
  p.z -= wave_scale
    * (0.8 * 1.0 * cos (a2) + 0.5 * 1.8 * cos (a3));

  // Analytic surface normal from the wave derivatives.
  const float dx = wave_scale
    * (1.2 * 0.020 * cos (a1) + 1.8 * 0.011 * cos (a3));
  const float dz = wave_scale
    * (1.0 * 0.023 * cos (a2) + 1.8 * 0.011 * cos (a3));

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

// Bilinear ground height under a point, in world meters.  R32F
// must be read() at integer coords, so filter by hand.
fragment float4
ocean_fragment (OceanVaryings in [[stage_in]],
		constant MoppeOceanUniforms& u [[buffer(MOPPE_BUF_FRAME)]],
		texture2d<float, access::read> heights
		  [[texture(MOPPE_TEX_HEIGHTS)]],
		texture2d<float, access::read> water_levels
		  [[texture(MOPPE_TEX_WATER_LEVELS_FRAGMENT)]],
		depth2d<float> shadow_map [[texture(MOPPE_TEX_SHADOW)]])
{
  const float time = u.params.x;
  const float3 to_frag = in.world_pos - u.camera_pos.xyz;
  const float dist = length (to_frag);

  const float ground = ocean_grid_height (in.world_pos.xz, u, heights);
  const float surface = u.params.w > 0.5
    ? ocean_grid_height (in.world_pos.xz, u, water_levels) : u.params.y;
  const float depth_m = max (surface - ground, 0.0);
  if (u.params.w > 0.5 && depth_m <= 0.04)
    discard_fragment ();

  float3 n = normalize (in.normal);

  // Small-scale ripples sparkling on top of the big swells, faded
  // with distance so the horizon doesn't shimmer with aliasing.
  const float ripple_fade = exp (-dist * 0.002);
  n.x += ripple_fade
      * (0.10 * sin (in.world_pos.x * 0.31 + time * 2.3)
	 + 0.05 * sin (in.world_pos.z * 0.83 - time * 3.1));
  n.z += ripple_fade
      * (0.10 * sin (in.world_pos.z * 0.27 - time * 2.0)
	 + 0.05 * sin (in.world_pos.x * 0.71 + time * 2.7));
  n = normalize (n);

  const float3 v = normalize (-to_frag);
  const float3 reflection_dir = reflect (-v, n);
  const float sun_visibility = moppe_sun_visibility
    (in.world_pos, n, u.sun_dir.xyz, in.fog, u.light_matrix,
     u.shadow.x, u.shadow.y, shadow_map);

  // Schlick fresnel with a proper F0 floor.
  const float fresnel = 0.02
      + 0.98 * pow (1.0 - max (dot (n, v), 0.0), 5.0);

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
  const float golden = daylight
    * (1.0 - smoothstep (0.15, 0.65, sun.y));
  const float3 glint_color =
    mix (moppe_srgb (float3 (0.92, 0.96, 1.0)),
	 moppe_srgb (float3 (1.0, 0.67, 0.34)), golden);

  const float3 deep = moppe_srgb (float3 (0.035, 0.17, 0.28));
  const float3 shallow = moppe_srgb (float3 (0.10, 0.42, 0.55));
  float3 water = mix (deep, shallow, 0.25 + 0.5 * fresnel);
  float alpha = mix (0.78, 0.95, fresnel);

  // Shoreline: the seabed shows through glassy turquoise shallows,
  // and a band of animated foam hugs the waterline.
  if (u.shore.w > 0.5) {
    // Clarity by water column: tropical turquoise over the sand.
    const float clarity = exp (-depth_m * 0.14);
    water = mix (water, moppe_srgb (float3 (0.13, 0.52, 0.50)),
		 0.6 * clarity);
    alpha = mix (alpha, 0.35, clarity * (1.0 - 0.5 * fresnel));

    // Foam: hugs the waterline (the pow sharpens the band so the
    // wide shallow shelf doesn't stripe), pulsing gently, broken up
    // by drifting value noise (plane-wave products read as plaid
    // moire from the air).
    const float surge = 0.5 + 0.5 * sin (time * 1.3 - depth_m * 2.4);
    float foam = pow (1.0 - smoothstep (0.0, 1.2, depth_m), 1.7)
      * (0.80 + 0.20 * surge);
    // The breakup noise only runs inside the shore band; deep water
    // keeps its zero foam without paying for it.
    if (foam > 1e-3) {
      const float b1 = moppe_value_noise
	(in.world_pos.xz * 0.22 + float2 (time * 0.10, -time * 0.07));
      const float b2 = moppe_value_noise
	(in.world_pos.xz * 0.55 - float2 (time * 0.05, time * 0.09));
      foam *= 0.25 + 0.75 * smoothstep (0.25, 0.80,
					0.55 * b1 + 0.45 * b2);
    }
    foam = saturate (foam) * (0.35 + 0.65 * daylight);

    const float3 foam_albedo = moppe_srgb (float3 (0.93, 0.97, 1.0));
    const float3 foam_light = moppe_hemisphere_light (u.ambient.rgb, n)
      + u.sun_diffuse.rgb * (0.35 + 0.65 * max (dot (n, sun), 0.0))
        * sun_visibility;
    water = mix (water, foam_albedo * foam_light, foam);
    alpha = max (alpha, foam * 0.9);
  }

  // Aerial perspective: same sun-warmed haze as the terrain.
  const float3 fog_c = moppe_warmed_fog (u.fog_color.rgb,
					 to_frag / max (dist, 1e-4),
					 sun);
  const float3 sky_reflection = moppe_sky_radiance
    (u.fog_color.rgb, reflection_dir, sun);

  const float3 color = mix (water, sky_reflection, 0.55 * fresnel)
      + glint_color * spec * daylight * sun_visibility;

  // Identical fog curve to the terrain so shorelines match.
  const float ff = smoothstep (0.0, 0.9, in.fog);
  return float4 (mix (color, fog_c, ff), alpha * (1.0 - 0.4 * ff));
}
