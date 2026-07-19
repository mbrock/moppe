#include <moppe/render/webgpu/webgpu_renderer.hh>

#include <webgpu/webgpu_cpp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace moppe::render {
  namespace {
    constexpr const char* shader_source = R"wgsl(
struct FrameUniforms {
  view_proj: mat4x4<f32>,
  hud_proj: mat4x4<f32>,
  model: mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> frame: FrameUniforms;
@group(1) @binding(0) var color_texture: texture_2d<f32>;
@group(1) @binding(1) var color_sampler: sampler;

struct VertexInput {
  @location(0) position: vec3<f32>,
  @location(1) normal: vec3<f32>,
  @location(2) uv: vec2<f32>,
  @location(3) color: vec4<f32>,
};

struct VertexOutput {
  @builtin(position) position: vec4<f32>,
  @location(0) uv: vec2<f32>,
  @location(1) color: vec4<f32>,
};

@vertex
fn world_vertex(input: VertexInput) -> VertexOutput {
  var output: VertexOutput;
  output.position = frame.view_proj * frame.model *
                    vec4<f32>(input.position, 1.0);
  output.uv = input.uv;
  output.color = input.color;
  return output;
}

@vertex
fn hud_vertex(input: VertexInput) -> VertexOutput {
  var output: VertexOutput;
  output.position = frame.hud_proj * vec4<f32>(input.position, 1.0);
  output.uv = input.uv;
  output.color = input.color;
  return output;
}

@fragment
fn color_fragment(input: VertexOutput) -> @location(0) vec4<f32> {
  return input.color * textureSample(color_texture, color_sampler, input.uv);
}
)wgsl";

    constexpr const char* terrain_shader_source = R"wgsl(
struct TerrainUniforms {
  view_proj: mat4x4<f32>,
  camera_fog: vec4<f32>,
  sun_direction: vec4<f32>,
  sun_diffuse: vec4<f32>,
  sun_specular: vec4<f32>,
  ambient: vec4<f32>,
  fog_color: vec4<f32>,
  terrain_scale: vec4<f32>,
  material_params: vec4<f32>,
};

struct ChunkUniforms {
  origin_step: vec4<f32>,
  morph: vec4<f32>,
  world_offset: vec4<f32>,
};

@group(0) @binding(0) var<uniform> terrain: TerrainUniforms;
@group(0) @binding(1) var heights: texture_2d<f32>;
@group(0) @binding(2) var normals: texture_2d<f32>;
@group(0) @binding(3) var grass_texture: texture_2d<f32>;
@group(0) @binding(4) var dirt_texture: texture_2d<f32>;
@group(0) @binding(5) var rock_texture: texture_2d<f32>;
@group(0) @binding(6) var snow_texture: texture_2d<f32>;
@group(0) @binding(7) var terrain_sampler: sampler;
@group(1) @binding(0) var<uniform> chunk: ChunkUniforms;

struct VertexInput {
  @location(0) local_grid: vec2<f32>,
};

struct VertexOutput {
  @builtin(position) position: vec4<f32>,
  @location(0) world_position: vec3<f32>,
  @location(1) normal: vec3<f32>,
  @location(2) normalized_height: f32,
  @location(3) fog: f32,
  @location(4) grid_coord: vec2<f32>,
  @location(5) @interpolate(flat) lod_step: f32,
};

fn sample_position(position: vec2<i32>) -> vec2<i32> {
  let limit = vec2<i32>(textureDimensions(heights)) - vec2<i32>(1);
  return clamp(position, vec2<i32>(0), limit);
}

fn read_height(position: vec2<i32>) -> f32 {
  return textureLoad(heights, sample_position(position), 0).r;
}

fn read_normal(position: vec2<i32>) -> vec3<f32> {
  let packed = textureLoad(normals, sample_position(position), 0).xyz;
  return normalize(vec3<f32>(packed.x, max(packed.y, 0.001), packed.z));
}

fn cubic(p0: f32, p1: f32, p2: f32, p3: f32, t: f32) -> vec2<f32> {
  let a = 0.5 * (-p0 + 3.0 * p1 - 3.0 * p2 + p3);
  let b = 0.5 * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3);
  let c = 0.5 * (-p0 + p2);
  return vec2<f32>(((a * t + b) * t + c) * t + p1,
                   (3.0 * a * t + 2.0 * b) * t + c);
}

fn smooth_height(grid: vec2<f32>) -> vec3<f32> {
  let dimensions = vec2<f32>(textureDimensions(heights));
  let clamped_grid = clamp(grid, vec2<f32>(0.0), dimensions - vec2<f32>(1.0));
  let cell = vec2<i32>(floor(clamped_grid));
  let fraction = fract(clamped_grid);
  var rows: array<f32, 4>;
  var derivatives_x: array<f32, 4>;
  for (var j = 0; j < 4; j = j + 1) {
    var samples: array<f32, 4>;
    for (var i = 0; i < 4; i = i + 1) {
      samples[i] = read_height(cell + vec2<i32>(i - 1, j - 1));
    }
    let value = cubic(samples[0], samples[1], samples[2], samples[3],
                      fraction.x);
    rows[j] = value.x;
    derivatives_x[j] = value.y;
  }
  let value_z = cubic(rows[0], rows[1], rows[2], rows[3], fraction.y);
  let value_x = cubic(derivatives_x[0], derivatives_x[1],
                      derivatives_x[2], derivatives_x[3], fraction.y).x;

  let h00 = read_height(cell);
  let h10 = read_height(cell + vec2<i32>(1, 0));
  let h01 = read_height(cell + vec2<i32>(0, 1));
  let h11 = read_height(cell + vec2<i32>(1, 1));
  let low = min(min(h00, h10), min(h01, h11));
  let high = max(max(h00, h10), max(h01, h11));
  if (value_z.x < low || value_z.x > high) {
    let h0 = mix(h00, h10, fraction.x);
    let h1 = mix(h01, h11, fraction.x);
    let dx = mix(h10 - h00, h11 - h01, fraction.y);
    return vec3<f32>(mix(h0, h1, fraction.y), dx, h1 - h0);
  }
  return vec3<f32>(value_z.x, value_x, value_z.y);
}

fn height_on_lattice(grid: vec2<f32>, step: f32) -> f32 {
  let limit = vec2<f32>(textureDimensions(heights)) - vec2<f32>(1.0);
  let cell = min(floor(grid / step) * step, limit - vec2<f32>(step));
  let fraction = clamp((grid - cell) / step, vec2<f32>(0.0),
                       vec2<f32>(1.0));
  let stride = i32(round(step));
  let p00 = vec2<i32>(cell);
  let h00 = read_height(p00);
  let h10 = read_height(p00 + vec2<i32>(stride, 0));
  let h01 = read_height(p00 + vec2<i32>(0, stride));
  let h11 = read_height(p00 + vec2<i32>(stride, stride));
  if (fraction.x + fraction.y <= 1.0) {
    return h00 + fraction.x * (h10 - h00) +
           fraction.y * (h01 - h00);
  }
  return h11 + (1.0 - fraction.y) * (h10 - h11) +
         (1.0 - fraction.x) * (h01 - h11);
}

fn normal_on_lattice(grid: vec2<f32>, step: f32) -> vec3<f32> {
  let limit = vec2<f32>(textureDimensions(normals)) - vec2<f32>(1.0);
  let cell = min(floor(grid / step) * step, limit - vec2<f32>(step));
  let fraction = clamp((grid - cell) / step, vec2<f32>(0.0),
                       vec2<f32>(1.0));
  let stride = i32(round(step));
  let p00 = vec2<i32>(cell);
  let n00 = read_normal(p00);
  let n10 = read_normal(p00 + vec2<i32>(stride, 0));
  let n01 = read_normal(p00 + vec2<i32>(0, stride));
  let n11 = read_normal(p00 + vec2<i32>(stride, stride));
  if (fraction.x + fraction.y <= 1.0) {
    return n00 + fraction.x * (n10 - n00) +
           fraction.y * (n01 - n00);
  }
  return n11 + (1.0 - fraction.y) * (n10 - n11) +
         (1.0 - fraction.x) * (n01 - n11);
}

fn normal_bilinear(grid: vec2<f32>) -> vec3<f32> {
  let dimensions = vec2<f32>(textureDimensions(normals));
  let clamped_grid = clamp(grid, vec2<f32>(0.0), dimensions - vec2<f32>(1.0));
  let cell = vec2<i32>(floor(clamped_grid));
  let fraction = fract(clamped_grid);
  let n0 = mix(read_normal(cell), read_normal(cell + vec2<i32>(1, 0)),
               fraction.x);
  let n1 = mix(read_normal(cell + vec2<i32>(0, 1)),
               read_normal(cell + vec2<i32>(1, 1)), fraction.x);
  return normalize(mix(n0, n1, fraction.y));
}

@vertex
fn terrain_vertex(input: VertexInput) -> VertexOutput {
  let grid = chunk.origin_step.xy + input.local_grid * chunk.origin_step.z;
  var height: f32;
  var normal: vec3<f32>;
  if (chunk.origin_step.z < 1.0) {
    let reconstructed = smooth_height(grid);
    height = reconstructed.x;
    let tangent_x = vec3<f32>(terrain.terrain_scale.x,
                              reconstructed.y * terrain.terrain_scale.y, 0.0);
    let tangent_z = vec3<f32>(0.0,
                              reconstructed.z * terrain.terrain_scale.y,
                              terrain.terrain_scale.z);
    normal = normalize(cross(tangent_z, tangent_x));
  } else {
    height = read_height(vec2<i32>(round(grid)));
    normal = read_normal(vec2<i32>(round(grid)));
  }

  let canonical_xz = grid * terrain.terrain_scale.xz;
  let world_xz = canonical_xz + chunk.world_offset.xz;
  if (chunk.origin_step.w > chunk.origin_step.z &&
      chunk.morph.y > chunk.morph.x) {
    let distance_to_camera = length(world_xz - terrain.camera_fog.xz);
    let morph = smoothstep(chunk.morph.x, chunk.morph.y,
                           distance_to_camera);
    if (morph > 0.0) {
      height = mix(height,
                   height_on_lattice(grid, chunk.origin_step.w), morph);
      normal = normalize(mix(normal,
                             normal_on_lattice(grid, chunk.origin_step.w),
                             morph));
    }
  }

  let world = vec3<f32>(grid.x * terrain.terrain_scale.x +
                          chunk.world_offset.x,
                        height * terrain.terrain_scale.y,
                        grid.y * terrain.terrain_scale.z +
                          chunk.world_offset.z);
  let distance_to_camera = length(world - terrain.camera_fog.xyz);
  var fog = 1.0 - exp(-pow(distance_to_camera * terrain.camera_fog.w, 1.5));
  let lowness = 1.0 - smoothstep(45.0, 170.0, world.y);
  fog += 0.3 * lowness * smoothstep(150.0, 1500.0, distance_to_camera);

  var output: VertexOutput;
  output.position = terrain.view_proj * vec4<f32>(world, 1.0);
  output.world_position = world;
  output.normal = normal;
  output.normalized_height = height;
  output.fog = clamp(fog, 0.0, 1.0);
  output.grid_coord = grid;
  output.lod_step = chunk.origin_step.z;
  return output;
}

