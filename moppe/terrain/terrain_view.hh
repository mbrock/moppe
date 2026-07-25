#ifndef MOPPE_TERRAIN_TERRAIN_VIEW_HH
#define MOPPE_TERRAIN_TERRAIN_VIEW_HH

#include <moppe/quantities.hh>
#include <moppe/terrain/domain.hh>
#include <moppe/terrain/terrain_quantities.hh>

#include <cstddef>
#include <span>
#include <stdexcept>

namespace moppe::terrain {
  // A borrowed, materialized terrain used by the remaining analysis APIs.
  class TerrainView {
  public:
    TerrainView (TerrainDomain domain, std::span<const float> heights)
        : m_domain (std::move (domain)), m_untyped_heights (heights) {
      validate (heights.size ());
    }

    TerrainView (TerrainDomain domain,
                 std::span<const SurfaceElevation> heights)
        : m_domain (std::move (domain)), m_typed_heights (heights) {
      validate (heights.size ());
    }

    const TerrainDomain& domain () const noexcept {
      return m_domain;
    }

    float at (std::size_t x, std::size_t y) const {
      if (x >= m_domain.width () || y >= m_domain.height ())
        throw std::out_of_range ("terrain sample outside grid");
      return at_offset (y * m_domain.width () + x);
    }

    meters_t elevation_at (std::size_t x, std::size_t y) const {
      return at (x, y) * mp_units::si::metre;
    }

  private:
    void validate (std::size_t sample_count) const {
      if (sample_count != m_domain.size ())
        throw std::invalid_argument ("invalid materialized terrain view");
    }

    float at_offset (std::size_t offset) const {
      return m_typed_heights.empty ()
               ? m_untyped_heights[offset]
               : surface_elevation_value (m_typed_heights[offset]);
    }

    TerrainDomain m_domain;
    std::span<const float> m_untyped_heights;
    std::span<const SurfaceElevation> m_typed_heights;
  };
}

#endif
