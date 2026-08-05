#ifndef MOPPE_GAME_LANDSCAPE_SUMMARY_HH
#define MOPPE_GAME_LANDSCAPE_SUMMARY_HH

#include <moppe/map/surface.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/world_recipe.hh>

#include <cstddef>
#include <cstdint>
#include <iosfwd>

namespace moppe::game {
  // One comparable, renderer-free reading of a completed landscape. The
  // fields stay explicit because unlike physical questions should not be
  // hidden behind one terrain quality score.
  struct LandscapeSummary {
    std::uint32_t seed = 0;
    int resolution = 0;
    double spacing_x_m = 0.0;
    double spacing_z_m = 0.0;
    double evolution_years = 0.0;
    double uplift_years = 0.0;
    double channel_initiation_area_m2 = 0.0;
    std::size_t land_cells = 0;
    double land_area_m2 = 0.0;
    double land_elevation_p10_m = 0.0;
    double land_elevation_p50_m = 0.0;
    double land_elevation_p90_m = 0.0;
    double land_elevation_p99_m = 0.0;
    double land_elevation_max_m = 0.0;
    double land_relief_m = 0.0;
    double slope_p50_deg = 0.0;
    double slope_p90_deg = 0.0;
    double slope_p99_deg = 0.0;
    double land_below_5_deg_fraction = 0.0;
    double land_below_10_deg_fraction = 0.0;
    double largest_connected_below_10_deg_m2 = 0.0;
    std::size_t river_reaches = 0;
    double river_length_m = 0.0;
    double drainage_density_m_per_km2 = 0.0;
    double largest_river_catchment_m2 = 0.0;
    std::size_t inland_water_bodies = 0;
    std::size_t lakes = 0;
    double inland_water_area_m2 = 0.0;
    double eroded_sediment_m3 = 0.0;
    double deposited_sediment_m3 = 0.0;
    double inferred_exported_sediment_m3 = 0.0;
  };

  LandscapeSummary summarize_landscape (const map::SurfaceGeometry& surface,
                                        const terrain::FloodField& flood,
                                        const terrain::LakeCensus& census,
                                        const terrain::DrainageGraph& drainage,
                                        const terrain::RiverNetwork& rivers,
                                        const terrain::WorldRecipe& recipe);

  void write_landscape_summary_csv (std::ostream& output,
                                    const LandscapeSummary& summary);

  // A compact reproducible elevation field accompanies a gazetteer so
  // terrain-scale evidence can be recalculated without rebuilding the world.
  void write_landscape_elevation_f32 (std::ostream& output,
                                      const map::SurfaceGeometry& surface);
}

#endif
