// One terrain-bound grass medium, evaluated two ways.
//
// The undergrowth mesh shader realizes the resolved fraction as individual
// blades. The terrain fragment shader keeps the habitat-coloured substrate
// beneath them. Both begin with the same medium, but only geometry is
// projected-size dependent: ground colour must remain stable when a blade
// turns edge-on.
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
  float forest_cover;
  float riparian;
  float3 blade_tint;
};

// Stable identity for plant-scale lattices. Cells near the origin sit at
// negative coordinates; two's-complement wrap keeps the hash well defined
// there, and the same mix serves every population that hashes world cells.
inline float moppe_plant_hash (uint2 cell, uint lane) {
  uint value = cell.x * 0x9e3779b9u ^ cell.y * 0x85ebca6bu ^ lane * 0xc2b2ae35u;
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  value ^= value >> 16;
  return float (value & 0x00ffffffu) / float (0x01000000u);
}

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
  grass.forest_cover = canopy;
  // A closed canopy starves the sward hard rather than merely trimming it:
  // the forest floor is sparse grass with room for its own populations,
  // not a shaded copy of the meadow outside.
  const float light = 1.0 - 0.55 * smoothstep (0.28, 0.90, canopy);
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

// ---- flowering drifts ----------------------------------------------
//
// Meadow flowers arrive in single-species colonies, never as confetti: a
// coarse world-anchored lattice decides where a drift lies and which
// species it is, and a finer noise shapes the drift's edge. Flowers are
// the highest-contrast thing the floor ever carries, so their colour must
// be organized by these continuous fields or a receding meadow turns to
// glitter. Both evaluators read the same drift -- the undergrowth mesh
// shader realizes it as flower shoots, and the terrain substrate keeps its
// colour wash after the heads have retired -- so a flowering hillside
// stays flowering from the glider.

struct MoppeFlowerDrift {
  float presence; // 0..1: how strongly this ground blooms
  float3 tint;    // display-space petal colour of the local species
  float head;     // petal-head half-size in metres
  float stem;     // species stem height over the sward, as a factor
};

inline MoppeFlowerDrift moppe_flower_drift_species (float choice) {
  MoppeFlowerDrift drift;
  if (choice < 0.30) { // oxeye daisy
    drift.tint = float3 (0.93, 0.93, 0.86);
    drift.head = 0.026;
    drift.stem = 1.15;
  } else if (choice < 0.56) { // buttercup
    drift.tint = float3 (0.97, 0.78, 0.14);
    drift.head = 0.016;
    drift.stem = 0.95;
  } else if (choice < 0.80) { // harebell
    drift.tint = float3 (0.44, 0.46, 0.88);
    drift.head = 0.019;
    drift.stem = 1.02;
  } else { // red campion
    drift.tint = float3 (0.88, 0.46, 0.62);
    drift.head = 0.020;
    drift.stem = 1.08;
  }
  return drift;
}

inline MoppeFlowerDrift moppe_flower_drift (float2 world_xz,
                                            float moisture,
                                            float forest_cover,
                                            float leaf_area) {
  // Species ownership lives on an 11-metre lattice, warped so no colony
  // border follows a straight line. The border noise shares the warp, so
  // a species boundary and a drift edge cannot slide apart.
  const float wander = moppe_value_noise (world_xz * 0.117);
  const float2 warped = world_xz + (wander - 0.5) * float2 (7.9, -6.1);
  const float2 cell = floor (warped / 11.0);
  const uint2 id = uint2 (int2 (cell));

  MoppeFlowerDrift drift =
    moppe_flower_drift_species (moppe_plant_hash (id, 29u));

  // This is flowering country: most open patches bloom at least somewhat,
  // and a fortunate patch blooms wall to wall. The threshold its edge
  // noise must clear keeps colonies dense where they occur rather than
  // thinly everywhere, but poverty is the exception, not the rule.
  const float rich = moppe_plant_hash (id, 31u);
  const float field = moppe_value_noise (warped * 0.22);
  const float colony =
    smoothstep (mix (0.70, 0.36, rich), mix (0.85, 0.54, rich), field);

  // Flowers stand in living grass on open ground. Deep shade belongs to
  // the ferns and standing water to the riparian grasses, but a damp
  // meadow blooms as readily as a dry one.
  const float open_sky = 1.0 - smoothstep (0.10, 0.45, forest_cover);
  const float damp_band = smoothstep (0.06, 0.20, moisture) *
                          (1.0 - smoothstep (0.82, 0.99, moisture));
  const float sward = smoothstep (0.12, 0.40, leaf_area);
  drift.presence = colony * open_sky * damp_band * sward;
  return drift;
}

inline float moppe_grass_blade_pixels (float focal_pixels, float distance) {
  return MOPPE_GRASS_BLADE_WIDTH_METRES * focal_pixels / max (distance, 0.5);
}

// Geometry owns only blades wide enough to remain a repeatable image feature.
// Blades that are too narrow to remain repeatable retire into the stable
// habitat-coloured terrain substrate.
inline float moppe_grass_resolved_fraction (float blade_pixels) {
  return smoothstep (0.16, 0.95, blade_pixels);
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
