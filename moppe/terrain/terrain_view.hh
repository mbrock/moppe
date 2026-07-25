#ifndef MOPPE_TERRAIN_TERRAIN_VIEW_HH
#define MOPPE_TERRAIN_TERRAIN_VIEW_HH

#include <moppe/quantities.hh>
#include <moppe/terrain/discretization.hh>
#include <moppe/terrain/terrain_quantities.hh>

#include <cstddef>
#include <span>
#include <stdexcept>

namespace moppe::terrain {
  // A borrowed, materialized terrain. Unlike FieldSamplingGrid2D, this carries
  // the physical scale and topology needed by neighborhood and global analyses.
  class TerrainView {
  public:
    TerrainView (TerrainGrid grid, std::span<const float> heights)
        : m_grid (grid), m_untyped_heights (heights) {
      validate (heights.size ());
    }

    TerrainView (TerrainGrid grid,
                 std::span<const RelativeTerrainElevation> heights)
        : m_grid (grid), m_typed_heights (heights) {
      validate (heights.size ());
    }

    const TerrainGrid& grid () const noexcept {
      return m_grid;
    }

    float at (std::size_t x, std::size_t y) const {
      if (x >= m_grid.width || y >= m_grid.height)
        throw std::out_of_range ("terrain sample outside grid");
      return at_offset (y * m_grid.width + x);
    }

    float at (GridPointIndex index) const {
      const auto [x, y] = m_grid.coordinates (index);
      return at_offset (y * m_grid.width + x);
    }

    auto relative_elevation_at (std::size_t x, std::size_t y) const {
      return at (x, y) * relative_elevation[mp_units::one];
    }

    auto relative_elevation_at (GridPointIndex index) const {
      return at (index) * relative_elevation[mp_units::one];
    }

    meters_t elevation_at (std::size_t x, std::size_t y) const {
      return at (x, y) * m_grid.height_scale;
    }

    meters_t elevation_at (GridPointIndex index) const {
      return at (index) * m_grid.height_scale;
    }

  private:
    void validate (std::size_t sample_count) const {
      if (m_grid.width < 2 || m_grid.height < 2 ||
          m_grid.spacing_x <= 0.0f * mp_units::si::metre ||
          m_grid.spacing_y <= 0.0f * mp_units::si::metre ||
          m_grid.height_scale <= 0.0f * mp_units::si::metre ||
          sample_count != m_grid.width * m_grid.height)
        throw std::invalid_argument ("invalid materialized terrain view");
    }

    float at_offset (std::size_t offset) const {
      return m_typed_heights.empty ()
               ? m_untyped_heights[offset]
               : m_typed_heights[offset].numerical_value_in (mp_units::one);
    }

    TerrainGrid m_grid;
    std::span<const float> m_untyped_heights;
    std::span<const RelativeTerrainElevation> m_typed_heights;
  };
}

#endif
