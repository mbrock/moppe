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

struct MoppeForestEnsembleLight {
  float3 radiance;
  float coverage;
};

// The resolved crown and the stand quotient must converge to one material,
// not merely to two greens that look related. This is the ensemble limit of
// the conifer leaf path: one display-space population tint, a normal
// distribution broader than a solid roof, wrapped direct light, and the
// same restrained chlorophyll transmission lobe. Callers decide how much
// optical depth the view ray accumulated; this function decides what that
// first visible foliage presents.
inline MoppeForestEnsembleLight
moppe_forest_distribution_light (float3 leaf_display,
                                 float3 distribution_normal,
                                 float3 sun_dir,
                                 float3 to_eye,
                                 float3 sun_diffuse,
                                 float3 ambient,
                                 float sun_visibility,
                                 float coverage,
                                 float grain) {
  MoppeForestEnsembleLight response;
  response.coverage = saturate (coverage);
  const float3 normal = normalize (distribution_normal);
  const float3 leaf = moppe_srgb (leaf_display);
  const float wrap = saturate ((dot (normal, sun_dir) + 0.30) / 1.30);
  response.radiance = leaf * grain *
                      (moppe_hemisphere_light (ambient, normal) * 0.68 +
                       sun_diffuse * wrap * sun_visibility * 0.68);

  const float back =
    pow (max (dot (-normal, sun_dir), 0.0), 1.4) * sun_visibility;
  const float toward = saturate (dot (to_eye, -sun_dir));
  response.radiance += sqrt (leaf) * sun_diffuse * float3 (0.92, 1.0, 0.28) *
                       back * (0.08 + 0.26 * toward * toward);
  return response;
}

inline MoppeForestEnsembleLight
moppe_forest_ensemble_light (float moisture,
                             float closure,
                             float3 distribution_normal,
                             float3 sun_dir,
                             float3 to_eye,
                             float3 sun_diffuse,
                             float3 ambient,
                             float sun_visibility,
                             float coverage,
                             float grain) {
  return moppe_forest_distribution_light (
    moppe_forest_conifer_tint (moisture, closure),
    distribution_normal,
    sun_dir,
    to_eye,
    sun_diffuse,
    ambient,
    sun_visibility,
    coverage,
    grain);
}

inline float moppe_forest_aggregate_fraction (float focal_pixels,
                                              float distance_metres) {
  const float crown_pixels = MOPPE_FOREST_MEAN_CROWN_DIAMETER_METRES *
                             focal_pixels / max (distance_metres, 1.0);
  // Stand-scale closure is already repeatable while a crown is in the
  // bundled-bough register. Let the quotient fill the unrepresented gaps
  // behind those resolved silhouettes instead of waiting until the forest
  // has become a regiment of porous cones.
  return 1.0 - smoothstep (8.0, 32.0, crown_pixels);
}

inline float moppe_forest_identity_transfer (float crown_pixels) {
  // This is deliberately the same interval as the aggregate arrival above.
  // In a closed stand, redundant individual identity yields exactly as its
  // population representation becomes available. A separate later transfer
  // left a conspicuous register in which rows of solid cone proxies sat in
  // front of an already complete canopy. Stand support still keeps isolated
  // trees fully individual.
  return 1.0 - smoothstep (8.0, 32.0, crown_pixels);
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
