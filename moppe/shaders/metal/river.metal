// Flow-aligned translucent ribbons for routed streams. Geometry supplies
// cross-stream u, downstream distance v, rapid strength in color.r, and a
// logarithmic discharge signal in color.g.

#include "common.h"

struct RiverVaryings {
  float4 position [[position]];
  float3 world_pos;
  float3 normal;
  float2 uv;
  float rapid;
  float discharge;
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
  return out;
}

fragment float4
river_fragment (RiverVaryings in [[stage_in]],
		 constant MoppeFrameUniforms& frame
		   [[buffer(MOPPE_BUF_FRAME)]])
{
  const float time = frame.misc.x;
  const float edge = smoothstep (0.0, 0.16, in.uv.x)
    * smoothstep (0.0, 0.16, 1.0 - in.uv.x);
  const float phase = in.uv.y * 0.22 - time * (2.2 + in.rapid * 2.5);
  const float ribs = 0.5 + 0.5 * sin (phase * 6.28318
				      + sin (in.uv.x * 18.0));
  const float streak = pow (ribs, 5.0)
    * (0.35 + 0.65 * sin (in.uv.x * 31.0 + phase * 0.7) * 0.5 + 0.325);

  float3 n = in.normal;
  n.x += 0.08 * sin (phase * 1.7 + in.world_pos.z * 0.09);
  n.z += 0.08 * sin (phase * 2.1 - in.world_pos.x * 0.08);
  n = normalize (n);
  const float3 view = normalize (frame.camera_pos.xyz - in.world_pos);
  const float fresnel = 0.035
    + 0.965 * pow (1.0 - max (dot (n, view), 0.0), 5.0);

  const float3 shallow = moppe_srgb (float3 (0.055, 0.32, 0.34));
  const float3 deep = moppe_srgb (float3 (0.025, 0.14, 0.22));
  float3 color = mix (shallow, deep, saturate (in.discharge));
  const float foam = saturate (in.rapid * (0.10 + 0.55 * streak));
  color = mix (color, moppe_srgb (float3 (0.82, 0.94, 0.96)), foam);

  const float3 half_vector = normalize (frame.sun_dir.xyz + view);
  const float glint = pow (max (dot (n, half_vector), 0.0), 96.0)
    * (0.35 + 0.65 * fresnel);
  color += frame.sun_specular.rgb * glint;

  const float3 to_frag = in.world_pos - frame.camera_pos.xyz;
  const float distance = length (to_frag);
  const float fog = moppe_distance_fog (distance, frame.fog_color.w);
  const float3 fog_color = moppe_warmed_fog
    (frame.fog_color.rgb, to_frag / max (distance, 1e-4),
     frame.sun_dir.xyz);
  color = mix (color, fog_color, smoothstep (0.0, 0.9, fog));

  const float alpha = edge
    * (0.44 + 0.14 * fresnel + 0.18 * foam);
  return float4 (color, alpha);
}
