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
vertex QuadVaryings quad_vertex (uint vid [[vertex_id]],
                                 constant MoppeQuadUniforms& q
                                 [[buffer (MOPPE_BUF_FRAME)]]) {
  const float2 pos[3] = { float2 (-1, -1), float2 (3, -1), float2 (-1, 3) };
  QuadVaryings out;
  out.position = float4 (pos[vid], 0.5, 1.0);
  float2 uv = float2 ((pos[vid].x + 1) * 0.5, 1.0 - (pos[vid].y + 1) * 0.5);
  const float zoom = q.params.x;
  out.uv = (uv - 0.5) / zoom + 0.5;
  return out;
}

fragment float4 quad_fragment (QuadVaryings in [[stage_in]],
                               constant MoppeQuadUniforms& q
                               [[buffer (MOPPE_BUF_FRAME)]],
                               texture2d<float> scene
                               [[texture (MOPPE_TEX_SCENE)]]) {
  constexpr sampler smp (address::clamp_to_edge, filter::linear);
  return float4 (scene.sample (smp, in.uv).rgb, 1.0) * q.tint;
}

// Bloom bright pass: soft-knee threshold into the quarter-res
// chain.  Only genuinely hot pixels pass -- the sun, the boost
// plume, glints, snowfields in direct light.  tint.w carries the
// auto-exposure so the glow tracks adaptation.
fragment float4 bloom_bright_fragment (QuadVaryings in [[stage_in]],
                                       constant MoppeQuadUniforms& q
                                       [[buffer (MOPPE_BUF_FRAME)]],
                                       texture2d<float> scene
                                       [[texture (MOPPE_TEX_SCENE)]]) {
  constexpr sampler smp (address::clamp_to_edge, filter::linear);
  const float3 c = scene.sample (smp, in.uv).rgb * q.tint.w;
  const float luma = dot (c, float3 (0.2126, 0.7152, 0.0722));
  const float k = smoothstep (0.85, 1.35, luma);
  return float4 (c * k, 1.0);
}

// Auto-exposure probe: a handful of wide taps averaged into a tiny
// target the CPU reads back for the adaptation loop.
fragment float4 probe_fragment (QuadVaryings in [[stage_in]],
                                constant MoppeQuadUniforms& q
                                [[buffer (MOPPE_BUF_FRAME)]],
                                texture2d<float> scene
                                [[texture (MOPPE_TEX_SCENE)]]) {
  constexpr sampler smp (address::clamp_to_edge, filter::linear);
  const float2 o = float2 (0.008, 0.014);
  float3 c = scene.sample (smp, in.uv).rgb * 0.2;
  c += scene.sample (smp, in.uv + o).rgb * 0.2;
  c += scene.sample (smp, in.uv - o).rgb * 0.2;
  c += scene.sample (smp, in.uv + float2 (o.x, -o.y)).rgb * 0.2;
  c += scene.sample (smp, in.uv + float2 (-o.x, o.y)).rgb * 0.2;
  c *= q.tint.w;
  return float4 (c, 1.0);
}

fragment float exposure_fragment (QuadVaryings in [[stage_in]],
                                  constant MoppeQuadUniforms& q
                                  [[buffer (MOPPE_BUF_FRAME)]]) {
  (void)in;
  return q.tint.x;
}

// Separable 9-tap gaussian represented by five linear samples. Each offset
// lands between the two source texels whose weights it combines, so hardware
// interpolation evaluates the same kernel with four fewer texture fetches.
// params.zw carries the texel step for this direction.
fragment float4 bloom_blur_fragment (QuadVaryings in [[stage_in]],
                                     constant MoppeQuadUniforms& q
                                     [[buffer (MOPPE_BUF_FRAME)]],
                                     texture2d<float> src
                                     [[texture (MOPPE_TEX_SCENE)]]) {
  constexpr sampler smp (address::clamp_to_edge, filter::linear);
  const float2 step = q.params.zw;
  constexpr float center_weight = 0.227027;
  constexpr float2 pair_weights (0.3162162, 0.0702700);
  constexpr float2 pair_offsets (1.3846154, 3.2307692);
  float3 c = src.sample (smp, in.uv).rgb * center_weight;
  for (int pair = 0; pair < 2; ++pair) {
    const float2 offset = step * pair_offsets[pair];
    c += src.sample (smp, in.uv + offset).rgb * pair_weights[pair];
    c += src.sample (smp, in.uv - offset).rgb * pair_weights[pair];
  }
  return float4 (c, 1.0);
}

