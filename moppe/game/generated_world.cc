#include <moppe/game/generated_world.hh>

#include <moppe/profile.hh>
#include <moppe/terrain/moisture.hh>
#include <moppe/terrain/river.hh>
#include <moppe/terrain/waterline.hh>

#include <algorithm>
#include <cmath>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace moppe::game {
  namespace {
    void bind_world_params (WorldParams& params,
                            const terrain::WorldRecipe& recipe) {
      params.map_size = recipe.extent ();
      params.resolution = recipe.resolution ();
      params.water_level = recipe.water_datum ();
      params.terrain_topology = recipe.topology ();
    }

    std::size_t storage_count (const map::RandomHeightMap& terrain) {
      return static_cast<std::size_t> (terrain.width ()) * terrain.height ();
    }

    std::vector<float> expand_scalar (const map::RandomHeightMap& terrain,
                                      std::span<const float> unique_values,
                                      std::size_t unique_width,
                                      std::size_t unique_height) {
      std::vector<float> expanded (storage_count (terrain));
      for (int y = 0; y < terrain.height (); ++y)
        for (int x = 0; x < terrain.width (); ++x)
          expanded[static_cast<std::size_t> (y) * terrain.width () + x] =
            unique_values[(static_cast<std::size_t> (y) % unique_height) *
                            unique_width +
                          static_cast<std::size_t> (x) % unique_width];
      return expanded;
    }
  }

  GeneratedWorld::Hydrology::Hydrology (terrain::FloodField standing_water,
                                        terrain::LakeCensus lakes,
                                        terrain::DrainageGraph drainage,
                                        terrain::FractionalDrainage channels,
                                        terrain::WaterNetwork waterways,
                                        terrain::RiverNetwork rivers)
      : m_standing_water (std::move (standing_water)),
        m_lakes (std::move (lakes)), m_drainage (std::move (drainage)),
        m_channels (std::move (channels)), m_waterways (std::move (waterways)),
        m_rivers (std::move (rivers)) {}

  GeneratedWorld::GeneratedWorld (WorldParams params,
                                  terrain::WorldRecipe recipe)
      : m_params (params), m_recipe (std::move (recipe)),
        m_terrain (m_recipe.resolution (),
                   m_recipe.resolution (),
                   extent_value (m_recipe.extent ()),
                   m_recipe.seed ().value,
                   m_recipe.topology ()) {
    bind_world_params (m_params, m_recipe);
  }

  GeneratedWorld::Builder GeneratedWorld::build () noexcept {
    return Builder (*this);
  }

  void GeneratedWorld::reset (terrain::WorldRecipe recipe) {
    const Vec3& current_extent = extent_value (m_params.map_size);
    const Vec3& replacement_extent = extent_value (recipe.extent ());
    if (recipe.resolution () != m_terrain.width () ||
        recipe.topology () != m_params.terrain_topology ||
        replacement_extent != current_extent)
      throw std::invalid_argument (
        "a generated world reset must preserve its terrain storage layout");

    m_recipe = std::move (recipe);
    bind_world_params (m_params, m_recipe);
    m_terrain.reseed (m_recipe.seed ().value);
    m_terrain.reset_sediment_ledger ();
    m_surface = map::Surface ();
    m_terrain_history.clear ();
    m_hydrology.reset ();
    m_water_surface.reset ();
    m_trails.reset ();
  }

  void GeneratedWorld::rebuild_surface () {
    MOPPE_PROFILE_ZONE ("GeneratedWorld::rebuild_surface");
    m_terrain.recompute_normals ();
    // Assignment deliberately preserves the Surface subobject's address:
    // Glider and Terrain Lab borrow that stable world member across a reset.
    m_surface = map::Surface (m_terrain);
  }

  void GeneratedWorld::analyze_hydrology (const HydrologyProgress& progress) {
    MOPPE_PROFILE_ZONE ("GeneratedWorld::analyze_hydrology");
    const auto report = [&progress] (HydrologyStage stage) {
      if (progress)
        progress (stage);
    };

    report (HydrologyStage::StandingWater);
    terrain::FloodField standing_water = terrain::analyze_standing_water (
      m_terrain.terrain_view (), m_recipe.normalized_water_datum ());

    report (HydrologyStage::Lakes);
    terrain::LakeCensus lakes = terrain::census_lakes (standing_water);

    report (HydrologyStage::Drainage);
    terrain::DrainageGraph drainage = terrain::analyze_wet_drainage (
      m_terrain.terrain_view (), standing_water, lakes);

    report (HydrologyStage::Waterways);
    terrain::WaterNetwork waterways =
      terrain::analyze_water_network (standing_water, lakes, drainage);

    report (HydrologyStage::Channels);
    terrain::FractionalDrainage channels =
      terrain::analyze_fractional_drainage (
        m_terrain.terrain_view (), standing_water, lakes);

    report (HydrologyStage::Rivers);
    terrain::RiverNetwork rivers = terrain::extract_river_network (
      standing_water,
      lakes,
      drainage,
      channels,
      terrain::visible_river_minimum_area (drainage.source_grid));

    m_hydrology.emplace (Hydrology (std::move (standing_water),
                                    std::move (lakes),
                                    std::move (drainage),
                                    std::move (channels),
                                    std::move (waterways),
                                    std::move (rivers)));
  }

  void GeneratedWorld::materialize_analyses (
    std::optional<terrain::TrailNetwork> generated_trails) {
    MOPPE_PROFILE_ZONE ("GeneratedWorld::materialize_analyses");
    m_water_surface.reset ();
    m_trails.reset ();

    if (m_hydrology) {
      const Hydrology& hydrology = *m_hydrology;
      const terrain::FractionalDrainage& channels = hydrology.channels ();
      {
        MOPPE_PROFILE_ZONE ("world.materialize_channel_flux");
        const auto& tangents =
          spatial::get<terrain::channel_tangent> (channels);
        const auto& areas =
          spatial::get<terrain::fractional_contributing_area> (channels);
        const terrain::TerrainGrid& grid = channels.domain ().grid ();
        const float floor_area_m2 =
          4.0f * square_meters_value (grid.cell_area ());
        const float channel_area_m2 =
          square_meters_value (terrain::visible_river_minimum_area (grid));
        const float activity_span =
          std::log (std::max (channel_area_m2 / floor_area_m2, 1.001f));
        const std::size_t unique_width = grid.unique_width ();
        const std::size_t unique_height = grid.unique_height ();
        std::vector<float> flux (2 * storage_count (m_terrain));
        for (int y = 0; y < m_terrain.height (); ++y)
          for (int x = 0; x < m_terrain.width (); ++x) {
            const std::size_t cell =
              (static_cast<std::size_t> (y) % unique_height) * unique_width +
              static_cast<std::size_t> (x) % unique_width;
            const float area_m2 = areas[cell].numerical_value_in (u::m * u::m);
            const float activity =
              std::clamp (std::log (std::max (area_m2 / floor_area_m2, 1e-6f)) /
                            activity_span,
                          0.0f,
                          1.0f);
            const Vec3 tangent = tangents[cell].numerical_value_in (one);
            const std::size_t out =
              2 * (static_cast<std::size_t> (y) * m_terrain.width () + x);
            flux[out] = tangent[0] * activity;
            flux[out + 1] = tangent[2] * activity;
          }
        m_surface.materialize_channel_flux (flux);
      }

      const terrain::WaterSheets sheets = [&] {
        MOPPE_PROFILE_ZONE ("world.paint_watercourses");
        return terrain::paint_watercourses (m_terrain.terrain_view (),
                                            hydrology.standing_water (),
                                            hydrology.lakes (),
                                            hydrology.drainage (),
                                            hydrology.rivers ());
      }();
      const std::size_t unique_width = hydrology.standing_water ().width ();
      const std::size_t unique_height = hydrology.standing_water ().height ();
      std::vector<float> water_levels (2 * storage_count (m_terrain));
      std::vector<float> water_flow (water_levels.size ());
      const std::span<const float> levels = sheets.surface.values ();
      for (int y = 0; y < m_terrain.height (); ++y)
        for (int x = 0; x < m_terrain.width (); ++x) {
          const std::size_t cell =
            (static_cast<std::size_t> (y) % unique_height) * unique_width +
            static_cast<std::size_t> (x) % unique_width;
          const std::size_t out =
            2 * (static_cast<std::size_t> (y) * m_terrain.width () + x);
          water_levels[out] = levels[cell];
          water_levels[out + 1] = sheets.amplitude[cell];
          water_flow[out] = sheets.flow[2 * cell];
          water_flow[out + 1] = sheets.flow[2 * cell + 1];
        }
      m_water_surface.emplace (m_surface.atlas ().domain (),
                               water_levels,
                               water_flow,
                               m_terrain.scale ()[1] * u::m);

      {
        MOPPE_PROFILE_ZONE ("world.materialize_waterline");
        const terrain::Waterline waterline = terrain::extract_waterline (
          m_terrain.terrain_view (), sheets.surface, hydrology.lakes ());
        const terrain::ScalarRaster proximity =
          terrain::waterline_proximity (waterline);
        m_surface.materialize_waterline_distance (expand_scalar (
          m_terrain, proximity.values (), unique_width, unique_height));
      }

      {
        MOPPE_PROFILE_ZONE ("world.materialize_moisture");
        const terrain::ScalarRaster moisture =
          terrain::analyze_moisture (hydrology.standing_water (),
                                     hydrology.lakes (),
                                     hydrology.drainage ());
        m_surface.materialize_moisture (expand_scalar (
          m_terrain, moisture.values (), unique_width, unique_height));
        m_surface.derive_tree_habitat (m_params.water_level,
                                       m_params.water_level + 145.0f * u::m);
      }

      const terrain::TerrainProgram& program = m_recipe.terrain_program ();
      const auto stage = std::find_if (
        program.transforms.begin (),
        program.transforms.end (),
        [] (const terrain::TerrainTransform& transform) {
          return std::holds_alternative<terrain::TrailFormation> (transform);
        });
      if (stage != program.transforms.end ()) {
        MOPPE_PROFILE_ZONE ("world.materialize_trails");
        const std::size_t stage_index = static_cast<std::size_t> (
          std::distance (program.transforms.begin (), stage));
        if (generated_trails)
          m_trails = std::move (generated_trails);
        else {
          const terrain::TerrainView trail_source =
            stage_index < m_terrain_history.size ()
              ? terrain::TerrainView (m_terrain.terrain_view ().grid (),
                                      m_terrain_history[stage_index])
              : m_terrain.terrain_view ();
          m_trails = terrain::analyze_trail_network (
            trail_source, std::get<terrain::TrailFormation> (*stage));
          if (stage_index + 1 < m_terrain_history.size ()) {
            const std::vector<float>& before = m_terrain_history[stage_index];
            const std::vector<float>& after =
              m_terrain_history[stage_index + 1];
            const std::size_t storage_width = m_terrain.width ();
            const float height_scale = m_terrain.scale ()[1];
            for (std::size_t y = 0; y < m_terrain.unique_height (); ++y)
              for (std::size_t x = 0; x < m_terrain.unique_width (); ++x) {
                const std::size_t source = y * storage_width + x;
                const std::size_t cell = y * m_terrain.unique_width () + x;
                m_trails->earthwork_delta_m[cell] =
                  (after[source] - before[source]) * height_scale;
              }
          }
        }
        m_surface.materialize_trail_influence (
          terrain::expand_trail_influence (*m_trails));
        m_surface.materialize_home_base_influence (
          terrain::expand_home_base_influence (*m_trails));
      }
    }

    if (m_surface.atlas ().ecology ().tree_habitat ())
      m_surface.derive_forest_cover (m_recipe.seed ().value ^ 0x6f12ad37U);

    const std::size_t count = storage_count (m_terrain);
    m_surface.derive_geology_materials (
      std::span (m_terrain.raw_eroded (), count),
      std::span (m_terrain.raw_deposited (), count));
  }

  map::RandomHeightMap& GeneratedWorld::Builder::terrain () noexcept {
    return m_world.m_terrain;
  }

  std::vector<std::vector<float>>&
  GeneratedWorld::Builder::terrain_history () noexcept {
    return m_world.m_terrain_history;
  }

  void GeneratedWorld::Builder::reset (terrain::WorldRecipe recipe) {
    m_world.reset (std::move (recipe));
  }

  void GeneratedWorld::Builder::rebuild_surface () {
    m_world.rebuild_surface ();
  }

  void GeneratedWorld::Builder::analyze_hydrology (
    const HydrologyProgress& progress) {
    m_world.analyze_hydrology (progress);
  }

  void GeneratedWorld::Builder::materialize_analyses (
    std::optional<terrain::TrailNetwork> generated_trails) {
    m_world.materialize_analyses (std::move (generated_trails));
  }
}
