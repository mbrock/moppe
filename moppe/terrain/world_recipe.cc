#include <moppe/terrain/world_recipe.hh>

#include <stdexcept>
#include <utility>
#include <variant>

namespace moppe::terrain {
  namespace {
    float normalized_water_datum_for (spatial_extent_t extent,
                                      meters_t water_datum) {
      return meters_value (water_datum) / extent_value (extent)[1];
    }

    void set_program_water_datum (TerrainProgram& program, float datum) {
      program.source.sea_level = datum;
      for (TerrainTransform& transform : program.transforms)
        if (auto* analytical = std::get_if<AnalyticalErosion> (&transform))
          analytical->sea_level = datum;
        else if (auto* orogeny = std::get_if<OrogenyEvolution> (&transform))
          orogeny->evolution.sea_level = datum;
        else if (auto* trails = std::get_if<TrailFormation> (&transform))
          trails->sea_level = datum;
    }
  }

  WorldRecipe::WorldRecipe (spatial_extent_t extent,
                            int resolution,
                            Topology topology,
                            Seed seed,
                            meters_t water_datum,
                            TerrainGenerationProfile generation_profile,
                            TerrainProgram terrain_program)
      : m_extent (extent), m_resolution (resolution), m_topology (topology),
        m_seed (seed), m_water_datum (water_datum),
        m_generation_profile (generation_profile),
        m_terrain_program (std::move (terrain_program)) {
    if (m_terrain_program.seed != m_seed)
      throw std::invalid_argument (
        "world recipe program seed must match the world seed");
  }

  float WorldRecipe::normalized_water_datum () const noexcept {
    return normalized_water_datum_for (m_extent, m_water_datum);
  }

  WorldRecipe WorldRecipe::with_terrain_program (TerrainProgram program) const {
    return { m_extent,      m_resolution,         m_topology,         m_seed,
             m_water_datum, m_generation_profile, std::move (program) };
  }

  WorldRecipe make_world_recipe (spatial_extent_t extent,
                                 int resolution,
                                 Topology topology,
                                 Seed seed,
                                 meters_t water_datum,
                                 TerrainGenerationProfile generation_profile) {
    TerrainProgram program =
      make_world_program (seed.value, generation_profile);
    set_program_water_datum (program,
                             normalized_water_datum_for (extent, water_datum));
    return { extent,      resolution,         topology,           seed,
             water_datum, generation_profile, std::move (program) };
  }

  WorldRecipe
  make_geological_world_recipe (spatial_extent_t extent,
                                int resolution,
                                Topology topology,
                                Seed seed,
                                meters_t water_datum,
                                TerrainGenerationProfile generation_profile) {
    TerrainProgram program = make_geological_program (seed.value);
    set_program_water_datum (program,
                             normalized_water_datum_for (extent, water_datum));
    return { extent,      resolution,         topology,           seed,
             water_datum, generation_profile, std::move (program) };
  }
}
