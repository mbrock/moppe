#pragma once

#include <string_view>

namespace atelier {
  // Mirrors the GPU-facing structs in atelier.hh field for field.
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

struct Wire {
  float3 position;
  float3 normal;
};

struct WirePoint {
  float4 position [[position]];
  half4 colour;
};

constant float3 backdrop = float3(0.025, 0.032, 0.05);
// normalize(float3(-0.4, 0.9, 0.25)), spelled out because program
// sources may not run global constructors.
constant float3 key_light = float3(-0.39364, 0.88569, 0.24603);

vertex WirePoint atelier_vertex(const device Wire* wires [[buffer(0)]],
                                constant Uniforms& uniforms [[buffer(1)]],
                                const device Tile* tiles [[buffer(2)]],
                                uint vertex_id [[vertex_id]],
                                uint tile_id [[instance_id]]) {
  const device Tile& tile = tiles[tile_id];
  const device Wire& wire = wires[vertex_id];
  const float4 place = tile.place_in_world * float4(wire.position, 1.0);
  const float4 clip = uniforms.world_to_clip * place;

  // The tiles only ever rotate, so the model matrix carries normals.
  const float3 normal =
    normalize((tile.place_in_world * float4(wire.normal, 0.0)).xyz);
  const float shine = 0.45 + 0.55 * max(0.0, dot(normal, key_light));

  // A metallic gleam where the key light mirrors toward the camera.
  const float3 view = normalize(uniforms.eye.xyz - place.xyz);
  const float3 mirrored = reflect(-key_light, normal);
  const float gleam = pow(max(0.0, dot(mirrored, view)), 24.0);
  const float3 lit = tile.glaze.rgb * shine
    + 0.7 * gleam * mix(tile.glaze.rgb, float3(1.0), 0.5);

  // Wires stay crisp out to arm's length, then sink into the night.
  const float away = max(0.0, clip.w - 8.0);
  const float presence = exp2(-0.02 * away * away);
  const float3 tone = mix(backdrop, lit, presence);
  return { clip, half4(half3(tone), 1.0h) };
}

fragment half4 atelier_fragment(WirePoint wire [[stage_in]]) {
  return wire.colour;
}
)";
}
