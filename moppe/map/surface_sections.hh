#ifndef MOPPE_MAP_SURFACE_SECTIONS_HH
#define MOPPE_MAP_SURFACE_SECTIONS_HH

#include <moppe/spatial/bundle.hh>
#include <moppe/terrain/domain.hh>
#include <moppe/terrain/terrain_quantities.hh>

namespace moppe::map {
  using terrain::surface_elevation;
  using terrain::SurfaceElevation;

  // The upward component of a broad local support plane. Snow responds to
  // this material-scale reading rather than the detailed lighting normal.
  inline constexpr struct snow_support
      : quantity_spec<mp_units::dimensionless> {
  } snow_support;

  inline constexpr struct eroded_surface_material
      : quantity_spec<mp_units::dimensionless, mp_units::is_kind> {
  } eroded_surface_material;

  inline constexpr struct deposited_surface_material
      : quantity_spec<mp_units::dimensionless, mp_units::is_kind> {
  } deposited_surface_material;

  // Planar channel direction scaled by log-compressed fluvial activity.
  inline constexpr struct channel_flux
      : quantity_spec<mp_units::dimensionless,
                      mp_units::quantity_tensor_order::vector,
                      mp_units::is_kind> {
  } channel_flux;

  using terrain::surface_moisture;
  using terrain::waterline_distance;

  // Normalized exposure of material removed during the world's history.
  inline constexpr struct erosion_exposure
      : quantity_spec<mp_units::dimensionless> {
  } erosion_exposure;

  // Normalized cover of material deposited during the world's history.
  inline constexpr struct deposition_cover
      : quantity_spec<mp_units::dimensionless> {
  } deposition_cover;

  // Ecological support from drainage moisture, slope, shore, and tree line.
  inline constexpr struct tree_habitat
      : quantity_spec<mp_units::dimensionless> {
  } tree_habitat;

  // Actual canopy recruitment after habitat, routes, and settlement.
  inline constexpr struct forest_cover
      : quantity_spec<mp_units::dimensionless> {
  } forest_cover;

  using terrain::home_base_influence;
  using terrain::trail_influence;

  using SurfaceNormal = terrain::TerrainNormal;
  using SnowSupport = quantity<snow_support[one], float>;
  using ErodedSurfaceMaterial = quantity<eroded_surface_material[one], float>;
  using DepositedSurfaceMaterial =
    quantity<deposited_surface_material[one], float>;
  using ChannelFlux = quantity<channel_flux[one], Vec3>;
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
                                          SurfaceNormal,
                                          ErodedSurfaceMaterial,
                                          DepositedSurfaceMaterial,
                                          SnowSupport>;
  using SurfaceReadings = spatial::Bundle<terrain::TerrainDomain,
                                          ChannelFlux,
                                          SurfaceMoisture,
                                          WaterlineDistance,
                                          ErosionExposure,
                                          DepositionCover,
                                          TreeHabitat,
                                          ForestCover,
                                          TrailInfluence,
                                          HomeBaseInfluence>;
}

#endif
