#ifndef MOPPE_MAP_SURFACE_SECTIONS_HH
#define MOPPE_MAP_SURFACE_SECTIONS_HH

#include <moppe/map/surface_domain.hh>
#include <moppe/spatial/bundle.hh>

namespace moppe::map {
  inline constexpr struct surface_elevation
      : quantity_spec<mp_units::isq::height, mp_units::is_kind> {
  } surface_elevation;

  inline constexpr struct surface_normal
      : quantity_spec<mp_units::dimensionless,
                      mp_units::quantity_tensor_order::vector,
                      mp_units::is_kind> {
  } surface_normal;

  // The upward component of a broad local support plane. Snow responds to
  // this material-scale reading rather than the detailed lighting normal.
  inline constexpr struct snow_support
      : quantity_spec<mp_units::dimensionless> {
  } snow_support;

  // Planar channel direction scaled by log-compressed fluvial activity.
  inline constexpr struct channel_flux
      : quantity_spec<mp_units::dimensionless,
                      mp_units::quantity_tensor_order::vector,
                      mp_units::is_kind> {
  } channel_flux;

  // Ground wetness synthesized from standing water and accumulated drainage.
  inline constexpr struct surface_moisture
      : quantity_spec<mp_units::dimensionless> {
  } surface_moisture;

  // Horizontal distance to the extracted wet/dry curve.
  inline constexpr struct waterline_distance
      : quantity_spec<mp_units::isq::length, mp_units::is_kind> {
  } waterline_distance;

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

  // Shoulder-blended membership in the generated trail network.
  inline constexpr struct trail_influence
      : quantity_spec<mp_units::dimensionless> {
  } trail_influence;

  // Membership in the Association's cleared arrival and gathering place.
  inline constexpr struct home_base_influence
      : quantity_spec<mp_units::dimensionless> {
  } home_base_influence;

  using SurfaceElevation =
    quantity_point<surface_elevation[u::m],
                   default_point_origin (surface_elevation[u::m]),
                   float>;
  using SurfaceNormal = quantity<surface_normal[one], Vec3>;
  using SnowSupport = quantity<snow_support[one], float>;
  using ChannelFlux = quantity<channel_flux[one], Vec3>;
  using SurfaceMoisture = quantity<surface_moisture[one], float>;
  using WaterlineDistance = quantity<waterline_distance[u::m], float>;
  using ErosionExposure = quantity<erosion_exposure[one], float>;
  using DepositionCover = quantity<deposition_cover[one], float>;
  using TreeHabitat = quantity<tree_habitat[one], float>;
  using ForestCover = quantity<forest_cover[one], float>;
  using TrailInfluence = quantity<trail_influence[one], float>;
  using HomeBaseInfluence = quantity<home_base_influence[one], float>;

  // Each named materialization group is a typed collection of 0-cochains over
  // the same SurfaceDomain. SurfaceAtlas makes a group's presence explicit,
  // so an unavailable reading cannot be mistaken for an ordinary zero.
  using SurfaceGeometrySections = spatial::
    Bundle<SurfaceDomain, SurfaceElevation, SurfaceNormal, SnowSupport>;
  using SurfaceChannelFluxSections =
    spatial::Bundle<SurfaceDomain, ChannelFlux>;
  using SurfaceMoistureSections =
    spatial::Bundle<SurfaceDomain, SurfaceMoisture>;
  using SurfaceWaterlineSections =
    spatial::Bundle<SurfaceDomain, WaterlineDistance>;
  using SurfaceGeologySections =
    spatial::Bundle<SurfaceDomain, ErosionExposure, DepositionCover>;
  using SurfaceHabitatSections = spatial::Bundle<SurfaceDomain, TreeHabitat>;
  using SurfaceForestSections = spatial::Bundle<SurfaceDomain, ForestCover>;
  using SurfaceTrailSections = spatial::Bundle<SurfaceDomain, TrailInfluence>;
  using SurfaceHomeBaseSections =
    spatial::Bundle<SurfaceDomain, HomeBaseInfluence>;
}

#endif
