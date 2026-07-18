// Data ABI shared by the Metal D-infinity route kernel and its host backend.
// Members stay scalar and naturally aligned so C++ and MSL agree directly.

#ifndef MOPPE_OROGENY_SHADER_TYPES_H
#define MOPPE_OROGENY_SHADER_TYPES_H

#ifdef __METAL_VERSION__
#include <metal_stdlib>
typedef metal::uint MoppeOrogenyUInt;
typedef int MoppeOrogenyInt;
#else
#include <cstdint>
typedef std::uint32_t MoppeOrogenyUInt;
typedef std::int32_t MoppeOrogenyInt;
#endif

#define MOPPE_OROGENY_LEVEL_BUFFER 0
#define MOPPE_OROGENY_TANGENT_BUFFER 1
#define MOPPE_OROGENY_ACTIVE_BUFFER 2
#define MOPPE_OROGENY_ROUTE_BUFFER 3
#define MOPPE_OROGENY_PARAMETER_BUFFER 4
#define MOPPE_OROGENY_STENCIL_BUFFER 5

struct MoppeOrogenyParameters {
  MoppeOrogenyUInt width;
  MoppeOrogenyUInt height;
  MoppeOrogenyUInt periodic;
  MoppeOrogenyUInt has_previous_tangent;
  float height_scale_m;
  float persistence;
  float padding0;
  float padding1;
};

struct MoppeOrogenyTangent {
  float x;
  float y;
  float z;
  float padding;
};

struct MoppeOrogenyNeighbour {
  MoppeOrogenyInt columns;
  MoppeOrogenyInt rows;
  float distance_m;
  float direction;
  float unit_x;
  float unit_z;
};

struct MoppeOrogenyFacet {
  MoppeOrogenyInt cardinal_x;
  MoppeOrogenyInt cardinal_y;
  MoppeOrogenyInt diagonal_x;
  MoppeOrogenyInt diagonal_y;
  float d1_m;
  float d2_m;
  float extent;
  float u1_x;
  float u1_z;
  float u2_x;
  float u2_z;
};

struct MoppeOrogenyStencil {
  MoppeOrogenyNeighbour neighbours[8];
  MoppeOrogenyFacet facets[8];
};

struct MoppeOrogenyRoute {
  MoppeOrogenyUInt receiver0;
  MoppeOrogenyUInt receiver1;
  MoppeOrogenyUInt arc_count;
  MoppeOrogenyUInt padding0;
  float fraction0;
  float fraction1;
  float interpolation;
  float run_m;
  float direction;
  float slope;
  float padding1;
  float padding2;
};

#ifndef __METAL_VERSION__
static_assert (sizeof (MoppeOrogenyParameters) == 32);
static_assert (sizeof (MoppeOrogenyTangent) == 16);
static_assert (sizeof (MoppeOrogenyNeighbour) == 24);
static_assert (sizeof (MoppeOrogenyFacet) == 44);
static_assert (sizeof (MoppeOrogenyStencil) == 544);
static_assert (sizeof (MoppeOrogenyRoute) == 48);
#endif

#endif
