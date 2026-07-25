#include <moppe/map/surface.hh>

#include <moppe/profile.hh>
#include <moppe/spatial/bundle_storage.hh>
#include <moppe/terrain/domain_storage.hh>
#include <moppe/terrain/river.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace moppe::map {
  // ---- Geometry ----

  namespace {
    struct SnowSupportStencil {
      int width;
      int height;
      int dx;
      int dz;
    };

    SnowSupportStencil snow_support_stencil (const SurfaceGeometry& geometry) {
      constexpr meters_t support_radius = 24.0f * u::m;
      const terrain::TerrainDomain& domain = geometry.domain ();
      return {
        .width = static_cast<int> (domain.width ()),
        .height = static_cast<int> (domain.height ()),
        .dx =
          std::max (1,
                    static_cast<int> (std::lround (
                      meters_value (support_radius) / domain.spacing_x_m ()))),
        .dz =
          std::max (1,
                    static_cast<int> (std::lround (
                      meters_value (support_radius) / domain.spacing_z_m ()))),
      };
    }

    Vec3 snow_support_normal (const SurfaceGeometry& geometry,
                              const SnowSupportStencil& stencil,
                              int column,
                              int row) {
      column = terrain::wrap_index (column, stencil.width);
      row = terrain::wrap_index (row, stencil.height);
      const auto sample = [&] (int x, int z) {
        const terrain::TerrainIndex index {
          static_cast<std::size_t> (
            terrain::wrap_index (column + x, stencil.width)),
          static_cast<std::size_t> (
            terrain::wrap_index (row + z, stencil.height))
        };
        return spatial::get<terrain::terrain_normal> (geometry[index])
          .numerical_value_in (mp_units::one);
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

    void populate_snow_support (SurfaceGeometry& geometry) {
      MOPPE_PROFILE_ZONE ("surface.populate_snow_support");
      const SnowSupportStencil stencil = snow_support_stencil (geometry);
      for (int row = 0; row < stencil.height; ++row)
        for (int column = 0; column < stencil.width; ++column) {
          const terrain::TerrainIndex index { static_cast<std::size_t> (column),
                                              static_cast<std::size_t> (row) };
          auto site = geometry[index];
          spatial::get<snow_support> (site) =
            std::clamp (snow_support_normal (geometry, stencil, column, row)[1],
                        0.0f,
                        1.0f) *
            snow_support[one];
        }
    }

    void recompute_surface_normals (SurfaceGeometry& geometry) {
      MOPPE_PROFILE_ZONE ("map::recompute_normals");
      const terrain::TerrainDomain& domain = geometry.domain ();
      const int width = static_cast<int> (domain.width ());
      const int height = static_cast<int> (domain.height ());
      const auto& elevations =
        spatial::get<terrain::surface_elevation> (geometry);
      auto& normals = spatial::get<terrain::terrain_normal> (geometry);
      std::ranges::fill (
        normals, Vec3 (0, 0, 0) * terrain::terrain_normal[mp_units::one]);

      const auto point = [&] (int column, int row) {
        const terrain::TerrainIndex index {
          static_cast<std::size_t> (terrain::wrap_index (column, width)),
          static_cast<std::size_t> (terrain::wrap_index (row, height))
        };
        return Vec3 (
          domain.spacing_x_m () * column,
          terrain::surface_elevation_value (elevations[domain.offset (index)]),
          domain.spacing_z_m () * row);
      };
      const auto face = [&] (int x1, int y1, int x2, int y2, int x3, int y3) {
        return normalized (cross (point (x2, y2) - point (x1, y1),
                                  point (x3, y3) - point (x1, y1)));
      };
      const auto add = [&] (int column, int row, const Vec3& value) {
        const terrain::TerrainIndex index {
          static_cast<std::size_t> (terrain::wrap_index (column, width)),
          static_cast<std::size_t> (terrain::wrap_index (row, height))
        };
        SurfaceNormal& normal = normals[domain.offset (index)];
        normal = (normal.numerical_value_in (mp_units::one) + value) *
                 terrain::terrain_normal[mp_units::one];
      };
      for (int row = 0; row < height; ++row)
        for (int column = 0; column < width; ++column) {
          const Vec3 left =
            face (column, row, column, row + 1, column + 1, row + 1);
          const Vec3 right =
            face (column, row, column + 1, row + 1, column + 1, row);
          add (column, row, left);
          add (column, row + 1, left);
          add (column + 1, row + 1, left);
          add (column, row, right);
          add (column + 1, row, right);
          add (column + 1, row + 1, right);
        }
      for (SurfaceNormal& value : normals) {
        Vec3 normal = value.numerical_value_in (mp_units::one);
        normalize (normal);
        value = normal * terrain::terrain_normal[mp_units::one];
      }
    }
  }

  void rebuild_geometry (SurfaceGeometry& geometry) {
    MOPPE_PROFILE_ZONE ("map::rebuild_geometry");
    recompute_surface_normals (geometry);
    populate_snow_support (geometry);
  }

  // ---- Generation ----

  namespace {
    constexpr float coastline = 0.4f;
    constexpr float initial_land_relief_m = 20.0f;
    constexpr float initial_bathymetric_relief_m = 240.0f;
    constexpr float maximum_uplift_m_per_year = 0.001f;

    void reset_material_history (SurfaceGeometry& surface) {
      std::ranges::fill (spatial::get<eroded_surface_material> (surface),
                         0.0f * eroded_surface_material[one]);
      std::ranges::fill (spatial::get<deposited_surface_material> (surface),
                         0.0f * deposited_surface_material[one]);
    }

    void record_material_change (SurfaceGeometry& surface,
                                 std::size_t offset,
                                 float delta) {
      auto& eroded = spatial::get<eroded_surface_material> (surface);
      auto& deposited = spatial::get<deposited_surface_material> (surface);
      if (delta < 0.0f)
        eroded[offset] = (eroded[offset].numerical_value_in (one) - delta) *
                         eroded_surface_material[one];
      else
        deposited[offset] =
          (deposited[offset].numerical_value_in (one) + delta) *
          deposited_surface_material[one];
    }

    void set_elevations (SurfaceGeometry& surface,
                         std::span<const float> heights) {
      const int width = static_cast<int> (surface.domain ().width ());
      const int height = static_cast<int> (surface.domain ().height ());
      for (int row = 0; row < height; ++row)
        for (int column = 0; column < width; ++column)
          spatial::get<terrain::surface_elevation> (
            surface[terrain::TerrainIndex { static_cast<std::size_t> (column),
                                            static_cast<std::size_t> (row) }]) =
            SurfaceElevation (
              heights[static_cast<std::size_t> (row) * width + column] *
              terrain::surface_elevation[u::m]);
    }
  }

  std::vector<meters_per_julian_year_t>
  initialize_terrain (SurfaceGeometry& surface,
                      terrain::Seed seed,
                      meters_t water_datum,
                      const terrain::GeologicalProgress& progress) {
    MOPPE_PROFILE_ZONE ("terrain.initialize");
    terrain::GeologicalSections geology =
      terrain::generate_geology (surface.domain (), seed, progress);
    const auto& continent = spatial::get<terrain::continent_shape> (geology);
    const auto& weights = spatial::get<terrain::uplift_weight> (geology);
    auto& elevations = spatial::get<terrain::surface_elevation> (surface);
    const float sea_level_m = meters_value (water_datum);

    std::vector<meters_per_julian_year_t> uplift;
    uplift.reserve (geology.size ());
    for (std::size_t offset = 0; offset < geology.size (); ++offset) {
      const float land = continent[offset].numerical_value_in (one) - coastline;
      const float relief =
        land < 0.0f ? initial_bathymetric_relief_m : initial_land_relief_m;
      elevations[offset] = SurfaceElevation ((sea_level_m + relief * land) *
                                             terrain::surface_elevation[u::m]);
      uplift.push_back (weights[offset].numerical_value_in (one) *
                        maximum_uplift_m_per_year * mp_units::si::metre /
                        mp_units::astronomy::Julian_year);
    }
    reset_material_history (surface);
    return uplift;
  }

  terrain::StreamPowerEvolutionReport
  evolve_terrain (SurfaceGeometry& surface,
                  std::span<const meters_per_julian_year_t> uplift,
                  const terrain::StreamPowerEvolution& parameters,
                  const terrain::StreamPowerEvolutionBackend* backend,
                  const terrain::StreamPowerProgress& progress) {
    MOPPE_PROFILE_ZONE ("terrain.evolve");
    terrain::StreamPowerEvolutionResult result =
      backend
        ? terrain::evolve_stream_power (
            surface, uplift, parameters, *backend, progress)
        : terrain::evolve_stream_power (surface, uplift, parameters, progress);
    set_elevations (surface, result.heights);
    return result.report;
  }

  terrain::TrailNetwork
  form_terrain_trails (SurfaceGeometry& surface,
                       const terrain::TrailFormation& parameters) {
    MOPPE_PROFILE_ZONE ("terrain.form_trails");
    terrain::TrailFormationResult result =
      terrain::form_trails (surface, parameters);
    const int width = static_cast<int> (surface.domain ().width ());
    const int height = static_cast<int> (surface.domain ().height ());
    for (int row = 0; row < height; ++row)
      for (int column = 0; column < width; ++column) {
        const std::size_t offset =
          static_cast<std::size_t> (row) * width + column;
        const float previous = terrain::surface_elevation_value (
          spatial::get<terrain::surface_elevation> (
            surface[terrain::TerrainIndex { static_cast<std::size_t> (column),
                                            static_cast<std::size_t> (row) }]));
        record_material_change (
          surface, offset, result.heights[offset] - previous);
        spatial::get<terrain::surface_elevation> (
          surface[terrain::TerrainIndex { static_cast<std::size_t> (column),
                                          static_cast<std::size_t> (row) }]) =
          SurfaceElevation (result.heights[offset] *
                            terrain::surface_elevation[u::m]);
      }
    return std::move (result.network);
  }

  // ---- Readings ----

  ChannelFluxMap
  analyze_channel_flux (const terrain::TerrainDomain& domain,
                        const terrain::FractionalDrainage& channels) {
    const auto& tangents = spatial::get<terrain::channel_tangent> (channels);
    const auto& areas =
      spatial::get<terrain::fractional_contributing_area> (channels);
    const terrain::TerrainDomain& grid = channels.domain ().terrain_domain ();
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

    ChannelFluxMap flux (domain);
    auto& column = spatial::get<channel_flux> (flux);
    for (std::size_t offset = 0; offset < domain.size (); ++offset) {
      const float area_m2 = areas[offset].numerical_value_in (u::m * u::m);
      const float activity = std::clamp (
        std::log (std::max (area_m2 / floor_area_m2, 1e-6f)) / activity_span,
        0.0f,
        1.0f);
      column[offset] = tangents[offset].numerical_value_in (one) * activity *
                       channel_flux[one];
    }
    return flux;
  }

  namespace {
    template <typename Values>
    float robust_positive_scale (const Values& values) {
      std::vector<float> positive;
      positive.reserve (values.size () / 8);
      for (const auto value : values) {
        const float scalar = value.numerical_value_in (mp_units::one);
        if (scalar > 0.0f)
          positive.push_back (scalar);
      }
      if (positive.empty ())
        return 1.0f;
      const std::size_t rank = positive.size () * 49 / 50;
      std::nth_element (
        positive.begin (), positive.begin () + rank, positive.end ());
      return std::max (positive[rank], 1e-6f);
    }
  }

  GeologyMaterials analyze_geology_materials (const SurfaceGeometry& geometry) {
    const auto& eroded = spatial::get<eroded_surface_material> (geometry);
    const auto& deposited = spatial::get<deposited_surface_material> (geometry);
    const float eroded_scale = robust_positive_scale (eroded);
    const float deposited_scale = robust_positive_scale (deposited);
    GeologyMaterials values (geometry.domain ());
    auto& exposure = spatial::get<erosion_exposure> (values);
    auto& cover = spatial::get<deposition_cover> (values);
    for (std::size_t offset = 0; offset < values.size (); ++offset) {
      exposure[offset] =
        std::clamp (eroded[offset].numerical_value_in (mp_units::one) /
                      eroded_scale,
                    0.0f,
                    1.0f) *
        erosion_exposure[one];
      cover[offset] =
        std::clamp (deposited[offset].numerical_value_in (mp_units::one) /
                      deposited_scale,
                    0.0f,
                    1.0f) *
        deposition_cover[one];
    }
    return values;
  }

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

  TreeHabitatMap analyze_tree_habitat (const SurfaceGeometry& geometry,
                                       const terrain::MoistureMap& moisture,
                                       meters_t water_level,
                                       meters_t tree_line) {
    if (moisture.domain () != geometry.domain ())
      throw std::invalid_argument (
        "Moisture does not share the surface domain");
    if (tree_line <= water_level + 20.0f * u::m)
      throw std::invalid_argument (
        "Tree line must leave a terrestrial habitat band");

    const float shore = meters_value (water_level);
    const float upper = meters_value (tree_line);
    const auto& elevation = spatial::get<terrain::surface_elevation> (geometry);
    const auto& normal = spatial::get<terrain::terrain_normal> (geometry);
    const auto& wetness_column = spatial::get<surface_moisture> (moisture);
    TreeHabitatMap values (geometry.domain ());
    auto& habitat = spatial::get<tree_habitat> (values);
    for (std::size_t offset = 0; offset < geometry.size (); ++offset) {
      const float height = terrain::surface_elevation_value (elevation[offset]);
      const float up = normal[offset].numerical_value_in (one)[1];
      const float dry_ground = smoothstep (shore + 3.0f, shore + 18.0f, height);
      const float below_tree_line =
        1.0f - smoothstep (upper - 35.0f, upper, height);
      const float stable_soil = smoothstep (0.72f, 0.96f, up);
      const float wetness = wetness_column[offset].numerical_value_in (one);
      const float hydrated = smoothstep (0.10f, 0.42f, wetness);
      const float not_sodden = 1.0f - smoothstep (0.78f, 0.98f, wetness);
      const float water_response = 0.28f + 0.72f * hydrated * not_sodden;
      habitat[offset] = dry_ground * below_tree_line * stable_soil *
                        water_response * tree_habitat[one];
    }
    return values;
  }

  ForestCoverMap analyze_forest_cover (const TreeHabitatMap& habitat,
                                       const terrain::TrailUseMap& use,
                                       std::uint32_t seed) {
    if (use.domain () != habitat.domain ())
      throw std::invalid_argument (
        "Trail use does not share the surface domain");
    const terrain::TerrainDomain& domain = habitat.domain ();
    const auto& habitat_column = spatial::get<tree_habitat> (habitat);
    const auto& trails = spatial::get<trail_influence> (use);
    const auto& home_base = spatial::get<home_base_influence> (use);
    ForestCoverMap values (domain);
    auto& cover = spatial::get<forest_cover> (values);
    const float width = static_cast<float> (domain.width ());
    const float height = static_cast<float> (domain.height ());

    for (std::size_t offset = 0; offset < domain.size (); ++offset) {
      const terrain::TerrainIndex index = domain.index (offset);
      const float u = static_cast<float> (index.column) / width;
      const float v = static_cast<float> (index.row) / height;
      const float broad =
        periodic_noise (u * 7.0f, v * 7.0f, 7, 7, seed ^ 0x4b1d9e37U);
      const float local =
        periodic_noise (u * 23.0f, v * 23.0f, 23, 23, seed ^ 0x91e10da5U);
      const float mosaic = 0.72f * broad + 0.28f * local;
      const float recruitment = smoothstep (0.44f, 0.61f, mosaic);
      const float support =
        std::pow (habitat_column[offset].numerical_value_in (one), 1.15f);
      const float route_clearance =
        1.0f - 0.96f * trails[offset].numerical_value_in (one);
      const float settled_clearance =
        1.0f - home_base[offset].numerical_value_in (one);
      cover[offset] =
        std::clamp (support * recruitment * route_clearance * settled_clearance,
                    0.0f,
                    1.0f) *
        forest_cover[one];
    }
    return values;
  }

  // ---- Cache ----

  bool try_load_cache (SurfaceGeometry& geometry, const std::string& path) {
    std::ifstream file (path, std::ios::binary);
    if (!file)
      return false;
    return spatial::load_bundle (file, geometry);
  }

  void save_cache (const SurfaceGeometry& geometry, const std::string& path) {
    std::ofstream file (path, std::ios::binary);
    if (!file)
      throw std::runtime_error ("can't write surface cache: " + path);
    spatial::write_bundle (file, geometry);
    if (!file)
      throw std::runtime_error ("can't write surface cache: " + path);
  }
}
