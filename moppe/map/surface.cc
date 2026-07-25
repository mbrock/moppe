#include <moppe/map/surface.hh>

#include <moppe/profile.hh>
#include <moppe/terrain/river.hh>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace moppe::map {
  namespace {
    struct SnowSupportStencil {
      int width;
      int height;
      int dx;
      int dz;
    };

    SnowSupportStencil snow_support_stencil (const Surface& surface) {
      constexpr meters_t support_radius = 24.0f * u::m;
      const Vec3 spacing = surface.sample_spacing ();
      return {
        .width = surface.width (),
        .height = surface.height (),
        .dx = std::max (1,
                        static_cast<int> (std::lround (
                          meters_value (support_radius) / spacing[0]))),
        .dz = std::max (1,
                        static_cast<int> (std::lround (
                          meters_value (support_radius) / spacing[2]))),
      };
    }

    Vec3 snow_support_normal (const Surface& surface,
                              const SnowSupportStencil& stencil,
                              int column,
                              int row) {
      column = terrain::wrap_index (column, stencil.width);
      row = terrain::wrap_index (row, stencil.height);
      const auto sample = [&] (int x, int z) {
        return surface.normal_at (
          terrain::wrap_index (column + x, stencil.width),
          terrain::wrap_index (row + z, stencil.height));
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

    void populate_snow_support (SurfaceGeometrySections& sections,
                                const Surface& surface) {
      MOPPE_PROFILE_ZONE ("surface.populate_snow_support");
      const SnowSupportStencil support_stencil = snow_support_stencil (surface);
      for (int row = 0; row < surface.height (); ++row)
        for (int column = 0; column < surface.width (); ++column) {
          const SurfaceIndex index { static_cast<std::size_t> (column),
                                     static_cast<std::size_t> (row) };
          auto site = sections[index];
          spatial::get<snow_support> (site) =
            std::clamp (
              snow_support_normal (surface, support_stencil, column, row)[1],
              0.0f,
              1.0f) *
            snow_support[one];
        }
    }

    template <typename Sections>
    const Sections& require_sections (const Sections* sections,
                                      const char* message) {
      if (!sections)
        throw std::logic_error (message);
      return *sections;
    }
  }

  Surface::Surface (int width, int height, const Vec3& size)
      : m_vertical_extent (size[1] * u::m),
        m_atlas (SurfaceDomain (static_cast<std::size_t> (width),
                                static_cast<std::size_t> (height),
                                size[0] / static_cast<float> (width) * u::m,
                                size[2] / static_cast<float> (height) * u::m)) {
    if (width < 2 || height < 2 || size[0] <= 0.0f || size[1] <= 0.0f ||
        size[2] <= 0.0f)
      throw std::invalid_argument ("Surface dimensions must be positive");
    reset_material_history ();
  }

  void Surface::rebuild_geometry_readings () {
    MOPPE_PROFILE_ZONE ("Surface::rebuild_geometry_readings");
    recompute_normals ();
    m_atlas.clear_derived ();
    populate_snow_support (m_atlas.geometry (), *this);
  }

  void Surface::set_use (SurfaceUseSections use) {
    mutable_atlas ().use ().set (std::move (use));
  }

  void Surface::set_moisture (SurfaceMoistureSections moisture) {
    mutable_atlas ().hydrology ().set_moisture (std::move (moisture));
  }

  void Surface::set_waterline_distance (SurfaceWaterlineSections distance) {
    mutable_atlas ().hydrology ().set_waterline (std::move (distance));
  }

  void
  Surface::derive_channel_flux (const terrain::FractionalDrainage& channels) {
    const auto& tangents = spatial::get<terrain::channel_tangent> (channels);
    const auto& areas =
      spatial::get<terrain::fractional_contributing_area> (channels);
    const terrain::TerrainDomain& grid = channels.domain ().terrain_domain ();
    const SurfaceDomain& domain = atlas ().domain ();
    if (grid.width () != domain.width () || grid.height () != domain.height ())
      throw std::invalid_argument (
        "Channel analysis does not share the surface lattice");
    // Activity compresses contributing area logarithmically onto 0..1:
    // hillslope cells fade out and anything carrying river-scale drainage
    // saturates.
    const float floor_area_m2 = 4.0f * square_meters_value (grid.cell_area ());
    const float channel_area_m2 =
      square_meters_value (terrain::visible_river_minimum_area (grid));
    const float activity_span =
      std::log (std::max (channel_area_m2 / floor_area_m2, 1.001f));
    SurfaceChannelFluxSections values (domain);
    auto& column = spatial::get<channel_flux> (values);
    for (std::size_t offset = 0; offset < domain.size (); ++offset) {
      const float area_m2 = areas[offset].numerical_value_in (u::m * u::m);
      const float activity = std::clamp (
        std::log (std::max (area_m2 / floor_area_m2, 1e-6f)) / activity_span,
        0.0f,
        1.0f);
      column[offset] = tangents[offset].numerical_value_in (one) * activity *
                       channel_flux[one];
    }
    mutable_atlas ().hydrology ().set_channel_flux (std::move (values));
  }

  SurfaceElevation Surface::elevation_at (const position_t& position) const {
    const Vec3 point = position_value (position);
    return SurfaceElevation (interpolated_height (point[0], point[2]) *
                             surface_elevation[u::m]);
  }

  SurfaceNormal Surface::normal_at (const position_t& position) const {
    return spatial::sample<terrain::terrain_normal> (atlas ().geometry (),
                                                     position);
  }

  SnowSupport Surface::snow_support_at (const position_t& position) const {
    return spatial::sample<snow_support> (atlas ().geometry (), position);
  }

  TreeHabitat Surface::tree_habitat_at (const position_t& position) const {
    return spatial::sample<tree_habitat> (
      require_sections (atlas ().ecology ().tree_habitat (),
                        "Surface tree habitat has not been materialized"),
      position);
  }

  ForestCover Surface::forest_cover_at (const position_t& position) const {
    return spatial::sample<forest_cover> (
      require_sections (atlas ().ecology ().forest_cover (),
                        "Surface forest cover has not been materialized"),
      position);
  }

}
