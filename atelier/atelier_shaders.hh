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
  float3 local_position;
  float3 world_position;
  float3 world_normal;
  float3 view_direction;
  half4 glaze;
};

constant float3 backdrop = float3(0.030, 0.034, 0.038);
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
    position,
    world.xyz,
    world_normal,
    normalize(uniforms.eye.xyz - world.xyz),
    half4(tile.glaze)
  };
}

inline float stone_hash(float3 point) {
  point = fract(point * float3(0.1031, 0.1030, 0.0973));
  point += dot(point, point.yxz + 33.33);
  return fract((point.x + point.y) * point.z);
}

inline float stone_noise(float3 point) {
  const float3 cell = floor(point);
  float3 blend = fract(point);
  blend = blend * blend * (3.0 - 2.0 * blend);
  const float lower = mix(
    mix(stone_hash(cell + float3(0, 0, 0)),
        stone_hash(cell + float3(1, 0, 0)), blend.x),
    mix(stone_hash(cell + float3(0, 1, 0)),
        stone_hash(cell + float3(1, 1, 0)), blend.x), blend.y);
  const float upper = mix(
    mix(stone_hash(cell + float3(0, 0, 1)),
        stone_hash(cell + float3(1, 0, 1)), blend.x),
    mix(stone_hash(cell + float3(0, 1, 1)),
        stone_hash(cell + float3(1, 1, 1)), blend.x), blend.y);
  return mix(lower, upper, blend.z);
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
  const float seed = float(puck.glaze.a);
  const float3 texture_offset = seed * float3(41.0, 73.0, 101.0);
  const float broad = stone_noise(puck.local_position * 5.0 + texture_offset);
  const float fine = stone_noise(puck.local_position * 23.0 + texture_offset);
  float3 albedo = float3(puck.glaze.rgb);
  albedo *= 1.0 + 0.075 * (0.72 * broad + 0.28 * fine - 0.5);

  const float light = max(0.0, dot(normal, key_light));
  const float banded = floor(light * 3.0 + 0.5) / 3.0;
  const float diffuse = 0.48 + 0.42 * mix(light, banded, 0.45);
  const float3 mirrored = reflect(-key_light, normal);
  const float gleam =
    pow(max(0.0, dot(mirrored, normalize(puck.view_direction))), 18.0);
  float3 lit = albedo * diffuse + 0.018 * gleam;

  const float facing = abs(dot(normal, normalize(puck.view_direction)));
  const float silhouette = 1.0 - smoothstep(0.08, 0.30, facing);
  const float radius = length(puck.local_position.xz);
  const float top_rim = smoothstep(0.78, 0.855, radius) *
                        smoothstep(0.78, 0.96, normal.y);
  const float outline = max(0.55 * silhouette, 0.34 * top_rim);
  lit = mix(lit, albedo * 0.28, outline);
  return half4(half3(lit), 1.0h);
}
)";
}
