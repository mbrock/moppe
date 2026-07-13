#pragma once

#include <string_view>

namespace atelier {
  // Mesh threadgroups procedurally emit either one beveled puck or one
  // ligament. There are no baked vertex buffers: changing the partition or
  // its deformation only changes compact instance data.
  inline constexpr std::string_view metal_shader_source = R"(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
  float4x4 world_to_clip;
  float4 eye;
  float4 atmosphere;
};

struct Tile {
  float4x4 place_in_world;
  float4 material;
};

struct Ligament {
  float4 start;
  float4 end;
  float4 start_normal;
  float4 end_normal;
  float4 physiology;
};

struct PuckPoint {
  float4 position [[position]];
  float3 local_position;
  float3 world_position;
  float3 world_normal;
  float3 view_direction;
  float elapsed;
  half4 material;
};

struct LigamentPoint {
  float4 position [[position]];
  float3 world_position;
  float3 world_normal;
  float3 view_direction;
  float2 fibre;
  float strain;
  float bend;
  float seed;
  float xylem;
  float phloem;
  float elapsed;
};

constant float3 backdrop = float3(0.032, 0.035, 0.038);
constant float3 sun_direction = float3(-0.250, 0.961, 0.120);
constant float3 sun_colour = float3(1.0, 0.95, 0.86);
constant float3 inner_radiance = float3(1.0, 0.70, 0.48);
constant uint vertex_count = 26;
constant uint triangle_count = 48;

using PuckMesh =
  metal::mesh<PuckPoint, void, 32, 48, metal::topology::triangle>;
using LigamentMesh =
  metal::mesh<LigamentPoint, void, 32, 48, metal::topology::triangle>;

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
    uniforms.atmosphere.x,
    half4(tile.material)
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

