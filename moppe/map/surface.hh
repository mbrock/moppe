#ifndef MOPPE_MAP_SURFACE_HH
#define MOPPE_MAP_SURFACE_HH

#include <moppe/spatial/bundle.hh>
#include <moppe/terrain/channel.hh>
#include <moppe/terrain/domain.hh>
#include <moppe/terrain/fractional_drainage.hh>
#include <moppe/terrain/geological.hh>
#include <moppe/terrain/moisture.hh>
#include <moppe/terrain/roads.hh>
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
  using LimestoneCover = quantity<limestone_cover[one], float>;
  using BedrockExposure = quantity<bedrock_exposure[one], float>;
  using SpruceAbundance = quantity<spruce_abundance[one], float>;
  using PineAbundance = quantity<pine_abundance[one], float>;
  using BirchAbundance = quantity<birch_abundance[one], float>;
  using MountainBirchAbundance = quantity<mountain_birch_abundance[one], float>;
  using LichenAbundance = quantity<lichen_abundance[one], float>;
  using HeatherAbundance = quantity<heather_abundance[one], float>;
  using BilberryAbundance = quantity<bilberry_abundance[one], float>;
  using SphagnumAbundance = quantity<sphagnum_abundance[one], float>;
  using DwarfBirchAbundance = quantity<dwarf_birch_abundance[one], float>;
  using SedgeAbundance = quantity<sedge_abundance[one], float>;
  using AlvarAbundance = quantity<alvar_abundance[one], float>;
  using FireweedAbundance = quantity<fireweed_abundance[one], float>;
  using SpringFenAbundance = quantity<spring_fen_abundance[one], float>;
  using GrazingPressure = quantity<grazing_pressure[one], float>;
  using StandRecovery = quantity<stand_recovery[one], float>;
  using BurnRecency = quantity<burn_recency[one], float>;

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
                                          HomeBaseInfluence,
                                          LimestoneCover,
                                          BedrockExposure,
                                          SpruceAbundance,
                                          PineAbundance,
                                          BirchAbundance,
                                          MountainBirchAbundance,
                                          LichenAbundance,
                                          HeatherAbundance,
                                          BilberryAbundance,
                                          SphagnumAbundance,
                                          DwarfBirchAbundance,
                                          SedgeAbundance,
                                          AlvarAbundance,
                                          FireweedAbundance,
                                          SpringFenAbundance,
                                          GrazingPressure,
                                          StandRecovery,
                                          BurnRecency>;

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

  // Give every river its bed (terrain/channel.hh): grade its long profile
  // through the hollows and sills erosion left along it, and cut the bed
  // and banks its discharge makes. The cut and the fill join the surface's
  // eroded and deposited material.
  terrain::ChannelFormationResult
  form_river_channels (SurfaceGeometry& surface,
                       meters_t water_datum,
                       const terrain::ChannelFormation& parameters = {});

  // Build the planned roads into the surface in place (terrain/roads.hh),
  // solving their profiles into `roads`. What the roads cut and fill joins
  // the surface's eroded and deposited material.
  terrain::RoadFormationResult
  form_terrain_roads (SurfaceGeometry& surface,
                      terrain::RoadNetwork& roads,
                      const terrain::FloodField& flood,
                      const terrain::DrainageGraph& drainage,
                      const terrain::RoadFormation& parameters = {});

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
  using VegetationMap = spatial::Bundle<terrain::TerrainDomain,
                                        LimestoneCover,
                                        BedrockExposure,
                                        SpruceAbundance,
                                        PineAbundance,
                                        BirchAbundance,
                                        MountainBirchAbundance,
                                        LichenAbundance,
                                        HeatherAbundance,
                                        BilberryAbundance,
                                        SphagnumAbundance,
                                        DwarfBirchAbundance,
                                        SedgeAbundance,
                                        AlvarAbundance,
                                        FireweedAbundance,
                                        SpringFenAbundance>;
  using DisturbanceMap = spatial::
    Bundle<terrain::TerrainDomain, GrazingPressure, StandRecovery, BurnRecency>;

  // Ground nothing has happened to: ungrazed, unburnt, fully recovered.
  DisturbanceMap undisturbed (const terrain::TerrainDomain& domain);

  // Scaled against the material this world's own history moved, so a calm
  // world reads as strongly as a violent one.
  GeologyMaterials analyze_geology_materials (const SurfaceGeometry& geometry);

  // The bedrock and what grows on it. Biomes are not drawn here: the world
  // was a crystalline basement under a sheet of limestone of uneven
  // thickness, cut through where its erosion went deeper than the sheet;
  // over it lie a few slow, hidden quantities -- how much water gathers,
  // how cold it gets (height, the slope's aspect, cold air pooling in the
  // hollows), how deep the soil runs, which way the ground faces the sun,
  // how exposed it is to the wind. Each species has a tolerance curve over
  // them and a strength against the others, and what holds each place is
  // the outcome of their competition, the trees first and the field layer
  // in the light they leave. The heath, the bog, the alvar, and the fjäll
  // are where those overlap, with edges as frayed as the curves are wide.
  // Two follow from the rock itself: where the ice scoured the basement
  // bare on knolls and ridges and no soil came back, granite shows; and
  // the rain that sank through the limestone runs out along the sheet's
  // foot, where it rests on the basement, so a hillside below a limestone
  // edge carries a line of springs and the spring fen they feed.
  // What has happened to the ground is one more such quantity: fireweed
  // takes a fresh scar, birch comes first after it and spruce last,
  // grazing keeps the trees off and leaves heath on poor ground and turf
  // on good, and fire spares the bog. LAND_TOP is the highest ground.
  VegetationMap analyze_vegetation (const SurfaceGeometry& geometry,
                                    std::span<const terrain::SoilWetness> wet,
                                    meters_t water_level,
                                    meters_t land_top,
                                    std::uint32_t seed,
                                    const DisturbanceMap& disturbance);

  TreeHabitatMap analyze_tree_habitat (const SurfaceGeometry& geometry,
                                       const terrain::MoistureMap& moisture,
                                       meters_t water_level,
                                       meters_t tree_line);

  // A closed canopy stands where the trees that close one -- spruce, then
  // birch -- hold the ground; pine and fell birch stand open, and the
  // forest plan plants them sparse itself.
  ForestCoverMap analyze_forest_cover (const TreeHabitatMap& habitat,
                                       const terrain::TrailUseMap& use,
                                       const VegetationMap& vegetation,
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
