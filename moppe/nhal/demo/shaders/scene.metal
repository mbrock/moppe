// The demo's shaders, hand-written in the NHAL binding contract
// (docs/nhal.md) until Luv's language generates them: buffer 0 is the frame
// block, buffer 1 a pulled storage buffer, texture 0 and sampler 0 the
// tonemap's input. scene.hlsl is the same program for Direct3D 12.
#include <metal_stdlib>
using namespace metal;

struct FrameState {
  float4 camera_position; // w: seconds
  float4 camera_right;    // right / tan(fov x / 2); w: tan
  float4 camera_up;       // up / tan(fov y / 2); w: tan
  float4 camera_forward;  // w: near plane
  float4 sun_direction;
  float4 sun_color;
  float4 sky_zenith;
  float4 sky_horizon;     // w: fog density
  float4 terrain;         // cell, samples per side, x0, z0
};

// Reversed-Z with an infinite far plane: depth is near / view distance.
static float4 project (constant FrameState& frame, float3 world) {
  const float3 rel = world - frame.camera_position.xyz;
  return float4 (dot (rel, frame.camera_right.xyz),
                 dot (rel, frame.camera_up.xyz),
                 frame.camera_forward.w,
                 dot (rel, frame.camera_forward.xyz));
}

static float3 sky (constant FrameState& frame, float3 ray) {
  const float up = clamp (ray.y, 0.0f, 1.0f);
  float3 color = mix (frame.sky_horizon.xyz, frame.sky_zenith.xyz,
                      pow (up, 0.6f));
  const float sun = max (dot (ray, frame.sun_direction.xyz), 0.0f);
  color += frame.sun_color.xyz * (pow (sun, 900.0f) * 6.0f
                                  + pow (sun, 12.0f) * 0.08f);
  return color;
}

static float3 shade (constant FrameState& frame, float3 albedo, float3 normal,
                     float3 world) {
  const float3 sun = frame.sun_direction.xyz;
  const float diffuse = max (dot (normal, sun), 0.0f);
  const float3 ambient = mix (frame.sky_horizon.xyz * 0.35f,
                              frame.sky_zenith.xyz,
                              normal.y * 0.5f + 0.5f) * 0.55f;
  const float3 lit = albedo * (frame.sun_color.xyz * diffuse + ambient);
  const float3 rel = world - frame.camera_position.xyz;
  const float distance = length (rel);
  const float fog = 1.0f - exp (-distance * frame.sky_horizon.w);
  return mix (lit, sky (frame, rel / distance), fog);
}

struct Surface {
  float4 position [[position]];
  float3 world [[user(loc0)]];
  float3 normal [[user(loc1)]];
  float3 albedo [[user(loc2)]];
};

// -- terrain -------------------------------------------------------------

vertex Surface terrain_vertex (uint vertex_id [[vertex_id]],
                               constant FrameState& frame [[buffer(0)]],
                               device const float4* samples [[buffer(1)]]) {
  const uint side = uint (frame.terrain.y);
  const float4 sample = samples[vertex_id];
  const float3 world = float3 (frame.terrain.z
                                 + float (vertex_id % side) * frame.terrain.x,
                               sample.x,
                               frame.terrain.w
                                 + float (vertex_id / side) * frame.terrain.x);
  Surface out;
  out.position = project (frame, world);
  out.world = world;
  out.normal = sample.yzw;
  out.albedo = float3 (0);
  return out;
}

static float lattice (float2 p) {
  return fract (sin (dot (floor (p), float2 (127.1f, 311.7f))) * 43758.5453f);
}

fragment float4 terrain_fragment (Surface in [[stage_in]],
                                  constant FrameState& frame [[buffer(0)]]) {
  const float3 normal = normalize (in.normal);
  const float patch = lattice (in.world.xz / 9.0f) * 0.5f
                      + lattice (in.world.xz / 2.3f) * 0.5f;
  const float3 grass = mix (float3 (0.10f, 0.16f, 0.05f),
                            float3 (0.20f, 0.22f, 0.09f), patch);
  const float3 rock = float3 (0.27f, 0.25f, 0.23f) * (0.8f + 0.4f * patch);
  const float steep = smoothstep (0.78f, 0.62f, normal.y);
  const float3 albedo = mix (grass, rock, steep);
  return float4 (shade (frame, albedo, normal, in.world), 1);
}

// -- trees ---------------------------------------------------------------

