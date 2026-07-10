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

vertex OceanVaryings
ocean_vertex (uint vid [[vertex_id]],
	      const device packed_float3* verts [[buffer(MOPPE_BUF_VERTICES)]],
	      constant MoppeOceanUniforms& u [[buffer(MOPPE_BUF_FRAME)]])
{
  float3 p = float3 (verts[vid]);
  const float time = u.params.x;

  // Three overlapping swells with different directions and speeds.
  const float a1 = p.x * 0.020 + time * 1.1;
  const float a2 = p.z * 0.023 - time * 0.9;
  const float a3 = (p.x + p.z) * 0.011 + time * 0.6;

  p.y += 1.2 * sin (a1) + 1.0 * sin (a2) + 1.8 * sin (a3);

  // Gerstner-style horizontal displacement sharpens the crests.
  p.x -= 0.8 * 1.2 * cos (a1) + 0.5 * 1.8 * cos (a3);
  p.z -= 0.8 * 1.0 * cos (a2) + 0.5 * 1.8 * cos (a3);

  // Analytic surface normal from the wave derivatives.
  const float dx = 1.2 * 0.020 * cos (a1) + 1.8 * 0.011 * cos (a3);
  const float dz = 1.0 * 0.023 * cos (a2) + 1.8 * 0.011 * cos (a3);

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
static float
ocean_ground_height (float2 world_xz,
		     constant MoppeOceanUniforms& u,
		     texture2d<float, access::read> heights)
{
  const float edge = u.shore.w - 2.0;
  const float gx = clamp (world_xz.x * u.shore.x, 0.0, edge);
  const float gz = clamp (world_xz.y * u.shore.y, 0.0, edge);
  const uint2 i = uint2 ((uint) gx, (uint) gz);
  const float fx = gx - (float) i.x;
  const float fz = gz - (float) i.y;
  const float h00 = heights.read (i).r;
  const float h10 = heights.read (i + uint2 (1, 0)).r;
  const float h01 = heights.read (i + uint2 (0, 1)).r;
  const float h11 = heights.read (i + uint2 (1, 1)).r;
  return mix (mix (h00, h10, fx), mix (h01, h11, fx), fz)
    * u.shore.z;
}

fragment float4
ocean_fragment (OceanVaryings in [[stage_in]],
		constant MoppeOceanUniforms& u [[buffer(MOPPE_BUF_FRAME)]],
		texture2d<float, access::read> heights [[texture(MOPPE_TEX_HEIGHTS)]])
{
  const float time = u.params.x;
  const float3 to_frag = in.world_pos - u.camera_pos.xyz;
  const float dist = length (to_frag);

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

  // Schlick fresnel with a proper F0 floor.
  const float fresnel = 0.02
      + 0.98 * pow (1.0 - max (dot (n, v), 0.0), 5.0);

  // Sun glint, dimmed by haze so it doesn't ghost through the fog.
  const float3 sun = normalize (u.sun_dir.xyz);
  const float3 h = normalize (sun + v);
  const float nh = max (dot (n, h), 0.0);
  const float sharp_glint = pow (nh, 120.0) * 0.85;
  const float broad_glint = pow (nh, 24.0) * 0.14;
  const float spec = (sharp_glint + broad_glint) * (1.0 - in.fog);

  const float daylight = smoothstep (-0.08, 0.18, sun.y);
  const float golden = daylight
    * (1.0 - smoothstep (0.15, 0.65, sun.y));
  const float3 glint_color = mix (float3 (0.92, 0.96, 1.0),
				  float3 (1.0, 0.67, 0.34), golden);

  const float3 deep = float3 (0.035, 0.17, 0.28);
  const float3 shallow = float3 (0.10, 0.42, 0.55);
  float3 water = mix (deep, shallow, 0.25 + 0.5 * fresnel);
  float alpha = mix (0.78, 0.95, fresnel);

  // Shoreline: the seabed shows through glassy turquoise shallows,
  // and a band of animated foam hugs the waterline.
  if (u.shore.w > 0.5) {
    const float ground = ocean_ground_height (in.world_pos.xz, u,
					      heights);
    const float depth_m = max (u.params.y - ground, 0.0);

    // Clarity by water column: tropical turquoise over the sand.
    const float clarity = exp (-depth_m * 0.14);
    water = mix (water, float3 (0.13, 0.52, 0.50), 0.6 * clarity);
    alpha = mix (alpha, 0.35, clarity * (1.0 - 0.5 * fresnel));

    // Foam: hugs the waterline (the pow sharpens the band so the
    // wide shallow shelf doesn't stripe), pulsing gently, broken up
    // by a cellular-ish product of two incommensurate waves rather
    // than one plane wave (which read as plaid moire from the air).
    const float surge = 0.5 + 0.5 * sin (time * 1.3 - depth_m * 2.4);
    float foam = pow (1.0 - smoothstep (0.0, 1.2, depth_m), 1.7)
      * (0.70 + 0.30 * surge);
    const float p1 =
      0.5 + 0.5 * sin (in.world_pos.x * 0.61
		       + in.world_pos.z * 1.13 + time * 0.8);
    const float p2 =
      0.5 + 0.5 * sin (in.world_pos.x * 1.31
		       - in.world_pos.z * 0.47 - time * 0.6);
    foam *= 0.45 + 0.55 * (p1 * p2 * 1.6);
    foam = saturate (foam) * (0.35 + 0.65 * daylight);

    water = mix (water, float3 (0.93, 0.97, 1.0), foam);
    alpha = max (alpha, foam * 0.9);
  }

  // Aerial perspective: same sun-warmed haze as the terrain.
  const float3 fog_c = moppe_warmed_fog (u.fog_color.rgb,
					 to_frag / max (dist, 1e-4),
					 sun);

  const float3 color = mix (water, fog_c, 0.55 * fresnel)
      + glint_color * spec * daylight;

  // Identical fog curve to the terrain so shorelines match.
  const float ff = smoothstep (0.0, 0.9, in.fog);
  return float4 (mix (color, fog_c, ff), alpha * (1.0 - 0.4 * ff));
}
