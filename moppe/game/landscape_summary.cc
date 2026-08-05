#include <moppe/game/landscape_summary.hh>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numbers>
#include <ostream>
#include <vector>

namespace moppe::game {
  namespace {
    double percentile (std::vector<double> values, double proportion) {
      if (values.empty ())
        return 0.0;
      std::sort (values.begin (), values.end ());
      const double position =
        proportion * static_cast<double> (values.size () - 1);
      const std::size_t lower = static_cast<std::size_t> (position);
      const std::size_t upper = std::min (lower + 1, values.size () - 1);
      const double fraction = position - static_cast<double> (lower);
      return values[lower] + fraction * (values[upper] - values[lower]);
    }

    std::size_t
    largest_connected_component (const terrain::TerrainDomain& domain,
                                 const std::vector<std::uint8_t>& eligible) {
      std::vector<std::uint8_t> visited (domain.size (), 0);
      std::vector<std::size_t> pending;
      pending.reserve (domain.size () / 8);
      std::size_t largest = 0;

      for (std::size_t origin = 0; origin < domain.size (); ++origin) {
        if (!eligible[origin] || visited[origin])
          continue;
        pending.clear ();
        pending.push_back (origin);
        visited[origin] = 1;
        std::size_t component = 0;
        while (!pending.empty ()) {
          const std::size_t cell = pending.back ();
          pending.pop_back ();
          ++component;
          domain.visit_neighbourhood (
            domain.index (cell), [&] (terrain::TerrainIndex neighbour, auto) {
              const std::size_t offset = domain.offset (neighbour);
              if (eligible[offset] && !visited[offset]) {
                visited[offset] = 1;
                pending.push_back (offset);
              }
            });
        }
        largest = std::max (largest, component);
      }
      return largest;
    }
  }