// An instance is two lanes: (root x, y, z, height) and (trunk radius, crown
// radius, crown base as a fraction of height, tint). Vertices 0-17 are the
// trunk's two rings of nine; then three crown tiers of a nine-vertex base
// ring and an apex.
vertex Surface trees_vertex (uint vertex_id [[vertex_id]],
                             uint instance_id [[instance_id]],
                             constant FrameState& frame [[buffer(0)]],
                             device const float4* instances [[buffer(1)]]) {
  const float4 root = instances[instance_id * 2];
  const float4 shape = instances[instance_id * 2 + 1];
  const float height = root.w;
  const float crown_base = shape.z * height;
  float3 local, normal, albedo;
  if (vertex_id < 18) {
    const uint ring = vertex_id / 9;
    const float angle = float (vertex_id % 9) * (M_PI_F / 4.0f);
    const float2 around = float2 (cos (angle), sin (angle));
    const float radius = shape.x * (ring == 0 ? 1.0f : 0.6f);
    local = float3 (around.x * radius,
                    ring == 0 ? -0.5f : crown_base + 0.5f,
                    around.y * radius);
    normal = float3 (around.x, 0, around.y);
    albedo = float3 (0.13f, 0.09f, 0.06f);
  } else {
    const uint k = vertex_id - 18;
    const float tier = float (k / 10);
    const float crown = height - crown_base;
    const float base_y = crown_base + crown * tier * 0.27f;
    const float apex_y = base_y + crown * 0.48f;
    const float radius = shape.y * (1.0f - tier * 0.27f);
    if (k % 10 == 9) {
      local = float3 (0, apex_y, 0);
      normal = float3 (0, 1, 0);
    } else {
      const float angle = float (k % 10) * (M_PI_F / 4.0f) + tier * 0.4f
                          + shape.w * 6.28f;
      const float2 around = float2 (cos (angle), sin (angle));
      local = float3 (around.x * radius, base_y, around.y * radius);
      normal = normalize (float3 (around.x * (apex_y - base_y), radius,
                                  around.y * (apex_y - base_y)));
    }
    albedo = mix (float3 (0.035f, 0.07f, 0.04f), float3 (0.06f, 0.10f, 0.045f),
                  shape.w);
  }
  const float3 world = root.xyz + local;
  Surface out;
  out.position = project (frame, world);
  out.world = world;
  out.normal = normal;
  out.albedo = albedo;
  return out;
}

fragment float4 trees_fragment (Surface in [[stage_in]],
                                constant FrameState& frame [[buffer(0)]]) {
  return float4 (shade (frame, in.albedo, normalize (in.normal), in.world), 1);
}

// -- sky and tonemap -----------------------------------------------------

struct Screen {
  float4 position [[position]];
  float2 ndc [[user(loc0)]];
};

// One triangle over the screen, at the far plane (reversed-Z zero).
static Screen fullscreen (uint vertex_id) {
  const float2 ndc = float2 (vertex_id == 1 ? 3.0f : -1.0f,
                             vertex_id == 2 ? 3.0f : -1.0f);
  Screen out;
  out.position = float4 (ndc, 0, 1);
  out.ndc = ndc;
  return out;
}

vertex Screen sky_vertex (uint vertex_id [[vertex_id]]) {
  return fullscreen (vertex_id);
}

fragment float4 sky_fragment (Screen in [[stage_in]],
                              constant FrameState& frame [[buffer(0)]]) {
  const float tan_x = frame.camera_right.w, tan_y = frame.camera_up.w;
  const float3 ray = normalize (
    frame.camera_forward.xyz
    + frame.camera_right.xyz * (in.ndc.x * tan_x * tan_x)
    + frame.camera_up.xyz * (in.ndc.y * tan_y * tan_y));
  return float4 (sky (frame, ray), 1);
}

vertex Screen tonemap_vertex (uint vertex_id [[vertex_id]]) {
  return fullscreen (vertex_id);
}

fragment float4 tonemap_fragment (Screen in [[stage_in]],
                                  constant FrameState& frame [[buffer(0)]],
                                  texture2d<float> scene [[texture(0)]],
                                  sampler linear_clamp [[sampler(0)]]) {
  const float2 uv = float2 (in.ndc.x * 0.5f + 0.5f, 0.5f - in.ndc.y * 0.5f);
  const float3 x = scene.sample (linear_clamp, uv).rgb * 0.9f;
  // Narkowicz's ACES fit, then the display's gamma.
  const float3 mapped = clamp ((x * (2.51f * x + 0.03f))
                                 / (x * (2.43f * x + 0.59f) + 0.14f),
                               0.0f, 1.0f);
  return float4 (pow (mapped, 1.0f / 2.2f), 1);
}
