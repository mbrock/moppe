#ifndef MOPPE_TERRAIN_TERRAIN_VIEW_HH
#define MOPPE_TERRAIN_TERRAIN_VIEW_HH

#include <moppe/terrain/topology.hh>

#include <cstddef>
#include <span>
#include <stdexcept>

namespace moppe::terrain {
  struct TerrainGrid {
    std::size_t width;
    std::size_t height;
    float spacing_x = 1.0f;
    float spacing_y = 1.0f;
    float height_scale = 1.0f;
    Topology topology = Topology::Bounded;

    std::size_t unique_width () const noexcept {
      return topology == Topology::Torus ? width - 1 : width;
    }

    std::size_t unique_height () const noexcept {
      return topology == Topology::Torus ? height - 1 : height;
    }

    std::size_t unique_size () const noexcept {
      return unique_width () * unique_height ();
    }
  };

  // A borrowed, materialized terrain. Unlike Domain2D, this carries the
  // physical scale and topology needed by neighborhood and global analyses.
  class TerrainView {
  public:
    TerrainView (TerrainGrid grid, std::span<const float> heights)
        : m_grid (grid), m_heights (heights) {
      if (grid.width < 2 || grid.height < 2 || grid.spacing_x <= 0.0f ||
          grid.spacing_y <= 0.0f || grid.height_scale <= 0.0f ||
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

  private:
    TerrainGrid m_grid;
    std::span<const float> m_heights;
  };
}

#endif
