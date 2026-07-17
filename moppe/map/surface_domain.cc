#include <moppe/map/surface_domain.hh>

#include <stdexcept>

namespace moppe::map {
  SurfaceDomain::SurfaceDomain (std::size_t width,
                                std::size_t height,
                                meters_t spacing_x,
                                meters_t spacing_z,
                                terrain::Topology topology)
      : m_width (width), m_height (height), m_spacing_x (spacing_x),
        m_spacing_z (spacing_z), m_topology (topology) {
    if (width < 2 || height < 2 || spacing_x <= 0.0f * u::m ||
        spacing_z <= 0.0f * u::m)
      throw std::invalid_argument ("Invalid surface domain");
    if (topology == terrain::Topology::Torus && (width < 3 || height < 3))
      throw std::invalid_argument ("Periodic surface needs a duplicated seam");
  }

  std::size_t SurfaceDomain::offset (SurfaceIndex index) const {
    if (index.column >= m_width || index.row >= m_height)
      throw std::out_of_range ("Surface index is outside the domain");
    return index.row * m_width + index.column;
  }

  SurfaceIndex SurfaceDomain::index (std::size_t offset) const {
    if (offset >= size ())
      throw std::out_of_range ("Surface offset is outside the domain");
    return { offset % m_width, offset / m_width };
  }
}
