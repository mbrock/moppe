#include <moppe/map/surface_readings.hh>

#include <moppe/terrain/river.hh>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace moppe::map {
  ChannelFluxMap
  analyze_channel_flux (const terrain::TerrainDomain& domain,
                        const terrain::FractionalDrainage& channels) {
    const auto& tangents = spatial::get<terrain::channel_tangent> (channels);
    const auto& areas =
      spatial::get<terrain::fractional_contributing_area> (channels);
    const terrain::TerrainDomain& grid = channels.domain ().terrain_domain ();
    if (grid.width () != domain.width () || grid.height () != domain.height ())
      throw std::invalid_argument (
        "Channel analysis does not share the surface lattice");

    // Activity compresses contributing area logarithmically onto 0..1:
    // hillslope cells fade out and anything carrying river-scale drainage
    // saturates.
    const float floor_area_m2 = 4.0f * square_meters_value (grid.cell_area ());
    const float channel_area_m2 =
      square_meters_value (terrain::visible_river_minimum_area (grid));
    const float activity_span =
      std::log (std::max (channel_area_m2 / floor_area_m2, 1.001f));

    ChannelFluxMap flux (domain);
    auto& column = spatial::get<channel_flux> (flux);
    for (std::size_t offset = 0; offset < domain.size (); ++offset) {
      const float area_m2 = areas[offset].numerical_value_in (u::m * u::m);
      const float activity = std::clamp (
        std::log (std::max (area_m2 / floor_area_m2, 1e-6f)) / activity_span,
        0.0f,
        1.0f);
      column[offset] = tangents[offset].numerical_value_in (one) * activity *
                       channel_flux[one];
    }
    return flux;
  }
}
