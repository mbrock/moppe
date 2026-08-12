// Quantities shared by resolved crowns and their stand-scale quotient. The
// aggregate begins only when a mean crown is becoming a small image feature;
// its spatial values come from a CPU raster of the actual ForestInstances.
#ifndef MOPPE_FOREST_MEDIUM_H
#define MOPPE_FOREST_MEDIUM_H

#include "common.h"

inline uint moppe_forest_mix (uint value) {
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  value ^= value >> 16;
  return value;
}

inline float moppe_forest_hash (uint seed, uint lane) {
  return float (moppe_forest_mix (seed ^ lane * 0x9e3779b9u) & 0x00ffffffu) /
         float (0x01000000u);
}

inline float3 moppe_forest_conifer_tint (float moisture, float closure) {
  return float3 (0.148, 0.280, 0.152) * float3 (1.08 - 0.20 * moisture,
                                                0.88 + 0.26 * moisture,
                                                0.90 + 0.16 * closure);
}

inline float moppe_forest_aggregate_fraction (float focal_pixels,
                                              float distance_metres) {
  const float crown_pixels = MOPPE_FOREST_MEAN_CROWN_DIAMETER_METRES *
                             focal_pixels / max (distance_metres, 1.0);
  // The quotient begins while one eight-metre reconstruction cell still has
  // a repeatable low-frequency footprint. It becomes authoritative before a
  // mean crown reaches its four-to-five-pixel individual retirement band, so
  // closure is already present rather than arriving after the spikes leave.
  return 1.0 - smoothstep (3.5, 16.0, crown_pixels);
}

inline float moppe_forest_aggregate_fraction_pixels (float crown_pixels) {
  return 1.0 - smoothstep (3.5, 16.0, crown_pixels);
}

inline float moppe_forest_stand_support (float closure) {
  // An aggregate surface is truthful for a closed canopy, not for open
  // woodland. This value is sampled over a 24-metre neighbourhood, where
  // twelve-percent sustained crown coverage already distinguishes a stand
  // from isolated organisms and forty-two percent is connected canopy. Fine
  // closure still controls every local hole. The broad interval keeps the
  // ecological boundary from becoming a new contour.
  return smoothstep (0.12, 0.42, closure);
}

#endif
