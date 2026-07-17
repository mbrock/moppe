#ifndef MOPPE_TERRAIN_WORLD_RECIPE_HH
#define MOPPE_TERRAIN_WORLD_RECIPE_HH

#include <moppe/gfx/math.hh>
#include <moppe/quantities.hh>
#include <moppe/terrain/program.hh>
#include <moppe/terrain/topology.hh>

namespace moppe::terrain {
  // The complete immutable input to terrain construction.  It binds the
  // physical world to the normalized terrain program, so every consumer sees
  // the same extent, water datum, topology, seed, and generation profile.
  class WorldRecipe {
  public:
    const spatial_extent_t& extent () const noexcept {
      return m_extent;
    }

    int resolution () const noexcept {
      return m_resolution;
    }

    Topology topology () const noexcept {
      return m_topology;
    }

    Seed seed () const noexcept {
      return m_seed;
    }

    meters_t water_datum () const noexcept {
      return m_water_datum;
    }

    float normalized_water_datum () const noexcept;

    TerrainGenerationProfile generation_profile () const noexcept {
      return m_generation_profile;
    }

    const TerrainProgram& terrain_program () const noexcept {
      return m_terrain_program;
    }

    // Program editors make a new recipe for an altered program rather than
    // mutating the value that described the generated world.
    WorldRecipe with_terrain_program (TerrainProgram program) const;

  private:
    friend WorldRecipe
    make_world_recipe (spatial_extent_t extent,
                       int resolution,
                       Topology topology,
                       Seed seed,
                       meters_t water_datum,
                       TerrainGenerationProfile generation_profile);
    friend WorldRecipe
    make_geological_world_recipe (spatial_extent_t extent,
                                  int resolution,
                                  Topology topology,
                                  Seed seed,
                                  meters_t water_datum,
                                  TerrainGenerationProfile generation_profile);

    WorldRecipe (spatial_extent_t extent,
                 int resolution,
                 Topology topology,
                 Seed seed,
                 meters_t water_datum,
                 TerrainGenerationProfile generation_profile,
                 TerrainProgram terrain_program);

    spatial_extent_t m_extent;
    int m_resolution;
    Topology m_topology;
    Seed m_seed;
    meters_t m_water_datum;
    TerrainGenerationProfile m_generation_profile;
    TerrainProgram m_terrain_program;
  };

  // Build the canonical orogeny-and-trails program for a physical world.
  WorldRecipe make_world_recipe (spatial_extent_t extent,
                                 int resolution,
                                 Topology topology,
                                 Seed seed,
                                 meters_t water_datum,
                                 TerrainGenerationProfile generation_profile);

  // Terrain Lab's fast field-only preview has the same world vocabulary even
  // though it deliberately omits the canonical erosion-and-trails program.
  WorldRecipe
  make_geological_world_recipe (spatial_extent_t extent,
                                int resolution,
                                Topology topology,
                                Seed seed,
                                meters_t water_datum,
                                TerrainGenerationProfile generation_profile);
}

#endif