fn hash_value(position: vec2<f32>) -> f32 {
  return fract(sin(dot(position, vec2<f32>(127.1, 311.7))) * 43758.5453);
}

fn value_noise(position: vec2<f32>) -> f32 {
  let cell = floor(position);
  var fraction = fract(position);
  fraction = fraction * fraction * (vec2<f32>(3.0) - 2.0 * fraction);
  let a = hash_value(cell);
  let b = hash_value(cell + vec2<f32>(1.0, 0.0));
  let c = hash_value(cell + vec2<f32>(0.0, 1.0));
  let d = hash_value(cell + vec2<f32>(1.0, 1.0));
  return mix(mix(a, b, fraction.x), mix(c, d, fraction.x), fraction.y);
}

fn authored_linear(color: vec3<f32>) -> vec3<f32> {
  return pow(max(color, vec3<f32>(0.0)), vec3<f32>(2.2));
}

fn display_srgb(linear_color: vec3<f32>) -> vec3<f32> {
  let positive = max(linear_color, vec3<f32>(0.0));
  let low = positive * 12.92;
  let high = 1.055 * pow(positive, vec3<f32>(1.0 / 2.4)) -
             vec3<f32>(0.055);
  return select(high, low, positive <= vec3<f32>(0.0031308));
}

@fragment
fn terrain_fragment(input: VertexOutput) -> @location(0) vec4<f32> {
  let resolved_normal = select(normalize(input.normal),
                               normal_bilinear(input.grid_coord),
                               terrain.material_params.z > 0.5 &&
                               input.lod_step >= 1.0);
  let normal = normalize(resolved_normal);
  let to_fragment = input.world_position - terrain.camera_fog.xyz;
  let distance_to_camera = length(to_fragment);
  let view_direction = to_fragment / max(distance_to_camera, 0.0001);
  let texture_coordinates = input.world_position.xz * terrain.material_params.x;
  let far_blend = smoothstep(40.0, 350.0, distance_to_camera);
  let far_offset = vec2<f32>(0.13, 0.71);

  let grass_near = textureSample(grass_texture, terrain_sampler,
                                 texture_coordinates).rgb;
  let grass_far = textureSample(grass_texture, terrain_sampler,
                                texture_coordinates * 0.19 + far_offset).rgb;
  var grass = mix(grass_near, grass_far, far_blend);
  let dirt_near = textureSample(dirt_texture, terrain_sampler,
                                texture_coordinates).rgb;
  let dirt_far = textureSample(dirt_texture, terrain_sampler,
                               texture_coordinates * 0.19 + far_offset).rgb;
  var dirt = mix(dirt_near, dirt_far, far_blend);
  let snow_near = textureSample(snow_texture, terrain_sampler,
                                texture_coordinates).rgb;
  let snow_far = textureSample(snow_texture, terrain_sampler,
                               texture_coordinates * 0.19 + far_offset).rgb;
  var snow = mix(snow_near, snow_far, far_blend);

  var projection_weights = abs(normal);
  projection_weights = projection_weights * projection_weights;
  projection_weights /= max(projection_weights.x + projection_weights.y +
                            projection_weights.z, 0.0001);
  let rock_scale = terrain.material_params.x * 1.7;
  let rock_x = mix(textureSample(rock_texture, terrain_sampler,
                                 input.world_position.zy * rock_scale).rgb,
                   textureSample(rock_texture, terrain_sampler,
                                 input.world_position.zy * rock_scale * 0.19 +
                                   far_offset).rgb,
                   far_blend);
  let rock_y = mix(textureSample(rock_texture, terrain_sampler,
                                 input.world_position.xz * rock_scale).rgb,
                   textureSample(rock_texture, terrain_sampler,
                                 input.world_position.xz * rock_scale * 0.19 +
                                   far_offset).rgb,
                   far_blend);
  let rock_z = mix(textureSample(rock_texture, terrain_sampler,
                                 input.world_position.xy * rock_scale).rgb,
                   textureSample(rock_texture, terrain_sampler,
                                 input.world_position.xy * rock_scale * 0.19 +
                                   far_offset).rgb,
                   far_blend);
  var rock = rock_x * projection_weights.x + rock_y * projection_weights.y +
             rock_z * projection_weights.z;

  let coarse = dot(textureSample(grass_texture, terrain_sampler,
                                 texture_coordinates * 0.083 +
                                   vec2<f32>(0.37, 0.19)).rgb,
                   vec3<f32>(0.299, 0.587, 0.114));
  let macro_variation =
    0.65 * value_noise(input.world_position.xz * 0.0027 +
                       vec2<f32>(19.1, 7.3)) +
    0.35 * value_noise(input.world_position.xz * 0.0091 +
                       vec2<f32>(3.7, 31.9));
  let jitter = coarse - 0.5;
  let height = input.normalized_height;
  let adjusted_height =
    height + 0.045 * jitter + 0.012 * (macro_variation - 0.5);
  let cliff = 1.0 - smoothstep(0.60, 0.80, normal.y + 0.06 * jitter);
  let scree = smoothstep(0.38, 0.58, adjusted_height);
  let snow_cover = smoothstep(0.55, 0.68, adjusted_height) *
                    smoothstep(0.58, 0.78, normal.y);
  let sea = terrain.terrain_scale.w;
  let beach_low = sea + 0.5 / terrain.material_params.y;
  let beach_high = sea + 3.0 / terrain.material_params.y;
  let beach = (1.0 - smoothstep(beach_low, beach_high, adjusted_height)) *
              smoothstep(0.55, 0.75, normal.y);

  let grass_value = dot(grass, vec3<f32>(0.299, 0.587, 0.114));
  grass = mix(grass,
              grass_value * authored_linear(vec3<f32>(0.68, 1.0, 0.52)),
              0.34) * 1.36;
  grass *= 0.88 + 0.55 * coarse;
  grass *= mix(vec3<f32>(0.84, 0.95, 0.76),
               vec3<f32>(1.10, 1.06, 0.92), macro_variation);
  let dirt_value = dot(dirt, vec3<f32>(0.299, 0.587, 0.114));
  dirt = mix(dirt, dirt_value * vec3<f32>(1.0, 0.96, 0.86), 0.72);
  let rock_value = dot(rock, vec3<f32>(0.299, 0.587, 0.114));
  rock = mix(rock, rock_value * vec3<f32>(0.78, 0.80, 0.79), 0.9);
  snow *= 0.88 + 0.32 * coarse;

  var material = grass;
  material = mix(material, dirt, scree);
  material = mix(material, rock, cliff);
  material = mix(material, snow, snow_cover);
  let sand = mix(dirt, dirt_value * vec3<f32>(1.12, 1.03, 0.82), 0.82);
  material = mix(material, sand, beach);

  let light_direction = normalize(terrain.sun_direction.xyz);
  let direct = clamp((dot(light_direction, normal) + 0.08) / 1.08,
                     0.0, 1.0);
  let hemisphere_mix = 0.5 + 0.5 * normal.y;
  let hemisphere = terrain.ambient.rgb *
    mix(authored_linear(vec3<f32>(0.92, 0.74, 0.58)),
        authored_linear(vec3<f32>(0.74, 0.92, 1.12)),
        hemisphere_mix);
  let diffuse = terrain.sun_diffuse.rgb * direct * 0.9 + hemisphere;
  var color = material * max(diffuse, vec3<f32>(0.08));
  let to_camera = normalize(terrain.camera_fog.xyz - input.world_position);
  let half_vector = normalize(light_direction + to_camera);
  let roughness = mix(0.92, 0.68, cliff);
  let specular = (0.012 + 0.035 * (1.0 - roughness)) *
                 pow(max(dot(normal, half_vector), 0.0),
                     mix(18.0, 72.0, 1.0 - roughness));
  color += terrain.sun_specular.rgb * specular;
  color += snow_cover * terrain.sun_specular.rgb *
           pow(max(dot(normal, half_vector), 0.0), 32.0) * 0.18;

  if (terrain.material_params.w > 0.5) {
    let cell = fract(input.grid_coord / input.lod_step);
    let width = max(fwidth(input.grid_coord / input.lod_step),
                    vec2<f32>(0.0001));
    let edge = min(min(cell.x, 1.0 - cell.x) / width.x,
                   min(cell.y, 1.0 - cell.y) / width.y);
    let line = 1.0 - smoothstep(0.45, 1.25, edge);
    color = mix(color, vec3<f32>(0.015, 0.16, 0.19), 0.82 * line);
  }

  let sun_glow = pow(max(dot(view_direction, light_direction), 0.0), 8.0);
  let fog_color = terrain.fog_color.rgb *
                  mix(vec3<f32>(1.0), vec3<f32>(1.10, 1.03, 0.91), sun_glow);
  let fog_factor = smoothstep(0.0, 0.9, input.fog);
  return vec4<f32>(display_srgb(mix(color, fog_color, fog_factor)), 1.0);
}
)wgsl";

    constexpr const char* sky_shader_source = R"wgsl(
struct SkyUniforms {
  view_proj: mat4x4<f32>,
  sun_direction: vec4<f32>,
  fog_color: vec4<f32>,
  params: vec4<f32>,
};

@group(0) @binding(0) var<uniform> sky: SkyUniforms;

struct VertexInput {
  @location(0) position: vec3<f32>,
};

struct VertexOutput {
  @builtin(position) position: vec4<f32>,
  @location(0) direction: vec3<f32>,
};

@vertex
fn sky_vertex(input: VertexInput) -> VertexOutput {
  var output: VertexOutput;
  var clip = sky.view_proj * vec4<f32>(input.position, 1.0);
  clip.z = 0.0;
  output.position = clip;
  output.direction = input.position;
  return output;
}

fn authored_linear(color: vec3<f32>) -> vec3<f32> {
  return pow(max(color, vec3<f32>(0.0)), vec3<f32>(2.2));
}

