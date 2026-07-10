#include <metal_stdlib>
#include "../../terrain/metal/field_shader_types.h"

using namespace metal;

// Specialization bakes only stable buffer slots into the stitched function.
// Parameter values and seeded permutation data remain ordinary mutable GPU
// buffers, so changing a recipe does not require rebuilding the pipeline.
constant uint moppe_field_slot [[function_constant(0)]];

[[stitchable]]
float moppe_field_parameter (device const float* parameters) {
  return parameters[moppe_field_slot];
}

[[stitchable]]
float moppe_field_x (float2 position) {
  return position.x;
}

[[stitchable]]
float moppe_field_y (float2 position) {
  return position.y;
}

[[stitchable]]
float moppe_field_finish
  (float value, float2, device const float*,
   device const MoppeFieldNoise*, device const int*) {
  return value;
}

[[stitchable]]
float moppe_field_add (float left, float right) {
  return left + right;
}

[[stitchable]]
float moppe_field_subtract (float left, float right) {
  return left - right;
}

[[stitchable]]
float moppe_field_multiply (float left, float right) {
  return left * right;
}

[[stitchable]]
float moppe_field_multiply_add (float multiplier, float multiplicand,
				float addend) {
  return fma (multiplier, multiplicand, addend);
}

[[stitchable]]
float moppe_field_sine (float value) {
  return sin (value);
}

[[stitchable]]
float moppe_field_smoothstep (float edge0, float edge1, float value) {
  return smoothstep (edge0, edge1, value);
}

inline int moppe_field_wrap_lattice (int value, int period) {
  const int wrapped = value % period;
  return select (wrapped, wrapped + period, wrapped < 0) & 255;
}

inline float moppe_field_fade (float value) {
  return value * value * value
    * (value * (value * 6.0f - 15.0f) + 10.0f);
}

inline float moppe_field_gradient (int hash, float x, float y) {
  switch (hash & 7) {
  case 0:  return  x + y;
  case 1:  return  x - y;
  case 2:  return -x + y;
  case 3:  return -x - y;
  case 4:  return  x;
  case 5:  return -x;
  case 6:  return  y;
  default: return -y;
  }
}

inline float moppe_field_perlin_value
  (float x, float y, int period_x, int period_y, uint table_offset,
   device const int* permutations) {
  const float floor_x = floor (x);
  const float floor_y = floor (y);
  const int xi = moppe_field_wrap_lattice (int (floor_x), period_x);
  const int yi = moppe_field_wrap_lattice (int (floor_y), period_y);
  const int xj = moppe_field_wrap_lattice (int (floor_x) + 1, period_x);
  const int yj = moppe_field_wrap_lattice (int (floor_y) + 1, period_y);
  const float xf = x - floor_x;
  const float yf = y - floor_y;
  const float u = moppe_field_fade (xf);
  const float v = moppe_field_fade (yf);

  device const int* table = permutations + table_offset;
  const int aa = table[table[xi] + yi];
  const int ab = table[table[xi] + yj];
  const int ba = table[table[xj] + yi];
  const int bb = table[table[xj] + yj];
  const float low = mix
    (moppe_field_gradient (aa, xf, yf),
     moppe_field_gradient (ba, xf - 1.0f, yf), u);
  const float high = mix
    (moppe_field_gradient (ab, xf, yf - 1.0f),
     moppe_field_gradient (bb, xf - 1.0f, yf - 1.0f), u);
  return mix (low, high, v);
}

[[stitchable]]
float moppe_field_perlin
  (float x, float y, device const MoppeFieldNoise* noises,
   device const int* permutations) {
  const MoppeFieldNoise noise = noises[moppe_field_slot];
  return moppe_field_perlin_value
    (x, y, noise.period_x, noise.period_y,
     noise.permutation_offset, permutations);
}

[[stitchable]]
float moppe_field_fbm
  (float x, float y, device const MoppeFieldNoise* noises,
   device const int* permutations) {
  const MoppeFieldNoise noise = noises[moppe_field_slot];
  float sum = 0.0f;
  float amplitude = 1.0f;
  float frequency = 1.0f;
  float norm = 0.0f;
  for (int octave = 0; octave < noise.octaves; ++octave) {
    sum += amplitude * moppe_field_perlin_value
      (x * frequency, y * frequency, 256, 256,
       noise.permutation_offset, permutations);
    norm += amplitude;
    amplitude *= noise.gain;
    frequency *= noise.lacunarity;
  }
  return sum / norm;
}

