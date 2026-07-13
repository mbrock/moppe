#include <moppe/map/surface.hh>

#include <moppe/map/generate.hh>
#include <moppe/profile.hh>

#include <stdexcept>

namespace moppe::map {
  namespace {
    SurfaceDomain domain_for (const HeightMap& map) {
      const Vec3 scale = map.scale ();
      return SurfaceDomain (static_cast<std::size_t> (map.width ()),
                            static_cast<std::size_t> (map.height ()),
                            scale[0] * u::m,
                            scale[2] * u::m,
                            map.periodic () ? terrain::Topology::Torus
                                            : terrain::Topology::Bounded);
    }

    void populate (SurfaceBundle& bundle, const HeightMap& map) {
      MOPPE_PROFILE_ZONE ("surface.populate_bundle");
      const float vertical_scale = map.scale ()[1];
      for (int row = 0; row < map.height (); ++row)
        for (int column = 0; column < map.width (); ++column) {
          const SurfaceIndex index { static_cast<std::size_t> (column),
                                     static_cast<std::size_t> (row) };
          auto sample = bundle[index];
          spatial::get<surface_elevation> (sample) = SurfaceElevation (
            map.get (column, row) * vertical_scale * surface_elevation[u::m]);
          spatial::get<surface_normal> (sample) =
            map.normal (column, row) * surface_normal[one];
        }
    }
  }

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

  Surface::Surface (const HeightMap& map) {
    refresh (map);
  }

  void Surface::refresh (const HeightMap& map) {
    MOPPE_PROFILE_ZONE ("Surface::refresh");
    const SurfaceDomain domain = domain_for (map);
    if (!m_samples || m_samples->domain () != domain)
      m_samples.emplace (domain);
    populate (*m_samples, map);
  }

  const SurfaceBundle& Surface::samples () const {
    if (!m_samples)
      throw std::logic_error ("Surface has not been materialized");
    return *m_samples;
  }
}
