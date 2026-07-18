#include "../../terrain/metal/orogeny_shader_types.h"
#include <metal_stdlib>

using namespace metal;

namespace {
  float normalized_angle (float radians) {
    constexpr float turn = 2.0f * M_PI_F;
    radians = fmod (radians, turn);
    return radians < 0.0f ? radians + turn : radians;
  }

  bool neighbour_index (uint cell,
                        int columns,
                        int rows,
                        constant MoppeOrogenyParameters& parameters,
                        thread uint& result) {
    const int width = int (parameters.width);
    const int height = int (parameters.height);
    int x = int (cell % parameters.width) + columns;
    int y = int (cell / parameters.width) + rows;
    if (parameters.periodic) {
      x = (x % width + width) % width;
      y = (y % height + height) % height;
    } else if (x < 0 || y < 0 || x >= width || y >= height) {
      return false;
    }
    result = uint (y) * parameters.width + uint (x);
    return true;
  }

  float route_score (float slope,
                     float2 direction,
                     float2 previous,
                     float persistence) {
    const float previous_length_squared = dot (previous, previous);
    if (previous_length_squared <= 1e-12f)
      return slope;
    const float alignment = clamp (
      dot (direction, previous) / sqrt (previous_length_squared), -1.0f, 1.0f);
    return slope * (1.0f + persistence * alignment);
  }
}

kernel void moppe_select_d_infinity_routes (
  device const float* levels [[buffer (MOPPE_OROGENY_LEVEL_BUFFER)]],
  device const MoppeOrogenyTangent* tangents
  [[buffer (MOPPE_OROGENY_TANGENT_BUFFER)]],
  device const uchar* active [[buffer (MOPPE_OROGENY_ACTIVE_BUFFER)]],
  device MoppeOrogenyRoute* routes [[buffer (MOPPE_OROGENY_ROUTE_BUFFER)]],
  constant MoppeOrogenyParameters& parameters
  [[buffer (MOPPE_OROGENY_PARAMETER_BUFFER)]],
  constant MoppeOrogenyStencil& stencil
  [[buffer (MOPPE_OROGENY_STENCIL_BUFFER)]],
  uint cell [[thread_position_in_grid]]) {
  const uint count = parameters.width * parameters.height;
  if (cell >= count)
    return;

  MoppeOrogenyRoute best {};
  best.receiver0 = 0xffffffffu;
  best.receiver1 = 0xffffffffu;
  if (!active[cell]) {
    routes[cell] = best;
    return;
  }

  const float center_m = levels[cell] * parameters.height_scale_m;
  const MoppeOrogenyTangent tangent = tangents[cell];
  const float2 previous = parameters.has_previous_tangent
                            ? float2 (tangent.x, tangent.z)
                            : float2 (0.0f);
  float best_score = 0.0f;

  for (uint i = 0; i < 8; ++i) {
    const MoppeOrogenyNeighbour geometry = stencil.neighbours[i];
    uint receiver = 0;
    if (!neighbour_index (
          cell, geometry.columns, geometry.rows, parameters, receiver))
      continue;
    const float slope =
      (center_m - levels[receiver] * parameters.height_scale_m) /
      geometry.distance_m;
    const float score = route_score (slope,
                                     float2 (geometry.unit_x, geometry.unit_z),
                                     previous,
                                     parameters.persistence);
    if (score > best_score) {
      best_score = score;
      best.receiver0 = receiver;
      best.receiver1 = 0xffffffffu;
      best.arc_count = 1;
      best.fraction0 = 1.0f;
      best.fraction1 = 0.0f;
      best.interpolation = 0.0f;
      best.run_m = geometry.distance_m;
      best.direction = geometry.direction;
      best.slope = slope;
    }
  }

  for (uint i = 0; i < 8; ++i) {
    const MoppeOrogenyFacet geometry = stencil.facets[i];
    uint cardinal = 0;
    uint diagonal = 0;
    if (!neighbour_index (cell,
                          geometry.cardinal_x,
                          geometry.cardinal_y,
                          parameters,
                          cardinal) ||
        !neighbour_index (
          cell, geometry.diagonal_x, geometry.diagonal_y, parameters, diagonal))
      continue;
    const float cardinal_m = levels[cardinal] * parameters.height_scale_m;
    const float diagonal_m = levels[diagonal] * parameters.height_scale_m;
    const float s1 = (center_m - cardinal_m) / geometry.d1_m;
    const float s2 = (cardinal_m - diagonal_m) / geometry.d2_m;
    if (s1 <= 0.0f || s2 <= 0.0f)
      continue;

    const float relative = atan2 (s2, s1);
    if (!(relative > 0.0f && relative < geometry.extent))
      continue;
    const float facet_slope = length (float2 (s1, s2));
    const float direction_x =
      cos (relative) * geometry.u1_x + sin (relative) * geometry.u2_x;
    const float direction_z =
      cos (relative) * geometry.u1_z + sin (relative) * geometry.u2_z;
    const float direction = normalized_angle (atan2 (direction_z, direction_x));
    const float score = route_score (facet_slope,
                                     float2 (cos (direction), sin (direction)),
                                     previous,
                                     parameters.persistence);
    if (score <= best_score)
      continue;
    best_score = score;
    best.receiver0 = cardinal;
    best.receiver1 = diagonal;
    best.arc_count = 2;
    best.fraction1 = relative / geometry.extent;
    best.fraction0 = 1.0f - best.fraction1;
    best.interpolation =
      clamp (geometry.d1_m * tan (relative) / geometry.d2_m, 0.0f, 1.0f);
    best.run_m = geometry.d1_m / cos (relative);
    best.direction = direction;
    best.slope = facet_slope;
  }
  routes[cell] = best;
}
