#include <moppe/map/surface.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace moppe::map {
  namespace {
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

  void Surface::derive_tree_habitat (meters_t water_level, meters_t tree_line) {
    SurfaceSections& values = mutable_sections ();
    if (!m_materialized.moisture)
      throw std::logic_error (
        "Tree habitat needs a materialized moisture section");
    if (tree_line <= water_level + 20.0f * u::m)
      throw std::invalid_argument (
        "Tree line must leave a terrestrial habitat band");

    const float shore = meters_value (water_level);
    const float upper = meters_value (tree_line);
    const auto& elevation = spatial::get<surface_elevation> (values);
    const auto& normal = spatial::get<surface_normal> (values);
    const auto& moisture = spatial::get<surface_moisture> (values);
    auto& habitat = spatial::get<tree_habitat> (values);
    for (std::size_t offset = 0; offset < values.size (); ++offset) {
      const float height =
        elevation[offset].quantity_from_zero ().numerical_value_in (u::m);
      const float up = normal[offset].numerical_value_in (one)[1];
      const float dry_ground = smoothstep (shore + 3.0f, shore + 18.0f, height);
      const float below_tree_line =
        1.0f - smoothstep (upper - 35.0f, upper, height);
      const float stable_soil = smoothstep (0.72f, 0.96f, up);
      const float wetness = moisture[offset].numerical_value_in (one);
      const float hydrated = smoothstep (0.10f, 0.42f, wetness);
      const float not_sodden = 1.0f - smoothstep (0.78f, 0.98f, wetness);
      const float water_response = 0.28f + 0.72f * hydrated * not_sodden;
      habitat[offset] = dry_ground * below_tree_line * stable_soil *
                        water_response * tree_habitat[one];
    }
    m_materialized.tree_habitat = true;
  }

  void Surface::derive_forest_cover (std::uint32_t seed) {
    SurfaceSections& values = mutable_sections ();
    if (!m_materialized.tree_habitat)
      throw std::logic_error (
        "Forest cover needs a materialized tree habitat section");
    const SurfaceDomain& domain = values.domain ();
    const auto& habitat = spatial::get<tree_habitat> (values);
    const auto& trails = spatial::get<trail_influence> (values);
    const auto& home = spatial::get<home_base_influence> (values);
    auto& cover = spatial::get<forest_cover> (values);
    const float unique_width = static_cast<float> (
      domain.topology () == terrain::Topology::Torus ? domain.width () - 1
                                                     : domain.width ());
    const float unique_height = static_cast<float> (
      domain.topology () == terrain::Topology::Torus ? domain.height () - 1
                                                     : domain.height ());

    for (std::size_t offset = 0; offset < values.size (); ++offset) {
      const SurfaceIndex index = domain.index (offset);
      const float u = static_cast<float> (index.column) / unique_width;
      const float v = static_cast<float> (index.row) / unique_height;
      const float broad =
        periodic_noise (u * 7.0f, v * 7.0f, 7, 7, seed ^ 0x4b1d9e37U);
      const float local =
        periodic_noise (u * 23.0f, v * 23.0f, 23, 23, seed ^ 0x91e10da5U);
      const float mosaic = 0.72f * broad + 0.28f * local;
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
    m_materialized.forest_cover = true;
  }
}
