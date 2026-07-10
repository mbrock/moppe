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

// Final scene grade.  The renderer intentionally preserves the game's
// gamma-space palette, so this is a compact display-referred treatment:
// a little separation, warm highlights, cool shadows, and a soft vignette.
// The HUD is drawn afterwards and stays pixel-accurate.
fragment float4
present_fragment (QuadVaryings in [[stage_in]],
		  texture2d<float> scene [[texture(MOPPE_TEX_SCENE)]])
{
  constexpr sampler smp (address::clamp_to_edge, filter::linear);
  float3 color = scene.sample (smp, in.uv).rgb;

  const float luma = dot (color, float3 (0.299, 0.587, 0.114));
  const float highlight = smoothstep (0.35, 0.85, luma);
  color *= mix (float3 (0.98, 1.00, 1.035),
		float3 (1.035, 1.00, 0.965), highlight);
  color = mix (float3 (luma), color, 1.045);
  color = (color - 0.5) * 1.045 + 0.512;

  const float2 centered = (in.uv - 0.5) * float2 (1.0, 0.72);
  const float vignette = 1.0 - 0.07
    * smoothstep (0.25, 0.72, dot (centered, centered));
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
