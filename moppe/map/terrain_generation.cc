#include <moppe/map/terrain_generation.hh>

#include <moppe/profile.hh>

#include <utility>

namespace moppe::map {
  namespace {
    constexpr float coastline = 0.4f;
    constexpr float initial_land_relief_m = 20.0f;
    constexpr float initial_bathymetric_relief_m = 240.0f;
    constexpr float maximum_uplift_m_per_year = 0.001f;

    void set_elevations (Surface& surface, std::span<const float> heights) {
      const int width = surface.width ();
      const int height = surface.height ();
      for (int row = 0; row < height; ++row)
        for (int column = 0; column < width; ++column)
          surface.set_elevation (
            column,
            row,
            SurfaceElevation (
              heights[static_cast<std::size_t> (row) * width + column] *
              terrain::surface_elevation[u::m]));
    }
  }

  std::vector<meters_per_julian_year_t>
  initialize_terrain (Surface& surface,
                      terrain::Seed seed,
                      meters_t water_datum,
                      const terrain::GeologicalProgress& progress) {
    MOPPE_PROFILE_ZONE ("terrain.initialize");
    terrain::GeologicalSections geology =
      terrain::generate_geology (surface.domain (), seed, progress);
    const auto& continent = spatial::get<terrain::continent_shape> (geology);
    const auto& weights = spatial::get<terrain::uplift_weight> (geology);
    auto& elevations =
      spatial::get<terrain::surface_elevation> (surface.geometry ());
    const float sea_level_m = meters_value (water_datum);

    std::vector<meters_per_julian_year_t> uplift;
    uplift.reserve (geology.size ());
    for (std::size_t offset = 0; offset < geology.size (); ++offset) {
      const float land = continent[offset].numerical_value_in (one) - coastline;
      const float relief =
        land < 0.0f ? initial_bathymetric_relief_m : initial_land_relief_m;
      elevations[offset] = SurfaceElevation ((sea_level_m + relief * land) *
                                             terrain::surface_elevation[u::m]);
      uplift.push_back (weights[offset].numerical_value_in (one) *
                        maximum_uplift_m_per_year * mp_units::si::metre /
                        mp_units::astronomy::Julian_year);
    }
    surface.reset_material_history ();
    return uplift;
  }

  terrain::StreamPowerEvolutionReport
  evolve_terrain (Surface& surface,
                  std::span<const meters_per_julian_year_t> uplift,
                  const terrain::StreamPowerEvolution& parameters,
                  const terrain::StreamPowerEvolutionBackend* backend,
                  const terrain::StreamPowerProgress& progress) {
    MOPPE_PROFILE_ZONE ("terrain.evolve");
    terrain::StreamPowerEvolutionResult result =
      backend ? terrain::evolve_stream_power (
                  surface.geometry (), uplift, parameters, *backend, progress)
              : terrain::evolve_stream_power (
                  surface.geometry (), uplift, parameters, progress);
    set_elevations (surface, result.heights);
    return result.report;
  }

  terrain::TrailNetwork
  form_terrain_trails (Surface& surface,
                       const terrain::TrailFormation& parameters) {
    MOPPE_PROFILE_ZONE ("terrain.form_trails");
    terrain::TrailFormationResult result =
      terrain::form_trails (surface.geometry (), parameters);
    const int width = surface.width ();
    const int height = surface.height ();
    for (int row = 0; row < height; ++row)
      for (int column = 0; column < width; ++column) {
        const std::size_t offset =
          static_cast<std::size_t> (row) * width + column;
        const float previous =
          terrain::surface_elevation_value (surface.elevation_at (column, row));
        surface.record_material_change (
          column, row, result.heights[offset] - previous);
        surface.set_elevation (
          column,
          row,
          SurfaceElevation (result.heights[offset] *
                            terrain::surface_elevation[u::m]));
      }
    return std::move (result.network);
  }
}
