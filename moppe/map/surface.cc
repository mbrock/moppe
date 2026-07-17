#include <moppe/map/surface.hh>

#include <moppe/map/generate.hh>
#include <moppe/profile.hh>

#include <algorithm>
#include <cmath>
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

    int surface_coordinate (int coordinate, int extent, bool periodic) {
      if (periodic)
        return terrain::wrap_index (coordinate, extent);
      return std::clamp (coordinate, 0, extent - 1);
    }

    struct SnowSupportStencil {
      int width;
      int height;
      int dx;
      int dz;
      bool periodic;
    };

    SnowSupportStencil snow_support_stencil (const HeightMap& map) {
      constexpr meters_t support_radius = 24.0f * u::m;
      const bool periodic = map.periodic ();
      const Vec3 spacing = map.scale ();
      return {
        .width = periodic ? map.width () - 1 : map.width (),
        .height = periodic ? map.height () - 1 : map.height (),
        .dx = std::max (1,
                        static_cast<int> (std::lround (
                          meters_value (support_radius) / spacing[0]))),
        .dz = std::max (1,
                        static_cast<int> (std::lround (
                          meters_value (support_radius) / spacing[2]))),
        .periodic = periodic,
      };
    }

    Vec3 snow_support_normal (const HeightMap& map,
                              const SnowSupportStencil& stencil,
                              int column,
                              int row) {
      column = surface_coordinate (column, stencil.width, stencil.periodic);
      row = surface_coordinate (row, stencil.height, stencil.periodic);
      const auto sample = [&] (int x, int z) {
        return map.normal (
          surface_coordinate (column + x, stencil.width, stencil.periodic),
          surface_coordinate (row + z, stencil.height, stencil.periodic));
      };
      Vec3 support = sample (0, 0) * 4.0f;
      support += (sample (-stencil.dx, 0) + sample (stencil.dx, 0) +
                  sample (0, -stencil.dz) + sample (0, stencil.dz)) *
                 2.0f;
      support +=
        sample (-stencil.dx, -stencil.dz) + sample (stencil.dx, -stencil.dz) +
        sample (-stencil.dx, stencil.dz) + sample (stencil.dx, stencil.dz);
      return normalized (support);
    }

    void populate_geometry (SurfaceSections& sections, const HeightMap& map) {
      MOPPE_PROFILE_ZONE ("surface.populate_geometry");
      const float vertical_scale = map.scale ()[1];
      const SnowSupportStencil support_stencil = snow_support_stencil (map);
      for (int row = 0; row < map.height (); ++row)
        for (int column = 0; column < map.width (); ++column) {
          const SurfaceIndex index { static_cast<std::size_t> (column),
                                     static_cast<std::size_t> (row) };
          auto site = sections[index];
          spatial::get<surface_elevation> (site) = SurfaceElevation (
            map.get (column, row) * vertical_scale * surface_elevation[u::m]);
          spatial::get<surface_normal> (site) =
            map.normal (column, row) * surface_normal[one];
          spatial::get<snow_support> (site) =
            std::clamp (
              snow_support_normal (map, support_stencil, column, row)[1],
              0.0f,
              1.0f) *
            snow_support[one];
          spatial::get<channel_flux> (site) = Vec3 () * channel_flux[one];
          spatial::get<tree_habitat> (site) = 0.0f * tree_habitat[one];
          spatial::get<forest_cover> (site) = 0.0f * forest_cover[one];
          spatial::get<trail_influence> (site) = 0.0f * trail_influence[one];
          spatial::get<home_base_influence> (site) =
            0.0f * home_base_influence[one];
        }
    }
  }

  Surface::Surface (const HeightMap& map) {
    refresh (map);
  }

  void Surface::refresh (const HeightMap& map) {
    MOPPE_PROFILE_ZONE ("Surface::refresh");
    const SurfaceDomain domain = domain_for (map);
    if (!m_sections || m_sections->domain () != domain)
      m_sections.emplace (domain);
    populate_geometry (*m_sections, map);
  }

  void Surface::materialize_trail_influence (std::span<const float> influence) {
    SurfaceSections& values = mutable_sections ();
    if (influence.size () != values.size ())
      throw std::invalid_argument (
        "Trail influence needs one value per surface sample");
    auto& trail = spatial::get<trail_influence> (values);
    for (std::size_t offset = 0; offset < values.size (); ++offset)
      trail[offset] =
        std::clamp (influence[offset], 0.0f, 1.0f) * trail_influence[one];
  }

  void
  Surface::materialize_home_base_influence (std::span<const float> influence) {
    SurfaceSections& values = mutable_sections ();
    if (influence.size () != values.size ())
      throw std::invalid_argument (
        "Home base influence needs one value per surface sample");
    auto& home_base = spatial::get<home_base_influence> (values);
    for (std::size_t offset = 0; offset < values.size (); ++offset)
      home_base[offset] =
        std::clamp (influence[offset], 0.0f, 1.0f) * home_base_influence[one];
  }

  void Surface::materialize_channel_flux (std::span<const float> flux) {
    SurfaceSections& values = mutable_sections ();
    if (flux.size () != 2 * values.size ())
      throw std::invalid_argument (
        "Channel flux needs one planar vector per surface sample");
    auto& column = spatial::get<channel_flux> (values);
    for (std::size_t offset = 0; offset < values.size (); ++offset) {
      Vec3 value (flux[2 * offset], 0.0f, flux[2 * offset + 1]);
      const float magnitude = std::sqrt (length2 (value));
      if (magnitude > 1.0f)
        value /= magnitude;
      column[offset] = value * channel_flux[one];
    }
  }

  const SurfaceSections& Surface::sections () const {
    if (!m_sections)
      throw std::logic_error ("Surface has not been materialized");
    return *m_sections;
  }

  SurfaceSections& Surface::mutable_sections () {
    if (!m_sections)
      throw std::logic_error ("Surface has not been materialized");
    return *m_sections;
  }
}
