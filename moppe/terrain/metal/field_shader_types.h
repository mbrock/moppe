// Data ABI shared by the stitched field primitives and their host backend.
// Keep members scalar and naturally aligned so C++ and MSL agree directly.

#ifndef MOPPE_FIELD_SHADER_TYPES_H
#define MOPPE_FIELD_SHADER_TYPES_H

#ifdef __METAL_VERSION__
#include <metal_stdlib>
typedef metal::uint MoppeFieldUInt;
typedef int MoppeFieldInt;
#else
#include <cstdint>
typedef std::uint32_t MoppeFieldUInt;
typedef std::int32_t MoppeFieldInt;
#endif

#define MOPPE_FIELD_OUTPUT_BUFFER       0
#define MOPPE_FIELD_DOMAIN_BUFFER       1
#define MOPPE_FIELD_PARAMETER_BUFFER    2
#define MOPPE_FIELD_NOISE_BUFFER        3
#define MOPPE_FIELD_PERMUTATION_BUFFER  4

struct MoppeFieldDomain {
  MoppeFieldUInt width;
  MoppeFieldUInt height;
  float min_x;
  float max_x;
  float min_y;
  float max_y;
};

struct MoppeFieldNoise {
  MoppeFieldUInt permutation_offset;
  MoppeFieldInt period_x;
  MoppeFieldInt period_y;
  MoppeFieldInt octaves;
  float lacunarity;
  float gain;
};

#ifndef __METAL_VERSION__
static_assert (sizeof (MoppeFieldDomain) == 24);
static_assert (sizeof (MoppeFieldNoise) == 24);
#endif

#endif
