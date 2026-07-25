#ifndef MOPPE_TERRAIN_DOMAIN_HH
#define MOPPE_TERRAIN_DOMAIN_HH

#include <moppe/gfx/math.hh>
#include <moppe/terrain/topology.hh>

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace moppe::terrain {
  struct TerrainIndex {
    std::size_t column;
    std::size_t row;

    friend bool operator== (const TerrainIndex&, const TerrainIndex&) = default;
  };

  // The one finite periodic lattice shared by terrain bundles. Exact section
  // access uses TerrainIndex; continuous access asks the domain for its
  // reconstruction stencil.
  class TerrainDomain {
  public:
    using index_type = TerrainIndex;

    TerrainDomain (std::size_t width,
                   std::size_t height,
                   meters_t spacing_x,
                   meters_t spacing_z)
        : m_width (width), m_height (height), m_spacing_x (spacing_x),
          m_spacing_z (spacing_z) {
      if (width < 2 || height < 2 || spacing_x <= 0.0f * u::m ||
          spacing_z <= 0.0f * u::m)
        throw std::invalid_argument ("invalid terrain domain");
    }

    friend bool operator== (const TerrainDomain&,
                            const TerrainDomain&) = default;

    std::size_t size () const noexcept {
      return m_width * m_height;
    }
    std::size_t width () const noexcept {
      return m_width;
    }
    std::size_t height () const noexcept {
      return m_height;
    }
    meters_t period_x () const noexcept {
      return static_cast<float> (m_width) * m_spacing_x;
    }
    meters_t period_z () const noexcept {
      return static_cast<float> (m_height) * m_spacing_z;
    }
    meters_t spacing_x () const noexcept {
      return m_spacing_x;
    }
    meters_t spacing_z () const noexcept {
      return m_spacing_z;
    }
    square_meters_t cell_area () const noexcept {
      return m_spacing_x * m_spacing_z;
    }

    std::size_t offset (TerrainIndex index) const {
      if (index.column >= m_width || index.row >= m_height)
        throw std::out_of_range ("terrain index outside domain");
      return index.row * m_width + index.column;
    }

    TerrainIndex index (std::size_t offset) const {
      if (offset >= size ())
        throw std::out_of_range ("terrain offset outside domain");
      return { .column = offset % m_width, .row = offset / m_width };
    }

    template <typename Visitor>
    void visit_interpolation_stencil (const position_t& position,
                                      Visitor&& visitor) const {
      const float x = wrap_coordinate (position_value (position)[0] /
                                         meters_value (m_spacing_x),
                                       static_cast<float> (m_width));
      const float z = wrap_coordinate (position_value (position)[2] /
                                         meters_value (m_spacing_z),
                                       static_cast<float> (m_height));

      const std::size_t x0 = static_cast<std::size_t> (std::floor (x));
      const std::size_t z0 = static_cast<std::size_t> (std::floor (z));
      const std::size_t x1 = (x0 + 1) % m_width;
      const std::size_t z1 = (z0 + 1) % m_height;
      const float tx = x - static_cast<float> (x0);
      const float tz = z - static_cast<float> (z0);

      visitor (TerrainIndex { x0, z0 }, (1.0f - tx) * (1.0f - tz));
      visitor (TerrainIndex { x1, z0 }, tx * (1.0f - tz));
      visitor (TerrainIndex { x0, z1 }, (1.0f - tx) * tz);
      visitor (TerrainIndex { x1, z1 }, tx * tz);
    }

  private:
    std::size_t m_width;
    std::size_t m_height;
    meters_t m_spacing_x;
    meters_t m_spacing_z;
  };
}

#endif
