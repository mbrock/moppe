#ifndef MOPPE_TERRAIN_DOMAIN_HH
#define MOPPE_TERRAIN_DOMAIN_HH

#include <moppe/gfx/math.hh>
#include <moppe/quantities.hh>
#include <moppe/spatial/bundle.hh>

#include <mp-units/framework.h>

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

// The vocabulary every terrain algorithm shares: what identifies a cell, what
// a cell carries, and the finite periodic lattice they live on. Algorithms
// themselves live in the neighbouring files; nothing here computes anything
// about a particular world. Persisting a domain is a separate concern with a
// separate dependency, so it stays in domain_storage.hh.

namespace moppe::terrain {
  // ---- Identity ----

  struct Seed {
    std::uint32_t value;

    friend constexpr bool operator== (Seed, Seed) = default;
  };

  constexpr Seed next_seed (Seed seed) {
    return Seed { seed.value + 1 };
  }

  template <typename Tag>
  struct Identifier {
    std::uint32_t value;

    constexpr Identifier () noexcept : value (0) {}
    constexpr Identifier (std::uint32_t value) noexcept : value (value) {}

    // Hydrology arrays are dense and indexed by these values. This conversion
    // keeps indexing cheap; construction and assignment remain nominal.
    constexpr operator std::uint32_t () const noexcept {
      return value;
    }

    friend constexpr bool operator== (Identifier, Identifier) = default;
    template <std::integral I>
    friend constexpr bool operator== (Identifier id, I value) noexcept {
      return id.value == static_cast<std::uint32_t> (value);
    }
    template <std::integral I>
    friend constexpr bool operator== (I value, Identifier id) noexcept {
      return static_cast<std::uint32_t> (value) == id.value;
    }

    template <typename OtherTag>
      requires (!std::same_as<Tag, OtherTag>)
    friend bool operator== (Identifier, Identifier<OtherTag>) = delete;
  };

  struct CellIndexTag;
  struct WaterBodyIdTag;
  struct RiverReachIdTag;

  using CellIndex = Identifier<CellIndexTag>;
  using WaterBodyId = Identifier<WaterBodyIdTag>;
  using RiverReachId = Identifier<RiverReachIdTag>;

  inline constexpr CellIndex no_cell {
    std::numeric_limits<std::uint32_t>::max ()
  };
  inline constexpr WaterBodyId no_water_body {
    std::numeric_limits<std::uint32_t>::max ()
  };
  inline constexpr RiverReachId no_river_reach {
    std::numeric_limits<std::uint32_t>::max ()
  };

  // Numbers of entities are quantities of dimension one.  Distinct kinds
  // preserve their algebra while preventing unrelated counts from mixing.
  inline constexpr struct cell_count
      : mp_units::quantity_spec<mp_units::dimensionless, mp_units::is_kind> {
  } cell_count;
  inline constexpr struct reach_count
      : mp_units::quantity_spec<mp_units::dimensionless, mp_units::is_kind> {
  } reach_count;
  inline constexpr struct iteration_count
      : mp_units::quantity_spec<mp_units::dimensionless, mp_units::is_kind> {
  } iteration_count;
  inline constexpr struct separation_cell_count
      : mp_units::quantity_spec<mp_units::dimensionless, mp_units::is_kind> {
  } separation_cell_count;

  using CellCount = mp_units::quantity<cell_count[mp_units::one], std::size_t>;
  using ReachCount =
    mp_units::quantity<reach_count[mp_units::one], std::size_t>;
  using IterationCount =
    mp_units::quantity<iteration_count[mp_units::one], int>;
  using SeparationCellCount =
    mp_units::quantity<separation_cell_count[mp_units::one], std::size_t>;

  template <mp_units::Quantity Q>
  constexpr auto count_value (Q value) noexcept {
    return value.numerical_value_in (mp_units::one);
  }

  // ---- Topology ----

  // The world is a torus.  These are the wrap operations every lattice
  // consumer shares; the period is always the full lattice extent.
  inline int wrap_index (int value, int period) {
    const int remainder = value % period;
    return remainder < 0 ? remainder + period : remainder;
  }

  inline float wrap_coordinate (float value, float period) {
    const float wrapped = value - std::floor (value / period) * period;
    return wrapped < period ? wrapped : 0.0f;
  }

  inline float minimum_image_delta (float delta, float period) {
    return delta - std::round (delta / period) * period;
  }

  inline float nearest_image (float value, float reference, float period) {
    return reference + minimum_image_delta (value - reference, period);
  }

  // ---- Cell quantities ----

  // A point in the world's vertical reference frame. Terrain storage uses
  // metres directly; differences between elevations are ordinary lengths.
  inline constexpr struct surface_elevation
      : quantity_spec<mp_units::isq::height, mp_units::is_kind> {
  } surface_elevation;

  using SurfaceElevation =
    quantity_point<surface_elevation[u::m],
                   default_point_origin (surface_elevation[u::m]),
                   float>;

  inline SurfaceElevation surface_elevation_point (meters_t value) {
    return SurfaceElevation (meters_value (value) * surface_elevation[u::m]);
  }