inline float cloud_illumination(float3 world_position, float elapsed) {
  const float2 wind = elapsed * float2(0.58, 0.21);
  const float2 place = world_position.xz + wind;
  const float broad = stone_noise(float3(place * 0.105, 11.7));
  const float detail = stone_noise(float3(place * 0.225, 29.3));
  const float cover = smoothstep(0.43, 0.66, 0.78 * broad + 0.22 * detail);
  return 1.0 - 0.45 * cover;
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

inline LigamentPoint ligament_vertex(uint index,
                                     constant Uniforms& uniforms,
                                     const device Ligament& ligament) {
  const uint ring = index / 6u;
  const uint side_index = index % 6u;
  const float t = float(ring) / 4.0;
  const float angle = float(side_index) * (2.0 * M_PI_F / 6.0);
  const float3 start = ligament.start.xyz;
  const float3 end = ligament.end.xyz;
  const float3 along = normalize(end - start);
  const float3 surface_normal = normalize(
    mix(ligament.start_normal.xyz, ligament.end_normal.xyz, t));
  const float3 across = normalize(cross(along, surface_normal));
  const float3 around = normalize(cross(across, along));
  const float arch = sin(M_PI_F * t);
  const float join = abs(2.0 * t - 1.0);
  const float tension = saturate(ligament.start.w * 90.0);
  const float rest_radius = max(0.006, ligament.end_normal.w);
  const float radius = mix(rest_radius, rest_radius * 0.60, tension) *
                       (0.76 + 0.24 * join);
  const float3 centre = mix(start, end, t) -
                        surface_normal * arch * (0.018 + 0.020 * tension);
  const float3 radial = cos(angle) * across + sin(angle) * around;
  const float3 world_position = centre + radius * radial;
  return {
    uniforms.world_to_clip * float4(world_position, 1.0),
    world_position,
    radial,
    normalize(uniforms.eye.xyz - world_position),
    float2(t, angle),
    ligament.start.w,
    ligament.end.w,
    ligament.start_normal.w,
    ligament.physiology.x,
    ligament.physiology.y,
    uniforms.atmosphere.x
  };
}

inline uint3 ligament_triangle(uint triangle) {
  const uint quad = triangle / 2u;
  const uint segment = quad / 6u;
  const uint side = quad % 6u;
  const uint next = (side + 1u) % 6u;
  const uint first = segment * 6u;
  const uint second = first + 6u;
  if ((triangle & 1u) == 0u)
    return uint3(first + side, second + side, second + next);
  return uint3(first + side, second + next, first + next);
}

[[mesh]] void ligament_mesh(
  LigamentMesh out,
  uint ligament_id [[threadgroup_position_in_grid]],
  uint thread_id [[thread_index_in_threadgroup]],
  constant Uniforms& uniforms [[buffer(1)]],
  const device Ligament* ligaments [[buffer(3)]]) {
  if (thread_id == 0u)
    out.set_primitive_count(48u);
  if (thread_id < 30u)
    out.set_vertex(thread_id,
                   ligament_vertex(thread_id, uniforms,
                                   ligaments[ligament_id]));
  for (uint triangle = thread_id; triangle < 48u; triangle += 32u) {
    const uint3 indices = ligament_triangle(triangle);
    out.set_index(triangle * 3u + 0u, indices.x);
    out.set_index(triangle * 3u + 1u, indices.y);
    out.set_index(triangle * 3u + 2u, indices.z);
  }
}

fragment half4 ligament_fragment(LigamentPoint ligament [[stage_in]]) {
  const float3 normal = normalize(ligament.world_normal);
  const float3 view = normalize(ligament.view_direction);
  const float tension = smoothstep(0.0002, 0.018, ligament.strain);
  const float bend = saturate(abs(ligament.bend) * 2.4);
  const float3 resting = float3(0.48, 0.245, 0.15);
  const float3 stretched = float3(1.0, 0.34, 0.095);
  const float3 positive_bend = float3(0.78, 0.25, 0.16);
  const float3 negative_bend = float3(0.24, 0.43, 0.37);
  float3 albedo = mix(resting, stretched, tension);
  albedo = mix(albedo,
               ligament.bend >= 0.0 ? positive_bend : negative_bend,
               0.28 * bend * (1.0 - tension));

  const float fibres = 0.5 + 0.5 * cos(ligament.fibre.y * 3.0 +
                                       ligament.fibre.x * 24.0 +
                                       ligament.seed * 9.0);
  albedo *= 0.88 + 0.16 * fibres;
  albedo *= 0.96 + 0.07 * ligament.seed;
  const float light = saturate((dot(normal, sun_direction) + 0.32) / 1.32);
  const float cloud = cloud_illumination(
    ligament.world_position, ligament.elapsed);
  float3 lit = albedo * (0.28 + 0.58 * light * cloud);

  const float rim = pow(1.0 - abs(dot(normal, view)), 2.0);
  const float waist = 1.0 - smoothstep(0.0, 0.20,
                                       abs(ligament.fibre.x - 0.5));
  const float pulse = 0.86 + 0.14 * sin(ligament.elapsed * 2.2 +
                                        ligament.fibre.x * M_PI_F);
  const float glow = tension * pulse * (0.075 + 0.20 * waist) +
                     bend * rim * 0.045;
  lit += mix(float3(1.0, 0.20, 0.07), inner_radiance, 0.35) * glow;
  lit += float3(0.72, 0.28, 0.16) * rim * 0.025;

  // Opposing transports inhabit the same oriented edge.  Their signs reverse
  // across the collar: xylem rises through the shoot while phloem returns,
  // then each travels the other way through the root tree.
  const float xylem_strength = saturate(abs(ligament.xylem) * 0.42);
  const float phloem_strength = saturate(abs(ligament.phloem) * 0.52);
  const float xylem_direction = sign(ligament.xylem);
  const float phloem_direction = sign(ligament.phloem);
  const float xylem_wave = pow(
    0.5 + 0.5 * cos(2.0 * M_PI_F *
                    (2.4 * ligament.fibre.x -
                     xylem_direction * 0.34 * ligament.elapsed)),
    9.0);
  const float phloem_wave = pow(
    0.5 + 0.5 * cos(2.0 * M_PI_F *
                    (2.0 * ligament.fibre.x -
                     phloem_direction * 0.27 * ligament.elapsed)),
    11.0);
  const float strand = 0.5 + 0.5 * cos(ligament.fibre.y * 2.0);
  lit += float3(0.20, 0.48, 0.72) * xylem_strength * xylem_wave *
         (0.025 + 0.035 * strand);
  lit += float3(0.92, 0.48, 0.16) * phloem_strength * phloem_wave *
         (0.020 + 0.030 * (1.0 - strand));
  return half4(half3(lit), 1.0h);
}

fragment half4 puck_fragment(PuckPoint puck [[stage_in]]) {
  const float3 normal = normalize(puck.world_normal);
  const float seed = float(puck.material.a);
  const float3 texture_offset = seed * float3(41.0, 73.0, 101.0);
  const float broad = stone_noise(puck.local_position * 5.0 + texture_offset);
  const float fine = stone_noise(puck.local_position * 23.0 + texture_offset);
  float3 albedo = float3(puck.material.rgb);
  const float chalk = 0.72 * broad + 0.28 * fine;
  const float pore = pow(saturate((fine - 0.58) / 0.42), 3.0);
  albedo *= 1.0 + 0.10 * (chalk - 0.5) - 0.035 * pore;

  const float raw_light = dot(normal, sun_direction);
  const float wrapped_light = saturate((raw_light + 0.20) / 1.20);
  const float banded = floor(wrapped_light * 4.0 + 0.5) / 4.0;
  const float soft_light = mix(wrapped_light, banded, 0.30);
  const float cloud = cloud_illumination(puck.world_position, puck.elapsed);
  float3 lit = albedo * 0.23;
  lit += albedo * sun_colour * cloud * (0.15 + 0.52 * soft_light);

  const float3 view = normalize(puck.view_direction);
  const float3 mirrored = reflect(-sun_direction, normal);
  const float gleam =
    pow(max(0.0, dot(mirrored, view)), 12.0);
  lit += 0.008 * sun_colour * gleam;

  const float facing = abs(dot(normal, view));
  const float silhouette = 1.0 - smoothstep(0.08, 0.30, facing);
  const float radius = length(puck.local_position.xz);
  const float top_rim = smoothstep(0.78, 0.855, radius) *
                        smoothstep(0.78, 0.96, normal.y);
  const float outline = max(0.42 * silhouette, 0.27 * top_rim);
  lit = mix(lit, albedo * 0.28, outline);

  const float grazing = pow(1.0 - facing, 2.0);
  const float backlit = pow(max(0.0, dot(-normal, sun_direction)), 1.5);
  const float chamfer = 1.0 - abs(normal.y);
  const float cloud_scatter = (1.0 - cloud) * (0.35 + 0.65 * grazing);
  const float aura = 0.040 * grazing + 0.080 * grazing * backlit +
                     0.028 * chamfer * (0.5 + 0.5 * cloud) +
                     0.040 * cloud_scatter;
  lit += inner_radiance * aura * (1.0 - 0.40 * outline);
  return half4(half3(lit), 1.0h);
}
)";
}
