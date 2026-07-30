#ifndef MOPPE_TESTS_SURFACE_FIXTURE_HH
#define MOPPE_TESTS_SURFACE_FIXTURE_HH

#include <moppe/map/surface.hh>
#include <moppe/terrain/waterline.hh>

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

// A completed world's readings arrive as one bundle, joined from analyses
// that a full generation run feeds with hydrology and trails. A test states
// only the inputs its subject depends on; the rest read as an unremarkable
// dry, untravelled lattice.

namespace moppe::test {
  inline terrain::MoistureMap
  moisture_map (const terrain::TerrainDomain& domain,
                std::span<const float> samples) {
    std::vector<terrain::SurfaceMoisture> values;
    std::vector<terrain::SoilWetness> wetness;
    values.reserve (samples.size ());
    wetness.reserve (samples.size ());
    for (float sample : samples) {
      values.push_back (sample * terrain::surface_moisture[one]);
      wetness.push_back (sample * terrain::soil_wetness[one]);
    }
    return terrain::MoistureMap (
      domain, std::move (values), std::move (wetness));
  }

  inline terrain::MoistureMap
  uniform_moisture (const terrain::TerrainDomain& domain, float value) {
    return terrain::MoistureMap (
      domain,
      std::vector<terrain::SurfaceMoisture> (
        domain.size (), value * terrain::surface_moisture[one]),
      std::vector<terrain::SoilWetness> (domain.size (),
                                         value * terrain::soil_wetness[one]));
  }

  inline terrain::WaterlineProximity
  waterline_map (const terrain::TerrainDomain& domain,
                 std::span<const float> metres) {
    std::vector<terrain::WaterlineDistance> values;
    values.reserve (metres.size ());
    for (float sample : metres)
      values.push_back (sample * terrain::waterline_distance[u::m]);
    return terrain::WaterlineProximity (domain, std::move (values));
  }

  inline terrain::TrailUseMap
  trail_use_map (const terrain::TerrainDomain& domain,
                 std::span<const float> trails,
                 std::span<const float> home_base) {
    std::vector<terrain::TrailInfluence> trail_values;
    std::vector<terrain::HomeBaseInfluence> home_values;
    trail_values.reserve (trails.size ());
    home_values.reserve (home_base.size ());
    for (float value : trails)
      trail_values.push_back (value * terrain::trail_influence[one]);
    for (float value : home_base)
      home_values.push_back (value * terrain::home_base_influence[one]);
    return terrain::TrailUseMap (
      domain, std::move (trail_values), std::move (home_values));
  }

  inline terrain::TrailUseMap
  uniform_use (const terrain::TerrainDomain& domain, float trail, float home) {
    return terrain::TrailUseMap (
      domain,
      std::vector<terrain::TrailInfluence> (
        domain.size (), trail * terrain::trail_influence[one]),
      std::vector<terrain::HomeBaseInfluence> (
        domain.size (), home * terrain::home_base_influence[one]));
  }

  struct ReadingsRecipe {
    std::optional<terrain::MoistureMap> moisture;
    std::optional<terrain::WaterlineProximity> waterline;
    std::optional<terrain::TrailUseMap> use;
    meters_t water_level = 50.0f * u::m;
    meters_t tree_line = 160.0f * u::m;
    std::uint32_t seed = 0xdecafbadU;
  };

  inline map::SurfaceReadings
  complete_readings (const map::SurfaceGeometry& surface,
                     ReadingsRecipe recipe = {}) {
    const terrain::TerrainDomain& domain = surface.domain ();
    terrain::MoistureMap moisture = recipe.moisture
                                      ? std::move (*recipe.moisture)
                                      : uniform_moisture (domain, 0.0f);
    terrain::TrailUseMap use =
      recipe.use ? std::move (*recipe.use) : uniform_use (domain, 0.0f, 0.0f);
    map::TreeHabitatMap habitat = map::analyze_tree_habitat (
      surface, moisture, recipe.water_level, recipe.tree_line);
    map::ForestCoverMap cover =
      map::analyze_forest_cover (habitat, use, recipe.seed);

    return spatial::join (map::ChannelFluxMap (domain),
                          std::move (moisture),
                          recipe.waterline
                            ? std::move (*recipe.waterline)
                            : terrain::WaterlineProximity (domain),
                          map::analyze_geology_materials (surface),
                          std::move (habitat),
                          std::move (cover),
                          std::move (use));
  }
}

#endif
