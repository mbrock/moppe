// Fullscreen passes: present blit, motion-blur ghost quads, and the
// underwater grade (port of shaders/underwater.frag).
//
// UV convention: v = 0 at the TOP of the texture (Metal render
// targets), the opposite of GL -- screen-space gradients from the
// GL shaders must flip.

#include "common.h"

struct QuadVaryings {
  float4 position [[position]];
  float2 uv;
};

// Fullscreen triangle; uv zoom shrinks toward the center for the
// motion-blur feedback ghosts.
vertex QuadVaryings
quad_vertex (uint vid [[vertex_id]],
	     constant MoppeQuadUniforms& q [[buffer(MOPPE_BUF_FRAME)]])
{
  const float2 pos[3] = { float2 (-1, -1), float2 (3, -1),
			  float2 (-1, 3) };
  QuadVaryings out;
  out.position = float4 (pos[vid], 0.5, 1.0);
  float2 uv = float2 ((pos[vid].x + 1) * 0.5,
		      1.0 - (pos[vid].y + 1) * 0.5);
  const float zoom = q.params.x;
  out.uv = (uv - 0.5) / zoom + 0.5;
  return out;
}

fragment float4
quad_fragment (QuadVaryings in [[stage_in]],
	       constant MoppeQuadUniforms& q [[buffer(MOPPE_BUF_FRAME)]],
	       texture2d<float> scene [[texture(MOPPE_TEX_SCENE)]])
{
  constexpr sampler smp (address::clamp_to_edge, filter::linear);
  return float4 (scene.sample (smp, in.uv).rgb, 1.0) * q.tint;
}

// Bloom bright pass: soft-knee threshold into the quarter-res
// chain.  Only genuinely hot pixels pass -- the sun, the boost
// plume, glints, snowfields in direct light.
fragment float4
bloom_bright_fragment (QuadVaryings in [[stage_in]],
		       texture2d<float> scene [[texture(MOPPE_TEX_SCENE)]])
{
  constexpr sampler smp (address::clamp_to_edge, filter::linear);
  const float3 c = scene.sample (smp, in.uv).rgb;
  const float luma = dot (c, float3 (0.299, 0.587, 0.114));
  // The scene is HDR now: only genuinely hot pixels (above display
  // white) feed the glow.
  const float k = smoothstep (0.85, 1.35, luma);
  return float4 (c * k, 1.0);
}

// Separable 9-tap gaussian; params.zw carries the texel step for
// this direction.
fragment float4
bloom_blur_fragment (QuadVaryings in [[stage_in]],
		     constant MoppeQuadUniforms& q [[buffer(MOPPE_BUF_FRAME)]],
		     texture2d<float> src [[texture(MOPPE_TEX_SCENE)]])
{
  constexpr sampler smp (address::clamp_to_edge, filter::linear);
  const float2 step = q.params.zw;
  const float w[5] = { 0.227027, 0.1945946, 0.1216216,
		       0.054054, 0.016216 };
  float3 c = src.sample (smp, in.uv).rgb * w[0];
  for (int i = 1; i < 5; ++i) {
    c += src.sample (smp, in.uv + step * (float) i).rgb * w[i];
    c += src.sample (smp, in.uv - step * (float) i).rgb * w[i];
  }
  return float4 (c, 1.0);
}

// Shoulder-only filmic tonemap for the HDR scene: exact identity
// below `start` (the game's palette is tuned in display space and
// must survive untouched), then an exponential rolloff that
// asymptotes at start + scale = 1.0.  Per-channel, so overdriven
// colors desaturate toward white the way film does.
static inline float3 moppe_tonemap (float3 c) {
  const float start = 0.75;
  const float scale = 0.25;
  const float3 over = max (c - start, 0.0);
  return min (c, start) + scale * (1.0 - exp (-over / scale));
}

