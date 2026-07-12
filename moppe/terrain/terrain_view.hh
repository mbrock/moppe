#ifndef MOPPE_TERRAIN_TERRAIN_VIEW_HH
#define MOPPE_TERRAIN_TERRAIN_VIEW_HH

#include <moppe/quantities.hh>
#include <moppe/terrain/discretization.hh>

#include <cstddef>
#include <span>
#include <stdexcept>

namespace moppe::terrain {
  // A borrowed, materialized terrain. Unlike FieldSamplingGrid2D, this carries
  // the physical scale and topology needed by neighborhood and global analyses.
  class TerrainView {
  public:
    TerrainView (TerrainGrid grid, std::span<const float> heights)
        : m_grid (grid), m_heights (heights) {
      if (grid.width < 2 || grid.height < 2 ||
          grid.spacing_x <= 0.0f * mp_units::si::metre ||
          grid.spacing_y <= 0.0f * mp_units::si::metre ||
          grid.height_scale <= 0.0f * mp_units::si::metre ||
          heights.size () != grid.width * grid.height)
        throw std::invalid_argument ("invalid materialized terrain view");
      if (grid.topology == Topology::Torus &&
          (grid.width < 3 || grid.height < 3))
        throw std::invalid_argument (
          "periodic terrain needs a duplicated seam");
    }

    const TerrainGrid& grid () const noexcept {
      return m_grid;
    }
    std::span<const float> heights () const noexcept {
      return m_heights;
    }

    float at (std::size_t x, std::size_t y) const {
      if (x >= m_grid.width || y >= m_grid.height)
        throw std::out_of_range ("terrain sample outside grid");
      return m_heights[y * m_grid.width + x];
    }

    float at (GridPointIndex index) const {
      const auto [x, y] = m_grid.coordinates (index);
      return m_heights[y * m_grid.width + x];
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
    TerrainGrid m_grid;
    std::span<const float> m_heights;
  };
}

#endif
