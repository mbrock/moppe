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
#define MOPPE_GRASS_TEXTURE_LINEAR_MEAN_LUMA 0.0902f

struct MoppeGrassMedium {
  // Leaf area is the conserved upper-leaf population measure. Basal cover is
  // the short turf and litter immediately above the soil; upper cover is the
  // vertical stand's top-view optical consequence. Neither is a second
  // habitat decision.
  float leaf_area;
  float cover;
  float basal_cover;
  float canopy_height;
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
  // Closure is the plan-view optical consequence of the retained crowns.
  // Recover the light reaching several overlapping conifer layers with a
  // Beer--Lambert-style power of the open fraction. The small floor keeps
  // shade-tolerant plants possible without turning a closed stand into the
  // same sward as the meadow outside.
  const float light = max (0.07, pow (1.0 - canopy, 3.2));
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
  // These are Beer--Lambert limits of one population, not separately painted
  // masks. The basal mat is denser in plan view than the upright upper leaves.
  grass.cover = 1.0 - exp (-1.85 * grass.leaf_area);
  grass.basal_cover = 1.0 - exp (-2.65 * grass.leaf_area);
  grass.canopy_height =
    MOPPE_SWARD_ENSEMBLE_HEIGHT_METRES * (0.72 + 0.22 * grass.moisture) *
    (0.88 + 0.12 * grass.clump) *
    mix (0.36, 1.0, smoothstep (0.06, 0.46, grass.leaf_area));

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

// ---- the level-of-detail ladder ------------------------------------
//
// Geometry owns only features wide enough to remain a repeatable image
// feature, and each family measures its OWN signature feature: the blade
// its width, the flower its head, the fern its frond. A family therefore
// descends the ladder -- resolved individuals, then ensemble geometry,
// then the substrate -- on its own schedule, and the meadow does not lose
// its flowers at the distance it loses its blades. What retires always
// dissolves into the stable habitat-coloured terrain substrate, whose
// grass cover and drift wash are the ladder's last rung.
inline float moppe_grass_resolved_fraction (float blade_pixels) {
  return smoothstep (0.16, 0.95, blade_pixels);
}

inline float moppe_sward_texture_detail (float3 linear_sample) {
  const float luma = dot (linear_sample, float3 (0.299, 0.587, 0.114));
  return clamp (luma / MOPPE_GRASS_TEXTURE_LINEAR_MEAN_LUMA, 0.62, 1.55);
}

inline float moppe_feature_pixels (float feature_metres,
                                   float focal_pixels,
                                   float distance) {
  return feature_metres * focal_pixels / max (distance, 0.5);
}

inline float moppe_flower_resolved_fraction (float head_pixels) {
  return smoothstep (0.45, 1.5, head_pixels);
}

// The middle rung is a continuous canopy surface, not a distance exception
// or a second population of coarse plants. It rises from the medium only as
// individual blades cease to be repeatable and sinks back into the integrated
// material once the sward's whole vertical extent is subpixel.
inline float moppe_sward_canopy_fraction (float blade_pixels,
                                          float focal_pixels,
                                          float distance_m) {
  const float fine = moppe_grass_resolved_fraction (blade_pixels);
  const float height_pixels = moppe_feature_pixels (
    MOPPE_SWARD_ENSEMBLE_HEIGHT_METRES, focal_pixels, distance_m);
  const float canopy_resolved = smoothstep (0.28, 1.15, height_pixels);
  return (1.0 - fine) * canopy_resolved;
}

#define MOPPE_FERN_FROND_WIDTH_METRES 0.13f

inline float moppe_fern_resolved_fraction (float frond_pixels) {
  return smoothstep (0.45, 1.5, frond_pixels);
}

// The chromaticity a drift keeps in the terrain substrate. A retiring
// head collapses toward this same colour, so the hand-off from geometry
// to substrate has no seam for a sample to rediscover.
inline float3 moppe_flower_wash_tint (float3 species_tint) {
  const float luma = dot (species_tint, float3 (0.299, 0.587, 0.114));
  return mix (float3 (luma), species_tint, 0.55);
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

// The blade fragment's own resolvable->0 limits, evaluated as a ground
// material: what one square metre of settled sward presents when no
// individual blade is wide enough to matter. Exposure sits at the field
// mean the far blades settle to, backlight at its ensemble floor, glint
// at zero. The terrain substrate colours grassy ground with THIS, so a
// retiring sward hands off between two evaluations of one formula
// rather than between two materials that merely resemble each other.
struct MoppeSwardOpticalResponse {
  float3 radiance;
  float coverage;
  float transmittance;
  float optical_depth;
};

// Mean projected area of the upright leaf-normal distribution, multiplied by
// the finite path through a clump. The lateral correlation length caps the
// grazing path smoothly: an infinite plane of grass must not become an
// infinite black slab at the horizon.
inline float moppe_sward_projected_path (float3 axis, float3 ray, float clump) {
  const float mu = abs (dot (normalize (axis), normalize (ray)));
  const float across = sqrt (saturate (1.0 - mu * mu));
  const float projected_area = mix (0.22, 0.68, across);
  const float finite_path = mix (2.55, 3.15, saturate (clump));
  const float limited = projected_area / finite_path;
  return projected_area / sqrt (mu * mu + limited * limited);
}

inline float moppe_sward_mean_transmission (float optical_depth) {
  return optical_depth < 0.001 ? 1.0
                               : (1.0 - exp (-optical_depth)) / optical_depth;
}

inline float moppe_sward_extinction (float leaf_area, float clump) {
  return 3.65 * saturate (leaf_area) * mix (0.88, 1.12, saturate (clump));
}

inline float moppe_sward_optical_depth (float3 axis,
                                        float3 ray,
                                        float leaf_area,
                                        float clump,
                                        float density_fraction) {
  return moppe_sward_extinction (leaf_area, clump) *
         saturate (density_fraction) *
         moppe_sward_projected_path (axis, ray, clump);
}

// Integrate the first moment of a symmetric, two-sided leaf-normal
// distribution. A sampled normal would merely exchange spatial aliasing for
// lighting noise. The four lobes have one constant tilt, so their weighted
// diffuse response can be evaluated as a float4 moment instead of a fragment
// loop without changing the distribution.
struct MoppeSwardLightMoments {
  float3 sky;
  float3 sun;
};

inline MoppeSwardLightMoments
moppe_sward_light_moments (float3 blade_tint_display,
                           float3 axis,
                           float3 sun_dir,
                           float3 view_dir,
                           float3 sun_diffuse,
                           float3 sun_specular,
                           float3 ambient,
                           float sun_mean_visibility) {
  const float ensemble_exposure = 0.72;
  const float3 base =
    moppe_srgb (blade_tint_display * (0.58 + 0.66 * ensemble_exposure));

  const float3 up = normalize (axis);
  const float3 helper =
    abs (up.y) < 0.92 ? float3 (0.0, 1.0, 0.0) : float3 (1.0, 0.0, 0.0);
  const float3 tangent = normalize (cross (helper, up));
  const float3 bitangent = normalize (cross (up, tangent));
  // The resolved ribbons bend their shading frames toward the open sky. Keep
  // that first normal moment here; a horizontal-only distribution would turn
  // the exact same plants into a dark wall the instant geometry retired.
  constexpr float upright = 0.872506f;
  constexpr float spread = 0.488603f;
  const float up_view = upright * dot (up, view_dir);
  const float tangent_view = spread * dot (tangent, view_dir);
  const float bitangent_view = spread * dot (bitangent, view_dir);
  const float4 view_weight = 0.12 + abs (float4 (up_view + tangent_view,
                                                 up_view + bitangent_view,
                                                 up_view - tangent_view,
                                                 up_view - bitangent_view));
  const float up_sun = upright * dot (up, sun_dir);
  const float tangent_sun = spread * dot (tangent, sun_dir);
  const float bitangent_sun = spread * dot (bitangent, sun_dir);
  const float4 sun_facing = abs (float4 (up_sun + tangent_sun,
                                         up_sun + bitangent_sun,
                                         up_sun - tangent_sun,
                                         up_sun - bitangent_sun));
  const float weight_sum = dot (view_weight, float4 (1.0));
  const float mean_sun =
    dot (view_weight, sun_facing) / max (weight_sum, 0.001);
  // Opposite sides of a thin leaf average to the midpoint of the sky/ground
  // hemisphere, independent of the particular lobe normal.
  const float3 sky = 0.5 * (moppe_hemisphere_light (ambient, up) +
                            moppe_hemisphere_light (ambient, -up));
  const float3 reflected_sun =
    base * sun_diffuse * mean_sun * sun_mean_visibility;

  const float toward = saturate (dot (view_dir, sun_dir));
  const float3 transmitted =
    sqrt (base) * sun_diffuse * moppe_grass_chlorophyll () *
    (0.14 * sun_mean_visibility) * moppe_grass_toward_lobe (toward);

  // The distribution's axial highlight is broad after individual blades are
  // unresolved. Its small bounded amplitude supplies coherent field sheen,
  // never the per-blade HDR spikes owned by resolved geometry.
  const float3 half_dir = normalize (sun_dir - view_dir);
  const float along = dot (up, half_dir);
  const float axial = sqrt (saturate (1.0 - along * along));
  const float sheen = pow (axial, 10.0) * (0.12 + 0.88 * toward * toward);
  const float3 specular = sun_specular * (0.055 * sheen * sun_mean_visibility);
  MoppeSwardLightMoments result;
  result.sky = base * sky;
  result.sun = reflected_sun + transmitted + specular;
  return result;
}

inline float3 moppe_sward_distribution_light (float3 blade_tint_display,
                                              float3 axis,
                                              float3 sun_dir,
                                              float3 view_dir,
                                              float3 sun_diffuse,
                                              float3 sun_specular,
                                              float3 ambient,
                                              float cast_light,
                                              float sun_visibility,
                                              float sun_mean_visibility) {
  const MoppeSwardLightMoments moments =
    moppe_sward_light_moments (blade_tint_display,
                               axis,
                               sun_dir,
                               view_dir,
                               sun_diffuse,
                               sun_specular,
                               ambient,
                               sun_mean_visibility);
  const float3 shade_fill =
    mix (float3 (0.80, 0.92, 1.14), float3 (1.0), cast_light);
  return shade_fill * moments.sky + sun_visibility * moments.sun;
}

// One bounded response for every unresolved evaluation of the upper leaves.
// density_fraction is the portion of the conserved upper-leaf population
// owned by this evaluator or vertical stratum. Coverage is Beer--Lambert
// extinction; radiance is the directly integrated leaf-normal distribution.
inline MoppeSwardOpticalResponse
moppe_sward_optical_response (float3 blade_tint_display,
                              float3 axis,
                              float leaf_area,
                              float clump,
                              float density_fraction,
                              float exposure,
                              float3 sun_dir,
                              float3 view_dir,
                              float3 sun_diffuse,
                              float3 sun_specular,
                              float3 ambient,
                              float cast_light,
                              float sun_visibility) {
  MoppeSwardOpticalResponse response;
  const float fraction = saturate (density_fraction);
  const float extinction = moppe_sward_extinction (leaf_area, clump);
  const float sun_path = moppe_sward_projected_path (axis, sun_dir, clump);
  response.optical_depth =
    moppe_sward_optical_depth (axis, view_dir, leaf_area, clump, fraction);
  response.transmittance = exp (-response.optical_depth);
  response.coverage = 1.0 - response.transmittance;
  // A pixel sees the first optical depth of an opaque stand, not the average
  // leaf buried through its entire thickness. Retain a skylit/front-layer
  // floor and let the slab mean describe only the remaining internal shadow.
  const float slab_sun_mean =
    moppe_sward_mean_transmission (extinction * sun_path);
  const float sun_mean = mix (0.92, 1.0, slab_sun_mean);
  response.radiance = moppe_sward_distribution_light (blade_tint_display,
                                                      axis,
                                                      sun_dir,
                                                      view_dir,
                                                      sun_diffuse,
                                                      sun_specular,
                                                      ambient,
                                                      cast_light,
                                                      sun_visibility,
                                                      sun_mean) *
                      mix (0.58, 1.0, saturate (exposure));
  return response;
}

inline float moppe_grass_gust (float2 world_xz, float time) {
  const float phase = world_xz.x * 0.043 + world_xz.y * 0.051;
  return sin (time * 1.13 + phase) +
         0.45 * sin (time * 2.63 + phase * 1.7 + 1.3);
}

inline float3
moppe_grass_ensemble_axis (float2 world_xz, float3 ground_normal, float time) {
  const float gust = moppe_grass_gust (world_xz, time);
  return normalize (
    mix (normalize (ground_normal), float3 (0.0, 1.0, 0.0), 0.72) +
    float3 (0.79, 0.0, 0.53) * (0.14 * gust));
}

// The substrate and the mesoscale canopy are two integrations of the same
// medium, so they must not acquire separate procedural mottles. This cascade
// leaves an octave near pixel scale across the traversal range and gives both
// representations the same first- and second-order colour structure.
inline float moppe_sward_grain (float2 world_xz, float footprint_metres) {
  const float fine_visible = 1.0 - smoothstep (30.0, 110.0, footprint_metres);
  const float mid_visible = smoothstep (40.0, 120.0, footprint_metres) *
                            (1.0 - smoothstep (400.0, 900.0, footprint_metres));
  const float broad_visible = smoothstep (250.0, 700.0, footprint_metres);
  const float fine = moppe_value_noise (world_xz * 1.9);
  const float mid = moppe_value_noise (world_xz * 0.31 + float2 (7.1, 43.9));
  const float coarse = moppe_value_noise (world_xz * 0.16 + float2 (31.7, 8.3));
  const float broad =
    moppe_value_noise (world_xz * 0.037 + float2 (11.3, 71.7));
  return (1.0 + 0.22 * (coarse - 0.5) * 2.0) *
         (1.0 + 0.18 * (fine - 0.5) * 2.0 * fine_visible) *
         (1.0 + 0.20 * (mid - 0.5) * 2.0 * mid_visible) *
         (1.0 + 0.16 * (broad - 0.5) * 2.0 * broad_visible);
}

#endif
