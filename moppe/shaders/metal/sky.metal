// Procedural sky dome -- port of shaders/sky.vert + sky.frag.
// Value-noise fBm clouds, approximate Rayleigh/Mie scattering, sun
// disk + glow, stars at night, horizon-to-fog blending, dithering.

#include "common.h"

struct SkyVaryings {
  float4 position [[position]];
  float3 dir;      // object-space direction from the dome center
};

vertex SkyVaryings
sky_vertex (uint vid [[vertex_id]],
	    const device packed_float3* verts [[buffer(MOPPE_BUF_VERTICES)]],
	    constant MoppeSkyUniforms& u [[buffer(MOPPE_BUF_FRAME)]])
{
  const float3 p = float3 (verts[vid]);
  SkyVaryings out;
  float4 clip = u.view_proj * float4 (p, 1.0);
  // Pin depth to the far plane (reversed-Z: z = 0) so terrain depth
  // culls hidden sky; must come from the vertex stage to keep
  // early-z on TBDR.
  clip.z = 0.0;
  out.position = clip;
  out.dir = p;
  return out;
}

static float sky_hash (float n) {
  return fract (sin (n) * 43758.5453);
}

// Lattice value noise with cubic interpolation.
static float sky_noise (float3 p) {
  float3 i = floor (p);
  float3 f = fract (p);

  f = f * f * (3.0 - 2.0 * f);

  float n = i.x + i.y * 57.0 + i.z * 113.0;

  float a = sky_hash (n);
  float b = sky_hash (n + 1.0);
  float c = sky_hash (n + 57.0);
  float d = sky_hash (n + 58.0);
  float e = sky_hash (n + 113.0);
  float f1 = sky_hash (n + 114.0);
  float g = sky_hash (n + 170.0);
  float h = sky_hash (n + 171.0);

  return mix (mix (mix (a, b, f.x),
		   mix (c, d, f.x), f.y),
	      mix (mix (e, f1, f.x),
		   mix (g, h, f.x), f.y), f.z);
}

// Fractal Brownian Motion; the domain rotates between octaves to
// break the axial grain of lattice value noise.
static float sky_fbm (float3 p) {
  float f = 0.0;
  float amplitude = 0.5;

  for (int i = 0; i < 4; i++) {
    f += amplitude * sky_noise (p);
    p = 2.03 * float3 (0.8 * p.x + 0.6 * p.z, p.y + 7.31,
		       -0.6 * p.x + 0.8 * p.z);
    amplitude *= 0.5;
  }

  return f;
}

// Atmospheric scattering approximation.
static float3 sky_atmosphere (float3 ray_dir, float3 sun_dir,
			      float sun_height) {
  // Rayleigh scattering coefficients.
  const float3 rayleigh_coeff = float3 (5.8e-6, 13.5e-6, 33.1e-6);

  float rayleigh_factor =
    1.0 + pow (1.0 - max (0.0, dot (ray_dir, sun_dir)), 5.0) * 2.0;
  float3 rayleigh = rayleigh_coeff * rayleigh_factor;

  // Mie scattering approximation.
  float mie_factor = pow (max (0.0, dot (ray_dir, sun_dir)), 8.0) * 0.5;
  float3 mie = float3 (mie_factor);

  float abs_zenith = abs (ray_dir.y);
  float sign_zenith = sign (ray_dir.y);
  float atmospheric_thickness = 1.0 - abs_zenith;

  const float3 day_zenith = float3 (0.0, 0.1, 0.6);
  const float3 day_horizon = float3 (0.3, 0.5, 0.9);
  const float3 night_zenith = float3 (0.0, 0.0, 0.1);
  const float3 night_horizon = float3 (0.05, 0.05, 0.1);

  // Below the horizon: dark grayish-blue ground reflection.
  const float3 ground_color = float3 (0.1, 0.1, 0.15);

  float3 base_color;

  if (sign_zenith >= 0.0) {
    // Upper hemisphere: day-night interpolated sky gradient.
    float3 zenith_color = mix (night_zenith, day_zenith, sun_height);
    float3 horizon_color = mix (night_horizon, day_horizon, sun_height);

    base_color = mix (zenith_color, horizon_color,
		      atmospheric_thickness);
    base_color += rayleigh * sun_height;
    base_color += mie * sun_height;
  } else {
    // Lower hemisphere: darkened ground with a horizon gradient.
    base_color = ground_color * max (0.2, sun_height);
    base_color = mix (base_color,
		      mix (night_horizon, day_horizon, sun_height),
		      pow (1.0 - abs_zenith, 8.0));
  }

  return base_color;
}

