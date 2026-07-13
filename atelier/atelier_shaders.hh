#pragma once

#include <string_view>

namespace atelier {
  // Each mesh threadgroup receives one current leaf of the cellular
  // partition and emits its beveled hexagonal puck directly.  There is no
  // baked vertex buffer: changing the partition only changes Tile instances.
  inline constexpr std::string_view metal_shader_source = R"(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
  float4x4 world_to_clip;
  float4 eye;
};

struct Tile {
  float4x4 place_in_world;
  float4 glaze;
};

struct PuckPoint {
  float4 position [[position]];
  float3 world_position;
  float3 world_normal;
  float3 view_direction;
  half4 glaze;
};

constant float3 backdrop = float3(0.025, 0.032, 0.05);
constant float3 key_light = float3(-0.39364, 0.88569, 0.24603);
constant uint vertex_count = 26;
constant uint triangle_count = 48;

using PuckMesh =
  metal::mesh<PuckPoint, void, 32, 48, metal::topology::triangle>;

inline float2 rim(uint side, float radius) {
  const float angle = float(side) * (2.0 * M_PI_F / 6.0);
  return float2(sin(angle), cos(angle)) * radius;
}

inline PuckPoint puck_vertex(uint index,
                            constant Uniforms& uniforms,
                            const device Tile& tile) {
  float3 position;
  float3 normal;
  if (index == 0u) {
    position = float3(0, 0.34, 0);
    normal = float3(0, 1, 0);
  } else if (index == 1u) {
    position = float3(0, -0.34, 0);
    normal = float3(0, -1, 0);
  } else {
    const uint ring_index = (index - 2u) / 6u;
    const uint side = (index - 2u) % 6u;
    const float radii[4] = { 0.86, 1.0, 1.0, 0.86 };
    const float heights[4] = { 0.34, 0.23, -0.23, -0.34 };
    const float2 xz = rim(side, radii[ring_index]);
    position = float3(xz.x, heights[ring_index], xz.y);
    const float3 ring_normals[4] = {
      float3(xz.x * 0.28, 1.0, xz.y * 0.28),
      float3(xz.x, 0.38, xz.y),
      float3(xz.x, -0.38, xz.y),
      float3(xz.x * 0.28, -1.0, xz.y * 0.28)
    };
    normal = normalize(ring_normals[ring_index]);
  }

  const float4 world = tile.place_in_world * float4(position, 1.0);
  const float3 world_normal = normalize(
    (tile.place_in_world * float4(normal, 0.0)).xyz);
  return {
    uniforms.world_to_clip * world,
    world.xyz,
    world_normal,
    normalize(uniforms.eye.xyz - world.xyz),
    half4(tile.glaze)
  };
}

inline uint3 puck_triangle(uint triangle) {
  const uint side = triangle % 6u;
  const uint next = (side + 1u) % 6u;
  const uint band = triangle / 6u;
  const uint top_inner = 2u;
  const uint top_outer = 8u;
  const uint bottom_outer = 14u;
  const uint bottom_inner = 20u;

  switch (band) {
  case 0u:
    return uint3(0u, top_inner + side, top_inner + next);
  case 1u:
    return uint3(top_inner + side, top_outer + side, top_outer + next);
  case 2u:
    return uint3(top_inner + side, top_outer + next, top_inner + next);
  case 3u:
    return uint3(top_outer + side, bottom_outer + side,
                 bottom_outer + next);
  case 4u:
    return uint3(top_outer + side, bottom_outer + next, top_outer + next);
  case 5u:
    return uint3(bottom_outer + side, bottom_inner + side,
                 bottom_inner + next);
  case 6u:
    return uint3(bottom_outer + side, bottom_inner + next,
                 bottom_outer + next);
  default:
    return uint3(1u, bottom_inner + next, bottom_inner + side);
  }
}

[[mesh]] void puck_mesh(
  PuckMesh out,
  uint tile_id [[threadgroup_position_in_grid]],
  uint thread_id [[thread_index_in_threadgroup]],
  constant Uniforms& uniforms [[buffer(1)]],
  const device Tile* tiles [[buffer(2)]]) {
  if (thread_id == 0u)
    out.set_primitive_count(triangle_count);
  if (thread_id < vertex_count)
    out.set_vertex(thread_id, puck_vertex(thread_id, uniforms, tiles[tile_id]));
  for (uint triangle = thread_id; triangle < triangle_count; triangle += 32u) {
    const uint3 indices = puck_triangle(triangle);
    out.set_index(triangle * 3u + 0u, indices.x);
    out.set_index(triangle * 3u + 1u, indices.y);
    out.set_index(triangle * 3u + 2u, indices.z);
  }
}

fragment half4 puck_fragment(PuckPoint puck [[stage_in]]) {
  const float3 normal = normalize(puck.world_normal);
  const float diffuse = 0.34 + 0.66 * max(0.0, dot(normal, key_light));
  const float3 mirrored = reflect(-key_light, normal);
  const float gleam =
    pow(max(0.0, dot(mirrored, normalize(puck.view_direction))), 32.0);
  const float3 lit = float3(puck.glaze.rgb) * diffuse + 0.34 * gleam;
  return half4(half3(lit), 1.0h);
}
)";
}
