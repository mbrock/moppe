#ifndef MOPPE_TERRAIN_GLACIAL_HH
#define MOPPE_TERRAIN_GLACIAL_HH

// Scandinavian glacial sculpting over a tectonically evolved landscape:
//
//   1. Warm-based glacial U-troughs & fjord mouths along the dominant
//      sea-reaching trunk valley(s): thick, fast-sliding valley ice grinds a
//      broad, flat-floored, gently graded U-shaped corridor all the way from
//      the highlands down to sea level. This simultaneously creates a
//      navigable highland-to-sea main-stem river and leaves side tributaries
//      perched on the flanking highland shelves as hanging-valley waterfalls.
//   2. High kalfjäll snowfield massif & descending glacier snow valley: an
//      organically lobed, gently rolling ice-cap plateau over the highest
//      alpine massif feeding a sweeping banked snow corridor down toward the
//      tree line.
//   3. Alpine glacial cirques (nisscher): asymmetric armchair bowls bitten
//      into the poleward/lee flanks of high mountain massifs, sharpening
//      rounded ridges into arêtes and rocky headwalls while packing wind-
//      sheltered snow drift on their concave floors.
//   4. Cold-based highland shelf (vidda) preservation & polish: gentle
//      high-elevation plateaus outside troughs and cliffs stay elevated and
//      wind-scoured, preserving the stepped Nordic upland feel.
//
// Runs deterministically on the periodic terrain torus using correctly rounded
// math, updating both bedrock erosion and alluvial/outwash valley-floor fill.

#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/sediment_transport.hh>

#include <cstddef>
#include <vector>

namespace moppe::terrain {
  struct GlacialSculpting {
    // Minimum catchment at a reach's mouth to sustain a warm-based glacial
    // trough reaching the sea.
    square_meters_t trough_mouth_area = 6000000.0f * u::m * u::m;
    // Minimum catchment along the trunk course where the warm-based trough
    // begins (upstream of this lies the natural highland trough head).
    square_meters_t trough_head_area = 1500000.0f * u::m * u::m;
    // Gentle concave long-profile grades for the graded U-trough floor:
    // near-level fjord grade at the coast, rising softly inland.
    grade_t coastal_grade = 0.0020f * grade[one];
    grade_t highland_grade = 0.0072f * grade[one];
    // Flat alluvial trough floor half-width bounds (scales with trunk area).
    meters_t minimum_floor_half_width = 26.0f * u::m;
    meters_t maximum_floor_half_width = 58.0f * u::m;
    // Characteristic U-valley wall steepness (rise over run): gentle alluvial
    // footslope rising smoothly into a 32-38 degree glaciated valley wall.
    slope_t wall_slope = 0.62f * terrain_slope[one];
    // Drowned fjord depth at the sea mouth, easing above sea level inland.
    meters_t fjord_mouth_depth = 2.6f * u::m;
    meters_t fjord_inlet_length = 260.0f * u::m;
    // Alluvial glacial outwash blanket laid on the carved U-trough floor.
    meters_t valley_floor_outwash = 2.6f * u::m;
    // High kalfjäll snowfield massif (rolling ice-cap plateau) and its
    // descending glacier tongue (snow-filled valley reaching toward tree line).
    bool sculpt_snowfield = true;
    proportion_t snowfield_relief_share = 0.84f * proportion[one];
    meters_t snowfield_radius_along = 920.0f * u::m;
    meters_t snowfield_radius_across = 660.0f * u::m;
    meters_t snow_valley_length = 1350.0f * u::m;
    meters_t snow_valley_half_width = 82.0f * u::m;
    proportion_t snow_valley_exit_relief_share = 0.57f * proportion[one];
    // Firn/drift moraine blanket laid on snowfields, glacier tongues, and
    // wind-sheltered cirque bowl floors.
    meters_t snow_basin_cover = 2.4f * u::m;
    // Alpine cirque elevation threshold above sea level and max count.
    meters_t cirque_floor_above_sea = 148.0f * u::m;
    std::size_t maximum_cirques = 18;
    // Cold-based plateau smoothing elevation threshold above sea level.
    meters_t vidda_above_sea = 105.0f * u::m;
    slope_t vidda_max_slope = 0.22f * terrain_slope[one];
    WaterPermanence permanence = {};
  };

  struct GlacialSculptingResult {
    std::vector<SurfaceElevation> heights;
    // Ground laid down (positive) or cut away (negative).
    std::vector<ElevationChange> change;
    // Fresh glacial outwash sediment laid on the flat trough floor.
    std::vector<SedimentThickness> outwash;
    cubic_meters_f64_t cut = 0.0 * u::m * u::m * u::m;
    std::size_t troughs = 0;
    std::size_t snowfields = 0;
    std::size_t cirques = 0;
  };

  namespace detail {
    GlacialSculptingResult
    sculpt_glacial_valleys (const TerrainDomain& domain,
                            std::span<const SurfaceElevation> elevations,
                            const FloodField& flood,
                            const LakeCensus& census,
                            const DrainageGraph& drainage,
                            const RiverNetwork& rivers,
                            SurfaceElevation sea_level,
                            const GlacialSculpting& parameters);
  }

  template <TerrainElevations Terrain>
  GlacialSculptingResult
  sculpt_glacial_valleys (const Terrain& terrain,
                          const FloodField& flood,
                          const LakeCensus& census,
                          const DrainageGraph& drainage,
                          const RiverNetwork& rivers,
                          SurfaceElevation sea_level,
                          const GlacialSculpting& parameters = {}) {
    return detail::sculpt_glacial_valleys (terrain.domain (),
                                           elevations (terrain),
                                           flood,
                                           census,
                                           drainage,
                                           rivers,
                                           sea_level,
                                           parameters);
  }
}

#endif
