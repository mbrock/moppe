// One terrain-bound grass medium, evaluated two ways.
//
// The undergrowth mesh shader realizes the resolved fraction as individual
// blades. The terrain fragment shader integrates the complementary fraction
// into an effective fibrous material. Both begin with this same habitat,
// optical state, and projected-size partition; neither owns a second grass.
#ifndef MOPPE_GRASS_MEDIUM_H
#define MOPPE_GRASS_MEDIUM_H

#include "common.h"

#define MOPPE_GRASS_BLADE_WIDTH_METRES 0.018f

struct MoppeGrassMedium {
  // Leaf area is the conserved population measure. Cover is its bounded
  // optical consequence rather than a separately authored density field.
  float leaf_area;
  float cover;
  float clump;
  float moisture;
  float riparian;
  float3 blade_tint;
};

inline float moppe_grass_clump (float2 world_xz) {
  return 0.55 * moppe_value_noise (world_xz * 0.085) +
         0.45 * moppe_value_noise (world_xz * 0.021 + float2 (17.3, 4.1));
}

// All arguments are already semantic readings. Texture layout and sampling
// remain with each evaluator; the medium owns what those readings mean for
// grass. signed_water_depth is positive under water and negative on land.
inline MoppeGrassMedium moppe_grass_medium (float2 world_xz,
                                            float moisture,
                                            float forest_cover,
                                            float2 worn,
                                            float ground_up,
                                            float snow_support,
                                            float relative_height,
                                            float signed_water_depth,
                                            float density_scale) {
  MoppeGrassMedium grass;
  grass.clump = moppe_grass_clump (world_xz);
  grass.moisture = saturate (moisture);

  const float canopy = saturate (forest_cover);
  const float light = 1.0 - 0.30 * smoothstep (0.32, 0.92, canopy);
  const float damp = 0.75 + 0.25 * smoothstep (0.02, 0.48, grass.moisture);
  const float standable = smoothstep (0.52, 0.78, ground_up);
  const float cleared =
    1.0 - saturate (max (saturate (worn.x), saturate (worn.y)) * 1.6);
  const float variation = 0.88 + 0.24 * smoothstep (0.18, 0.72, grass.clump);
  const float snow_habitat = smoothstep (0.55, 0.68, relative_height) *
                             smoothstep (0.58, 0.78, snow_support);
  const float alpine_survival = 1.0 - smoothstep (0.50, 0.67, relative_height);
  const float dry_ground = 1.0 - smoothstep (0.002, 0.030, signed_water_depth);
  const float shore = 1.0 - smoothstep (0.05, 1.35, abs (signed_water_depth));
  grass.riparian = shore * dry_ground * smoothstep (0.52, 0.80, ground_up);

  const float rooted = light * damp * standable * cleared * variation *
                       alpine_survival * (1.0 - snow_habitat) * dry_ground *
                       (1.0 + 0.18 * grass.riparian);
  grass.leaf_area = saturate (rooted * density_scale);
  grass.cover = smoothstep (0.10, 0.82, grass.leaf_area);

  grass.blade_tint = float3 (0.185, 0.315, 0.112);
  grass.blade_tint *= float3 (1.12 - 0.24 * grass.moisture,
                              0.84 + 0.30 * grass.moisture,
                              0.82 + 0.22 * grass.moisture);
  grass.blade_tint *=
    mix (float3 (1.0), float3 (0.82, 1.10, 0.88), grass.riparian);
  return grass;
}

inline float moppe_grass_blade_pixels (float focal_pixels, float distance) {
  return MOPPE_GRASS_BLADE_WIDTH_METRES * focal_pixels / max (distance, 0.5);
}

// Geometry owns only blades wide enough to remain a repeatable image feature.
// Everything it releases is integrated by the terrain material. The overlap
// is a partition of leaf area, not two unrelated cross-fades.
inline float moppe_grass_resolved_fraction (float blade_pixels) {
  return smoothstep (0.16, 0.95, blade_pixels);
}

inline float moppe_grass_integrated_fraction (float blade_pixels) {
  return 1.0 - moppe_grass_resolved_fraction (blade_pixels);
}

inline float3 moppe_grass_chlorophyll () {
  return float3 (0.92, 1.0, 0.24);
}

inline float moppe_grass_toward_lobe (float toward) {
  return 0.20 + 0.80 * toward * toward * toward;
}

inline float
moppe_grass_leaf_back (float3 normal, float3 sun, float resolvable) {
  return mix (0.30, pow (max (dot (-normal, sun), 0.0), 1.8), resolvable);
}

inline float moppe_grass_gust (float2 world_xz, float time) {
  const float phase = world_xz.x * 0.043 + world_xz.y * 0.051;
  return sin (time * 1.13 + phase) +
         0.45 * sin (time * 2.63 + phase * 1.7 + 1.3);
}

inline float3 moppe_grass_ensemble_axis (float2 world_xz, float time) {
  const float gust = moppe_grass_gust (world_xz, time);
  return normalize (float3 (0.0, 1.0, 0.0) +
                    float3 (0.79, 0.0, 0.53) * (0.14 * gust));
}

#endif
