// Flow-aligned translucent water for routed streams. Geometry supplies
// level cross-sections riding in carved channels: cross-stream u,
// downstream distance v in meters, rapid strength in color.r, a
// logarithmic discharge signal in color.g, waterfall strength in color.b,
// and fade opacity in color.a. Surface detail scrolls along v so the
// pattern advects downstream like a flow map.

#include "common.h"

struct RiverVaryings {
  float4 position [[position]];
  float3 world_pos;
  float3 normal;
  float2 uv;
  float rapid;
  float discharge;
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
  out.discharge = float (v.color.g) / 255.0;
  out.waterfall = float (v.color.b) / 255.0;
  out.opacity = float (v.color.a) / 255.0;
  return out;
}

fragment float4
river_fragment (RiverVaryings in [[stage_in]],
		 constant MoppeFrameUniforms& frame
		   [[buffer(MOPPE_BUF_FRAME)]])
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

  const float3 to_frag = in.world_pos - frame.camera_pos.xyz;
  const float distance = length (to_frag);
  const float fog = moppe_distance_fog (distance, frame.fog_color.w);
  const float3 fog_color = moppe_warmed_fog
    (frame.fog_color.rgb, to_frag / max (distance, 1e-4),
     frame.sun_dir.xyz);

  const float3 shallow = moppe_srgb (float3 (0.10, 0.38, 0.38));
  const float3 deep = moppe_srgb (float3 (0.03, 0.15, 0.24));
  float3 color = mix (shallow, deep, saturate (in.discharge));
  // The sky reflects off the surface exactly as it does off the ocean;
  // without this the channel reads as a black trench.
  color = mix (color, fog_color, 0.5 * fresnel + 0.06);

  // Foam: broken water in rapids and falls, plus a thin contact line
  // where the surface meets the carved bank.
  const float churn = saturate
    (0.6 * in.rapid * smoothstep (0.55, 0.95,
				  0.5 * (n1 + n2) + 0.25 * in.rapid));
  const float fall = saturate
    (in.waterfall * (0.45 + 0.55 * smoothstep (0.3, 0.75, n1)));
  const float bank = smoothstep (0.82, 0.98, abs (in.uv.x - 0.5) * 2.0)
    * (0.18 + 0.30 * surface) * smoothstep (0.30, 0.75, surface)
    * (0.4 + 0.6 * in.discharge) * (1.0 - in.waterfall);
  const float foam = saturate (max (max (churn, fall), bank));
  color = mix (color, moppe_srgb (float3 (0.87, 0.95, 0.97)), foam);

  const float3 half_vector = normalize (frame.sun_dir.xyz + view);
  const float glint = pow (max (dot (n, half_vector), 0.0), 96.0)
    * (0.35 + 0.65 * fresnel);
  color += frame.sun_specular.rgb * glint;

  color = mix (color, fog_color, smoothstep (0.0, 0.9, fog));

  // Filled channels read as water bodies: deeper discharge is nearly
  // opaque, which also hides the double blend where strip segments
  // overlap on the inside of a bend.
  const float alpha = edge * in.opacity
    * saturate (0.62 + 0.30 * in.discharge + 0.10 * fresnel
		+ 0.20 * in.waterfall + 0.30 * foam);
  return float4 (color, alpha);
}
