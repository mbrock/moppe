// Flow-aligned translucent water for routed streams. Geometry supplies
// level cross-sections riding in carved channels: cross-stream u,
// downstream distance v in meters, rapid strength in color.r, the water
// column depth in color.g (normalized by MOPPE_RIVER_DEPTH_SPAN meters),
// waterfall strength in color.b, and fade opacity in color.a. Surface
// detail scrolls along v so the pattern advects downstream like a flow
// map; transparency follows the actual water column, so shallow water is
// glassy and deep water reads as a body.

#include "common.h"

#define MOPPE_RIVER_DEPTH_SPAN 2.0

struct RiverVaryings {
  float4 position [[position]];
  float3 world_pos;
  float3 normal;
  float2 uv;
  float rapid;
  float depth;
  float waterfall;
  float opacity;
};

vertex RiverVaryings
river_vertex (uint vid [[vertex_id]],
	      const device MoppeVertexIn* verts
	        [[buffer(MOPPE_BUF_VERTICES)]],
	      constant MoppeFrameUniforms& frame
	        [[buffer(MOPPE_BUF_FRAME)]],
	      constant MoppeDrawUniforms& draw
	        [[buffer(MOPPE_BUF_DRAW)]])
{
  const MoppeVertexIn v = verts[vid];
  const float4 world = draw.model * float4 (float3 (v.position), 1.0);
  const float3x3 nrm (draw.nrm0.xyz, draw.nrm1.xyz, draw.nrm2.xyz);
  RiverVaryings out;
  out.position = frame.view_proj * world;
  out.world_pos = world.xyz;
  out.normal = normalize (nrm * float3 (v.normal));
  out.uv = float2 (v.uv);
  out.rapid = float (v.color.r) / 255.0;
  out.depth = float (v.color.g) / 255.0;
  out.waterfall = float (v.color.b) / 255.0;
  out.opacity = float (v.color.a) / 255.0;
  return out;
}

fragment float4
river_fragment (RiverVaryings in [[stage_in]],
		 constant MoppeFrameUniforms& frame
		   [[buffer(MOPPE_BUF_FRAME)]],
		 depth2d<float> shadow_map [[texture(MOPPE_TEX_SHADOW)]])
{
  const float time = frame.misc.x;
  const float edge = smoothstep (0.0, 0.08, in.uv.x)
    * smoothstep (0.0, 0.08, 1.0 - in.uv.x);

  // Two noise layers advect downstream at different speeds so the
  // surface shears instead of sliding as one sheet. v is in meters;
  // wavelengths sit around one to two meters.
  const float speed = 2.0 + 3.5 * in.rapid + 5.0 * in.waterfall;
  const float2 flow1 = float2 (in.uv.x * 7.0,
			       in.uv.y * 0.9 - time * speed * 0.9);
  const float2 flow2 = float2 (in.uv.x * 11.0 + 3.7,
			       in.uv.y * 0.45 - time * speed * 0.55);
  const float n1 = moppe_value_noise (flow1);
  const float n2 = moppe_value_noise (flow2);
  const float surface = 0.6 * n1 + 0.4 * n2;

  // Normal perturbation from the scrolled field; the pattern's motion,
  // not the tilt axes, carries the flow direction.
  const float bump = 0.10 + 0.10 * in.rapid + 0.10 * in.waterfall;
  float3 n = in.normal;
  n.x += bump * (moppe_value_noise (flow1 + float2 (0.31, 0.0)) - n1
		 + 0.6 * (moppe_value_noise (flow2 + float2 (0.27, 0.0)) - n2));
  n.z += bump * (moppe_value_noise (flow1 + float2 (0.0, 0.23)) - n1
		 + 0.6 * (moppe_value_noise (flow2 + float2 (0.0, 0.33)) - n2));
  n = normalize (n);

  const float3 view = normalize (frame.camera_pos.xyz - in.world_pos);
  const float fresnel = 0.035
    + 0.965 * pow (1.0 - max (dot (n, view), 0.0), 5.0);
  const float3 reflection_dir = reflect (-view, n);

  const float3 to_frag = in.world_pos - frame.camera_pos.xyz;
  const float distance = length (to_frag);
  const float fog = moppe_distance_fog (distance, frame.fog_color.w);
  const float sun_visibility = moppe_sun_visibility
    (in.world_pos, n, frame.sun_dir.xyz, fog, frame.light_matrix,
     frame.shadow.x, frame.shadow.y, shadow_map);
  const float3 fog_color = moppe_warmed_fog
    (frame.fog_color.rgb, to_frag / max (distance, 1e-4),
     frame.sun_dir.xyz);
  const float3 sky_reflection = moppe_sky_radiance
    (frame.fog_color.rgb, reflection_dir, frame.sun_dir.xyz);

  const float depth_m = in.depth * MOPPE_RIVER_DEPTH_SPAN;
  const float3 shallow = moppe_srgb (float3 (0.10, 0.38, 0.38));
  const float3 deep = moppe_srgb (float3 (0.03, 0.15, 0.24));
  float3 color = mix (shallow, deep, smoothstep (0.15, 1.2, depth_m));
  // The sky reflects off the surface exactly as it does off the ocean;
  // without this the channel reads as a black trench.
  color = mix (color, sky_reflection, 0.5 * fresnel + 0.06);

  // Foam: broken water in rapids and falls, plus a thin contact line
  // where the surface meets the carved bank.  Shallow streams get no
  // bank foam at all so they stay clear water instead of white tubes.
  const float churn = saturate
    (0.6 * in.rapid * smoothstep (0.55, 0.95,
				  0.5 * (n1 + n2) + 0.25 * in.rapid));
  const float fall = saturate
    (in.waterfall * (0.45 + 0.55 * smoothstep (0.3, 0.75, n1)));
  const float bank = smoothstep (0.82, 0.98, abs (in.uv.x - 0.5) * 2.0)
    * (0.18 + 0.30 * surface) * smoothstep (0.30, 0.75, surface)
    * smoothstep (0.25, 0.9, depth_m) * (1.0 - in.waterfall);
  const float foam = saturate (max (max (churn, fall), bank));
  const float3 foam_albedo = moppe_srgb (float3 (0.87, 0.95, 0.97));
  const float3 foam_light = moppe_hemisphere_light (frame.ambient.rgb, n)
    + frame.sun_diffuse.rgb * (0.35 + 0.65
      * max (dot (n, frame.sun_dir.xyz), 0.0)) * sun_visibility;
  color = mix (color, foam_albedo * foam_light, foam);

  const float3 half_vector = normalize (frame.sun_dir.xyz + view);
  const float glint = pow (max (dot (n, half_vector), 0.0), 96.0)
    * (0.35 + 0.65 * fresnel);
  color += frame.sun_specular.rgb * glint * sun_visibility;

  color = mix (color, fog_color, smoothstep (0.0, 0.9, fog));

  // Transparency follows the water column: a hand-deep stream is glassy
  // over its bed, a meter of water reads as a body, and foam or falling
  // water stays visible regardless.
  const float body = smoothstep (0.05, 1.0, depth_m);
  const float alpha = edge * in.opacity
    * saturate (0.15 + 0.65 * body + 0.10 * fresnel
		+ 0.25 * in.waterfall + 0.45 * foam);
  return float4 (color, alpha);
}
