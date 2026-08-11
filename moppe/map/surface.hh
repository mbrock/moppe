#ifndef MOPPE_MAP_SURFACE_HH
#define MOPPE_MAP_SURFACE_HH

#include <moppe/spatial/bundle.hh>
#include <moppe/terrain/domain.hh>
#include <moppe/terrain/fractional_drainage.hh>
#include <moppe/terrain/geological.hh>
#include <moppe/terrain/moisture.hh>
#include <moppe/terrain/stream_power_evolution.hh>
#include <moppe/terrain/trail.hh>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// The whole life of the world's finite ground surface: the quantities a
// column carries, the generation passes that give them values, the readings
// analysed back out of the finished geometry, and the file a previous run
// left behind. Terrain algorithms themselves live in moppe/terrain/; this is
// where they meet one concrete surface.

namespace moppe::map {
  // ---- Surface quantities ----
  //
  // The world's ground geometry is the SurfaceGeometry bundle itself:
  // elevation, normals, and the material history the world's erosion left
  // behind, over one terrain domain. Readings analysed over the same domain
  // live in a separate bundle the completed world owns. The specs these
  // measure are declared in moppe/quantities.hh; here they become the
  // concrete columns a surface stores.

  using terrain::home_base_influence;
  using terrain::sediment_thickness;
  using terrain::SedimentThickness;
  using terrain::surface_elevation;
  using terrain::surface_moisture;
  using terrain::SurfaceElevation;
  using terrain::trail_influence;
  using terrain::waterline_distance;

  using SurfaceNormal = terrain::TerrainNormal;
  using SnowSupport = quantity<snow_support[one], float>;
  using ErodedSurfaceMaterial = quantity<eroded_surface_material[u::m], float>;
  using DepositedSurfaceMaterial =
    quantity<deposited_surface_material[u::m], float>;
  using terrain::SoilWetness;
  using terrain::SurfaceMoisture;
  using terrain::WaterlineDistance;
  using ErosionExposure = quantity<erosion_exposure[one], float>;
  using DepositionCover = quantity<deposition_cover[one], float>;
  using TreeHabitat = quantity<tree_habitat[one], float>;
  using ForestCover = quantity<forest_cover[one], float>;
  using terrain::HomeBaseInfluence;
  using terrain::TrailInfluence;

  // Surface owns one mandatory geometry bundle and one derived-reading bundle
  // over the same TerrainDomain. The latter is absent until world analysis
  // begins; completed worlds contain every column.
  using SurfaceGeometry = spatial::Bundle<terrain::TerrainDomain,
                                          SurfaceElevation,
                                          SedimentThickness,
                                          SurfaceNormal,
                                          ErodedSurfaceMaterial,
                                          DepositedSurfaceMaterial,
                                          SnowSupport>;
  using SurfaceReadings = spatial::Bundle<terrain::TerrainDomain,
                                          SurfaceMoisture,
                                          SoilWetness,
                                          WaterlineDistance,
                                          ErosionExposure,
                                          DepositionCover,
                                          TreeHabitat,
                                          ForestCover,
                                          TrailInfluence,
                                          HomeBaseInfluence>;

  // ---- Geometry ----

  // Normals and the broad snow support plane follow from elevation; rebuild
  // them whenever the heightfield changes.
  void rebuild_geometry (SurfaceGeometry& geometry);

  // ---- Generation ----

  // Draw the canonical seeded geology into a physical surface and return the
  // uplift rate used by the following evolution pass.
  std::vector<meters_per_julian_year_t>
  initialize_terrain (SurfaceGeometry& surface,
                      terrain::Seed seed,
                      meters_t water_datum,
                      const terrain::GeologicalProgress& progress = {});

  // Evolve the initialized physical elevations in place.
  terrain::StreamPowerEvolutionReport
  evolve_terrain (SurfaceGeometry& surface,
                  std::span<const meters_per_julian_year_t> uplift,
                  const terrain::StreamPowerEvolution& parameters,
                  const terrain::StreamPowerProgress& progress = {});

  // Form the canonical built circuit in place and return its useful network.
  terrain::TrailNetwork
  form_terrain_trails (SurfaceGeometry& surface,
                       const terrain::TrailFormation& parameters);

  // ---- Readings ----
  //
  // Every reading over the world's surface comes from one analysis of the
  // finished geometry. Each returns its own narrow bundle; a completed world
  // joins them, in the order SurfaceReadings declares, into the wide store the
  // game samples.

  using GeologyMaterials =
    spatial::Bundle<terrain::TerrainDomain, ErosionExposure, DepositionCover>;
  using TreeHabitatMap = spatial::Bundle<terrain::TerrainDomain, TreeHabitat>;
  using ForestCoverMap = spatial::Bundle<terrain::TerrainDomain, ForestCover>;

  // Scaled against the material this world's own history moved, so a calm
  // world reads as strongly as a violent one.
  GeologyMaterials analyze_geology_materials (const SurfaceGeometry& geometry);

  TreeHabitatMap analyze_tree_habitat (const SurfaceGeometry& geometry,
                                       const terrain::MoistureMap& moisture,
                                       meters_t water_level,
                                       meters_t tree_line);

  ForestCoverMap analyze_forest_cover (const TreeHabitatMap& habitat,
                                       const terrain::TrailUseMap& use,
                                       std::uint32_t seed);

  // ---- Cache ----
  //
  // A saved surface is the expensive part of world generation. The geometry
  // bundle knows how to write and read itself, so this is only the question of
  // where it lives: every column travels, and a file whose lattice or column
  // shape no longer matches counts as no file at all.

  // Fills every geometry column from a previous run's file, if one matches
  // this lattice and this bundle's shape.
  bool try_load_cache (SurfaceGeometry& geometry, const std::string& path);

  void save_cache (const SurfaceGeometry& geometry, const std::string& path);
}

#endif