  inline float surface_elevation_value (SurfaceElevation value) {
    return value.quantity_from_zero ().numerical_value_in (u::m);
  }

  inline constexpr struct terrain_normal
      : quantity_spec<mp_units::dimensionless,
                      mp_units::quantity_tensor_order::vector,
                      mp_units::is_kind> {
  } terrain_normal;

  using TerrainNormal = mp_units::quantity<terrain_normal[mp_units::one], Vec3>;

  inline constexpr struct surface_moisture
      : quantity_spec<mp_units::dimensionless> {
  } surface_moisture;

  inline constexpr struct waterline_distance
      : quantity_spec<mp_units::isq::length, mp_units::is_kind> {
  } waterline_distance;

  inline constexpr struct standing_water_depth
      : quantity_spec<mp_units::isq::length, mp_units::non_negative> {
  } standing_water_depth;

  inline constexpr struct wave_amplitude
      : quantity_spec<mp_units::dimensionless> {
  } wave_amplitude;

  inline constexpr struct water_velocity
      : quantity_spec<mp_units::isq::speed,
                      mp_units::quantity_tensor_order::vector,
                      mp_units::is_kind> {
  } water_velocity;

  inline constexpr struct trail_influence
      : quantity_spec<mp_units::dimensionless> {
  } trail_influence;

  inline constexpr struct home_base_influence
      : quantity_spec<mp_units::dimensionless> {
  } home_base_influence;

  using SurfaceMoisture = quantity<surface_moisture[one], float>;
  using WaterlineDistance = quantity<waterline_distance[u::m], float>;
  using StandingWaterDepth = quantity<standing_water_depth[u::m], float>;
  using WaveAmplitude = quantity<wave_amplitude[one], float>;
  using WaterVelocity = quantity<water_velocity[u::m / u::s], Vec3>;
  using TrailInfluence = quantity<trail_influence[one], float>;
  using HomeBaseInfluence = quantity<home_base_influence[one], float>;

  static_assert (sizeof (SurfaceElevation) == sizeof (float));
  static_assert (alignof (SurfaceElevation) == alignof (float));
  static_assert (std::is_trivially_copyable_v<SurfaceElevation>);
  static_assert (sizeof (TerrainNormal) == sizeof (Vec3));
  static_assert (alignof (TerrainNormal) == alignof (Vec3));
  static_assert (std::is_trivially_copyable_v<TerrainNormal>);

  // ---- The lattice ----

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
                   meters_t spacing_x = 1.0f * u::m,
                   meters_t spacing_z = 1.0f * u::m)
        : m_width (width), m_height (height), m_spacing_x (spacing_x),
          m_spacing_z (spacing_z) {
      if (width < 2 || height < 2 || spacing_x <= 0.0f * u::m ||
          spacing_z <= 0.0f * u::m)
        throw std::invalid_argument ("invalid terrain domain");
    }

    TerrainDomain (std::size_t width,
                   std::size_t height,
                   const spatial_extent_t& extent)
        : TerrainDomain (
            width,
            height,
            extent_value (extent)[0] / static_cast<float> (width) * u::m,
            extent_value (extent)[2] / static_cast<float> (height) * u::m) {}

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
    float spacing_x_m () const noexcept {
      return meters_value (m_spacing_x);
    }
    float spacing_z_m () const noexcept {
      return meters_value (m_spacing_z);
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

  // ---- Elevation over the lattice ----

  using ElevationMap = spatial::Bundle<TerrainDomain, SurfaceElevation>;

  template <typename Bundle>
  concept TerrainElevations =
    requires { typename std::remove_cvref_t<Bundle>::domain_type; } &&
    std::same_as<typename std::remove_cvref_t<Bundle>::domain_type,
                 TerrainDomain> &&
    spatial::BundleContains<surface_elevation, std::remove_cvref_t<Bundle>>;

  template <TerrainElevations Bundle>
  std::span<const SurfaceElevation>
  elevations (const Bundle& terrain) noexcept {
    return spatial::get<surface_elevation> (terrain);
  }

  template <std::ranges::contiguous_range Samples>
    requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<Samples>>,
                          float>
  ElevationMap make_elevation_map (TerrainDomain domain, Samples&& samples) {
    const std::span<const float> values (std::ranges::data (samples),
                                         std::ranges::size (samples));
    if (values.size () != domain.size ())
      throw std::invalid_argument (
        "elevation samples do not match terrain domain");
    ElevationMap result (std::move (domain));
    std::ranges::transform (values,
                            spatial::get<surface_elevation> (result).begin (),
                            [] (float value) {
                              return SurfaceElevation (value *
                                                       surface_elevation[u::m]);
                            });
    return result;
  }

  inline float elevation_at (const TerrainDomain& domain,
                             std::span<const SurfaceElevation> values,
                             std::size_t column,
                             std::size_t row) {
    return surface_elevation_value (
      values[domain.offset ({ .column = column, .row = row })]);
  }
}

#endif
