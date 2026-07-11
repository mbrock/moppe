// Procedural sky dome -- port of shaders/sky.vert + sky.frag.
// Value-noise fBm clouds, approximate Rayleigh/Mie scattering, sun
// disk + glow, stars at night, horizon-to-fog blending, dithering.

#include "common.h"

struct SkyVaryings {
  float4 position [[position]];
  float3 dir; // object-space direction from the dome center
};

vertex SkyVaryings sky_vertex (uint vid [[vertex_id]],
                               const device packed_float3* verts
                               [[buffer (MOPPE_BUF_VERTICES)]],
                               constant MoppeSkyUniforms& u
                               [[buffer (MOPPE_BUF_FRAME)]]) {
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

  return mix (mix (mix (a, b, f.x), mix (c, d, f.x), f.y),
              mix (mix (e, f1, f.x), mix (g, h, f.x), f.y),
              f.z);
}

// Fractal Brownian Motion; the domain rotates between octaves to
// break the axial grain of lattice value noise.
static float sky_fbm (float3 p) {
  float f = 0.0;
  float amplitude = 0.5;

  for (int i = 0; i < 4; i++) {
    f += amplitude * sky_noise (p);
    p =
      2.03 * float3 (0.8 * p.x + 0.6 * p.z, p.y + 7.31, -0.6 * p.x + 0.8 * p.z);
    amplitude *= 0.5;
  }

  return f;
}

// Atmospheric scattering approximation: a three-stop Rayleigh-ish
// gradient (deep zenith, mid band, bright hazy horizon) plus a
// two-lobed Mie forward scatter around the sun.
static float3
sky_atmosphere (float3 ray_dir, float3 sun_dir, float sun_height) {
  (void)sun_height;
  const float daylight = smoothstep (-0.08, 0.18, sun_dir.y);
  const float golden = daylight * (1.0 - smoothstep (0.15, 0.65, sun_dir.y));
  float abs_zenith = abs (ray_dir.y);
  float sign_zenith = sign (ray_dir.y);
  float atmospheric_thickness = 1.0 - abs_zenith;

  const float3 day_zenith = moppe_srgb (float3 (0.06, 0.20, 0.55));
  const float3 day_mid = moppe_srgb (float3 (0.25, 0.46, 0.78));
  const float3 day_horizon = moppe_srgb (float3 (0.55, 0.68, 0.84));
  const float3 night_zenith = moppe_srgb (float3 (0.004, 0.009, 0.05));
  const float3 night_mid = moppe_srgb (float3 (0.015, 0.022, 0.06));
  const float3 night_horizon = moppe_srgb (float3 (0.035, 0.045, 0.09));

  // Below the horizon: dark grayish-blue ground reflection.
  const float3 ground_color = moppe_srgb (float3 (0.08, 0.09, 0.13));

  float3 base_color;

  if (sign_zenith >= 0.0) {
    const float3 zenith_color = mix (night_zenith, day_zenith, daylight);
    const float3 mid_color = mix (night_mid, day_mid, daylight);
    const float3 horizon_color = mix (night_horizon, day_horizon, daylight);

    // Three stops read as a real atmosphere: the mid band keeps the
    // zenith from bleeding straight into the horizon haze.
    const float t = pow (atmospheric_thickness, 0.72);
    base_color = t < 0.62 ? mix (zenith_color, mid_color, t / 0.62)
                          : mix (mid_color, horizon_color, (t - 0.62) / 0.38);

    // Warm only the low atmospheric band, leaving the rest of the
    // sky clean blue.  Forward scattering supplies the bright area
    // around the sun that ties into the directional ground haze.
    const float horizon_band = pow (atmospheric_thickness, 4.0);
    base_color = mix (base_color,
                      moppe_srgb (float3 (0.94, 0.52, 0.26)),
                      0.16 * golden * horizon_band);

    // Two Mie lobes: a broad ambient brightening plus a tighter
    // shaft that hugs the sun, both strongest low in the sky.
    const float sun_dot = max (dot (ray_dir, sun_dir), 0.0);
    const float broad = pow (sun_dot, 4.0);
    const float tight = pow (sun_dot, 32.0);
    const float3 scatter_color = mix (moppe_srgb (float3 (1.0, 0.96, 0.82)),
                                      moppe_srgb (float3 (1.0, 0.55, 0.24)),
                                      golden);
    base_color += scatter_color * daylight *
                  (broad * (0.07 + 0.12 * golden) +
                   tight * (0.10 + 0.22 * golden) * (0.4 + 0.6 * horizon_band));
  } else {
    // Lower hemisphere: darkened ground with a horizon gradient.
    base_color = ground_color * (0.2 + 0.8 * daylight);
    base_color = mix (base_color,
                      mix (night_horizon, day_horizon, daylight),
                      pow (1.0 - abs_zenith, 8.0));
  }

  return base_color;
}

// Cloud shape and structure.
static float sky_cloud_shape (float3 p, float coverage, float time) {
  float base = sky_fbm (p * 0.3);
  float detail = sky_fbm (p * 1.2 + float3 (time * 0.05, 0.0, time * 0.03));

  float cloud_coverage = 0.4 + coverage * 0.4;

  // A tighter edge gives defined puffs instead of amorphous smears.
  return smoothstep (
    cloud_coverage, cloud_coverage + 0.13, base + detail * 0.2);
}

