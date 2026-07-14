#ifndef MOPPE_MAP_SURFACE_HH
#define MOPPE_MAP_SURFACE_HH

#include <moppe/gfx/math.hh>
#include <moppe/spatial/bundle.hh>
#include <moppe/terrain/topology.hh>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>

namespace moppe::map {
  class HeightMap;

  inline constexpr struct surface_elevation
      : quantity_spec<mp_units::isq::height, mp_units::is_kind> {
  } surface_elevation;

  inline constexpr struct surface_normal
      : quantity_spec<mp_units::dimensionless,
                      mp_units::quantity_tensor_order::vector,
                      mp_units::is_kind> {
  } surface_normal;

  // A dimensionless ecological reading materialized over the same surface
  // domain as elevation and normal. It combines drainage moisture, slope,
  // shore clearance, and the local tree line; consumers select sites from the
  // bundle instead of rediscovering those policies from the heightmap.
  inline constexpr struct tree_habitat
      : quantity_spec<mp_units::dimensionless> {
  } tree_habitat;

  // Shoulder-blended membership in the generated trail network. This is a
  // material mask rather than a generic scalar: zero is untouched terrain,
  // one is the graded centerline.
  inline constexpr struct trail_influence
      : quantity_spec<mp_units::dimensionless> {
  } trail_influence;

  using SurfaceElevation =
    quantity_point<surface_elevation[u::m],
                   default_point_origin (surface_elevation[u::m]),
                   float>;
  using SurfaceNormal = quantity<surface_normal[one], Vec3>;
  using TreeHabitat = quantity<tree_habitat[one], float>;
  using TrailInfluence = quantity<trail_influence[one], float>;

  struct SurfaceIndex {
    std::size_t column;
    std::size_t row;

    friend bool operator== (const SurfaceIndex&, const SurfaceIndex&) = default;
  };

  // The finite terrain lattice and its embedding in horizontal world space.
  // Exact bundle access uses SurfaceIndex; continuous access asks the domain
  // for the four samples and weights of a bilinear reconstruction.
  class SurfaceDomain {
  public:
    using index_type = SurfaceIndex;

    SurfaceDomain (std::size_t width,
                   std::size_t height,
                   meters_t spacing_x,
                   meters_t spacing_z,
                   terrain::Topology topology);

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
    terrain::Topology topology () const noexcept {
      return m_topology;
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
      float x = position_value (position)[0] / meters_value (m_spacing_x);
      float z = position_value (position)[2] / meters_value (m_spacing_z);
      if (m_topology == terrain::Topology::Torus) {
        x = terrain::wrap_coordinate (x, static_cast<float> (m_width - 1));
        z = terrain::wrap_coordinate (z, static_cast<float> (m_height - 1));
      }

      std::ptrdiff_t x0 = static_cast<std::ptrdiff_t> (std::floor (x));
      std::ptrdiff_t z0 = static_cast<std::ptrdiff_t> (std::floor (z));
      x0 = std::clamp<std::ptrdiff_t> (x0, 0, m_width - 2);
      z0 = std::clamp<std::ptrdiff_t> (z0, 0, m_height - 2);
      const float tx = x - static_cast<float> (x0);
      const float tz = z - static_cast<float> (z0);

      visitor (SurfaceIndex { static_cast<std::size_t> (x0),
                              static_cast<std::size_t> (z0) },
               (1.0f - tx) * (1.0f - tz));
      visitor (SurfaceIndex { static_cast<std::size_t> (x0 + 1),
                              static_cast<std::size_t> (z0) },
               tx * (1.0f - tz));
      visitor (SurfaceIndex { static_cast<std::size_t> (x0),
                              static_cast<std::size_t> (z0 + 1) },
               (1.0f - tx) * tz);
      visitor (SurfaceIndex { static_cast<std::size_t> (x0 + 1),
                              static_cast<std::size_t> (z0 + 1) },
               tx * tz);
    }

  private:
    std::size_t m_width;
    std::size_t m_height;
    meters_t m_spacing_x;
    meters_t m_spacing_z;
    terrain::Topology m_topology;
  };

  using SurfaceBundle = spatial::Bundle<SurfaceDomain,
                                        SurfaceElevation,
                                        SurfaceNormal,
                                        TreeHabitat,
                                        TrailInfluence>;

  class Surface {
  public:
    Surface () = default;
    explicit Surface (const HeightMap& map);

    // Terrain generation mutates the authoritative heightmap.  Refresh is the
    // explicit materialization barrier after heights and normals are final.
    void refresh (const HeightMap& map);

    SurfaceElevation elevation_at (const position_t& position) const {
      return spatial::sample<surface_elevation> (samples (), position);
    }

    SurfaceNormal normal_at (const position_t& position) const {
      return spatial::sample<surface_normal> (samples (), position);
    }

    TreeHabitat tree_habitat_at (const position_t& position) const {
      return spatial::sample<tree_habitat> (samples (), position);
    }

    TrailInfluence trail_influence_at (const position_t& position) const {
      return spatial::sample<trail_influence> (samples (), position);
    }

    // Hydrology is known after the geometric surface is refreshed. This is
    // the explicit second materialization barrier that turns moisture and
    // topography into a vegetation-support reading.
    void derive_tree_habitat (std::span<const float> moisture,
                              meters_t water_level,
                              meters_t tree_line);

    void materialize_trail_influence (std::span<const float> influence);

    const SurfaceBundle& samples () const;

  private:
    std::optional<SurfaceBundle> m_samples;
  };
}

#endif