[[stitchable]]
float moppe_field_ridged
  (float x, float y, device const MoppeFieldNoise* noises,
   device const int* permutations) {
  const MoppeFieldNoise noise = noises[moppe_field_slot];
  float sum = 0.0f;
  float amplitude = 0.5f;
  float frequency = 1.0f;
  float weight = 1.0f;
  float norm = 0.0f;
  for (int octave = 0; octave < noise.octaves; ++octave) {
    float value = 1.0f - abs (moppe_field_perlin_value
      (x * frequency, y * frequency, 256, 256,
       noise.permutation_offset, permutations));
    value *= value;
    value *= weight;
    weight = clamp (value * 2.0f, 0.0f, 1.0f);
    sum += value * amplitude;
    norm += amplitude;
    amplitude *= noise.gain;
    frequency *= noise.lacunarity;
  }
  return sum / norm;
}

[[stitchable]]
float moppe_field_periodic_fbm
  (float x, float y, device const MoppeFieldNoise* noises,
   device const int* permutations) {
  const MoppeFieldNoise noise = noises[moppe_field_slot];
  float sum = 0.0f;
  float amplitude = 1.0f;
  float frequency = 1.0f;
  float norm = 0.0f;
  int period_x = noise.period_x;
  int period_y = noise.period_y;
  const int lacunarity = int (noise.lacunarity);
  for (int octave = 0; octave < noise.octaves; ++octave) {
    sum += amplitude * moppe_field_perlin_value
      (x * frequency, y * frequency, period_x, period_y,
       noise.permutation_offset, permutations);
    norm += amplitude;
    amplitude *= noise.gain;
    frequency *= noise.lacunarity;
    period_x *= lacunarity;
    period_y *= lacunarity;
  }
  return sum / norm;
}

[[stitchable]]
float moppe_field_periodic_ridged
  (float x, float y, device const MoppeFieldNoise* noises,
   device const int* permutations) {
  const MoppeFieldNoise noise = noises[moppe_field_slot];
  float sum = 0.0f;
  float amplitude = 0.5f;
  float frequency = 1.0f;
  float weight = 1.0f;
  float norm = 0.0f;
  int period_x = noise.period_x;
  int period_y = noise.period_y;
  const int lacunarity = int (noise.lacunarity);
  for (int octave = 0; octave < noise.octaves; ++octave) {
    float value = 1.0f - abs (moppe_field_perlin_value
      (x * frequency, y * frequency, period_x, period_y,
       noise.permutation_offset, permutations));
    value *= value;
    value *= weight;
    weight = clamp (value * 2.0f, 0.0f, 1.0f);
    sum += value * amplitude;
    norm += amplitude;
    amplitude *= noise.gain;
    frequency *= noise.lacunarity;
    period_x *= lacunarity;
    period_y *= lacunarity;
  }
  return sum / norm;
}

[[visible]]
float moppe_evaluate_field
  (float2 position, device const float* parameters,
   device const MoppeFieldNoise* noises,
   device const int* permutations);

kernel void moppe_materialize_field
  (device float* output [[buffer(MOPPE_FIELD_OUTPUT_BUFFER)]],
   constant MoppeFieldDomain& domain [[buffer(MOPPE_FIELD_DOMAIN_BUFFER)]],
   device const float* parameters
     [[buffer(MOPPE_FIELD_PARAMETER_BUFFER)]],
   device const MoppeFieldNoise* noises
     [[buffer(MOPPE_FIELD_NOISE_BUFFER)]],
   device const int* permutations
     [[buffer(MOPPE_FIELD_PERMUTATION_BUFFER)]],
   uint2 position [[thread_position_in_grid]]) {
  if (position.x >= domain.width || position.y >= domain.height)
    return;
  const float fx = float (position.x) / float (domain.width - 1);
  const float fy = float (position.y) / float (domain.height - 1);
  const float2 sample_position = float2
    (mix (domain.min_x, domain.max_x, fx),
     mix (domain.min_y, domain.max_y, fy));
  output[position.y * domain.width + position.x] = moppe_evaluate_field
    (sample_position, parameters, noises, permutations);
}
