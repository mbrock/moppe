#include <moppe/terrain/world_recipe.hh>

#include <stdexcept>
#include <utility>
#include <variant>

namespace moppe::terrain {
  namespace {
    void set_program_water_datum (TerrainProgram& program, meters_t datum) {
      const float elevation_m = meters_value (datum);
      program.source.sea_level = elevation_m;
      for (TerrainTransform& transform : program.transforms)
        if (auto* orogeny = std::get_if<OrogenyEvolution> (&transform))
          orogeny->evolution.sea_level = elevation_m;
        else if (auto* trails = std::get_if<TrailFormation> (&transform))
          trails->sea_level = elevation_m;
    }
  }

  WorldRecipe::WorldRecipe (spatial_extent_t extent,
                            int resolution,
                            Seed seed,
                            meters_t water_datum,
                            TerrainGenerationProfile generation_profile,
                            TerrainProgram terrain_program)
      : m_extent (extent), m_resolution (resolution), m_seed (seed),
        m_water_datum (water_datum), m_generation_profile (generation_profile),
        m_terrain_program (std::move (terrain_program)) {
    if (m_terrain_program.seed != m_seed)
      throw std::invalid_argument (
        "world recipe program seed must match the world seed");
  }

  WorldRecipe WorldRecipe::with_terrain_program (TerrainProgram program) const {
    return { m_extent,      m_resolution,         m_seed,
             m_water_datum, m_generation_profile, std::move (program) };
  }

  WorldRecipe make_world_recipe (spatial_extent_t extent,
                                 int resolution,
                                 Seed seed,
                                 meters_t water_datum,
                                 TerrainGenerationProfile generation_profile) {
    TerrainProgram program =
      make_world_program (seed.value, generation_profile);
    set_program_water_datum (program, water_datum);
    return { extent,      resolution,         seed,
             water_datum, generation_profile, std::move (program) };
  }
}