  LandscapeSummary summarize_landscape (const map::SurfaceGeometry& surface,
                                        const terrain::FloodField& flood,
                                        const terrain::LakeCensus& census,
                                        const terrain::DrainageGraph& drainage,
                                        const terrain::RiverNetwork& rivers,
                                        const terrain::WorldRecipe& recipe) {
    const terrain::TerrainDomain& domain = surface.domain ();
    const double cell_area_m2 =
      domain.cell_area ().numerical_value_in (u::m * u::m);
    const double sea_level_m = recipe.water_datum ().numerical_value_in (u::m);
    const auto& elevations = spatial::get<terrain::surface_elevation> (surface);
    const auto& eroded = spatial::get<map::eroded_surface_material> (surface);
    const auto& deposited =
      spatial::get<map::deposited_surface_material> (surface);
    const auto& mobile = spatial::get<terrain::sediment_thickness> (surface);
    const auto slopes = drainage.slopes ();

    std::vector<double> land_elevations;
    std::vector<double> land_slopes;
    std::vector<std::uint8_t> low_gradient (domain.size (), 0);
    std::vector<std::uint8_t> rolling_gradient (domain.size (), 0);
    std::size_t below_5 = 0;
    std::size_t below_10 = 0;
    std::size_t below_20 = 0;
    double eroded_m3 = 0.0;
    double deposited_m3 = 0.0;
    double mobile_m3 = 0.0;

    for (std::size_t cell = 0; cell < domain.size (); ++cell) {
      const double elevation_m =
        terrain::surface_elevation_value (elevations[cell]);
      eroded_m3 += eroded[cell].numerical_value_in (u::m) * cell_area_m2;
      deposited_m3 += deposited[cell].numerical_value_in (u::m) * cell_area_m2;
      mobile_m3 += mobile[cell].numerical_value_in (u::m) * cell_area_m2;
      if (elevation_m <= sea_level_m || flood.water_depth_m (cell) > 0.0f)
        continue;
      const double slope_deg =
        std::atan (slopes[cell].numerical_value_in (mp_units::one)) * 180.0 /
        std::numbers::pi;
      land_elevations.push_back (elevation_m);
      land_slopes.push_back (slope_deg);
      if (slope_deg <= 5.0)
        ++below_5;
      if (slope_deg <= 10.0) {
        ++below_10;
        low_gradient[cell] = 1;
      }
      if (slope_deg <= 20.0) {
        ++below_20;
        rolling_gradient[cell] = 1;
      }
    }

    double river_length_m = 0.0;
    double largest_catchment_m2 = 0.0;
    std::vector<std::uint32_t> incoming_reaches (rivers.reaches.size (), 0);
    std::vector<std::uint32_t> incoming_at_cell (domain.size (), 0);
    for (const terrain::RiverReach& reach : rivers.reaches) {
      river_length_m += reach.alignment.length.numerical_value_in (u::m);
      largest_catchment_m2 =
        std::max (largest_catchment_m2,
                  static_cast<double> (
                    reach.downstream_area.numerical_value_in (u::m * u::m)));
      if (reach.downstream_reach == terrain::RiverReach::no_id ||
          reach.cells.empty ())
        continue;
      ++incoming_reaches[reach.downstream_reach];
      const std::uint32_t junction = drainage.receiver[reach.cells.back ()];
      ++incoming_at_cell[junction];
    }
    const std::size_t river_graph_junctions = std::ranges::count_if (
      incoming_reaches, [] (std::uint32_t incoming) { return incoming >= 2; });
    const std::size_t dry_confluence_cells = std::ranges::count_if (
      incoming_at_cell, [] (std::uint32_t incoming) { return incoming >= 2; });

    std::size_t inland_bodies = 0;
    std::size_t lakes = 0;
    double inland_water_area_m2 = 0.0;
    for (const terrain::WaterBody& body : census.water_bodies ()) {
      if (body.ocean_connected)
        continue;
      ++inland_bodies;
      inland_water_area_m2 += body.area.numerical_value_in (u::m * u::m);
      if (body.classification == terrain::WaterBodyClass::Lake)
        ++lakes;
    }

    const std::size_t land_cells = land_elevations.size ();
    const double land_area_m2 = land_cells * cell_area_m2;
    const double minimum_land = percentile (land_elevations, 0.0);
    const double maximum_land = percentile (land_elevations, 1.0);
    return {
      .seed = recipe.seed ().value,
      .resolution = recipe.resolution (),
      .spacing_x_m = domain.spacing_x ().numerical_value_in (u::m),
      .spacing_z_m = domain.spacing_z ().numerical_value_in (u::m),
      .evolution_years = recipe.evolution ().duration.numerical_value_in (
        mp_units::astronomy::Julian_year),
      .uplift_years = recipe.evolution ().uplift_duration.numerical_value_in (
        mp_units::astronomy::Julian_year),
      .channel_initiation_area_m2 =
        recipe.evolution ().channel_initiation_area.numerical_value_in (u::m *
                                                                        u::m),
      .runoff_m_per_year =
        recipe.evolution ().fluvial_transport.runoff_rate.numerical_value_in (
          u::m / mp_units::astronomy::Julian_year),
      .sediment_concentration_at_unit_slope =
        recipe.evolution ()
          .fluvial_transport.concentration_at_unit_slope.numerical_value_in (
            mp_units::one),
      .critical_hillslope_gradient =
        recipe.evolution ().critical_hillslope_gradient.numerical_value_in (
          mp_units::one),
      .maximum_hillslope_multiplier =
        recipe.evolution ()
          .maximum_hillslope_diffusivity_multiplier.numerical_value_in (
            mp_units::one),
      .land_cells = land_cells,
      .land_area_m2 = land_area_m2,
      .land_elevation_p10_m = percentile (land_elevations, 0.10),
      .land_elevation_p50_m = percentile (land_elevations, 0.50),
      .land_elevation_p90_m = percentile (land_elevations, 0.90),
      .land_elevation_p99_m = percentile (land_elevations, 0.99),
      .land_elevation_max_m = maximum_land,
      .land_relief_m = maximum_land - minimum_land,
      .slope_p50_deg = percentile (land_slopes, 0.50),
      .slope_p90_deg = percentile (land_slopes, 0.90),
      .slope_p99_deg = percentile (land_slopes, 0.99),
      .land_below_5_deg_fraction =
        land_cells ? static_cast<double> (below_5) / land_cells : 0.0,
      .land_below_10_deg_fraction =
        land_cells ? static_cast<double> (below_10) / land_cells : 0.0,
      .land_below_20_deg_fraction =
        land_cells ? static_cast<double> (below_20) / land_cells : 0.0,
      .largest_connected_below_10_deg_m2 =
        largest_connected_component (domain, low_gradient) * cell_area_m2,
      .largest_connected_below_20_deg_m2 =
        largest_connected_component (domain, rolling_gradient) * cell_area_m2,
      .river_reaches = rivers.reaches.size (),
      .river_graph_junctions = river_graph_junctions,
      .dry_confluence_cells = dry_confluence_cells,
      .river_length_m = river_length_m,
      .drainage_density_m_per_km2 =
        land_area_m2 > 0.0 ? river_length_m / (land_area_m2 / 1000000.0) : 0.0,
      .largest_river_catchment_m2 = largest_catchment_m2,
      .inland_water_bodies = inland_bodies,
      .lakes = lakes,
      .inland_water_area_m2 = inland_water_area_m2,
      .mobile_sediment_m3 = mobile_m3,
      .eroded_sediment_m3 = eroded_m3,
      .deposited_sediment_m3 = deposited_m3,
      .inferred_bedrock_detached_m3 = eroded_m3 - deposited_m3 + mobile_m3,
      .inferred_exported_sediment_m3 = eroded_m3 - deposited_m3,
    };
  }

