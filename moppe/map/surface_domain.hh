#ifndef MOPPE_MAP_SURFACE_DOMAIN_HH
#define MOPPE_MAP_SURFACE_DOMAIN_HH

#include <moppe/gfx/math.hh>
#include <moppe/terrain/topology.hh>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace moppe::map {
  struct SurfaceIndex {
    std::size_t column;
    std::size_t row;

    friend bool operator== (const SurfaceIndex&, const SurfaceIndex&) = default;
  };

  // The combinatorial and chart data for the terrain's vertex lattice.
  // Exact section access uses SurfaceIndex. Continuous access asks the domain
  // for a reconstruction stencil, so boundary behavior has one owner.
  class SurfaceDomain {
  public:
    using index_type = SurfaceIndex;

    SurfaceDomain (std::size_t width,
                   std::size_t height,
                   meters_t spacing_x,
                   meters_t spacing_z);

    friend bool operator== (const SurfaceDomain&,
                            const SurfaceDomain&) = default;

    std::size_t size () const noexcept {
      return m_width * m_height;
    }
    std::size_t width () const noexcept {
      return m_width;
    }
    std::size_t height () const noexcept {
      return m_height;
    }
    meters_t maximum_interpolated_x () const noexcept {
      return static_cast<float> (m_width - 2) * m_spacing_x;
    }
    meters_t maximum_interpolated_z () const noexcept {
      return static_cast<float> (m_height - 2) * m_spacing_z;
    }
    meters_t spacing_x () const noexcept {
      return m_spacing_x;
    }
    meters_t spacing_z () const noexcept {
      return m_spacing_z;
    }

    std::size_t offset (SurfaceIndex index) const;
    SurfaceIndex index (std::size_t offset) const;

    template <typename Visitor>
    void visit_interpolation_stencil (const position_t& position,
                                      Visitor&& visitor) const {
      const float x = terrain::wrap_coordinate (position_value (position)[0] /
                                                  meters_value (m_spacing_x),
                                                static_cast<float> (m_width));
      const float z = terrain::wrap_coordinate (position_value (position)[2] /
                                                  meters_value (m_spacing_z),
                                                static_cast<float> (m_height));

      const std::size_t x0 = static_cast<std::size_t> (std::floor (x));
      const std::size_t z0 = static_cast<std::size_t> (std::floor (z));
      const std::size_t x1 = (x0 + 1) % m_width;
      const std::size_t z1 = (z0 + 1) % m_height;
      const float tx = x - static_cast<float> (x0);
      const float tz = z - static_cast<float> (z0);

      visitor (SurfaceIndex { x0, z0 }, (1.0f - tx) * (1.0f - tz));
      visitor (SurfaceIndex { x1, z0 }, tx * (1.0f - tz));
      visitor (SurfaceIndex { x0, z1 }, (1.0f - tx) * tz);
      visitor (SurfaceIndex { x1, z1 }, tx * tz);
    }

  private:
    std::size_t m_width;
    std::size_t m_height;
    meters_t m_spacing_x;
    meters_t m_spacing_z;
  };
}

#endif