// Final scene grade.  The scene arrives HDR (half-float); after the
// lens stack (fringe, bloom, flare) the tonemapper brings it to
// display range, then a compact display-referred treatment: a little
// separation, warm highlights, cool shadows, animated grain, and a
// soft vignette.  The HUD is drawn afterwards and stays
// pixel-accurate.
fragment float4
present_fragment (QuadVaryings in [[stage_in]],
		  constant MoppeQuadUniforms& q [[buffer(MOPPE_BUF_FRAME)]],
		  texture2d<float> scene [[texture(MOPPE_TEX_SCENE)]],
		  texture2d<float> bloom [[texture(MOPPE_TEX_BLOOM)]])
{
  constexpr sampler smp (address::clamp_to_edge, filter::linear);
  const float time = q.params.y;

  // Chromatic fringe, quadratic toward the corners so the center
  // stays razor sharp.
  const float2 centered_uv = in.uv - 0.5;
  const float rr = dot (centered_uv, centered_uv);
  const float2 fringe = centered_uv * rr * 0.010;
  float3 color = float3 (scene.sample (smp, in.uv + fringe).r,
			 scene.sample (smp, in.uv).g,
			 scene.sample (smp, in.uv - fringe).b);

  // Bloom, prefiltered and blurred at quarter resolution.
  color += bloom.sample (smp, in.uv).rgb * 0.45;

  // Lens flare: warm veil + anamorphic streak on the sun, plus a
  // few tinted ghosts mirrored through the screen center.  sun.z
  // already folds in terrain occlusion, clouds, and edge fade.
  if (q.sun.z > 0.001) {
    float2 d = in.uv - q.sun.xy;
    d.x *= q.sun.w;
    const float dist = length (d);

    const float veil = exp (-dist * 2.6) * 0.15;
    const float streak = exp (-abs (d.y) * 46.0)
      * exp (-abs (d.x) * 3.2) * 0.42;
    color += float3 (1.0, 0.86, 0.62) * (veil + streak) * q.sun.z;

    const float2 sun_c = q.sun.xy - 0.5;
    const float ghost_k[3] = { 0.45, 0.9, 1.5 };
    const float ghost_r[3] = { 0.05, 0.085, 0.13 };
    const float3 ghost_tint[3] = {
      float3 (0.45, 0.75, 1.0),
      float3 (1.0, 0.62, 0.38),
      float3 (0.55, 1.0, 0.65),
    };
    for (int g = 0; g < 3; ++g) {
      float2 gd = in.uv - (0.5 - sun_c * ghost_k[g]);
      gd.x *= q.sun.w;
      const float falloff =
	exp (-dot (gd, gd) / (ghost_r[g] * ghost_r[g]));
      color += ghost_tint[g] * falloff * 0.05 * q.sun.z;
    }
  }

  // HDR -> display: filmic shoulder (identity below 0.75, so the
  // tuned palette holds; hot plume cores and sun glints roll off
  // to white instead of clipping).
  color = moppe_tonemap (color);

  // Gentle filmic S-curve (gamma-space): richer shadows and a
  // rounder highlight shoulder without crushing the 8-bit palette.
  color = mix (color, color * color * (3.0 - 2.0 * color), 0.22);

  const float luma = dot (color, float3 (0.299, 0.587, 0.114));

  // Split tone: cool teal shadows against warm amber highlights.
  const float highlight = smoothstep (0.30, 0.85, luma);
  color *= mix (float3 (0.960, 1.005, 1.060),
		float3 (1.060, 1.005, 0.940), highlight);

  // Vibrance: muted pixels gain saturation faster than rich ones,
  // so grass and sky deepen while skin-tone-ish dust stays natural.
  const float maxc = max (color.r, max (color.g, color.b));
  const float minc = min (color.r, min (color.g, color.b));
  color = mix (float3 (luma), color,
	       1.0 + 0.14 * (1.0 - (maxc - minc)));

  // A touch of global contrast around mid-gray.
  color = (color - 0.5) * 1.03 + 0.508;

  // Animated film grain, stronger in the shadows.  Hashed from the
  // pixel coordinate (bounded first: sin() loses precision on big
  // arguments) so it's per-pixel, not a smooth gradient.
  const float2 gp = fmod (in.position.xy
			  + float2 (time * 173.0, time * 251.0),
			  1024.0);
  const float grain = fract (sin (dot (gp, float2 (12.9898, 78.233)))
			     * 43758.5453);
  const float gl = dot (color, float3 (0.299, 0.587, 0.114));
  color += (grain - 0.5) * 0.020 * (1.0 - 0.65 * gl);

  const float2 centered = centered_uv * float2 (1.0, 0.72);
  const float vignette = 1.0 - 0.09
    * smoothstep (0.22, 0.72, dot (centered, centered));
  return float4 (saturate (color * vignette), 1.0);
}

// Underwater grade: sinusoidal wobble, blue-green mix, vertical
// murk (darker toward the bottom -- GL's uv.y gradient, flipped),
// drifting shimmer.
fragment float4
underwater_fragment (QuadVaryings in [[stage_in]],
		     constant MoppeQuadUniforms& q [[buffer(MOPPE_BUF_FRAME)]],
		     texture2d<float> scene [[texture(MOPPE_TEX_SCENE)]])
{
  constexpr sampler smp (address::clamp_to_edge, filter::linear);
  const float time = q.params.y;

  float2 uv = in.uv;
  uv.x += 0.007 * sin (uv.y * 32.0 + time * 2.3);
  uv.y += 0.007 * sin (uv.x * 27.0 - time * 1.9);
  uv = clamp (uv, 0.0, 1.0);

  float3 c = scene.sample (smp, uv).rgb;
  c = mix (c, c * float3 (0.30, 0.62, 0.85), 0.5);

  // GL murk was 0.75 + 0.25 * uv.y with v=0 at the bottom.
  c *= 0.75 + 0.25 * (1.0 - in.uv.y);

  // Drifting shimmer (multiplicative, as in the GL build).
  c *= 0.95 + 0.05 * sin (time * 1.7 + in.uv.x * 9.0
			  + in.uv.y * 4.0);

  return float4 (c, 1.0);
}
