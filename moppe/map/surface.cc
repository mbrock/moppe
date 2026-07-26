#include <moppe/gfx/signal.hh>
#include <moppe/map/surface.hh>

#include <moppe/profile.hh>
#include <moppe/spatial/bundle_operations.hh>
#include <moppe/spatial/bundle_storage.hh>
#include <moppe/terrain/domain_storage.hh>
#include <moppe/terrain/river.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace moppe::map {
  // ---- Geometry ----

  namespace {
    // How far the broad support plane reaches, as a step across the lattice.
    // This is the one place the metre radius meets the sample spacing.
    struct SnowSupportStencil {
      int dx;
      int dz;
    };

    SnowSupportStencil
    snow_support_stencil (const terrain::TerrainDomain& domain) {
      constexpr meters_t support_radius = 24.0f * u::m;
      const auto cells = [] (meters_t radius, meters_t spacing) {
        return std::max (
          1,
          static_cast<int> (std::lround (
            (radius / spacing).numerical_value_in (mp_units::one))));
      };
      return {
        .dx = cells (support_radius, domain.spacing_x ()),
        .dz = cells (support_radius, domain.spacing_z ()),
      };
    }

    Vec3 snow_support_normal (const SurfaceGeometry& geometry,
                              const SnowSupportStencil& stencil,
                              terrain::TerrainIndex site) {
      const terrain::TerrainDomain& domain = geometry.domain ();
      const auto sample = [&] (int dx, int dz) {
        return spatial::get<terrain::terrain_normal> (
                 geometry[domain.shifted (site, dx, dz)])
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
      const SnowSupportStencil stencil =
        snow_support_stencil (geometry.domain ());
      spatial::for_each_site (geometry, [&] (const auto& site) {
        spatial::get<snow_support> (site) =
          std::clamp (snow_support_normal (geometry, stencil, site.index ())[1],
                      0.0f,
                      1.0f) *
          snow_support[one];
      });
    }

    void recompute_surface_normals (SurfaceGeometry& geometry) {
      MOPPE_PROFILE_ZONE ("map::recompute_normals");
      const terrain::TerrainDomain& domain = geometry.domain ();
      std::ranges::fill (spatial::get<terrain::terrain_normal> (geometry),
                         Vec3 (0, 0, 0) *
                           terrain::terrain_normal[mp_units::one]);

      // A cell corner, named as a step away from the site that owns the cell.
      // The elevation comes from the wrapped position, but the coordinate does
      // not wrap: a face spanning the seam has to stay continuous, or its
      // normal would fold back on itself there.
      const auto corner = [&] (terrain::TerrainIndex site, int dx, int dz) {
        return Vec3 (domain.spacing_x ().numerical_value_in (u::m) *
                       (static_cast<int> (site.column) + dx),
                     terrain::surface_elevation_value (
                       spatial::get<terrain::surface_elevation> (
                         geometry[domain.shifted (site, dx, dz)])),
                     domain.spacing_z ().numerical_value_in (u::m) *
                       (static_cast<int> (site.row) + dz));
      };
      const auto add =
        [&] (terrain::TerrainIndex site, int dx, int dz, const Vec3& value) {
          SurfaceNormal& normal = spatial::get<terrain::terrain_normal> (
            geometry[domain.shifted (site, dx, dz)]);
          normal = (normal.numerical_value_in (mp_units::one) + value) *
                   terrain::terrain_normal[mp_units::one];
        };

      // Each site owns the cell reaching one step further along both axes,
      // split into two triangles whose face normals every touched corner
      // accumulates.
      for (const terrain::TerrainIndex site : spatial::sites (geometry)) {
        const Vec3 origin = corner (site, 0, 0);
        const Vec3 left = normalized (
          cross (corner (site, 0, 1) - origin, corner (site, 1, 1) - origin));
        const Vec3 right = normalized (
          cross (corner (site, 1, 1) - origin, corner (site, 1, 0) - origin));
        add (site, 0, 0, left);
        add (site, 0, 1, left);
        add (site, 1, 1, left);
        add (site, 0, 0, right);
        add (site, 1, 0, right);
        add (site, 1, 1, right);
      }

      for (SurfaceNormal& value :
           spatial::get<terrain::terrain_normal> (geometry)) {
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
    constexpr auto coastline = 0.4f * terrain::continent_shape[one];
    constexpr meters_t initial_land_relief = 20.0f * u::m;
    constexpr meters_t initial_bathymetric_relief = 240.0f * u::m;
    constexpr auto maximum_uplift_m_per_year =
      0.001f * u::m / mp_units::astronomy::Julian_year;

    void reset_material_history (SurfaceGeometry& surface) {
      std::ranges::fill (spatial::get<eroded_surface_material> (surface),
                         0.0f * eroded_surface_material[one]);
      std::ranges::fill (spatial::get<deposited_surface_material> (surface),
                         0.0f * deposited_surface_material[one]);
    }

    void record_material_change (SurfaceGeometry& surface,
                                 terrain::TerrainIndex site,
                                 float delta) {
      const auto row = surface[site];
      if (delta < 0.0f)
        spatial::get<eroded_surface_material> (row) -=
          delta * eroded_surface_material[one];
      else
        spatial::get<deposited_surface_material> (row) +=
          delta * deposited_surface_material[one];
    }

    // The solver still answers in bare lattice samples, so this is where a
    // linear buffer meets the surface. Asking the domain for each site's
    // offset keeps that the only place the two orders have to agree.
    void set_elevations (SurfaceGeometry& surface,
                         std::span<const float> heights) {
      const terrain::TerrainDomain& domain = surface.domain ();
      for (const terrain::TerrainIndex site : spatial::sites (surface))
        spatial::get<terrain::surface_elevation> (surface[site]) =
          SurfaceElevation (heights[domain.offset (site)] *
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
    std::vector<meters_per_julian_year_t> uplift;
    uplift.reserve (geology.size ());
    for (const terrain::TerrainIndex site : spatial::sites (surface)) {
      const auto ground = geology[site];
      const auto land =
        spatial::get<terrain::continent_shape> (ground) - coastline;
      const meters_t relief =
        land < 0.0f ? initial_bathymetric_relief : initial_land_relief;
      spatial::get<terrain::surface_elevation> (surface[site]) =
        terrain::surface_elevation_point (water_datum + relief * land);
      uplift.push_back (spatial::get<terrain::uplift_weight> (ground) *
                        maximum_uplift_m_per_year);
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
    const terrain::TerrainDomain& domain = surface.domain ();
    for (const terrain::TerrainIndex site : spatial::sites (surface)) {
      const float formed = result.heights[domain.offset (site)];
      auto& elevation =
        spatial::get<terrain::surface_elevation> (surface[site]);
      record_material_change (
        surface, site, formed - terrain::surface_elevation_value (elevation));
      elevation = SurfaceElevation (formed * terrain::surface_elevation[u::m]);
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
    const square_meters_t floor_area = 4.0f * grid.cell_area ();
    const auto reach = [&] (auto area) {
      return (area / floor_area).numerical_value_in (mp_units::one);
    };
    const float activity_span = std::log (
      std::max (reach (terrain::visible_river_minimum_area (grid)), 1.001f));

    // Still an offset walk: the drainage is a bundle over TerrainCellDomain
    // and the flux is one over TerrainDomain, so the two are related only by
    // sharing a storage order. Saying this in positions needs a way to map one
    // domain's sites onto another's, which the spatial layer does not have.
    ChannelFluxMap flux (domain);
    auto& column = spatial::get<channel_flux> (flux);
    for (std::size_t offset = 0; offset < domain.size (); ++offset) {
      const float activity = std::clamp (
        std::log (std::max (reach (areas[offset]), 1e-6f)) / activity_span,
        0.0f,
        1.0f);
      column[offset] = tangents[offset].numerical_value_in (one) * activity *
                       channel_flux[one];
    }
    return flux;
  }

  namespace {
    // Quantities of one spec are ordered, so the percentile never has to
    // leave the column's own type: nth_element compares them directly and
    // the scale that comes back divides its own readings.
    template <typename Values>
    auto robust_positive_scale (const Values& values) {
      using Value = std::ranges::range_value_t<Values>;
      constexpr Value zero {};
      std::vector<Value> positive;
      positive.reserve (values.size () / 8);
      for (const Value value : values)
        if (value > zero)
          positive.push_back (value);
      if (positive.empty ())
        return 1.0f * Value::reference;
      const std::size_t rank = positive.size () * 49 / 50;
      std::nth_element (
        positive.begin (), positive.begin () + rank, positive.end ());
      return std::max (positive[rank], 1e-6f * Value::reference);
    }
  }

  GeologyMaterials analyze_geology_materials (const SurfaceGeometry& geometry) {
    const auto& eroded = spatial::get<eroded_surface_material> (geometry);
    const auto& deposited = spatial::get<deposited_surface_material> (geometry);
    const auto eroded_scale = robust_positive_scale (eroded);
    const auto deposited_scale = robust_positive_scale (deposited);
    GeologyMaterials values (geometry.domain ());
    for (const terrain::TerrainIndex site : spatial::sites (geometry)) {
      const auto ground = geometry[site];
      const auto reading = values[site];
      // A reading over the scale drawn from its own column is a ratio, and a
      // ratio of two like quantities has no unit to speak of.
      const auto share = [] (auto value, auto scale) {
        return std::clamp (
          (value / scale).numerical_value_in (mp_units::one), 0.0f, 1.0f);
      };
      spatial::get<erosion_exposure> (reading) =
        share (spatial::get<eroded_surface_material> (ground), eroded_scale) *
        erosion_exposure[one];
      spatial::get<deposition_cover> (reading) =
        share (spatial::get<deposited_surface_material> (ground),
               deposited_scale) *
        deposition_cover[one];
    }
    return values;
  }

  TreeHabitatMap analyze_tree_habitat (const SurfaceGeometry& geometry,
                                       const terrain::MoistureMap& moisture,
                                       meters_t water_level,
                                       meters_t tree_tops) {
    if (moisture.domain () != geometry.domain ())
      throw std::invalid_argument (
        "Moisture does not share the surface domain");

    using spatial::get;
    using u::m;

    if (tree_tops <= water_level + 20.0f * m)
      throw std::invalid_argument (
        "Tree line must leave a terrestrial habitat band");

    TreeHabitatMap values (geometry.domain ());

    for (const terrain::TerrainIndex site : spatial::sites (geometry)) {
      const auto ground = geometry[site];
      const auto ground_normal = get<terrain::terrain_normal> (ground);
      const auto ground_level =
        get<terrain::surface_elevation> (ground).quantity_from_zero ();

      const auto ground_horizontality =
        ground_normal.numerical_value_in (one)[1] * one;

      const auto soil_stability =
        band (0.72f * one, 0.96f * one, ground_horizontality);

      const auto soil_dryness =
        band (water_level + 3.0f * m, water_level + 18.0f * m, ground_level);

      // Trees thin out as the ground approaches the tree line and stop at
      // it. The band rises from 0 to 1 across the last 35 m of climb, so one
      // minus it is the headroom still left below the line: full well down
      // the slope, nothing at the top.
      const auto tree_line_headroom =
        1 - band (tree_tops - 35.0f * m, tree_tops, ground_level);

      const auto wetness = get<surface_moisture> (moisture[site]);

      const auto hydrated = band (0.10f * one, 0.42f * one, wetness);
      const auto not_sodden = 1 - band (0.78f * one, 0.98f * one, wetness);
      const auto water_response = 0.28f + 0.72f * hydrated * not_sodden;
      const auto treeishness =
        soil_dryness * tree_line_headroom * soil_stability * water_response;

      get<tree_habitat> (values[site]) = treeishness * tree_habitat[one];
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
    ForestCoverMap values (domain);
    // The mosaic is a periodic field over the torus, so a site's place is
    // needed as a fraction of the lap rather than as a storage position.
    const float lap_x = static_cast<float> (domain.width ());
    const float lap_z = static_cast<float> (domain.height ());

    for (const terrain::TerrainIndex site : spatial::sites (habitat)) {
      const float u = static_cast<float> (site.column) / lap_x;
      const float v = static_cast<float> (site.row) / lap_z;
      // Two octaves of the same periodic field; the mosaic stays a noise
      // signal until the recruitment threshold reads it as a proportion.
      const noise_signal_t broad =
        periodic_noise (u * 7.0f, v * 7.0f, 7, 7, seed ^ 0x4b1d9e37U);
      const noise_signal_t local =
        periodic_noise (u * 23.0f, v * 23.0f, 23, 23, seed ^ 0x91e10da5U);
      const noise_signal_t mosaic = 0.72f * broad + 0.28f * local;
      const float recruitment =
        band (0.44f * noise_signal[one], 0.61f * noise_signal[one], mosaic)
          .numerical_value_in (one);
      const float support = std::pow (
        spatial::get<tree_habitat> (habitat[site]).numerical_value_in (one),
        1.15f);
      const auto trodden = use[site];
      const float route_clearance =
        1.0f -
        0.96f *
          spatial::get<trail_influence> (trodden).numerical_value_in (one);
      const float settled_clearance =
        1.0f -
        spatial::get<home_base_influence> (trodden).numerical_value_in (one);
      spatial::get<forest_cover> (values[site]) =
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