  void write_landscape_summary_csv (std::ostream& output,
                                    const LandscapeSummary& s) {
    output << "seed,resolution,spacing_x_m,spacing_z_m,evolution_years,"
              "uplift_years,channel_initiation_area_m2,runoff_m_per_year,"
              "sediment_concentration_at_unit_slope,"
              "critical_hillslope_gradient,maximum_hillslope_multiplier,"
              "land_cells,"
              "land_area_m2,land_elevation_p10_m,land_elevation_p50_m,"
              "land_elevation_p90_m,land_elevation_p99_m,"
              "land_elevation_max_m,land_relief_m,slope_p50_deg,"
              "slope_p90_deg,slope_p99_deg,land_below_5_deg_fraction,"
              "land_below_10_deg_fraction,"
              "land_below_20_deg_fraction,"
              "largest_connected_below_10_deg_m2,"
              "largest_connected_below_20_deg_m2,river_reaches,"
              "river_graph_junctions,dry_confluence_cells,"
              "river_length_m,drainage_density_m_per_km2,"
              "largest_river_catchment_m2,inland_water_bodies,lakes,"
              "inland_water_area_m2,mobile_sediment_m3,eroded_sediment_m3,"
              "deposited_sediment_m3,inferred_bedrock_detached_m3,"
              "inferred_exported_sediment_m3\n";
    output << std::setprecision (12) << s.seed << ',' << s.resolution << ','
           << s.spacing_x_m << ',' << s.spacing_z_m << ',' << s.evolution_years
           << ',' << s.uplift_years << ',' << s.channel_initiation_area_m2
           << ',' << s.runoff_m_per_year << ','
           << s.sediment_concentration_at_unit_slope << ','
           << s.critical_hillslope_gradient << ','
           << s.maximum_hillslope_multiplier << ',' << s.land_cells << ','
           << s.land_area_m2 << ',' << s.land_elevation_p10_m << ','
           << s.land_elevation_p50_m << ',' << s.land_elevation_p90_m << ','
           << s.land_elevation_p99_m << ',' << s.land_elevation_max_m << ','
           << s.land_relief_m << ',' << s.slope_p50_deg << ','
           << s.slope_p90_deg << ',' << s.slope_p99_deg << ','
           << s.land_below_5_deg_fraction << ',' << s.land_below_10_deg_fraction
           << ',' << s.land_below_20_deg_fraction << ','
           << s.largest_connected_below_10_deg_m2 << ','
           << s.largest_connected_below_20_deg_m2 << ',' << s.river_reaches
           << ',' << s.river_graph_junctions << ',' << s.dry_confluence_cells
           << ',' << s.river_length_m << ',' << s.drainage_density_m_per_km2
           << ',' << s.largest_river_catchment_m2 << ','
           << s.inland_water_bodies << ',' << s.lakes << ','
           << s.inland_water_area_m2 << ',' << s.mobile_sediment_m3 << ','
           << s.eroded_sediment_m3 << ',' << s.deposited_sediment_m3 << ','
           << s.inferred_bedrock_detached_m3 << ','
           << s.inferred_exported_sediment_m3 << '\n';
  }

  void write_landscape_elevation_f32 (std::ostream& output,
                                      const map::SurfaceGeometry& surface) {
    const auto& elevations = spatial::get<terrain::surface_elevation> (surface);
    for (const terrain::SurfaceElevation elevation : elevations) {
      const float value = terrain::surface_elevation_value (elevation);
      output.write (reinterpret_cast<const char*> (&value), sizeof (value));
    }
  }
}