fn display_srgb(linear_color: vec3<f32>) -> vec3<f32> {
  let positive = max(linear_color, vec3<f32>(0.0));
  let low = positive * 12.92;
  let high = 1.055 * pow(positive, vec3<f32>(1.0 / 2.4)) -
             vec3<f32>(0.055);
  return select(high, low, positive <= vec3<f32>(0.0031308));
}

fn sky_hash(value: f32) -> f32 {
  return fract(sin(value) * 43758.5453);
}

fn sky_noise(position: vec3<f32>) -> f32 {
  let cell = floor(position);
  let fraction = fract(position);
  let blend = fraction * fraction *
              (vec3<f32>(3.0) - 2.0 * fraction);
  let index = cell.x + cell.y * 57.0 + cell.z * 113.0;
  let lower = mix(mix(sky_hash(index), sky_hash(index + 1.0), blend.x),
                  mix(sky_hash(index + 57.0), sky_hash(index + 58.0),
                      blend.x),
                  blend.y);
  let upper = mix(mix(sky_hash(index + 113.0), sky_hash(index + 114.0),
                      blend.x),
                  mix(sky_hash(index + 170.0), sky_hash(index + 171.0),
                      blend.x),
                  blend.y);
  return mix(lower, upper, blend.z);
}

fn sky_fbm(start: vec3<f32>) -> f32 {
  var position = start;
  var result = 0.0;
  var amplitude = 0.5;
  for (var octave = 0; octave < 4; octave = octave + 1) {
    result += amplitude * sky_noise(position);
    position = 2.03 *
      vec3<f32>(0.8 * position.x + 0.6 * position.z,
                position.y + 7.31,
                -0.6 * position.x + 0.8 * position.z);
    amplitude *= 0.5;
  }
  return result;
}

fn atmosphere(ray: vec3<f32>, sun: vec3<f32>) -> vec3<f32> {
  let daylight = smoothstep(-0.08, 0.18, sun.y);
  let golden = daylight * (1.0 - smoothstep(0.15, 0.65, sun.y));
  let zenith = abs(ray.y);
  let thickness = 1.0 - zenith;
  let day_zenith = authored_linear(vec3<f32>(0.06, 0.20, 0.55));
  let day_middle = authored_linear(vec3<f32>(0.25, 0.46, 0.78));
  let day_horizon = authored_linear(vec3<f32>(0.55, 0.68, 0.84));
  let night_zenith = authored_linear(vec3<f32>(0.004, 0.009, 0.05));
  let night_middle = authored_linear(vec3<f32>(0.015, 0.022, 0.06));
  let night_horizon = authored_linear(vec3<f32>(0.035, 0.045, 0.09));
  let horizon_color = mix(night_horizon, day_horizon, daylight);

  var color: vec3<f32>;
  if (ray.y >= 0.0) {
    let zenith_color = mix(night_zenith, day_zenith, daylight);
    let middle_color = mix(night_middle, day_middle, daylight);
    let t = pow(thickness, 0.72);
    if (t < 0.62) {
      color = mix(zenith_color, middle_color, t / 0.62);
    } else {
      color = mix(middle_color, horizon_color, (t - 0.62) / 0.38);
    }
    let horizon_band = pow(thickness, 4.0);
    color = mix(color, authored_linear(vec3<f32>(0.94, 0.52, 0.26)),
                0.16 * golden * horizon_band);
    let toward_sun = max(dot(ray, sun), 0.0);
    let broad = pow(toward_sun, 4.0);
    let tight = pow(toward_sun, 32.0);
    let scatter = mix(authored_linear(vec3<f32>(1.0, 0.96, 0.82)),
                      authored_linear(vec3<f32>(1.0, 0.55, 0.24)), golden);
    color += scatter * daylight *
             (broad * (0.07 + 0.12 * golden) +
              tight * (0.10 + 0.22 * golden) *
                (0.4 + 0.6 * horizon_band));
  } else {
    let ground = authored_linear(vec3<f32>(0.08, 0.09, 0.13));
    color = mix(ground * (0.2 + 0.8 * daylight), horizon_color,
                pow(1.0 - zenith, 8.0));
  }
  return color;
}

fn cloud_shape(position: vec3<f32>, coverage: f32, time: f32) -> f32 {
  let base = sky_fbm(position * 0.3);
  let detail = sky_fbm(position * 1.2 +
                       vec3<f32>(time * 0.05, 0.0, time * 0.03));
  let threshold = 0.4 + coverage * 0.4;
  return smoothstep(threshold, threshold + 0.13, base + detail * 0.2);
}

fn cloud_lighting(density: f32, ray: vec3<f32>,
                  sun: vec3<f32>) -> vec3<f32> {
  let daylight = smoothstep(-0.08, 0.18, sun.y);
  let golden = daylight * (1.0 - smoothstep(0.15, 0.65, sun.y));
  let sunset = golden * golden;
  let lit = mix(authored_linear(vec3<f32>(1.04, 1.03, 1.00)),
                authored_linear(vec3<f32>(1.05, 0.76, 0.50)), sunset);
  let shade = mix(authored_linear(vec3<f32>(0.55, 0.63, 0.76)),
                  authored_linear(vec3<f32>(0.48, 0.44, 0.58)), sunset);
  let core = pow(clamp(density, 0.0, 1.0), 0.75);
  var color = mix(lit, shade, 0.85 * core);
  color += lit * pow(max(dot(ray, sun), 0.0), 16.0) *
           (1.0 - core) * (0.5 + 0.7 * golden) * daylight;
  return mix(color * 0.12, color, daylight);
}