// Cloud lighting: white sunlit faces, cool blue-gray cores, and a
// silver lining where thin edges face the sun.  (The old warm-gray
// mix read as brown smudges against the blue.)
static float3 sky_cloud_lighting (float cloud_density,
                                  float3 view_dir,
                                  float3 sun_dir,
                                  float sun_height) {
  (void)sun_height;
  const float daylight = smoothstep (-0.08, 0.18, sun_dir.y);
  const float golden = daylight * (1.0 - smoothstep (0.15, 0.65, sun_dir.y));

  // The warm shift is quadratic in `golden` so mid-afternoon clouds
  // stay white-on-blue; only a truly low sun paints them.
  const float sunset = golden * golden;
  const float3 lit = mix (moppe_srgb (float3 (1.04, 1.03, 1.00)),
                          moppe_srgb (float3 (1.05, 0.76, 0.50)),
                          sunset);
  const float3 shade = mix (moppe_srgb (float3 (0.55, 0.63, 0.76)),
                            moppe_srgb (float3 (0.48, 0.44, 0.58)),
                            sunset);

  // Dense cores shade toward cool gray-blue; wisps stay bright.
  const float core = pow (saturate (cloud_density), 0.75);
  float3 cloud_color = mix (lit, shade, 0.85 * core);

  // Silver lining: thin cloud edges glow where they face the sun.
  const float toward_sun = pow (max (dot (view_dir, sun_dir), 0.0), 16.0);
  cloud_color +=
    lit * toward_sun * (1.0 - core) * (0.5 + 0.7 * golden) * daylight;

  return mix (cloud_color * 0.12, cloud_color, daylight);
}

fragment float4 sky_fragment (SkyVaryings in [[stage_in]],
                              constant MoppeSkyUniforms& u
                              [[buffer (MOPPE_BUF_FRAME)]]) {
  const float time = u.params.x;
  const float sun_height = u.params.y;
  const float cloudiness = u.params.z;

  const float3 dir = normalize (in.dir);

  // One sun for the whole scene, straight from the game.
  const float3 sun = normalize (u.sun_dir.xyz);

  float3 sky_color = sky_atmosphere (dir, sun, sun_height);

  // High cirrus: soft wind-sheared veils far above the cumulus
  // deck, faint and always present enough to give the blue depth.
  if (dir.y > 0.08) {
    float3 ci = dir * (900.0 / dir.y);
    ci += float3 (time * 0.6, 0.0, time * 0.25);
    // Mild anisotropy hints at wind shear without reading as
    // radial motion streaks.
    float streak =
      sky_fbm (float3 (ci.x * 0.0035, ci.y * 0.008, ci.z * 0.0065));
    streak = smoothstep (0.48, 0.85, streak) * smoothstep (0.08, 0.25, dir.y) *
             (0.10 + 0.08 * cloudiness);
    const float daylight_ci = smoothstep (-0.08, 0.18, sun.y);
    const float3 ci_color = mix (moppe_srgb (float3 (0.9, 0.95, 1.05)),
                                 moppe_srgb (float3 (1.0, 0.9, 0.8)),
                                 pow (max (dot (dir, sun), 0.0), 4.0));
    sky_color = mix (sky_color, ci_color, streak * (0.25 + 0.75 * daylight_ci));
  }

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
      float3 cloud_color = sky_cloud_lighting (clouds, dir, sun, sun_height);
      sky_color = mix (sky_color, cloud_color, clouds * 0.9);
    }
  }

  // Blend the horizon into the exact fog color the terrain and
  // ocean fade to, so distant silhouettes meet the sky seamlessly.
  sky_color =
    mix (sky_color, u.fog_color.rgb, pow (1.0 - clamp (dir.y, 0.0, 1.0), 6.0));

  // Sun: a compact hot disc, a tight corona, and a faint wide bloom
  // (the old pow-12 glow washed a quarter of the sky to white).
  float sun_dot = max (0.0, dot (dir, sun));
  const float sun_disk = pow (sun_dot, 2600.0) * 1.35;
  const float corona = pow (sun_dot, 160.0) * 0.30;
  const float bloom = pow (sun_dot, 14.0) * 0.075;

  const float daylight = smoothstep (-0.08, 0.18, sun.y);
  const float golden = daylight * (1.0 - smoothstep (0.15, 0.65, sun.y));
  const float3 sun_color = mix (moppe_srgb (float3 (1.0, 0.96, 0.84)),
                                moppe_srgb (float3 (1.0, 0.58, 0.28)),
                                golden);

  // Clouds occlude the disc and corona but not the wide bloom.
  // The disc is pushed well past display white: the HDR target
  // keeps it, the bloom pass feeds on it, and the tonemapper rolls
  // it off to a hot core instead of a flat clip.
  const float occlude = 1.0 - saturate (clouds) * 0.92;
  sky_color += (sun_disk * 3.0 + corona * 1.7) * sun_color * occlude;
  sky_color += bloom * sun_color * daylight;

  // Stars at night, upper hemisphere only.
  if (daylight < 0.2 && dir.y > 0.0) {
    float star_pattern = sky_noise (dir * 100.0);
    star_pattern = pow (star_pattern, 20.0);

    // Fade stars near the horizon.
    float horizon_fade = smoothstep (0.0, 0.4, dir.y);
    float star_brightness = (1.0 - daylight) * 0.3 * horizon_fade;
    star_brightness = max (0.0, star_brightness);

    float3 star_color = mix (moppe_srgb (float3 (0.8, 0.9, 1.0)),
                             moppe_srgb (float3 (1.0, 0.9, 0.8)),
                             sky_noise (dir * 10.0));

    sky_color += star_pattern * star_brightness * star_color;
  }

  // Dither breaks up 8-bit banding in the smooth gradients.
  sky_color +=
    (fract (sin (dot (dir.xy, float2 (12.9898, 78.233))) * 43758.5453) - 0.5) /
    255.0;

  return float4 (sky_color, 1.0);
}
