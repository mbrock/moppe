#include <moppe/map/surface.hh>

#include <moppe/map/generate.hh>
#include <moppe/profile.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
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
      support += sample (-stencil.dx, -stencil.dz) +
                 sample (stencil.dx, -stencil.dz) +
                 sample (-stencil.dx, stencil.dz) +
                 sample (stencil.dx, stencil.dz);
      return normalized (support);
    }

    void populate (SurfaceBundle& bundle, const HeightMap& map) {
      MOPPE_PROFILE_ZONE ("surface.populate_bundle");
      const float vertical_scale = map.scale ()[1];
      const SnowSupportStencil support_stencil = snow_support_stencil (map);
      for (int row = 0; row < map.height (); ++row)
        for (int column = 0; column < map.width (); ++column) {
          const SurfaceIndex index { static_cast<std::size_t> (column),
                                     static_cast<std::size_t> (row) };
          auto sample = bundle[index];
          spatial::get<surface_elevation> (sample) = SurfaceElevation (
            map.get (column, row) * vertical_scale * surface_elevation[u::m]);
          spatial::get<surface_normal> (sample) =
            map.normal (column, row) * surface_normal[one];
          spatial::get<snow_support> (sample) =
            std::clamp (
              snow_support_normal (map, support_stencil, column, row)[1],
              0.0f,
              1.0f) *
            snow_support[one];
          spatial::get<channel_flux> (sample) = Vec3 () * channel_flux[one];
          spatial::get<tree_habitat> (sample) = 0.0f * tree_habitat[one];
          spatial::get<forest_cover> (sample) = 0.0f * forest_cover[one];
          spatial::get<trail_influence> (sample) = 0.0f * trail_influence[one];
          spatial::get<home_base_influence> (sample) =
            0.0f * home_base_influence[one];
        }
    }

    float smoothstep (float edge0, float edge1, float value) {
      const float t =
        std::clamp ((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t);
    }

    std::uint32_t
    forest_hash (std::uint32_t x, std::uint32_t z, std::uint32_t seed) {
      std::uint32_t value = seed ^ (x * 0x9e3779b9U) ^ (z * 0x85ebca6bU);
      value ^= value >> 16;
      value *= 0x7feb352dU;
      value ^= value >> 15;
      value *= 0x846ca68bU;
      value ^= value >> 16;
      return value;
    }

    float
    forest_hash_value (std::uint32_t x, std::uint32_t z, std::uint32_t seed) {
      return static_cast<float> (forest_hash (x, z, seed) & 0x00ffffffU) /
             static_cast<float> (0x01000000U);
    }

    float periodic_noise (float x,
                          float z,
                          std::uint32_t period_x,
                          std::uint32_t period_z,
                          std::uint32_t seed) {
      const float xf = std::floor (x);
      const float zf = std::floor (z);
      const auto wrap = [] (std::int64_t value, std::uint32_t period) {
        const std::int64_t p = static_cast<std::int64_t> (period);
        return static_cast<std::uint32_t> ((value % p + p) % p);
      };
      const std::uint32_t x0 = wrap (static_cast<std::int64_t> (xf), period_x);
      const std::uint32_t z0 = wrap (static_cast<std::int64_t> (zf), period_z);
      const std::uint32_t x1 = (x0 + 1) % period_x;
      const std::uint32_t z1 = (z0 + 1) % period_z;
      const float tx = smoothstep (0.0f, 1.0f, x - xf);
      const float tz = smoothstep (0.0f, 1.0f, z - zf);
      const float a = forest_hash_value (x0, z0, seed);
      const float b = forest_hash_value (x1, z0, seed);
      const float c = forest_hash_value (x0, z1, seed);
      const float d = forest_hash_value (x1, z1, seed);
      return std::lerp (std::lerp (a, b, tx), std::lerp (c, d, tx), tz);
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

  void Surface::derive_tree_habitat (std::span<const float> moisture,
                                     meters_t water_level,
                                     meters_t tree_line) {
    if (!m_samples)
      throw std::logic_error ("Surface has not been materialized");
    SurfaceBundle& bundle = *m_samples;
    if (moisture.size () != bundle.size ())
      throw std::invalid_argument (
        "Tree habitat needs one moisture value per surface sample");
    if (tree_line <= water_level + 20.0f * u::m)
      throw std::invalid_argument (
        "Tree line must leave a terrestrial habitat band");

    const float shore = meters_value (water_level);
    const float upper = meters_value (tree_line);
    const auto& elevation = spatial::get<surface_elevation> (bundle);
    const auto& normal = spatial::get<surface_normal> (bundle);
    auto& habitat = spatial::get<tree_habitat> (bundle);
    for (std::size_t offset = 0; offset < bundle.size (); ++offset) {
      const float height =
        elevation[offset].quantity_from_zero ().numerical_value_in (u::m);
      const float up = normal[offset].numerical_value_in (one)[1];
      const float dry_ground = smoothstep (shore + 3.0f, shore + 18.0f, height);
      const float below_tree_line =
        1.0f - smoothstep (upper - 35.0f, upper, height);
      const float stable_soil = smoothstep (0.72f, 0.96f, up);
      const float hydrated = smoothstep (0.10f, 0.42f, moisture[offset]);
      const float not_sodden =
        1.0f - smoothstep (0.78f, 0.98f, moisture[offset]);
      const float water_response = 0.28f + 0.72f * hydrated * not_sodden;
      habitat[offset] = dry_ground * below_tree_line * stable_soil *
                        water_response * tree_habitat[one];
    }
  }

  void Surface::derive_forest_cover (std::uint32_t seed) {
    if (!m_samples)
      throw std::logic_error ("Surface has not been materialized");
    SurfaceBundle& bundle = *m_samples;
    const SurfaceDomain& domain = bundle.domain ();
    const auto& habitat = spatial::get<tree_habitat> (bundle);
    const auto& trails = spatial::get<trail_influence> (bundle);
    const auto& home = spatial::get<home_base_influence> (bundle);
    auto& cover = spatial::get<forest_cover> (bundle);
    const float unique_width = static_cast<float> (
      domain.topology () == terrain::Topology::Torus ? domain.width () - 1
                                                     : domain.width ());
    const float unique_height = static_cast<float> (
      domain.topology () == terrain::Topology::Torus ? domain.height () - 1
                                                     : domain.height ());

    for (std::size_t offset = 0; offset < bundle.size (); ++offset) {
      const SurfaceIndex index = domain.index (offset);
      const float u = static_cast<float> (index.column) / unique_width;
      const float v = static_cast<float> (index.row) / unique_height;
      const float broad =
        periodic_noise (u * 7.0f, v * 7.0f, 7, 7, seed ^ 0x4b1d9e37U);
      const float local =
        periodic_noise (u * 23.0f, v * 23.0f, 23, 23, seed ^ 0x91e10da5U);
      const float mosaic = 0.72f * broad + 0.28f * local;
      // A narrow establishment band yields dense stands and real openings,
      // rather than a uniform savanna of half-probable trees.
      const float recruitment = smoothstep (0.44f, 0.61f, mosaic);
      const float support =
        std::pow (habitat[offset].numerical_value_in (one), 1.15f);
      const float route_clearance =
        1.0f - 0.96f * trails[offset].numerical_value_in (one);
      const float settled_clearance =
        1.0f - home[offset].numerical_value_in (one);
      cover[offset] =
        std::clamp (support * recruitment * route_clearance * settled_clearance,
                    0.0f,
                    1.0f) *
        forest_cover[one];
    }
  }

  void Surface::materialize_trail_influence (std::span<const float> influence) {
    if (!m_samples)
      throw std::logic_error ("Surface has not been materialized");
    SurfaceBundle& bundle = *m_samples;
    if (influence.size () != bundle.size ())
      throw std::invalid_argument (
        "Trail influence needs one value per surface sample");
    auto& trail = spatial::get<trail_influence> (bundle);
    for (std::size_t offset = 0; offset < bundle.size (); ++offset)
      trail[offset] =
        std::clamp (influence[offset], 0.0f, 1.0f) * trail_influence[one];
  }

  void
  Surface::materialize_home_base_influence (std::span<const float> influence) {
    if (!m_samples)
      throw std::logic_error ("Surface has not been materialized");
    SurfaceBundle& bundle = *m_samples;
    if (influence.size () != bundle.size ())
      throw std::invalid_argument (
        "Home base influence needs one value per surface sample");
    auto& home_base = spatial::get<home_base_influence> (bundle);
    for (std::size_t offset = 0; offset < bundle.size (); ++offset)
      home_base[offset] =
        std::clamp (influence[offset], 0.0f, 1.0f) * home_base_influence[one];
  }

  void Surface::materialize_channel_flux (std::span<const float> flux) {
    if (!m_samples)
      throw std::logic_error ("Surface has not been materialized");
    SurfaceBundle& bundle = *m_samples;
    if (flux.size () != 2 * bundle.size ())
      throw std::invalid_argument (
        "Channel flux needs one planar vector per surface sample");
    auto& column = spatial::get<channel_flux> (bundle);
    for (std::size_t offset = 0; offset < bundle.size (); ++offset) {
      Vec3 value (flux[2 * offset], 0.0f, flux[2 * offset + 1]);
      const float length = std::sqrt (length2 (value));
      if (length > 1.0f)
        value /= length;
      column[offset] = value * channel_flux[one];
    }
  }

  const SurfaceBundle& Surface::samples () const {
    if (!m_samples)
      throw std::logic_error ("Surface has not been materialized");
    return *m_samples;
  }
}
