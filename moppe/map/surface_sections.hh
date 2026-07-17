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
  using TreeHabitat = quantity<tree_habitat[one], float>;
  using ForestCover = quantity<forest_cover[one], float>;
  using TrailInfluence = quantity<trail_influence[one], float>;
  using HomeBaseInfluence = quantity<home_base_influence[one], float>;

  // The intrinsic 0-cochains currently carried by the terrain lattice.
  // Their common domain makes cross-section rules explicit and guarantees
  // that one row always means one surface site.
  using SurfaceSections = spatial::Bundle<SurfaceDomain,
                                          SurfaceElevation,
                                          SurfaceNormal,
                                          SnowSupport,
                                          ChannelFlux,
                                          TreeHabitat,
                                          ForestCover,
                                          TrailInfluence,
                                          HomeBaseInfluence>;
}

#endif