// ACES filmic fit (Narkowicz): linear scene radiance in, display-
// linear [0,1] out, with the classic toe, shoulder, and highlight
// desaturation.
static inline float3 moppe_aces (float3 x) {
  const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
  return saturate ((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Final scene treatment.  The scene arrives as linear HDR
// radiance; the lens stack (fringe, bloom, flare) composes in
// linear, auto-exposure scales it, ACES tonemaps, sRGB encodes,
// and then a colorist-style CDL grade, vibrance, grain, and
// vignette finish the frame.  The HUD is drawn afterwards and
// stays pixel-accurate.
fragment float4 present_fragment (QuadVaryings in [[stage_in]],
                                  constant MoppeQuadUniforms& q
                                  [[buffer (MOPPE_BUF_FRAME)]],
                                  texture2d<float> scene
                                  [[texture (MOPPE_TEX_SCENE)]],
                                  texture2d<float> bloom
                                  [[texture (MOPPE_TEX_BLOOM)]]) {
  constexpr sampler smp (address::clamp_to_edge, filter::linear);
  const float time = q.params.y;

  // Real lateral chromatic aberration belongs to the outer lens. Keep the
  // central image on one sample, then split red and blue only beyond the
  // image circle's outer third. Besides removing false color from the
  // subject and HUD-adjacent scenery, most pixels avoid two scene fetches.
  const float2 centered_uv = in.uv - 0.5;
  const float rr = dot (centered_uv, centered_uv);
  float3 color = scene.sample (smp, in.uv).rgb;
  if (rr > 0.18) {
    const float fringe_strength = smoothstep (0.18, 0.50, rr);
    const float2 fringe = centered_uv * rr * (0.006 * fringe_strength);
    color.r = scene.sample (smp, in.uv + fringe).r;
    color.b = scene.sample (smp, in.uv - fringe).b;
  }

  // Eye adaptation, then bloom (the bright pass already saw the
  // exposed scene, so it adds in matching units).
  color *= q.tint.w;
  color += bloom.sample (smp, in.uv).rgb * 0.45;

  // Lens flare: warm veil + anamorphic streak on the sun, plus a
  // few tinted ghosts mirrored through the screen center.  sun.z
  // already folds in terrain occlusion, clouds, and edge fade.
  if (q.sun.z > 0.001) {
    float2 d = in.uv - q.sun.xy;
    d.x *= q.sun.w;
    const float dist = length (d);

    const float veil = exp (-dist * 2.6) * 0.15;
    const float streak =
      exp (-abs (d.y) * 46.0) * exp (-abs (d.x) * 3.2) * 0.42;
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
      const float falloff = exp (-dot (gd, gd) / (ghost_r[g] * ghost_r[g]));
      color += ghost_tint[g] * falloff * 0.05 * q.sun.z;
    }
  }

  const float3 hdr_color = color;

  // Linear HDR -> filmic display, then encode for the SDR grade.
  color = moppe_aces (color);
  color = pow (color, 1.0 / 2.2);

  // Warm print-film base with cyan-blue shadow density. Keep the treatment
  // restrained: mustard grass, coral earth, turquoise water, and creamy
  // highlights should feel designed without flattening the time of day.
  color = color * float3 (1.045, 1.005, 0.955) + float3 (-0.004, 0.002, 0.012);
  color = pow (max (color, float3 (0.0)), float3 (1.05, 1.02, 1.00));

  const float grade_luma = dot (color, float3 (0.299, 0.587, 0.114));
  const float shadow_tone = 1.0 - smoothstep (0.10, 0.48, grade_luma);
  const float highlight_tone = smoothstep (0.58, 0.96, grade_luma);
  color += shadow_tone * float3 (-0.004, 0.004, 0.010);
  color += highlight_tone * float3 (0.010, 0.006, -0.004);

  // Vibrance: muted pixels gain saturation faster than rich ones,
  // so grass and sky deepen while skin-tone-ish dust stays natural.
  const float luma = dot (color, float3 (0.299, 0.587, 0.114));
  const float maxc = max (color.r, max (color.g, color.b));
  const float minc = min (color.r, min (color.g, color.b));
  color = mix (float3 (luma), color, 1.0 + 0.10 * (1.0 - (maxc - minc)));

  // Animated film grain, stronger in the shadows.  Hashed from the
  // pixel coordinate (bounded first: sin() loses precision on big
  // arguments) so it's per-pixel, not a smooth gradient.
  const float2 gp =
    fmod (in.position.xy + float2 (time * 173.0, time * 251.0), 1024.0);
  const float grain =
    fract (sin (dot (gp, float2 (12.9898, 78.233))) * 43758.5453);
  const float gl = dot (color, float3 (0.299, 0.587, 0.114));
  color += (grain - 0.5) * 0.020 * (1.0 - 0.65 * gl);

  const float2 centered = centered_uv * float2 (1.0, 0.72);
  const float vignette =
    1.0 - 0.09 * smoothstep (0.22, 0.72, dot (centered, centered));
  color *= vignette;

  if (q.params.w > 0.5) {
    // macOS EDR uses extended-linear sRGB: preserve the familiar SDR
    // grade below 1.0, then let only true scene highlights rise above
    // diffuse white. The live headroom tracks the current display mode.
    float3 linear = pow (saturate (color), 2.2);
    const float peak = max (hdr_color.r, max (hdr_color.g, hdr_color.b));
    const float extra = min (max (log2 (max (peak, 1.0)), 0.0) * 0.42,
                             max (q.params.z - 1.0, 0.0));
    linear += hdr_color / max (peak, 0.0001) * extra * vignette;
    return float4 (min (linear, float3 (q.params.z)), 1.0);
  }

  return float4 (saturate (color), 1.0);
}

// Underwater grade: sinusoidal wobble, blue-green mix, vertical
// murk (darker toward the bottom -- GL's uv.y gradient, flipped),
// drifting shimmer.
fragment float4 underwater_fragment (QuadVaryings in [[stage_in]],
                                     constant MoppeQuadUniforms& q
                                     [[buffer (MOPPE_BUF_FRAME)]],
                                     texture2d<float> scene
                                     [[texture (MOPPE_TEX_SCENE)]]) {
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
  c *= 0.95 + 0.05 * sin (time * 1.7 + in.uv.x * 9.0 + in.uv.y * 4.0);

  return float4 (c, 1.0);
}

// Sun shafts: march each view ray through the camera-local shadow map and
// gather forward-scattered sunlight over the lit spans. The scene depth
// stops the march at the first surface; the shadow map supplies the tree-
// and terrain-shaped occlusion that turns open sky into visible beams. An
// interleaved-gradient jitter per pixel replaces banding with fine noise
// that stays deterministic for captures. The march is the whole cost of the
// effect and the beams are low-frequency light, so it runs at half
// resolution and a separate pass adds the filtered result to the scene.
fragment float4 shafts_gather_fragment (QuadVaryings in [[stage_in]],
                                        constant MoppeShaftUniforms& u
                                        [[buffer (MOPPE_BUF_FRAME)]],
                                        depth2d<float> scene_depth
                                        [[texture (MOPPE_TEX_POST_DEPTH)]],
                                        depth2d<float> shadow_map
                                        [[texture (MOPPE_TEX_SHADOW)]]) {
  constexpr sampler depth_smp (
    coord::normalized, address::clamp_to_edge, filter::nearest);
  constexpr sampler shadow_smp (coord::normalized,
                                address::clamp_to_edge,
                                filter::linear,
                                compare_func::less_equal);
  const float3 dir =
    normalize (u.ray_forward.xyz + u.ray_right.xyz * (2.0 * in.uv.x - 1.0) +
               u.ray_up.xyz * (1.0 - 2.0 * in.uv.y));
  const float surface_z = scene_depth.sample (depth_smp, in.uv);
  const float2 px = in.position.xy;
  const float jitter =
    fract (52.9829189 * fract (0.06711056 * px.x + 0.00583715 * px.y));

  const uint steps = uint (u.params.z);
  const float dt = u.params.x / float (steps);
  const float sigma = u.params.y;
  float scatter = 0.0;
  for (uint i = 0u; i < steps; ++i) {
    const float t = (float (i) + jitter) * dt;
    const float3 world = u.camera_pos.xyz + dir * t;
    const float4 clip = u.view_proj * float4 (world, 1.0);
    if (clip.w <= 0.0)
      break;
    // Reversed-Z: a sample farther than the first surface reads smaller.
    if (clip.z / clip.w < surface_z)
      break;
    const float4 sc = u.light_matrix * float4 (world, 1.0);
    const float3 proj = sc.xyz / sc.w;
    float lit = 1.0;
    if (all (proj >= 0.0) && all (proj <= 1.0))
      lit = shadow_map.sample_compare (shadow_smp, proj.xy, proj.z - 0.0015);
    scatter += lit * exp (-sigma * t);
  }
  scatter *= sigma * dt;

  // Henyey-Greenstein forward lobe: beams live around the solar direction.
  const float mu = dot (dir, normalize (u.sun_dir.xyz));
  const float g = 0.60;
  const float phase =
    (1.0 - g * g) / (4.0 * 3.14159265 * pow (1.0 + g * g - 2.0 * g * mu, 1.5));
  return float4 (u.sun_color.rgb * u.sun_color.w * phase * scatter, 1.0);
}

fragment float4 shafts_add_fragment (QuadVaryings in [[stage_in]],
                                     texture2d<float> scene
                                     [[texture (MOPPE_TEX_SCENE)]],
                                     texture2d<float> shafts
                                     [[texture (MOPPE_TEX_BLOOM)]]) {
  constexpr sampler smp (
    coord::normalized, address::clamp_to_edge, filter::linear);
  return float4 (scene.sample (smp, in.uv).rgb + shafts.sample (smp, in.uv).rgb,
                 1.0);
}

// ---- ambient occlusion ---------------------------------------------

static inline float moppe_gtao_view_depth (float device_z, float n, float f) {
  // Reversed-Z perspective: near maps to 1, far to 0.
  return n * f / (device_z * (f - n) + n);
}

static inline float3
moppe_gtao_position (float2 uv, float device_z, constant MoppeGtaoUniforms& u) {
  const float3 dir =
    normalize (u.ray_forward.xyz + u.ray_right.xyz * (2.0 * uv.x - 1.0) +
               u.ray_up.xyz * (1.0 - 2.0 * uv.y));
  const float forward =
    moppe_gtao_view_depth (device_z, u.params.z, u.params.w);
  return u.camera_pos.xyz + dir * (forward / dot (dir, u.ray_forward.xyz));
}

// Alchemy-style obscurance over a jittered spiral of depth taps: crevices,
// trunk bases, and branch junctions darken by how much nearby geometry
// leans over the shading point. Runs at half resolution over the stored
// scene depth; a depth-aware blur removes the sampling noise.
fragment float4 gtao_gather_fragment (QuadVaryings in [[stage_in]],
                                      constant MoppeGtaoUniforms& u
                                      [[buffer (MOPPE_BUF_FRAME)]],
                                      depth2d<float> scene_depth
                                      [[texture (MOPPE_TEX_POST_DEPTH)]]) {
  constexpr sampler depth_smp (
    coord::normalized, address::clamp_to_edge, filter::nearest);
  const float device_z = scene_depth.sample (depth_smp, in.uv);
  if (device_z <= 1e-6)
    return float4 (1.0); // sky
  const float3 position = moppe_gtao_position (in.uv, device_z, u);
  const float3 normal = normalize (cross (dfdy (position), dfdx (position)));
  const float forward =
    moppe_gtao_view_depth (device_z, u.params.z, u.params.w);

  // World-space sampling radius projected into uv per axis.
  const float radius = u.params.x;
  const float2 uv_radius =
    radius / (2.0 * forward) *
    float2 (1.0 / length (u.ray_right.xyz), 1.0 / length (u.ray_up.xyz));

  const float2 px = in.position.xy;
  const float ign =
    fract (52.9829189 * fract (0.06711056 * px.x + 0.00583715 * px.y));
  const uint taps = 10u;
  const float bias = 0.02 + 0.002 * forward;
  float obscurance = 0.0;
  for (uint i = 0u; i < taps; ++i) {
    const float angle = 6.2831853 * (ign + float (i) * 0.618034);
    const float reach = pow ((float (i) + 0.5 + ign) / float (taps), 0.7);
    const float2 tap_uv =
      in.uv + float2 (cos (angle), sin (angle)) * uv_radius * reach;
    const float tap_z = scene_depth.sample (depth_smp, tap_uv);
    if (tap_z <= 1e-6)
      continue;
    const float3 tap = moppe_gtao_position (tap_uv, tap_z, u);
    const float3 to_tap = tap - position;
    obscurance += max (0.0, dot (to_tap, normal) - bias) * radius /
                  max (dot (to_tap, to_tap), 0.01);
  }
  const float ao = saturate (1.0 - u.params.y * obscurance / float (taps));
  return float4 (ao);
}

// Separable depth-aware blur: neighbours vote only when they belong to the
// same surface, so occlusion never bleeds across silhouettes.
fragment float4 gtao_blur_fragment (QuadVaryings in [[stage_in]],
                                    constant MoppeGtaoUniforms& u
                                    [[buffer (MOPPE_BUF_FRAME)]],
                                    texture2d<float> occlusion
                                    [[texture (MOPPE_TEX_BLOOM)]],
                                    depth2d<float> scene_depth
                                    [[texture (MOPPE_TEX_POST_DEPTH)]]) {
  constexpr sampler smp (
    coord::normalized, address::clamp_to_edge, filter::linear);
  constexpr sampler depth_smp (
    coord::normalized, address::clamp_to_edge, filter::nearest);
  const float centre_depth = moppe_gtao_view_depth (
    scene_depth.sample (depth_smp, in.uv), u.params.z, u.params.w);
  const float weights[5] = { 0.153388, 0.221461, 0.250301, 0.221461, 0.153388 };
  float total = 0.0;
  float value = 0.0;
  for (int i = -2; i <= 2; ++i) {
    const float2 uv = in.uv + u.blur.xy * float (i);
    const float depth = moppe_gtao_view_depth (
      scene_depth.sample (depth_smp, uv), u.params.z, u.params.w);
    const float same_surface =
      saturate (1.0 - 8.0 * abs (depth - centre_depth) / centre_depth);
    const float weight = weights[i + 2] * same_surface;
    value += occlusion.sample (smp, uv).r * weight;
    total += weight;
  }
  return float4 (total > 0.0 ? value / total : 1.0);
}

fragment float4 gtao_apply_fragment (QuadVaryings in [[stage_in]],
                                     texture2d<float> scene
                                     [[texture (MOPPE_TEX_SCENE)]],
                                     texture2d<float> occlusion
                                     [[texture (MOPPE_TEX_BLOOM)]]) {
  constexpr sampler smp (
    coord::normalized, address::clamp_to_edge, filter::linear);
  return float4 (
    scene.sample (smp, in.uv).rgb * occlusion.sample (smp, in.uv).r, 1.0);
}