// Cloud shape and structure.
static float sky_cloud_shape (float3 p, float coverage, float time) {
  float base = sky_fbm (p * 0.3);
  float detail = sky_fbm (p * 1.2
			  + float3 (time * 0.05, 0.0, time * 0.03));

  float cloud_coverage = 0.4 + coverage * 0.4;

  return smoothstep (cloud_coverage, cloud_coverage + 0.2,
		     base + detail * 0.2);
}

// Cloud lighting: directional light, self-shadowing, sunset tint.
static float3 sky_cloud_lighting (float cloud_density, float3 sun_dir,
				  float sun_height) {
  float3 cloud_color = float3 (0.9, 0.9, 0.95);

  float sun_light =
    max (0.0, dot (normalize (float3 (0.0, 1.0, 0.0)), sun_dir));
  float direct_light = pow (sun_light, 4.0) * 0.5 + 0.5;

  float self_shadow = 1.0 - cloud_density * 0.5;

  float sunset =
    pow (max (0.0, dot (normalize (float3 (0.0, 0.3, 1.0)), sun_dir)),
	 2.0);
  float3 sunset_color =
    mix (float3 (1.0, 0.8, 0.5), float3 (1.0, 0.4, 0.2), sunset);

  cloud_color = cloud_color
    * mix (float3 (0.5, 0.5, 0.65), sunset_color, direct_light)
    * self_shadow;

  // Darken clouds at night.
  cloud_color = mix (cloud_color * 0.2, cloud_color, sun_height);

  return cloud_color;
}

fragment float4
sky_fragment (SkyVaryings in [[stage_in]],
	      constant MoppeSkyUniforms& u [[buffer(MOPPE_BUF_FRAME)]])
{
  const float time = u.params.x;
  const float sun_height = u.params.y;
  const float cloudiness = u.params.z;

  const float3 dir = normalize (in.dir);

  // One sun for the whole scene, straight from the game.
  const float3 sun = normalize (u.sun_dir.xyz);

  float3 sky_color = sky_atmosphere (dir, sun, sun_height);

  // Clouds, upper hemisphere only, projected onto a dome.
  float clouds = 0.0;
  if (dir.y > 0.05) {
    const float cloud_height = 200.0;
    float3 cloud_pos = dir * (cloud_height / dir.y);

    // Time-based drift.
    cloud_pos += float3 (time * 2.0, 0.0, time * 1.0);

    clouds = sky_cloud_shape (cloud_pos * 0.01, cloudiness, time);

    // Fade clouds at the horizon.
    clouds *= smoothstep (0.05, 0.1, dir.y);

    if (clouds > 0.01) {
      float3 cloud_color = sky_cloud_lighting (clouds, sun, sun_height);
      sky_color = mix (sky_color, cloud_color, clouds * 0.9);
    }
  }

  // Blend the horizon into the exact fog color the terrain and
  // ocean fade to, so distant silhouettes meet the sky seamlessly.
  sky_color = mix (sky_color, u.fog_color.rgb,
		   pow (1.0 - clamp (dir.y, 0.0, 1.0), 6.0));

  // Sun disk with realistic shape (drawn over the haze).
  float sun_dot = max (0.0, dot (dir, sun));
  float sun_disk = pow (sun_dot, 256.0);
  float sun_glow = pow (sun_dot, 8.0) * 0.3;

  // Sun color varies with height (redder at horizon).
  float3 sun_color =
    mix (float3 (1.0, 0.6, 0.3), float3 (1.0, 0.9, 0.7), sun_height);

  // Add sun to sky; the corona warms only when the sun is low.
  sky_color += sun_disk * sun_color;
  sky_color += sun_glow
    * mix (float3 (1.0, 0.4, 0.2), float3 (1.0, 0.85, 0.6), sun_height)
    * sun_height;

  // Stars at night, upper hemisphere only.
  if (sun_height < 0.2 && dir.y > 0.0) {
    float star_pattern = sky_noise (dir * 100.0);
    star_pattern = pow (star_pattern, 20.0);

    // Fade stars near the horizon.
    float horizon_fade = smoothstep (0.0, 0.4, dir.y);
    float star_brightness =
      (1.0 - sun_height * 5.0) * 0.3 * horizon_fade;
    star_brightness = max (0.0, star_brightness);

    float3 star_color = mix (float3 (0.8, 0.9, 1.0),
			     float3 (1.0, 0.9, 0.8),
			     sky_noise (dir * 10.0));

    sky_color += star_pattern * star_brightness * star_color;
  }

  // Dither breaks up 8-bit banding in the smooth gradients.
  sky_color += (fract (sin (dot (dir.xy, float2 (12.9898, 78.233)))
		       * 43758.5453) - 0.5) / 255.0;

  return float4 (sky_color, 1.0);
}
