#ifndef MOPPE_TERRAIN_TERRAIN_VIEW_HH
#define MOPPE_TERRAIN_TERRAIN_VIEW_HH

#include <moppe/quantities.hh>
#include <moppe/terrain/field.hh>
#include <moppe/terrain/topology.hh>

#include <cstddef>
#include <span>
#include <stdexcept>

namespace moppe::terrain {
  struct TerrainGrid {
    std::size_t width;
    std::size_t height;
    meters_t spacing_x = 1.0f * mp_units::si::metre;
    meters_t spacing_y = 1.0f * mp_units::si::metre;
    meters_t height_scale = 1.0f * mp_units::si::metre;
    Topology topology = Topology::Bounded;

    float spacing_x_m () const {
      return meters_value (spacing_x);
    }
    float spacing_y_m () const {
      return meters_value (spacing_y);
    }
    float height_scale_m () const {
      return meters_value (height_scale);
    }
    square_meters_t cell_area () const {
      return spacing_x * spacing_y;
    }

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

    auto relative_elevation_at (std::size_t x, std::size_t y) const {
      return at (x, y) * relative_elevation[mp_units::one];
    }

    meters_t elevation_at (std::size_t x, std::size_t y) const {
      return at (x, y) * m_grid.height_scale;
    }

  private:
    TerrainGrid m_grid;
    std::span<const float> m_heights;
  };
}

#endif
