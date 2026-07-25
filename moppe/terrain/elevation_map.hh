#ifndef MOPPE_TERRAIN_ELEVATION_MAP_HH
#define MOPPE_TERRAIN_ELEVATION_MAP_HH

#include <moppe/spatial/bundle.hh>
#include <moppe/terrain/domain.hh>
#include <moppe/terrain/terrain_quantities.hh>

#include <algorithm>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace moppe::terrain {
  using ElevationMap = spatial::Bundle<TerrainDomain, SurfaceElevation>;

  template <typename Bundle>
  concept TerrainElevations =
    requires { typename std::remove_cvref_t<Bundle>::domain_type; } &&
    std::same_as<typename std::remove_cvref_t<Bundle>::domain_type,
                 TerrainDomain> &&
    spatial::BundleContains<surface_elevation, std::remove_cvref_t<Bundle>>;

  template <TerrainElevations Bundle>
  std::span<const SurfaceElevation>
  elevations (const Bundle& terrain) noexcept {
    return spatial::get<surface_elevation> (terrain);
  }

  template <std::ranges::contiguous_range Samples>
    requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<Samples>>,
                          float>
  ElevationMap make_elevation_map (TerrainDomain domain, Samples&& samples) {
    const std::span<const float> values (std::ranges::data (samples),
                                         std::ranges::size (samples));
    if (values.size () != domain.size ())
      throw std::invalid_argument (
        "elevation samples do not match terrain domain");
    ElevationMap result (std::move (domain));
    std::ranges::transform (values,
                            spatial::get<surface_elevation> (result).begin (),
                            [] (float value) {
                              return SurfaceElevation (value *
                                                       surface_elevation[u::m]);
                            });
    return result;
  }

  inline float elevation_at (const TerrainDomain& domain,
                             std::span<const SurfaceElevation> values,
                             std::size_t column,
                             std::size_t row) {
    return surface_elevation_value (
      values[domain.offset ({ .column = column, .row = row })]);
  }
}

#endif
