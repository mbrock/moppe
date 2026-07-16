// The forward "uber" shader for everything the DrawList records:
// props, vehicles, wildlife, baked city sectors, dust
// billboards, blob shadows.  Replaces fixed-function GL lighting
// (one directional sun, AMBIENT_AND_DIFFUSE color material,
// separate specular) plus the unified distance haze.

#include "common.h"

struct UberVaryings {
  float4 position [[position]];
  float3 world_pos;
  float3 normal;
  float2 uv;
  float4 color;
  float lit;
  float fogged;
};

vertex UberVaryings uber_vertex (uint vid [[vertex_id]],
                                 const device MoppeVertexIn* verts
                                 [[buffer (MOPPE_BUF_VERTICES)]],
                                 constant MoppeFrameUniforms& frame
                                 [[buffer (MOPPE_BUF_FRAME)]],
                                 constant MoppeDrawUniforms& draw
                                 [[buffer (MOPPE_BUF_DRAW)]]) {
  const MoppeVertexIn v = verts[vid];

  float4 world = draw.model * float4 (float3 (v.position), 1.0);
  const float3x3 nrm (draw.nrm0.xyz, draw.nrm1.xyz, draw.nrm2.xyz);

  if (v.flags.z)
    world.xyz = moppe_wind (world.xyz, float (v.flags.z) / 255.0, frame.misc.x);

  UberVaryings out;
  out.position = frame.view_proj * world;
  out.world_pos = world.xyz;
  out.normal = nrm * float3 (v.normal);
  out.uv = float2 (v.uv);
  // Vertex colors are authored in display space; decode to linear
  // here so they interpolate and light correctly (alpha stays).
  const float4 c = float4 (v.color) / 255.0;
  out.color = float4 (moppe_srgb (c.rgb), c.a);
  out.lit = v.flags.x ? 1.0 : 0.0;
  out.fogged = v.flags.y ? 1.0 : 0.0;
  return out;
}

fragment float4 uber_fragment (UberVaryings in [[stage_in]],
                               constant MoppeFrameUniforms& frame
                               [[buffer (MOPPE_BUF_FRAME)]],
                               texture2d<float> tex
                               [[texture (MOPPE_TEX_COLOR)]],
                               depth2d<float> shadow_map
                               [[texture (MOPPE_TEX_SHADOW)]],
                               sampler smp [[sampler (0)]]) {
  float4 base = in.color * tex.sample (smp, in.uv);

  float3 color = base.rgb;
  if (in.lit > 0.5) {
    float3 n = normalize (in.normal);
    const float3 l = frame.sun_dir.xyz;
    const float lambert = saturate ((dot (n, l) + 0.10) / 1.10);
    const float dist = length (in.world_pos - frame.camera_pos.xyz);
    const float fog = moppe_distance_fog (dist, frame.fog_color.w);
    const float sun_visibility = moppe_sun_visibility (in.world_pos,
                                                       n,
                                                       l,
                                                       fog,
                                                       frame.light_matrix,
                                                       frame.shadow.x,
                                                       frame.shadow.y,
                                                       shadow_map);

    // AMBIENT_AND_DIFFUSE color material: both terms scale the
    // vertex color.
    float3 lit = base.rgb * (moppe_hemisphere_light (frame.ambient.rgb, n) +
                             frame.sun_diffuse.rgb * lambert * sun_visibility);

    // Separate specular (global material, shininess 64).
    const float3 v = normalize (frame.camera_pos.xyz - in.world_pos);
    const float3 h = normalize (l + v);
    const float3 specular_tint = mix (base.rgb, float3 (1.0), 0.20);
    // Soft ground overlays and translucent effects must not inherit the hard
    // glossy prop highlight. Besides looking wet, it aliases badly when a
    // nearly coplanar decal is seen at a grazing angle.
    const float specular_coverage = smoothstep (0.82, 0.98, base.a);
    lit += specular_tint * frame.sun_specular.rgb * sun_visibility * 0.22 *
           pow (max (dot (n, h), 0.0), 64.0) * specular_coverage;

    // A restrained sky rim separates moving silhouettes from the
    // landscape, especially on their shadowed side.
    const float rim =
      pow (1.0 - max (dot (n, v), 0.0), 3.0) * (0.35 + 0.65 * max (n.y, 0.0));
    lit += base.rgb * float3 (0.025, 0.04, 0.065) * rim;

    color = lit;
  }

  if (in.fogged > 0.5) {
    const float3 to_frag = in.world_pos - frame.camera_pos.xyz;
    const float dist = length (to_frag);
    const float fog = moppe_distance_fog (dist, frame.fog_color.w);
    const float3 fog_c = moppe_warmed_fog (
      frame.fog_color.rgb, to_frag / max (dist, 1e-4), frame.sun_dir.xyz);
    color = mix (color, fog_c, smoothstep (0.0, 0.9, fog));
  }

  return float4 (color, base.a);
}

// ---------------------------------------------------------------
// HUD: 2D point-space overlay, y-down, no lighting, no fog.

struct HudVaryings {
  float4 position [[position]];
  float2 uv;
  float4 color;
};

vertex HudVaryings hud_vertex (uint vid [[vertex_id]],
                               const device MoppeVertexIn* verts
                               [[buffer (MOPPE_BUF_VERTICES)]],
                               constant MoppeHudUniforms& hud
                               [[buffer (MOPPE_BUF_FRAME)]]) {
  const MoppeVertexIn v = verts[vid];
  HudVaryings out;
  out.position = hud.proj * float4 (float3 (v.position), 1.0);
  out.uv = float2 (v.uv);
  out.color = float4 (v.color) / 255.0;
  return out;
}

fragment float4 hud_fragment (HudVaryings in [[stage_in]],
                              constant MoppeHudUniforms& hud
                              [[buffer (MOPPE_BUF_FRAME)]],
                              texture2d<float> tex
                              [[texture (MOPPE_TEX_COLOR)]],
                              sampler smp [[sampler (0)]]) {
  float4 color = in.color * tex.sample (smp, in.uv);
  if (hud.params.x > 0.5)
    color.rgb = pow (color.rgb, 2.2);
  return color;
}
