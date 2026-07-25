#ifndef MOPPE_MAP_SURFACE_READINGS_HH
#define MOPPE_MAP_SURFACE_READINGS_HH

#include <moppe/map/surface_sections.hh>
#include <moppe/terrain/fractional_drainage.hh>
#include <moppe/terrain/moisture.hh>
#include <moppe/terrain/trail.hh>

#include <cstdint>

// Every reading over the world's surface comes from one analysis of the
// finished geometry. Each returns its own narrow bundle; a completed world
// joins them, in the order SurfaceReadings declares, into the wide store the
// game samples.

namespace moppe::map {
  using ChannelFluxMap = spatial::Bundle<terrain::TerrainDomain, ChannelFlux>;
  using GeologyMaterials =
    spatial::Bundle<terrain::TerrainDomain, ErosionExposure, DepositionCover>;
  using TreeHabitatMap = spatial::Bundle<terrain::TerrainDomain, TreeHabitat>;
  using ForestCoverMap = spatial::Bundle<terrain::TerrainDomain, ForestCover>;

  // The drainage analysis carries a domain of its own, so the surface lattice
  // its result must agree with is named separately.
  ChannelFluxMap
  analyze_channel_flux (const terrain::TerrainDomain& domain,
                        const terrain::FractionalDrainage& channels);

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
}

#endif