@fragment
fn sky_fragment(input: VertexOutput) -> @location(0) vec4<f32> {
  let time = sky.params.x;
  let cloudiness = sky.params.z;
  let ray = normalize(input.direction);
  let sun = normalize(sky.sun_direction.xyz);
  var color = atmosphere(ray, sun);

  if (ray.y > 0.08) {
    var cirrus_position = ray * (900.0 / ray.y);
    cirrus_position += vec3<f32>(time * 0.6, 0.0, time * 0.25);
    var streak = sky_fbm(vec3<f32>(cirrus_position.x * 0.0035,
                                   cirrus_position.y * 0.008,
                                   cirrus_position.z * 0.0065));
    streak = smoothstep(0.48, 0.85, streak) *
             smoothstep(0.08, 0.25, ray.y) *
             (0.10 + 0.08 * cloudiness);
    let daylight = smoothstep(-0.08, 0.18, sun.y);
    let cirrus_color = mix(authored_linear(vec3<f32>(0.9, 0.95, 1.05)),
                           authored_linear(vec3<f32>(1.0, 0.9, 0.8)),
                           pow(max(dot(ray, sun), 0.0), 4.0));
    color = mix(color, cirrus_color, streak * (0.25 + 0.75 * daylight));
  }

  var clouds = 0.0;
  if (ray.y > 0.05) {
    var cloud_position = ray * (200.0 / ray.y);
    cloud_position += vec3<f32>(time * 2.0, 0.0, time);
    clouds = cloud_shape(cloud_position * 0.01, cloudiness, time) *
             smoothstep(0.05, 0.1, ray.y);
    if (clouds > 0.01) {
      color = mix(color, cloud_lighting(clouds, ray, sun), clouds * 0.9);
    }
  }

  color = mix(color, sky.fog_color.rgb,
              pow(1.0 - clamp(ray.y, 0.0, 1.0), 6.0));
  let sun_dot = max(dot(ray, sun), 0.0);
  let daylight = smoothstep(-0.08, 0.18, sun.y);
  let golden = daylight * (1.0 - smoothstep(0.15, 0.65, sun.y));
  let sun_color = mix(authored_linear(vec3<f32>(1.0, 0.96, 0.84)),
                      authored_linear(vec3<f32>(1.0, 0.58, 0.28)), golden);
  let occlusion = 1.0 - clamp(clouds, 0.0, 1.0) * 0.92;
  color += (pow(sun_dot, 2600.0) * 4.05 +
            pow(sun_dot, 160.0) * 0.51) * sun_color * occlusion;
  color += pow(sun_dot, 14.0) * 0.075 * sun_color * daylight;

  if (daylight < 0.2 && ray.y > 0.0) {
    let stars = pow(sky_noise(ray * 100.0), 20.0) *
                (1.0 - daylight) * 0.3 * smoothstep(0.0, 0.4, ray.y);
    let star_color = mix(authored_linear(vec3<f32>(0.8, 0.9, 1.0)),
                         authored_linear(vec3<f32>(1.0, 0.9, 0.8)),
                         sky_noise(ray * 10.0));
    color += stars * star_color;
  }

  color += vec3<f32>((fract(sin(dot(ray.xy, vec2<f32>(12.9898, 78.233))) *
                              43758.5453) - 0.5) / 255.0);
  return vec4<f32>(display_srgb(color), 1.0);
}
)wgsl";

    struct FrameUniforms {
      Mat4 view_proj;
      Mat4 hud_proj;
      Mat4 model;
    };

    struct alignas (16) TerrainUniforms {
      Mat4 view_proj;
      std::array<float, 4> camera_fog;
      std::array<float, 4> sun_direction;
      std::array<float, 4> sun_diffuse;
      std::array<float, 4> sun_specular;
      std::array<float, 4> ambient;
      std::array<float, 4> fog_color;
      std::array<float, 4> terrain_scale;
      std::array<float, 4> material_params;
    };

    struct alignas (16) TerrainChunkUniforms {
      std::array<float, 4> origin_step;
      std::array<float, 4> morph;
      std::array<float, 4> world_offset;
    };

    struct alignas (16) SkyUniforms {
      Mat4 view_proj;
      std::array<float, 4> sun_direction;
      std::array<float, 4> fog_color;
      std::array<float, 4> params;
    };

    constexpr int terrain_lod_count = 5;
    constexpr std::array<float, terrain_lod_count> terrain_lod_steps = {
      0.25f, 1.0f, 2.0f, 4.0f, 8.0f
    };
    constexpr std::array<int, terrain_lod_count> terrain_lod_vertices = {
      513, 129, 65, 33, 17
    };
    constexpr uint32_t frame_uniform_stride = 256;
    constexpr uint32_t frame_uniform_slots = 4096;

    struct WebGpuTexture final : Texture {
      wgpu::Texture texture;
      wgpu::TextureView view;
      wgpu::Sampler sampler;
      wgpu::BindGroup bind_group;
    };

    struct WebGpuMesh final : Mesh {
      std::vector<Vertex> vertices;
      std::vector<DrawList::Run> runs;
      wgpu::Buffer vertex_buffer;
    };

    int pipeline_key (const DrawState& state, bool hud) {
      return (hud ? 1 : 0) | (state.blend ? 2 : 0) | (state.additive ? 4 : 0) |
             (state.depth_test ? 8 : 0) | (state.depth_write ? 16 : 0) |
             (state.cull ? 32 : 0);
    }

    template <typename T>
    wgpu::Buffer upload_buffer (const wgpu::Device& device,
                                const std::vector<T>& values,
                                wgpu::BufferUsage usage) {
      if (values.empty ())
        return {};
      wgpu::BufferDescriptor descriptor {};
      descriptor.size = values.size () * sizeof (T);
      descriptor.usage = usage;
      descriptor.mappedAtCreation = true;
      wgpu::Buffer buffer = device.CreateBuffer (&descriptor);
      std::memcpy (buffer.GetMappedRange (), values.data (), descriptor.size);
      buffer.Unmap ();
      return buffer;
    }

    float srgb_to_linear (uint8_t value) {
      const float encoded = value / 255.0f;
      return encoded <= 0.04045f ? encoded / 12.92f
                                 : std::pow ((encoded + 0.055f) / 1.055f, 2.4f);
    }

    float display_to_linear (float value) {
      return std::pow (std::max (value, 0.0f), 2.2f);
    }

    uint8_t linear_to_srgb (float value) {
      const float linear = std::clamp (value, 0.0f, 1.0f);
      const float encoded =
        linear <= 0.0031308f ? linear * 12.92f
                             : 1.055f * std::pow (linear, 1.0f / 2.4f) - 0.055f;
      return static_cast<uint8_t> (
        std::clamp (std::lround (encoded * 255.0f), 0l, 255l));
    }

    std::vector<uint8_t> downsample_srgba (const std::vector<uint8_t>& source,
                                           int width,
                                           int height) {
      const int next_width = std::max (1, width / 2);
      const int next_height = std::max (1, height / 2);
      std::vector<uint8_t> result (static_cast<std::size_t> (next_width) *
                                   next_height * 4);
      for (int y = 0; y < next_height; ++y)
        for (int x = 0; x < next_width; ++x) {
          std::array<float, 4> total {};
          int samples = 0;
          for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) {
              const int source_x = std::min (width - 1, x * 2 + dx);
              const int source_y = std::min (height - 1, y * 2 + dy);
              const std::size_t offset =
                (static_cast<std::size_t> (source_y) * width + source_x) * 4;
              for (int component = 0; component < 3; ++component)
                total[component] += srgb_to_linear (source[offset + component]);
              total[3] += source[offset + 3] / 255.0f;
              ++samples;
            }
          const std::size_t destination =
            (static_cast<std::size_t> (y) * next_width + x) * 4;
          for (int component = 0; component < 3; ++component)
            result[destination + component] =
              linear_to_srgb (total[component] / samples);
          result[destination + 3] = static_cast<uint8_t> (
            std::clamp (std::lround (total[3] * 255.0f / samples), 0l, 255l));
        }
      return result;
    }
  }

  struct WebGpuRenderer::State {
    std::string canvas_selector;
    int width_points;
    int height_points;
    float scale;
    int width_pixels;
    int height_pixels;

    wgpu::Instance instance;
    wgpu::Surface surface;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Queue queue;
    wgpu::TextureFormat surface_format = wgpu::TextureFormat::Undefined;
    wgpu::Texture depth_texture;
    wgpu::TextureView depth_view;

    wgpu::ShaderModule shader;
    wgpu::BindGroupLayout frame_layout;
    wgpu::BindGroupLayout texture_layout;
    wgpu::PipelineLayout pipeline_layout;
    std::map<int, wgpu::RenderPipeline> pipelines;
    std::shared_ptr<WebGpuTexture> white;

    wgpu::ShaderModule sky_shader;
    wgpu::BindGroupLayout sky_layout;
    wgpu::PipelineLayout sky_pipeline_layout;
    wgpu::RenderPipeline sky_pipeline;
    wgpu::Buffer sky_vertices;
    uint32_t sky_vertex_count = 0;

    wgpu::ShaderModule terrain_shader;
    wgpu::BindGroupLayout terrain_frame_layout;
    wgpu::BindGroupLayout terrain_chunk_layout;
    wgpu::PipelineLayout terrain_pipeline_layout;
    wgpu::RenderPipeline terrain_pipeline;
    wgpu::Texture terrain_heights;
    wgpu::TextureView terrain_height_view;
    wgpu::Texture terrain_normals;
    wgpu::TextureView terrain_normal_view;
    std::array<wgpu::Buffer, terrain_lod_count> terrain_vertices;
    std::array<wgpu::Buffer, terrain_lod_count> terrain_indices;
    std::array<uint32_t, terrain_lod_count> terrain_index_counts {};
    TerrainParams terrain_params {};
    TexturePtr terrain_grass;
    TexturePtr terrain_dirt;
    TexturePtr terrain_rock;
    TexturePtr terrain_snow;
    MeshPtr ocean_mesh;
    bool have_terrain = false;

    wgpu::SurfaceTexture surface_texture;
    wgpu::TextureView surface_view;
    wgpu::CommandEncoder command_encoder;
    wgpu::RenderPassEncoder pass;
    wgpu::Buffer frame_buffer;
    wgpu::BindGroup frame_bind_group;
    uint32_t frame_uniform_cursor = 0;
    std::vector<wgpu::Buffer> frame_vertex_buffers;
    std::vector<wgpu::Buffer> frame_uniform_buffers;
    std::vector<wgpu::BindGroup> frame_bind_groups;
    FrameParams frame_params;
    std::string device_error;
    bool frame_open = false;

    State (const char* selector, int width, int height, float factor)
        : canvas_selector (selector), width_points (width),
          height_points (height), scale (factor),
          width_pixels (std::max (1, int (width * factor))),
          height_pixels (std::max (1, int (height * factor))) {}

    void configure_surface () {
      if (!device || !surface)
        return;
      width_pixels = std::max (1, int (width_points * scale));
      height_pixels = std::max (1, int (height_points * scale));

      wgpu::SurfaceConfiguration configuration {};
      configuration.device = device;
      configuration.format = surface_format;
      configuration.usage = wgpu::TextureUsage::RenderAttachment;
      configuration.width = width_pixels;
      configuration.height = height_pixels;
      configuration.alphaMode = wgpu::CompositeAlphaMode::Opaque;
      configuration.presentMode = wgpu::PresentMode::Fifo;
      surface.Configure (&configuration);

      wgpu::TextureDescriptor depth_descriptor {};
      depth_descriptor.size = {
        static_cast<uint32_t> (width_pixels),
        static_cast<uint32_t> (height_pixels),
        1,
      };
      depth_descriptor.format = wgpu::TextureFormat::Depth32Float;
      depth_descriptor.usage = wgpu::TextureUsage::RenderAttachment;
      depth_texture = device.CreateTexture (&depth_descriptor);
      depth_view = depth_texture.CreateView ();
    }

    void build_shared_resources () {
      wgpu::ShaderSourceWGSL wgsl {};
      wgsl.code = shader_source;
      wgpu::ShaderModuleDescriptor shader_descriptor {};
      shader_descriptor.nextInChain = &wgsl;
      shader = device.CreateShaderModule (&shader_descriptor);

      wgpu::BindGroupLayoutEntry frame_entry {};
      frame_entry.binding = 0;
      frame_entry.visibility =
        wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
      frame_entry.buffer.type = wgpu::BufferBindingType::Uniform;
      frame_entry.buffer.hasDynamicOffset = true;
      frame_entry.buffer.minBindingSize = sizeof (FrameUniforms);
      wgpu::BindGroupLayoutDescriptor frame_descriptor {};
      frame_descriptor.entryCount = 1;
      frame_descriptor.entries = &frame_entry;
      frame_layout = device.CreateBindGroupLayout (&frame_descriptor);

      wgpu::BufferDescriptor frame_buffer_descriptor {};
      frame_buffer_descriptor.size = frame_uniform_stride * frame_uniform_slots;
      frame_buffer_descriptor.usage =
        wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
      frame_buffer = device.CreateBuffer (&frame_buffer_descriptor);
      wgpu::BindGroupEntry persistent_frame_entry {};
      persistent_frame_entry.binding = 0;
      persistent_frame_entry.buffer = frame_buffer;
      persistent_frame_entry.size = sizeof (FrameUniforms);
      wgpu::BindGroupDescriptor persistent_frame_descriptor {};
      persistent_frame_descriptor.layout = frame_layout;
      persistent_frame_descriptor.entryCount = 1;
      persistent_frame_descriptor.entries = &persistent_frame_entry;
      frame_bind_group = device.CreateBindGroup (&persistent_frame_descriptor);

      std::array<wgpu::BindGroupLayoutEntry, 2> texture_entries {};
      texture_entries[0].binding = 0;
      texture_entries[0].visibility = wgpu::ShaderStage::Fragment;
      texture_entries[0].texture.sampleType = wgpu::TextureSampleType::Float;
      texture_entries[0].texture.viewDimension =
        wgpu::TextureViewDimension::e2D;
      texture_entries[1].binding = 1;
      texture_entries[1].visibility = wgpu::ShaderStage::Fragment;
      texture_entries[1].sampler.type = wgpu::SamplerBindingType::Filtering;
      wgpu::BindGroupLayoutDescriptor texture_descriptor {};
      texture_descriptor.entryCount = texture_entries.size ();
      texture_descriptor.entries = texture_entries.data ();
      texture_layout = device.CreateBindGroupLayout (&texture_descriptor);

      std::array<wgpu::BindGroupLayout, 2> layouts = {
        frame_layout,
        texture_layout,
      };
      wgpu::PipelineLayoutDescriptor layout_descriptor {};
      layout_descriptor.bindGroupLayoutCount = layouts.size ();
      layout_descriptor.bindGroupLayouts = layouts.data ();
      pipeline_layout = device.CreatePipelineLayout (&layout_descriptor);

      TextureDesc white_descriptor;
      white_descriptor.width = 1;
      white_descriptor.height = 1;
      const std::array<unsigned char, 4> white_pixel = { 255, 255, 255, 255 };
      white = std::static_pointer_cast<WebGpuTexture> (
        make_texture (white_descriptor, white_pixel.data ()));
      build_sky_resources ();
      build_terrain_resources ();
    }

    void build_sky_resources () {
      wgpu::ShaderSourceWGSL wgsl {};
      wgsl.code = sky_shader_source;
      wgpu::ShaderModuleDescriptor shader_descriptor {};
      shader_descriptor.nextInChain = &wgsl;
      sky_shader = device.CreateShaderModule (&shader_descriptor);
      sky_shader.GetCompilationInfo (
        wgpu::CallbackMode::AllowSpontaneous,
        [] (wgpu::CompilationInfoRequestStatus status,
            const wgpu::CompilationInfo* info,
            State* state) {
          if (status != wgpu::CompilationInfoRequestStatus::Success || !info)
            return;
          for (size_t i = 0; i < info->messageCount; ++i) {
            const wgpu::CompilationMessage& diagnostic = info->messages[i];
            if (diagnostic.type != wgpu::CompilationMessageType::Error)
              continue;
            const std::string message (
              diagnostic.message.data ? diagnostic.message.data : "",
              diagnostic.message.length);
            state->device_error =
              "sky shader line " + std::to_string (diagnostic.lineNum) + ":" +
              std::to_string (diagnostic.linePos) + ": " + message;
            std::cerr << "moppe: " << state->device_error << std::endl;
          }
        },
        this);

      wgpu::BindGroupLayoutEntry uniform_entry {};
      uniform_entry.binding = 0;
      uniform_entry.visibility =
        wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
      uniform_entry.buffer.type = wgpu::BufferBindingType::Uniform;
      uniform_entry.buffer.minBindingSize = sizeof (SkyUniforms);
      wgpu::BindGroupLayoutDescriptor layout_descriptor {};
      layout_descriptor.entryCount = 1;
      layout_descriptor.entries = &uniform_entry;
      sky_layout = device.CreateBindGroupLayout (&layout_descriptor);
      wgpu::PipelineLayoutDescriptor pipeline_layout_descriptor {};
      pipeline_layout_descriptor.bindGroupLayoutCount = 1;
      pipeline_layout_descriptor.bindGroupLayouts = &sky_layout;
      sky_pipeline_layout =
        device.CreatePipelineLayout (&pipeline_layout_descriptor);

      wgpu::VertexAttribute attribute {};
      attribute.format = wgpu::VertexFormat::Float32x3;
      attribute.shaderLocation = 0;
      wgpu::VertexBufferLayout vertex_layout {};
      vertex_layout.arrayStride = sizeof (float) * 3;
      vertex_layout.stepMode = wgpu::VertexStepMode::Vertex;
      vertex_layout.attributeCount = 1;
      vertex_layout.attributes = &attribute;
      wgpu::ColorTargetState target {};
      target.format = surface_format;
      wgpu::FragmentState fragment {};
      fragment.module = sky_shader;
      fragment.entryPoint = "sky_fragment";
      fragment.targetCount = 1;
      fragment.targets = &target;
      wgpu::DepthStencilState depth {};
      depth.format = wgpu::TextureFormat::Depth32Float;
      depth.depthWriteEnabled = wgpu::OptionalBool::False;
      depth.depthCompare = wgpu::CompareFunction::GreaterEqual;
      wgpu::RenderPipelineDescriptor pipeline_descriptor {};
      pipeline_descriptor.layout = sky_pipeline_layout;
      pipeline_descriptor.vertex.module = sky_shader;
      pipeline_descriptor.vertex.entryPoint = "sky_vertex";
      pipeline_descriptor.vertex.bufferCount = 1;
      pipeline_descriptor.vertex.buffers = &vertex_layout;
      pipeline_descriptor.primitive.topology =
        wgpu::PrimitiveTopology::TriangleList;
      pipeline_descriptor.primitive.cullMode = wgpu::CullMode::None;
      pipeline_descriptor.fragment = &fragment;
      pipeline_descriptor.depthStencil = &depth;
      sky_pipeline = device.CreateRenderPipeline (&pipeline_descriptor);

      constexpr float radius = 4000.0f;
      constexpr int slices = 24;
      constexpr int stacks = 24;
      constexpr float pi = 3.14159265358979323846f;
      std::vector<std::array<float, 3>> vertices;
      vertices.reserve (stacks * slices * 6);
      for (int stack = 0; stack < stacks; ++stack) {
        const float altitude0 = pi * stack / stacks - pi / 2.0f;
        const float altitude1 = pi * (stack + 1) / stacks - pi / 2.0f;
        const float y0 = radius * std::sin (altitude0);
        const float y1 = radius * std::sin (altitude1);
        const float ring0 = radius * std::cos (altitude0);
        const float ring1 = radius * std::cos (altitude1);
        for (int slice = 0; slice < slices; ++slice) {
          const float azimuth0 = 2.0f * pi * slice / slices;
          const float azimuth1 = 2.0f * pi * (slice + 1) / slices;
          const std::array<std::array<float, 3>, 4> points = {
            std::array<float, 3> {
              ring0 * std::cos (azimuth0), y0, ring0 * std::sin (azimuth0) },
            std::array<float, 3> {
              ring0 * std::cos (azimuth1), y0, ring0 * std::sin (azimuth1) },
            std::array<float, 3> {
              ring1 * std::cos (azimuth0), y1, ring1 * std::sin (azimuth0) },
            std::array<float, 3> {
              ring1 * std::cos (azimuth1), y1, ring1 * std::sin (azimuth1) },
          };
          for (int index : { 0, 2, 1, 1, 2, 3 })
            vertices.push_back (points[index]);
        }
      }
      sky_vertex_count = vertices.size ();
      sky_vertices =
        upload_buffer (device, vertices, wgpu::BufferUsage::Vertex);
    }

    void build_terrain_resources () {
      wgpu::ShaderSourceWGSL wgsl {};
      wgsl.code = terrain_shader_source;
      wgpu::ShaderModuleDescriptor shader_descriptor {};
      shader_descriptor.nextInChain = &wgsl;
      terrain_shader = device.CreateShaderModule (&shader_descriptor);
      terrain_shader.GetCompilationInfo (
        wgpu::CallbackMode::AllowSpontaneous,
        [] (wgpu::CompilationInfoRequestStatus status,
            const wgpu::CompilationInfo* info,
            State* state) {
          if (status != wgpu::CompilationInfoRequestStatus::Success || !info)
            return;
          for (size_t i = 0; i < info->messageCount; ++i) {
            const wgpu::CompilationMessage& diagnostic = info->messages[i];
            if (diagnostic.type != wgpu::CompilationMessageType::Error)
              continue;
            const std::string message (
              diagnostic.message.data ? diagnostic.message.data : "",
              diagnostic.message.length);
            state->device_error =
              "terrain shader line " + std::to_string (diagnostic.lineNum) +
              ":" + std::to_string (diagnostic.linePos) + ": " + message;
            std::cerr << "moppe: " << state->device_error << std::endl;
          }
        },
        this);

      std::array<wgpu::BindGroupLayoutEntry, 8> frame_entries {};
      frame_entries[0].binding = 0;
      frame_entries[0].visibility =
        wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
      frame_entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
      frame_entries[0].buffer.minBindingSize = sizeof (TerrainUniforms);
      frame_entries[1].binding = 1;
      frame_entries[1].visibility =
        wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
      frame_entries[1].texture.sampleType =
        wgpu::TextureSampleType::UnfilterableFloat;
      frame_entries[1].texture.viewDimension = wgpu::TextureViewDimension::e2D;
      frame_entries[2].binding = 2;
      frame_entries[2].visibility =
        wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
      frame_entries[2].texture.sampleType = wgpu::TextureSampleType::Float;
      frame_entries[2].texture.viewDimension = wgpu::TextureViewDimension::e2D;
      for (uint32_t binding = 3; binding <= 6; ++binding) {
        frame_entries[binding].binding = binding;
        frame_entries[binding].visibility = wgpu::ShaderStage::Fragment;
        frame_entries[binding].texture.sampleType =
          wgpu::TextureSampleType::Float;
        frame_entries[binding].texture.viewDimension =
          wgpu::TextureViewDimension::e2D;
      }
      frame_entries[7].binding = 7;
      frame_entries[7].visibility = wgpu::ShaderStage::Fragment;
      frame_entries[7].sampler.type = wgpu::SamplerBindingType::Filtering;
      wgpu::BindGroupLayoutDescriptor frame_descriptor {};
      frame_descriptor.entryCount = frame_entries.size ();
      frame_descriptor.entries = frame_entries.data ();
      terrain_frame_layout = device.CreateBindGroupLayout (&frame_descriptor);

      wgpu::BindGroupLayoutEntry chunk_entry {};
      chunk_entry.binding = 0;
      chunk_entry.visibility = wgpu::ShaderStage::Vertex;
      chunk_entry.buffer.type = wgpu::BufferBindingType::Uniform;
      chunk_entry.buffer.minBindingSize = sizeof (TerrainChunkUniforms);
      wgpu::BindGroupLayoutDescriptor chunk_descriptor {};
      chunk_descriptor.entryCount = 1;
      chunk_descriptor.entries = &chunk_entry;
      terrain_chunk_layout = device.CreateBindGroupLayout (&chunk_descriptor);

      std::array<wgpu::BindGroupLayout, 2> layouts = {
        terrain_frame_layout,
        terrain_chunk_layout,
      };
      wgpu::PipelineLayoutDescriptor layout_descriptor {};
      layout_descriptor.bindGroupLayoutCount = layouts.size ();
      layout_descriptor.bindGroupLayouts = layouts.data ();
      terrain_pipeline_layout =
        device.CreatePipelineLayout (&layout_descriptor);

      wgpu::VertexAttribute attribute {};
      attribute.format = wgpu::VertexFormat::Float32x2;
      attribute.offset = 0;
      attribute.shaderLocation = 0;
      wgpu::VertexBufferLayout vertex_layout {};
      vertex_layout.arrayStride = sizeof (float) * 2;
      vertex_layout.stepMode = wgpu::VertexStepMode::Vertex;
      vertex_layout.attributeCount = 1;
      vertex_layout.attributes = &attribute;

      wgpu::ColorTargetState target {};
      target.format = surface_format;
      target.writeMask = wgpu::ColorWriteMask::All;
      wgpu::FragmentState fragment {};
      fragment.module = terrain_shader;
      fragment.entryPoint = "terrain_fragment";
      fragment.targetCount = 1;
      fragment.targets = &target;
      wgpu::DepthStencilState depth {};
      depth.format = wgpu::TextureFormat::Depth32Float;
      depth.depthWriteEnabled = wgpu::OptionalBool::True;
      depth.depthCompare = wgpu::CompareFunction::GreaterEqual;

      wgpu::RenderPipelineDescriptor pipeline_descriptor {};
      pipeline_descriptor.layout = terrain_pipeline_layout;
      pipeline_descriptor.vertex.module = terrain_shader;
      pipeline_descriptor.vertex.entryPoint = "terrain_vertex";
      pipeline_descriptor.vertex.bufferCount = 1;
      pipeline_descriptor.vertex.buffers = &vertex_layout;
      pipeline_descriptor.primitive.topology =
        wgpu::PrimitiveTopology::TriangleStrip;
      pipeline_descriptor.primitive.stripIndexFormat =
        wgpu::IndexFormat::Uint32;
      pipeline_descriptor.primitive.frontFace = wgpu::FrontFace::CCW;
      pipeline_descriptor.primitive.cullMode = wgpu::CullMode::Back;
      pipeline_descriptor.multisample.count = 1;
      pipeline_descriptor.fragment = &fragment;
      pipeline_descriptor.depthStencil = &depth;
      terrain_pipeline = device.CreateRenderPipeline (&pipeline_descriptor);

      for (int lod = 0; lod < terrain_lod_count; ++lod) {
        const int vertices_per_row = terrain_lod_vertices[lod];
        std::vector<std::array<float, 2>> vertices;
        vertices.reserve (vertices_per_row * vertices_per_row);
        for (int z = 0; z < vertices_per_row; ++z)
          for (int x = 0; x < vertices_per_row; ++x)
            vertices.push_back (
              { static_cast<float> (x), static_cast<float> (z) });
        terrain_vertices[lod] =
          upload_buffer (device, vertices, wgpu::BufferUsage::Vertex);

        std::vector<uint32_t> indices;
        const int cells = vertices_per_row - 1;
        indices.reserve (cells * (vertices_per_row * 2 + 1));
        for (int z = 0; z < cells; ++z) {
          for (int x = 0; x < vertices_per_row; ++x) {
            indices.push_back (z * vertices_per_row + x);
            indices.push_back ((z + 1) * vertices_per_row + x);
          }
          indices.push_back (0xFFFFFFFFu);
        }
        terrain_index_counts[lod] = indices.size ();
        terrain_indices[lod] =
          upload_buffer (device, indices, wgpu::BufferUsage::Index);
      }
    }

    TexturePtr make_texture (const TextureDesc& descriptor,
                             const void* pixels) {
      if (!device)
        throw std::logic_error ("WebGPU texture created before device");
      auto result = std::make_shared<WebGpuTexture> ();
      result->width = descriptor.width;
      result->height = descriptor.height;

      std::vector<uint8_t> base (static_cast<std::size_t> (descriptor.width) *
                                 descriptor.height * 4);
      const auto* source = static_cast<const uint8_t*> (pixels);
      if (descriptor.format == TextureFormat::RGB8) {
        for (int i = 0; i < descriptor.width * descriptor.height; ++i) {
          base[i * 4 + 0] = source[i * 3 + 0];
          base[i * 4 + 1] = source[i * 3 + 1];
          base[i * 4 + 2] = source[i * 3 + 2];
          base[i * 4 + 3] = 255;
        }
      } else {
        std::memcpy (base.data (), pixels, base.size ());
      }

      const bool generate_mips = descriptor.filter == TextureFilter::Mipmap;
      std::vector<std::vector<uint8_t>> levels;
      levels.push_back (std::move (base));
      int level_width = descriptor.width;
      int level_height = descriptor.height;
      while (generate_mips && (level_width > 1 || level_height > 1)) {
        levels.push_back (
          downsample_srgba (levels.back (), level_width, level_height));
        level_width = std::max (1, level_width / 2);
        level_height = std::max (1, level_height / 2);
      }

      wgpu::TextureDescriptor texture_descriptor {};
      texture_descriptor.size = {
        static_cast<uint32_t> (descriptor.width),
        static_cast<uint32_t> (descriptor.height),
        1,
      };
      texture_descriptor.format = generate_mips
                                    ? wgpu::TextureFormat::RGBA8UnormSrgb
                                    : wgpu::TextureFormat::RGBA8Unorm;
      texture_descriptor.mipLevelCount = levels.size ();
      texture_descriptor.usage =
        wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
      result->texture = device.CreateTexture (&texture_descriptor);
      result->view = result->texture.CreateView ();

      level_width = descriptor.width;
      level_height = descriptor.height;
      for (uint32_t level = 0; level < levels.size (); ++level) {
        wgpu::TexelCopyTextureInfo destination {};
        destination.texture = result->texture;
        destination.mipLevel = level;
        wgpu::TexelCopyBufferLayout layout {};
        layout.bytesPerRow = level_width * 4;
        layout.rowsPerImage = level_height;
        const wgpu::Extent3D extent = {
          static_cast<uint32_t> (level_width),
          static_cast<uint32_t> (level_height),
          1,
        };
        queue.WriteTexture (&destination,
                            levels[level].data (),
                            levels[level].size (),
                            &layout,
                            &extent);
        level_width = std::max (1, level_width / 2);
        level_height = std::max (1, level_height / 2);
      }

      wgpu::SamplerDescriptor sampler_descriptor {};
      const bool nearest = descriptor.filter == TextureFilter::Nearest;
      sampler_descriptor.magFilter =
        nearest ? wgpu::FilterMode::Nearest : wgpu::FilterMode::Linear;
      sampler_descriptor.minFilter = sampler_descriptor.magFilter;
      sampler_descriptor.mipmapFilter = nearest
                                          ? wgpu::MipmapFilterMode::Nearest
                                          : wgpu::MipmapFilterMode::Linear;
      const wgpu::AddressMode address = descriptor.wrap == TextureWrap::Repeat
                                          ? wgpu::AddressMode::Repeat
                                          : wgpu::AddressMode::ClampToEdge;
      sampler_descriptor.addressModeU = address;
      sampler_descriptor.addressModeV = address;
      sampler_descriptor.maxAnisotropy = static_cast<uint16_t> (
        std::clamp (std::lround (descriptor.max_anisotropy), 1l, 16l));
      result->sampler = device.CreateSampler (&sampler_descriptor);

      std::array<wgpu::BindGroupEntry, 2> entries {};
      entries[0].binding = 0;
      entries[0].textureView = result->view;
      entries[1].binding = 1;
      entries[1].sampler = result->sampler;
      wgpu::BindGroupDescriptor bind_group_descriptor {};
      bind_group_descriptor.layout = texture_layout;
      bind_group_descriptor.entryCount = entries.size ();
      bind_group_descriptor.entries = entries.data ();
      result->bind_group = device.CreateBindGroup (&bind_group_descriptor);
      return result;
    }

    wgpu::RenderPipeline pipeline_for (const DrawState& state, bool hud) {
      const int key = pipeline_key (state, hud);
      if (const auto found = pipelines.find (key); found != pipelines.end ())
        return found->second;

      std::array<wgpu::VertexAttribute, 4> attributes {};
      attributes[0].format = wgpu::VertexFormat::Float32x3;
      attributes[0].offset = 0;
      attributes[0].shaderLocation = 0;
      attributes[1].format = wgpu::VertexFormat::Float32x3;
      attributes[1].offset = 12;
      attributes[1].shaderLocation = 1;
      attributes[2].format = wgpu::VertexFormat::Float32x2;
      attributes[2].offset = 24;
      attributes[2].shaderLocation = 2;
      attributes[3].format = wgpu::VertexFormat::Unorm8x4;
      attributes[3].offset = 32;
      attributes[3].shaderLocation = 3;
      wgpu::VertexBufferLayout vertex_layout {};
      vertex_layout.arrayStride = sizeof (Vertex);
      vertex_layout.stepMode = wgpu::VertexStepMode::Vertex;
      vertex_layout.attributeCount = attributes.size ();
      vertex_layout.attributes = attributes.data ();

      wgpu::BlendComponent color_blend {};
      color_blend.operation = wgpu::BlendOperation::Add;
      color_blend.srcFactor = wgpu::BlendFactor::SrcAlpha;
      color_blend.dstFactor = state.additive
                                ? wgpu::BlendFactor::One
                                : wgpu::BlendFactor::OneMinusSrcAlpha;
      wgpu::BlendComponent alpha_blend {};
      alpha_blend.operation = wgpu::BlendOperation::Add;
      alpha_blend.srcFactor = wgpu::BlendFactor::One;
      alpha_blend.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
      wgpu::BlendState blend { color_blend, alpha_blend };

      wgpu::ColorTargetState target {};
      target.format = surface_format;
      if (hud || state.blend || state.additive)
        target.blend = &blend;
      target.writeMask = wgpu::ColorWriteMask::All;

      wgpu::FragmentState fragment {};
      fragment.module = shader;
      fragment.entryPoint = "color_fragment";
      fragment.targetCount = 1;
      fragment.targets = &target;

      wgpu::DepthStencilState depth {};
      depth.format = wgpu::TextureFormat::Depth32Float;
      depth.depthWriteEnabled = !hud && state.depth_write
                                  ? wgpu::OptionalBool::True
                                  : wgpu::OptionalBool::False;
      depth.depthCompare = hud || !state.depth_test
                             ? wgpu::CompareFunction::Always
                             : wgpu::CompareFunction::GreaterEqual;

      wgpu::RenderPipelineDescriptor descriptor {};
      descriptor.layout = pipeline_layout;
      descriptor.vertex.module = shader;
      descriptor.vertex.entryPoint = hud ? "hud_vertex" : "world_vertex";
      descriptor.vertex.bufferCount = 1;
      descriptor.vertex.buffers = &vertex_layout;
      descriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
      descriptor.primitive.frontFace = wgpu::FrontFace::CCW;
      descriptor.primitive.cullMode =
        !hud && state.cull ? wgpu::CullMode::Back : wgpu::CullMode::None;
      descriptor.multisample.count = 1;
      descriptor.fragment = &fragment;
      descriptor.depthStencil = &depth;

      wgpu::RenderPipeline pipeline = device.CreateRenderPipeline (&descriptor);
      pipelines.emplace (key, pipeline);
      return pipeline;
    }

    void play_buffer (const wgpu::Buffer& vertex_buffer,
                      std::size_t vertex_count,
                      const std::vector<DrawList::Run>& runs,
                      bool hud,
                      const Mat4& model) {
      if (!frame_open || !vertex_buffer || vertex_count == 0)
        return;
      if (frame_uniform_cursor >= frame_uniform_slots)
        throw std::runtime_error ("WebGPU frame uniform ring exhausted");
      const FrameUniforms uniforms {
        .view_proj = frame_params.proj * frame_params.view,
        .hud_proj = Mat4::hud_ortho (static_cast<float> (width_points),
                                     static_cast<float> (height_points)),
        .model = model,
      };
      const uint32_t uniform_offset =
        frame_uniform_cursor++ * frame_uniform_stride;
      queue.WriteBuffer (
        frame_buffer, uniform_offset, &uniforms, sizeof (uniforms));
      pass.SetVertexBuffer (0, vertex_buffer);
      pass.SetBindGroup (0, frame_bind_group, 1, &uniform_offset);

      for (const DrawList::Run& run : runs) {
        if (run.count == 0)
          continue;
        pass.SetPipeline (pipeline_for (run.state, hud));
        const auto* texture = static_cast<const WebGpuTexture*> (run.texture);
        if (!texture)
          texture = white.get ();
        pass.SetBindGroup (1, texture->bind_group);
        pass.Draw (run.count, 1, run.first, 0);
      }
    }

    void play (const std::vector<Vertex>& vertices,
               const std::vector<DrawList::Run>& runs,
               bool hud) {
      if (!frame_open || vertices.empty ())
        return;
      wgpu::Buffer vertex_buffer =
        upload_buffer (device, vertices, wgpu::BufferUsage::Vertex);
      frame_vertex_buffers.push_back (vertex_buffer);
      play_buffer (
        vertex_buffer, vertices.size (), runs, hud, Mat4::identity ());
    }
  };

  WebGpuRenderer::WebGpuRenderer (const char* canvas_selector,
                                  int width_points,
                                  int height_points,
                                  float scale_factor)
      : m_state (std::make_unique<State> (
          canvas_selector, width_points, height_points, scale_factor)) {
    m_state->instance = wgpu::Instance::Acquire (wgpuCreateInstance (nullptr));
    wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvas {};
    canvas.selector = m_state->canvas_selector.c_str ();
    wgpu::SurfaceDescriptor surface_descriptor {};
    surface_descriptor.nextInChain = &canvas;
    m_state->surface = m_state->instance.CreateSurface (&surface_descriptor);
  }

  WebGpuRenderer::~WebGpuRenderer () = default;

  void WebGpuRenderer::initialize (Ready ready) {
    wgpu::RequestAdapterOptions options {};
    options.compatibleSurface = m_state->surface;
    options.powerPreference = wgpu::PowerPreference::HighPerformance;
    m_state->instance.RequestAdapter (
      &options,
      wgpu::CallbackMode::AllowSpontaneous,
      [this, ready = std::move (ready)] (wgpu::RequestAdapterStatus status,
                                         wgpu::Adapter adapter,
                                         wgpu::StringView message) mutable {
        if (status != wgpu::RequestAdapterStatus::Success) {
          ready (
            false,
            std::string (message.data ? message.data : "", message.length));
          return;
        }
        m_state->adapter = std::move (adapter);
        wgpu::DeviceDescriptor descriptor {};
        descriptor.SetUncapturedErrorCallback (
          [] (const wgpu::Device&,
              wgpu::ErrorType type,
              wgpu::StringView error,
              State* state) {
            const std::string message (error.data ? error.data : "",
                                       error.length);
            if (state->device_error.empty ())
              state->device_error = message;
            std::cerr << "moppe: WebGPU error " << static_cast<int> (type)
                      << ": " << message << std::endl;
          },
          m_state.get ());
        m_state->adapter.RequestDevice (
          &descriptor,
          wgpu::CallbackMode::AllowSpontaneous,
          [this, ready = std::move (ready)] (wgpu::RequestDeviceStatus result,
                                             wgpu::Device device,
                                             wgpu::StringView error) mutable {
            if (result != wgpu::RequestDeviceStatus::Success) {
              ready (false,
                     std::string (error.data ? error.data : "", error.length));
              return;
            }
            m_state->device = std::move (device);
            m_state->queue = m_state->device.GetQueue ();
            wgpu::SurfaceCapabilities capabilities {};
            m_state->surface.GetCapabilities (m_state->adapter, &capabilities);
            if (capabilities.formatCount == 0) {
              ready (false, "WebGPU surface has no color formats");
              return;
            }
            m_state->surface_format = capabilities.formats[0];
            m_state->configure_surface ();
            m_state->build_shared_resources ();
            ready (true, {});
          });
      });
  }

  void WebGpuRenderer::resize (int width, int height, float factor) {
    m_state->width_points = std::max (1, width);
    m_state->height_points = std::max (1, height);
    m_state->scale = std::max (1.0f, factor);
    m_state->configure_surface ();
  }

  TexturePtr WebGpuRenderer::create_texture (const TextureDesc& descriptor,
                                             const void* pixels) {
    return m_state->make_texture (descriptor, pixels);
  }

  MeshPtr WebGpuRenderer::create_mesh (const DrawList& recorded) {
    auto mesh = std::make_shared<WebGpuMesh> ();
    mesh->vertices = recorded.vertices ();
    mesh->runs = recorded.runs ();
    mesh->vertex_buffer = upload_buffer (
      m_state->device, mesh->vertices, wgpu::BufferUsage::Vertex);
    return mesh;
  }

  void WebGpuRenderer::set_terrain (const TerrainParams& params,
                                    const float* heights,
                                    const Vec3* normals) {
    if (params.width < 2 || params.height < 2 || !heights)
      throw std::invalid_argument ("invalid WebGPU terrain raster");
    m_state->terrain_params = params;

    wgpu::TextureDescriptor height_descriptor {};
    height_descriptor.size = {
      static_cast<uint32_t> (params.width),
      static_cast<uint32_t> (params.height),
      1,
    };
    height_descriptor.format = wgpu::TextureFormat::R32Float;
    height_descriptor.usage =
      wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    m_state->terrain_heights =
      m_state->device.CreateTexture (&height_descriptor);
    m_state->terrain_height_view = m_state->terrain_heights.CreateView ();
    wgpu::TexelCopyTextureInfo height_destination {};
    height_destination.texture = m_state->terrain_heights;
    wgpu::TexelCopyBufferLayout height_layout {};
    height_layout.bytesPerRow = params.width * sizeof (float);
    height_layout.rowsPerImage = params.height;
    const wgpu::Extent3D extent = {
      static_cast<uint32_t> (params.width),
      static_cast<uint32_t> (params.height),
      1,
    };
    m_state->queue.WriteTexture (&height_destination,
                                 heights,
                                 params.width * params.height * sizeof (float),
                                 &height_layout,
                                 &extent);

    std::vector<int8_t> packed_normals (
      static_cast<std::size_t> (params.width) * params.height * 4);
    for (std::size_t i = 0;
         i < static_cast<std::size_t> (params.width) * params.height;
         ++i) {
      const Vec3 normal = normals ? normals[i] : Vec3 (0.0f, 1.0f, 0.0f);
      for (int component = 0; component < 3; ++component) {
        const float value = std::clamp (normal[component], -1.0f, 1.0f);
        packed_normals[i * 4 + component] =
          static_cast<int8_t> (std::round (value * 127.0f));
      }
      packed_normals[i * 4 + 3] = 127;
    }
    wgpu::TextureDescriptor normal_descriptor = height_descriptor;
    normal_descriptor.format = wgpu::TextureFormat::RGBA8Snorm;
    m_state->terrain_normals =
      m_state->device.CreateTexture (&normal_descriptor);
    m_state->terrain_normal_view = m_state->terrain_normals.CreateView ();
    wgpu::TexelCopyTextureInfo normal_destination {};
    normal_destination.texture = m_state->terrain_normals;
    wgpu::TexelCopyBufferLayout normal_layout {};
    normal_layout.bytesPerRow = params.width * 4;
    normal_layout.rowsPerImage = params.height;
    m_state->queue.WriteTexture (&normal_destination,
                                 packed_normals.data (),
                                 packed_normals.size (),
                                 &normal_layout,
                                 &extent);
    m_state->have_terrain = true;
  }
  void WebGpuRenderer::set_terrain_topology_overlay (bool enabled) {
    m_state->terrain_params.topology_overlay = enabled;
  }
  void WebGpuRenderer::set_terrain_textures (TexturePtr grass,
                                             TexturePtr dirt,
                                             TexturePtr rock,
                                             TexturePtr snow) {
    m_state->terrain_grass = std::move (grass);
    m_state->terrain_dirt = std::move (dirt);
    m_state->terrain_rock = std::move (rock);
    m_state->terrain_snow = std::move (snow);
  }
  void WebGpuRenderer::set_terrain_overlay (const TerrainOverlayParams&,
                                            std::span<const float>) {}
  void WebGpuRenderer::clear_terrain_overlay () {}
  void WebGpuRenderer::render_terrain_shadow (const Mat4&) {}
  void WebGpuRenderer::set_ocean (const OceanSetup& setup,
                                  std::span<const float>) {
    DrawList water;
    DrawState state;
    state.blend = true;
    state.cull = false;
    water.state (state);
    water.lit (false);
    water.color (0.08f, 0.38f, 0.52f, 0.72f);
    water.normal (Vec3 (0.0f, 1.0f, 0.0f));
    const float x0 = setup.center[0] - setup.half_extent;
    const float x1 = setup.center[0] + setup.half_extent;
    const float z0 = setup.center[2] - setup.half_extent;
    const float z1 = setup.center[2] + setup.half_extent;
    water.begin (Prim::Triangles);
    water.vertex (x0, setup.level, z0);
    water.vertex (x0, setup.level, z1);
    water.vertex (x1, setup.level, z0);
    water.vertex (x1, setup.level, z0);
    water.vertex (x0, setup.level, z1);
    water.vertex (x1, setup.level, z1);
    water.end ();
    m_state->ocean_mesh = create_mesh (water);
  }

  bool WebGpuRenderer::begin_frame (const FrameParams& params) {
    if (!m_state->device_error.empty ())
      throw std::runtime_error (m_state->device_error);
    if (!m_state->device || m_state->frame_open)
      return false;
    m_state->surface_texture = {};
    m_state->surface.GetCurrentTexture (&m_state->surface_texture);
    if (!m_state->surface_texture.texture)
      return false;
    m_state->surface_view = m_state->surface_texture.texture.CreateView ();
    m_state->command_encoder = m_state->device.CreateCommandEncoder ();
    m_state->frame_params = params;
    m_state->frame_vertex_buffers.clear ();
    m_state->frame_uniform_buffers.clear ();
    m_state->frame_bind_groups.clear ();
    m_state->frame_uniform_cursor = 0;

    wgpu::RenderPassColorAttachment color {};
    color.view = m_state->surface_view;
    color.loadOp = wgpu::LoadOp::Clear;
    color.storeOp = wgpu::StoreOp::Store;
    color.clearValue = {
      params.clear_color.red,
      params.clear_color.green,
      params.clear_color.blue,
      1.0,
    };
    wgpu::RenderPassDepthStencilAttachment depth {};
    depth.view = m_state->depth_view;
    depth.depthClearValue = 0.0f;
    depth.depthLoadOp = wgpu::LoadOp::Clear;
    depth.depthStoreOp = wgpu::StoreOp::Store;
    wgpu::RenderPassDescriptor pass_descriptor {};
    pass_descriptor.colorAttachmentCount = 1;
    pass_descriptor.colorAttachments = &color;
    pass_descriptor.depthStencilAttachment = &depth;
    m_state->pass = m_state->command_encoder.BeginRenderPass (&pass_descriptor);
    m_state->frame_open = true;
    return true;
  }

  void WebGpuRenderer::draw_terrain (const ChunkDraw* chunks, int count) {
    if (!m_state->frame_open || !m_state->have_terrain || count < 1)
      return;
    const FrameParams& frame = m_state->frame_params;
    const TerrainParams& terrain = m_state->terrain_params;
    const TerrainUniforms uniforms {
      .view_proj = frame.proj * frame.view,
      .camera_fog = { frame.camera_pos[0],
                      frame.camera_pos[1],
                      frame.camera_pos[2],
                      terrain.fog_scale },
      .sun_direction = { frame.sun_dir[0],
                         frame.sun_dir[1],
                         frame.sun_dir[2],
                         0.0f },
      .sun_diffuse = { display_to_linear (frame.sun_diffuse.red),
                       display_to_linear (frame.sun_diffuse.green),
                       display_to_linear (frame.sun_diffuse.blue),
                       1.0f },
      .sun_specular = { display_to_linear (frame.sun_specular.red),
                        display_to_linear (frame.sun_specular.green),
                        display_to_linear (frame.sun_specular.blue),
                        1.0f },
      .ambient = { display_to_linear (frame.ambient.red),
                   display_to_linear (frame.ambient.green),
                   display_to_linear (frame.ambient.blue),
                   1.0f },
      .fog_color = { display_to_linear (frame.clear_color.red),
                     display_to_linear (frame.clear_color.green),
                     display_to_linear (frame.clear_color.blue),
                     1.0f },
      .terrain_scale = { terrain.scale[0],
                         terrain.scale[1],
                         terrain.scale[2],
                         terrain.sea_level_norm },
      .material_params = { terrain.tex_scale,
                           terrain.height_scale,
                           terrain.fragment_normals ? 1.0f : 0.0f,
                           terrain.topology_overlay ? 1.0f : 0.0f },
    };
    std::vector<TerrainUniforms> terrain_uniforms { uniforms };
    wgpu::Buffer terrain_buffer = upload_buffer (
      m_state->device, terrain_uniforms, wgpu::BufferUsage::Uniform);
    m_state->frame_uniform_buffers.push_back (terrain_buffer);
    std::array<wgpu::BindGroupEntry, 8> terrain_entries {};
    terrain_entries[0].binding = 0;
    terrain_entries[0].buffer = terrain_buffer;
    terrain_entries[0].size = sizeof (TerrainUniforms);
    terrain_entries[1].binding = 1;
    terrain_entries[1].textureView = m_state->terrain_height_view;
    terrain_entries[2].binding = 2;
    terrain_entries[2].textureView = m_state->terrain_normal_view;
    const auto grass =
      m_state->terrain_grass
        ? std::static_pointer_cast<WebGpuTexture> (m_state->terrain_grass)
        : m_state->white;
    const auto dirt =
      m_state->terrain_dirt
        ? std::static_pointer_cast<WebGpuTexture> (m_state->terrain_dirt)
        : m_state->white;
    const auto rock =
      m_state->terrain_rock
        ? std::static_pointer_cast<WebGpuTexture> (m_state->terrain_rock)
        : m_state->white;
    const auto snow =
      m_state->terrain_snow
        ? std::static_pointer_cast<WebGpuTexture> (m_state->terrain_snow)
        : m_state->white;
    terrain_entries[3].binding = 3;
    terrain_entries[3].textureView = grass->view;
    terrain_entries[4].binding = 4;
    terrain_entries[4].textureView = dirt->view;
    terrain_entries[5].binding = 5;
    terrain_entries[5].textureView = rock->view;
    terrain_entries[6].binding = 6;
    terrain_entries[6].textureView = snow->view;
    terrain_entries[7].binding = 7;
    terrain_entries[7].sampler = grass->sampler;
    wgpu::BindGroupDescriptor terrain_descriptor {};
    terrain_descriptor.layout = m_state->terrain_frame_layout;
    terrain_descriptor.entryCount = terrain_entries.size ();
    terrain_descriptor.entries = terrain_entries.data ();
    wgpu::BindGroup terrain_group =
      m_state->device.CreateBindGroup (&terrain_descriptor);
    m_state->frame_bind_groups.push_back (terrain_group);

    m_state->pass.SetPipeline (m_state->terrain_pipeline);
    m_state->pass.SetBindGroup (0, terrain_group);
    for (int i = 0; i < count; ++i) {
      const int requested_lod = static_cast<int> (chunks[i].lod);
      const int lod = std::clamp (requested_lod, 0, terrain_lod_count - 1);
      const float parent_step = lod + 1 < terrain_lod_count
                                  ? terrain_lod_steps[lod + 1]
                                  : terrain_lod_steps[lod];
      const TerrainChunkUniforms chunk_uniforms {
        .origin_step = { static_cast<float> (chunks[i].x0),
                         static_cast<float> (chunks[i].z0),
                         terrain_lod_steps[lod],
                         parent_step },
        .morph = { chunks[i].morph_start, chunks[i].morph_end, 0.0f, 0.0f },
        .world_offset = { chunks[i].offset_x, 0.0f, chunks[i].offset_z, 0.0f },
      };
      std::vector<TerrainChunkUniforms> chunk_values { chunk_uniforms };
      wgpu::Buffer chunk_buffer = upload_buffer (
        m_state->device, chunk_values, wgpu::BufferUsage::Uniform);
      m_state->frame_uniform_buffers.push_back (chunk_buffer);
      wgpu::BindGroupEntry chunk_entry {};
      chunk_entry.binding = 0;
      chunk_entry.buffer = chunk_buffer;
      chunk_entry.size = sizeof (TerrainChunkUniforms);
      wgpu::BindGroupDescriptor chunk_descriptor {};
      chunk_descriptor.layout = m_state->terrain_chunk_layout;
      chunk_descriptor.entryCount = 1;
      chunk_descriptor.entries = &chunk_entry;
      wgpu::BindGroup chunk_group =
        m_state->device.CreateBindGroup (&chunk_descriptor);
      m_state->frame_bind_groups.push_back (chunk_group);
      m_state->pass.SetBindGroup (1, chunk_group);
      m_state->pass.SetVertexBuffer (0, m_state->terrain_vertices[lod]);
      m_state->pass.SetIndexBuffer (m_state->terrain_indices[lod],
                                    wgpu::IndexFormat::Uint32);
      m_state->pass.DrawIndexed (m_state->terrain_index_counts[lod]);
    }
  }
  void WebGpuRenderer::draw_sky (const SkyParams& params) {
    if (!m_state->frame_open || !m_state->sky_pipeline ||
        !m_state->sky_vertices)
      return;
    Mat4 view_rotation = m_state->frame_params.view;
    view_rotation.m[12] = 0.0f;
    view_rotation.m[13] = 0.0f;
    view_rotation.m[14] = 0.0f;
    const SkyUniforms uniforms {
      .view_proj = m_state->frame_params.proj * view_rotation,
      .sun_direction = { params.sun_dir[0],
                         params.sun_dir[1],
                         params.sun_dir[2],
                         0.0f },
      .fog_color = { display_to_linear (params.fog_color.red),
                     display_to_linear (params.fog_color.green),
                     display_to_linear (params.fog_color.blue),
                     1.0f },
      .params = { params.time, params.sun_height, params.cloudiness, 0.0f },
    };
    std::vector<SkyUniforms> values { uniforms };
    wgpu::Buffer uniform_buffer =
      upload_buffer (m_state->device, values, wgpu::BufferUsage::Uniform);
    m_state->frame_uniform_buffers.push_back (uniform_buffer);
    wgpu::BindGroupEntry entry {};
    entry.binding = 0;
    entry.buffer = uniform_buffer;
    entry.size = sizeof (SkyUniforms);
    wgpu::BindGroupDescriptor descriptor {};
    descriptor.layout = m_state->sky_layout;
    descriptor.entryCount = 1;
    descriptor.entries = &entry;
    wgpu::BindGroup group = m_state->device.CreateBindGroup (&descriptor);
    m_state->frame_bind_groups.push_back (group);
    m_state->pass.SetPipeline (m_state->sky_pipeline);
    m_state->pass.SetBindGroup (0, group);
    m_state->pass.SetVertexBuffer (0, m_state->sky_vertices);
    m_state->pass.Draw (m_state->sky_vertex_count);
  }
  void WebGpuRenderer::draw_ocean (const OceanParams& params) {
    if (!m_state->ocean_mesh)
      return;
    draw_mesh (*m_state->ocean_mesh, Mat4::translation (params.world_offset));
  }
  void WebGpuRenderer::draw_rivers (const Mesh& mesh, const Mat4& model) {
    draw_mesh (mesh, model);
  }

  void WebGpuRenderer::draw_mesh (const Mesh& mesh, const Mat4& model) {
    const auto& source = static_cast<const WebGpuMesh&> (mesh);
    m_state->play_buffer (
      source.vertex_buffer, source.vertices.size (), source.runs, false, model);
  }

  void WebGpuRenderer::draw_list (const DrawList& list) {
    m_state->play (list.vertices (), list.runs (), false);
  }

  void WebGpuRenderer::apply_underwater (float) {}
  void WebGpuRenderer::apply_motion_blur (float) {}
  void WebGpuRenderer::apply_scene_blur () {}

  void WebGpuRenderer::draw_hud (const DrawList& list) {
    m_state->play (list.vertices (), list.runs (), true);
  }

  void WebGpuRenderer::end_frame () {
    if (!m_state->frame_open)
      return;
    m_state->pass.End ();
    wgpu::CommandBuffer command = m_state->command_encoder.Finish ();
    m_state->queue.Submit (1, &command);
    m_state->frame_open = false;
    m_state->pass = {};
    m_state->command_encoder = {};
    m_state->surface_view = {};
    m_state->surface_texture = {};
  }

  int WebGpuRenderer::width_pts () const {
    return m_state->width_points;
  }
  int WebGpuRenderer::height_pts () const {
    return m_state->height_points;
  }
  float WebGpuRenderer::scale_factor () const {
    return m_state->scale;
  }
}
