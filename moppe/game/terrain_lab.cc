#include <moppe/game/terrain_lab.hh>
#include <moppe/profile.hh>
#include <moppe/terrain/editor.hh>
#include <moppe/terrain/river.hh>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace moppe {
  namespace game {
    namespace {
      using ParameterDomain = terrain::ParameterDomain;
      using PropertyText = terrain::TransformProperty;

      std::string_view
      water_body_class_name (terrain::WaterBodyClass classification) {
        switch (classification) {
        case terrain::WaterBodyClass::Puddle:
          return "PUDDLE";
        case terrain::WaterBodyClass::Pond:
          return "POND";
        case terrain::WaterBodyClass::Lake:
          return "LAKE";
        case terrain::WaterBodyClass::Sea:
          return "SEA";
        }
        return "WATER";
      }

      constexpr float window_width = 520.0f;
      constexpr float window_height = 640.0f;
      constexpr float left_x = 14.0f;
      constexpr float left_width = 226.0f;
      constexpr float right_x = 248.0f;
      constexpr float right_width = 258.0f;
      constexpr float stage_row_height = 38.0f;
      constexpr float stage_row_stride = 41.0f;
      constexpr int visible_stage_rows = 7;
      constexpr float readings_width = 360.0f;
      constexpr float readings_height = 394.0f;

      constexpr terrain::GeologicalLayer layers[] = {
        terrain::GeologicalLayer::Combined,
        terrain::GeologicalLayer::Continent,
        terrain::GeologicalLayer::Plains,
        terrain::GeologicalLayer::Mountains,
        terrain::GeologicalLayer::MountainMask,
        terrain::GeologicalLayer::WarpX,
        terrain::GeologicalLayer::WarpY
      };

      constexpr const char* layer_labels[] = { "ALL",    "LAND", "PLAINS",
                                               "RIDGES", "MASK", "WARP X",
                                               "WARP Y" };

      UiRect friendly_panel_rect (int height) {
        return { 0, 0, 330, static_cast<float> (std::max (420, height - 36)) };
      }

      UiRect friendly_content_rect (int height) {
        return ui_inset (friendly_panel_rect (height), 20.0f);
      }

      UiRect friendly_action_rect (int index, int height) {
        UiRect row = friendly_content_rect (height);
        row.y += 28.0f;
        return ui_grid_cell (row, 3, index, 50.0f, 6.0f);
      }

      UiRect friendly_preset_rect (int index, int height) {
        UiRect grid = friendly_content_rect (height);
        grid.y += 112.0f;
        return ui_grid_cell (grid, 2, index, 54.0f, 6.0f);
      }

      UiRect friendly_overlay_rect (int index, int height) {
        UiRect grid = friendly_content_rect (height);
        grid.y += 264.0f;
        return ui_grid_cell (grid, 2, index, 30.0f, 4.0f);
      }

      UiRect observe_back_rect () {
        return { 434, 38, 72, 28 };
      }

      UiRect overlay_rect (int index) {
        constexpr float gap = 4.0f;
        constexpr float margin = 10.0f;
        constexpr float width = (readings_width - 2 * margin - 3 * gap) / 4;
        const int row = index / 4;
        const int column = index % 4;
        return {
          margin + column * (width + gap), 42.0f + row * 34.0f, width, 28
        };
      }

      UiRect close_rect () {
        return { 491, 6, 22, 22 };
      }

      UiRect reset_rect () {
        return { left_x, 38, 84, 28 };
      }

      UiRect seed_rect () {
        return { left_x + 90, 38, 128, 28 };
      }

      UiRect fit_rect () {
        return { 238, 38, 72, 28 };
      }

      UiRect view_rect () {
        return { 316, 38, 112, 28 };
      }

      UiRect layer_rect (int index) {
        const float gap = 3.0f;
        const float width = (492.0f - 6 * gap) / 7.0f;
        return { left_x + index * (width + gap), 75, width, 29 };
      }

      UiRect pipeline_header_rect () {
        return { left_x, 114, left_width, 24 };
      }

      UiRect property_header_rect () {
        return { right_x, 114, right_width, 24 };
      }

      UiRect source_rect () {
        return { left_x, 143, left_width, stage_row_height };
      }

      UiRect stage_rect (int visible_index) {
        return { left_x,
                 187 + visible_index * stage_row_stride,
                 left_width,
                 stage_row_height };
      }

      UiRect stage_list_rect () {
        return {
          left_x, 187, left_width, visible_stage_rows * stage_row_stride
        };
      }

      UiRect add_stage_rect (int index) {
        const float gap = 3.0f;
        const float width = (left_width - 6 * gap) / 7.0f;
        return { left_x + index * (width + gap), 497, width, 29 };
      }

      UiRect edit_stage_rect (int index) {
        const float gap = 3.0f;
        const float width = (left_width - 3 * gap) / 4.0f;
        return { left_x + index * (width + gap), 544, width, 29 };
      }

      UiRect property_rect (int index) {
        return { right_x, 143 + index * 41.0f, right_width, 39.0f };
      }

      std::string format_float (float value, int precision) {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision (precision) << value;
        return stream.str ();
      }

      std::string format_count (int value) {
        std::string text = std::to_string (value);
        for (int i = static_cast<int> (text.size ()) - 3; i > 0; i -= 3)
          text.insert (static_cast<std::size_t> (i), ",");
        return text;
      }

      std::string format_ledger (double value) {
        std::ostringstream stream;
        stream << std::scientific << std::setprecision (2) << value;
        return stream.str ();
      }

      std::string stage_name (const terrain::TerrainTransform& stage) {
        return std::string (
          terrain::terrain_transform_description (stage).title);
      }

      std::string stage_detail (const terrain::TerrainTransform& stage) {
        return terrain::terrain_transform_detail (stage);
      }

      std::string
      semantics_detail (const terrain::TerrainTransform& transform) {
        const terrain::TransformSemantics semantics =
          terrain::terrain_transform_semantics (transform);
        const char* spatial =
          semantics.spatial_scope == terrain::SpatialScope::Pointwise
            ? "POINTWISE"
          : semantics.spatial_scope == terrain::SpatialScope::Neighborhood
            ? "NEIGHBORHOOD"
            : "GLOBAL";
        const char* order =
          semantics.evaluation_order == terrain::EvaluationOrder::Direct
            ? "DIRECT"
          : semantics.evaluation_order == terrain::EvaluationOrder::Reduction
            ? "REDUCTION"
            : "ITERATIVE";
        return std::string (spatial) + " / " + order;
      }

    }

    TerrainLab::TerrainLab ()
        : m_observe_window ({ 18.0f, 18.0f, 330.0f, 604.0f }),
          m_build_window ({ 14.0f, 14.0f, window_width, window_height }),
          m_readings_window (
            { 548.0f, 14.0f, readings_width, readings_height }),
          m_renderer (0), m_map (0), m_terrain (0), m_world (0),
          m_world_recipe (0), m_graphics (0), m_history (nullptr),
          m_history_index (0), m_history_age (0.0f), m_history_playing (false),
          m_active (false), m_overlay (OverlayMode::None),
          m_selected_stage (-1), m_stage_scroll (0), m_pointer_x (0),
          m_pointer_y (0), m_pointer_down (false), m_camera_drag (false),
          m_camera_drag_distance (0.0f), m_pan_drag (false),
          m_parameter_drag (false), m_build_ui (false), m_friendly_preset (-1),
          m_ui_width (960), m_ui_height (640), m_drag_property (-1),
          m_drag_start_y (0.0f), m_drag_start_normalized (0.0f),
          m_parameter_rebuild_pending (false), m_parameter_rebuild_stage (-1),
          m_parameter_rebuild_delay (0.0f), m_target (), m_yaw (PI),
          m_pitch (1.48f), m_distance (4200.0f), m_fit_distance (4200.0f),
          m_orbit_left (false), m_orbit_right (false), m_zoom_in (false),
          m_zoom_out (false), m_tilt_up (false), m_tilt_down (false),
          m_scroll_zoom_target (4200.0f), m_view (ViewMode::Tile) {}

    void TerrainLab::load (render::Renderer& renderer) {
      m_ui.load (renderer);
    }

    void TerrainLab::enter (render::Renderer& renderer,
                            map::RandomHeightMap& map,
                            Terrain& terrain,
                            const WorldParams& world,
                            const GraphicsSettings& graphics,
                            const terrain::WorldRecipe& recipe,
                            std::span<const float> trail_influence,
                            std::span<const float> home_base_influence,
                            const std::vector<std::vector<float>>& history,
                            const Vec3& sun_dir) {
      if (m_active)
        return;

      m_renderer = &renderer;
      m_map = &map;
      if (!m_source_evaluator)
        m_source_evaluator = platform::create_field_evaluator ();
      if (!m_evolution_backend)
        m_evolution_backend =
          platform::create_stream_power_evolution_backend ();
      m_terrain = &terrain;
      m_world = &world;
      m_world_recipe = &recipe;
      m_graphics = &graphics;
      m_sun_dir = sun_dir;
      terrain::TerrainProgram lab_program = recipe.terrain_program ();
      m_saved_trail_influence.assign (trail_influence.begin (),
                                      trail_influence.end ());
      m_saved_home_base_influence.assign (home_base_influence.begin (),
                                          home_base_influence.end ());
      m_history = &history;
      m_history_index = history.empty () ? 0 : history.size () - 1;
      m_history_age = 0.0f;
      m_history_playing = false;
      bool env_stages = false;
      if (std::getenv ("MOPPE_LAB_OROGENY")) {
        lab_program = calibrated_world_program (
          lab_program.seed, terrain::TerrainGenerationProfile::Fast);
        if (!lab_program.transforms.empty () &&
            std::holds_alternative<terrain::TrailFormation> (
              lab_program.transforms.back ()))
          lab_program.transforms.pop_back ();
        env_stages = true;
      }
      if (std::getenv ("MOPPE_LAB_ANALYTICAL")) {
        terrain::AnalyticalErosion analytical;
        analytical.sea_level = lab_program.source.sea_level;
        lab_program.transforms.emplace_back (analytical);
        env_stages = true;
      }
      m_model.begin (map,
                     lab_program,
                     m_source_evaluator.get (),
                     m_evolution_backend.get ());
      m_selected_stage = -1;
      m_stage_scroll = 0;
      m_pointer_down = false;
      m_camera_drag = false;
      m_pan_drag = false;
      m_parameter_drag = false;
      m_observe_window.end_drag ();
      m_build_window.end_drag ();
      m_readings_window.end_drag ();
      m_build_ui = std::getenv ("MOPPE_LAB_EXPERT") != nullptr;
      m_friendly_preset = -1;
      m_drag_property = -1;
      m_parameter_rebuild_pending = false;
      m_parameter_rebuild_delay = 0.0f;
      m_overlay = OverlayMode::None;
      if (const char* overlay = std::getenv ("MOPPE_LAB_OVERLAY")) {
        const std::string name (overlay);
        if (name == "height")
          m_overlay = OverlayMode::Height;
        else if (name == "slope")
          m_overlay = OverlayMode::Slope;
        else if (name == "flow")
          m_overlay = OverlayMode::Flow;
        else if (name == "streams")
          m_overlay = OverlayMode::Streams;
        else if (name == "basins")
          m_overlay = OverlayMode::Basins;
        else if (name == "sinks")
          m_overlay = OverlayMode::Sinks;
        else if (name == "delta")
          m_overlay = OverlayMode::HeightDelta;
        else if (name == "trace")
          m_overlay = OverlayMode::Trace;
        else if (name == "water")
          m_overlay = OverlayMode::StandingWater;
        else if (name == "lakes")
          m_overlay = OverlayMode::PermanentWater;
        else if (name == "falls")
          m_overlay = OverlayMode::Waterfalls;
        else if (name == "eroded")
          m_overlay = OverlayMode::Eroded;
        else if (name == "deposited")
          m_overlay = OverlayMode::Deposited;
      }
      m_drainage.reset ();
      m_channel_drainage.reset ();
      m_water_network.reset ();
      m_rivers.reset ();
      m_river_surface.clear ();
      m_flood.reset ();
      m_lakes.reset ();
      m_inspected_cell.reset ();
      m_overlay_status = "NO READING — terrain materials";
      m_view = ViewMode::Tile;
      m_orbit_left = m_orbit_right = false;
      m_zoom_in = m_zoom_out = false;
      m_tilt_up = m_tilt_down = false;
      fit_view ();

      m_active = true;
      // The map on screen IS this program's output (or the world it was
      // loaded from): entering the lab is just another view of it, so
      // don't rebuild.  The pipeline reruns only once something is
      // edited.  Env-var stages are additions and do need a run.
      if (env_stages)
        rebuild_program ();
      else
        refresh ();
      if (std::getenv ("MOPPE_LAB_RIVERS"))
        drainage ();
      if (const char* stage = std::getenv ("MOPPE_LAB_STAGE")) {
        const int selected = std::atoi (stage);
        if (selected >= 0 &&
            selected < static_cast<int> (this->program ().transforms.size ())) {
          m_selected_stage = selected;
          if (m_overlay == OverlayMode::HeightDelta)
            update_overlay ();
        }
      }
      if (m_overlay == OverlayMode::Trace) {
        const char* trace_x = std::getenv ("MOPPE_LAB_TRACE_X");
        const char* trace_y = std::getenv ("MOPPE_LAB_TRACE_Y");
        if (trace_x && trace_y)
          inspect_drainage (std::atof (trace_x), std::atof (trace_y));
      }
    }

    void TerrainLab::leave () {
      if (!m_active)
        return;
      m_overlay = OverlayMode::None;
      if (m_renderer)
        m_renderer->clear_terrain_overlay ();
      restore_game_map ();
      m_saved_trail_influence.clear ();
      m_saved_trail_influence.shrink_to_fit ();
      m_saved_home_base_influence.clear ();
      m_saved_home_base_influence.shrink_to_fit ();
      m_river_surface.clear ();
      m_orbit_left = m_orbit_right = false;
      m_zoom_in = m_zoom_out = false;
      m_tilt_up = m_tilt_down = false;
      m_pointer_down = false;
      m_camera_drag = false;
      m_pan_drag = false;
      m_parameter_drag = false;
      m_parameter_rebuild_pending = false;
      m_active = false;
      m_model.leave ();
      m_history = nullptr;
      m_map = 0;
      m_terrain = 0;
      m_world = 0;
      m_world_recipe = 0;
    }

    void TerrainLab::fit_view () {
      if (!m_world)
        return;
      const Vec3& world_extent = extent_value (m_world->map_size);
      m_target =
        Vec3 (world_extent[0] * 0.5f,
              m_view == ViewMode::Torus ? 0.0f : world_extent[1] * 0.10f,
              world_extent[2] * 0.5f);
      m_yaw = m_view == ViewMode::Tile ? PI : 0.72f;
      if (m_view == ViewMode::Torus) {
        m_pitch = 0.48f;
        m_distance = 4700.0f;
      } else if (m_view == ViewMode::Cover) {
        // High and oblique rather than top-down: the player sees the same
        // atmospheric horizon as the riding camera, only from a god's-eye
        // vantage over the whole landscape.
        // Leave enough of the upper hemisphere in frame for the sky to read
        // blue rather than showing only its pale, hazy horizon band.
        m_pitch = 0.20f;
        m_distance = 8000.0f;
      } else {
        // A map should open as a map. Fit the complete finite tile with north
        // at the top; the small residual tilt keeps look_at's up vector
        // well-conditioned without turning the overview into a horizon shot.
        // The controls occupy the left third of the screen, so offset the
        // orbit target to frame the tile in the unobstructed map viewport.
        m_target[0] += world_extent[0] * 0.25f;
        const float aspect = m_renderer
                               ? static_cast<float> (m_renderer->width_pts ()) /
                                   std::max (1, m_renderer->height_pts ())
                               : 1.6f;
        const float half_span =
          1.08f *
          std::max (world_extent[2] * 0.5f, world_extent[0] * 0.5f / aspect);
        m_pitch = 1.48f;
        m_distance =
          half_span / std::tan (35.0f * PI / 180.0f) + world_extent[1] * 0.35f;
      }
      m_fit_distance = m_distance;
      m_scroll_zoom_target = m_distance;
    }

    void TerrainLab::cycle_view () {
      if (m_view == ViewMode::Tile)
        m_view = ViewMode::Cover;
      else if (m_view == ViewMode::Cover)
        m_view = ViewMode::Torus;
      else
        m_view = ViewMode::Tile;
      fit_view ();
      refresh ();
    }

    void TerrainLab::restore_game_map () {
      if (!m_model.active ())
        return;
      m_model.restore_original_map ();
      refresh (false);
    }

    void TerrainLab::select (terrain::GeologicalLayer layer) {
      if (layer == program ().source.layer)
        return;
      program ().source.layer = layer;
      m_selected_stage = -1;
      rebuild_program ();
    }

    void TerrainLab::reset_program () {
      const terrain::GeologicalLayer layer = program ().source.layer;
      program () = recipe_program (program ().seed);
      program ().source.layer = layer;
      m_selected_stage = -1;
      m_stage_scroll = 0;
      rebuild_program ();
    }

    terrain::TerrainProgram
    TerrainLab::recipe_program (terrain::Seed seed) const {
      if (!m_world_recipe)
        throw std::logic_error ("terrain lab has no world recipe");
      const bool geological = m_world_recipe->terrain_program ().source.mode ==
                              terrain::GeologicalSource::Mode::Relief;
      const terrain::WorldRecipe recipe =
        geological
          ? terrain::make_geological_world_recipe (
              m_world_recipe->extent (),
              m_world_recipe->resolution (),
              m_world_recipe->topology (),
              seed,
              m_world_recipe->water_datum (),
              m_world_recipe->generation_profile ())
          : terrain::make_world_recipe (m_world_recipe->extent (),
                                        m_world_recipe->resolution (),
                                        m_world_recipe->topology (),
                                        seed,
                                        m_world_recipe->water_datum (),
                                        m_world_recipe->generation_profile ());
      return recipe.terrain_program ();
    }

    terrain::TerrainProgram TerrainLab::calibrated_world_program (
      terrain::Seed seed, terrain::TerrainGenerationProfile profile) const {
      if (!m_world_recipe)
        throw std::logic_error ("terrain lab has no world recipe");
      const terrain::WorldRecipe recipe =
        terrain::make_world_recipe (m_world_recipe->extent (),
                                    m_world_recipe->resolution (),
                                    m_world_recipe->topology (),
                                    seed,
                                    m_world_recipe->water_datum (),
                                    profile);
      return recipe.terrain_program ();
    }

    void TerrainLab::rebuild_program () {
      if (!m_model.active ())
        return;
      m_model.rebuild_program ();
      invalidate_analysis ();
      refresh ();
    }

    void TerrainLab::rerun_program_from (int first_stage) {
      if (!m_model.active () || first_stage < 0) {
        rebuild_program ();
        return;
      }
      m_model.rerun_program_from (static_cast<std::size_t> (first_stage));
      invalidate_analysis ();
      refresh ();
    }

    void TerrainLab::invalidate_analysis () {
      m_drainage.reset ();
      m_channel_drainage.reset ();
      m_water_network.reset ();
      m_rivers.reset ();
      m_river_surface.clear ();
      m_flood.reset ();
      m_lakes.reset ();
      m_inspected_cell.reset ();
    }

    const terrain::FloodField& TerrainLab::standing_water () {
      if (!m_map || !m_world)
        throw std::logic_error ("standing water requested without terrain");
      if (!m_flood) {
        const auto start = std::chrono::steady_clock::now ();
        const float sea_level = meters_value (m_world->water_level) /
                                extent_value (m_world->map_size)[1];
        m_flood =
          terrain::analyze_standing_water (m_map->terrain_view (), sea_level);
        m_lakes = terrain::census_lakes (*m_flood);
        const double milliseconds = std::chrono::duration<double, std::milli> (
                                      std::chrono::steady_clock::now () - start)
                                      .count ();
        std::size_t wet_cells = 0;
        float maximum_depth = 0.0f;
        for (const float depth : m_flood->water_depth.values ()) {
          if (depth > 1e-7f)
            ++wet_cells;
          maximum_depth = std::max (maximum_depth, depth);
        }
        std::ostringstream status;
        status << format_count (static_cast<int> (wet_cells)) << " wet cells | "
               << std::fixed << std::setprecision (1)
               << maximum_depth * extent_value (m_world->map_size)[1]
               << "m max | " << std::setprecision (0) << milliseconds
               << "ms flood";
        m_flood_status = status.str ();

        int puddles = 0, ponds = 0, lakes = 0;
        for (const terrain::WaterBody& body : m_lakes->bodies)
          switch (body.classification) {
          case terrain::WaterBodyClass::Puddle:
            ++puddles;
            break;
          case terrain::WaterBodyClass::Pond:
            ++ponds;
            break;
          case terrain::WaterBodyClass::Lake:
            ++lakes;
            break;
          case terrain::WaterBodyClass::Sea:
            break;
          }
        std::ostringstream census_status;
        census_status << format_count (puddles) << " puddles, "
                      << format_count (ponds) << " ponds, "
                      << format_count (lakes) << " lakes";
        m_census_status = census_status.str ();
      }
      return *m_flood;
    }

    const terrain::DrainageGraph& TerrainLab::drainage () {
      if (!m_map)
        throw std::logic_error ("drainage requested without terrain");
      if (!m_drainage) {
        const auto start = std::chrono::steady_clock::now ();
        m_drainage = terrain::analyze_wet_drainage (
          m_map->terrain_view (), standing_water (), *m_lakes);
        m_water_network =
          terrain::analyze_water_network (*m_flood, *m_lakes, *m_drainage);
        m_channel_drainage = terrain::analyze_fractional_drainage (
          m_map->terrain_view (), *m_flood, *m_lakes);
        m_rivers = terrain::extract_river_network (
          *m_flood,
          *m_lakes,
          *m_drainage,
          *m_channel_drainage,
          terrain::visible_river_minimum_area (m_drainage->source_grid));
        m_river_surface.rebuild (*m_renderer, *m_map, *m_rivers);
        const double milliseconds = std::chrono::duration<double, std::milli> (
                                      std::chrono::steady_clock::now () - start)
                                      .count ();
        std::ostringstream status;
        std::size_t inlets = 0;
        for (const terrain::WaterBodyFlow& flow : m_water_network->bodies)
          inlets += flow.inlets.size ();
        status << m_drainage->sinks.size () << " outlets, "
               << format_count (static_cast<int> (inlets)) << " entries, "
               << m_rivers->reaches.size () << " reaches, "
               << m_rivers->waterfalls.size () << " falls | " << std::fixed
               << std::setprecision (0) << milliseconds << " ms analysis";
        m_analysis_status = status.str ();
      }
      return *m_drainage;
    }

    void TerrainLab::set_overlay (OverlayMode mode) {
      if (m_overlay == mode)
        return;
      m_overlay = mode;
      update_overlay ();
    }

    void TerrainLab::render_rivers (render::Renderer& renderer,
                                    const Vec3& camera) const {
      m_river_surface.draw (renderer, camera);
    }

    std::optional<Vec3> TerrainLab::terrain_point_at_screen (float x,
                                                             float y) const {
      if (!m_map || !m_renderer || m_view == ViewMode::Torus)
        return std::nullopt;
      const float width = static_cast<float> (m_renderer->width_pts ());
      const float height = static_cast<float> (m_renderer->height_pts ());
      const float aspect = width / std::max (1.0f, height);
      const float tangent = tan (70.0f * u::deg / 2);
      const float screen_x = 2.0f * x / width - 1.0f;
      const float screen_y = 1.0f - 2.0f * y / height;
      const Vec3 direction_forward = forward ();
      const Vec3 direction_right =
        normalized (cross (direction_forward, Vec3 (0, 1, 0)));
      const Vec3 direction_up =
        normalized (cross (direction_right, direction_forward));
      const Vec3 direction = normalized (
        direction_forward + direction_right * (screen_x * aspect * tangent) +
        direction_up * (screen_y * tangent));
      const Vec3 origin = position ();
      float previous_t = 0.0f;
      float previous_clearance =
        origin[1] - m_map->interpolated_height (origin[0], origin[2]);
      for (float t = 20.0f; t <= 14000.0f; t += 20.0f) {
        const Vec3 point = origin + direction * t;
        const float clearance =
          point[1] - m_map->interpolated_height (point[0], point[2]);
        if (previous_clearance > 0.0f && clearance <= 0.0f) {
          float low = previous_t, high = t;
          for (int i = 0; i < 10; ++i) {
            const float middle = 0.5f * (low + high);
            const Vec3 candidate = origin + direction * middle;
            if (candidate[1] >
                m_map->interpolated_height (candidate[0], candidate[2]))
              low = middle;
            else
              high = middle;
          }
          return origin + direction * (0.5f * (low + high));
        }
        previous_t = t;
        previous_clearance = clearance;
      }
      return std::nullopt;
    }

    void TerrainLab::inspect_drainage (float x, float y) {
      if (!m_map || !m_renderer || m_view == ViewMode::Torus) {
        m_overlay_status = "TRACE — use Tile or Cover view";
        return;
      }
      const float width = static_cast<float> (m_renderer->width_pts ());
      const float height = static_cast<float> (m_renderer->height_pts ());
      const float aspect = width / std::max (1.0f, height);
      const float tangent = tan (70.0f * u::deg / 2);
      const float screen_x = 2.0f * x / width - 1.0f;
      const float screen_y = 1.0f - 2.0f * y / height;
      const Vec3 direction_forward = forward ();
      const Vec3 direction_right =
        normalized (cross (direction_forward, Vec3 (0, 1, 0)));
      const Vec3 direction_up =
        normalized (cross (direction_right, direction_forward));
      const Vec3 direction = normalized (
        direction_forward + direction_right * (screen_x * aspect * tangent) +
        direction_up * (screen_y * tangent));
      const Vec3 origin = position ();

      float previous_t = 0.0f;
      float previous_clearance =
        origin[1] - m_map->interpolated_height (origin[0], origin[2]);
      bool hit = false;
      float hit_t = 0.0f;
      for (float t = 20.0f; t <= 14000.0f; t += 20.0f) {
        const Vec3 point = origin + direction * t;
        const float clearance =
          point[1] - m_map->interpolated_height (point[0], point[2]);
        if (previous_clearance > 0.0f && clearance <= 0.0f) {
          float low = previous_t, high = t;
          for (int i = 0; i < 10; ++i) {
            const float middle = 0.5f * (low + high);
            const Vec3 candidate = origin + direction * middle;
            if (candidate[1] >
                m_map->interpolated_height (candidate[0], candidate[2]))
              low = middle;
            else
              high = middle;
          }
          hit_t = 0.5f * (low + high);
          hit = true;
          break;
        }
        previous_t = t;
        previous_clearance = clearance;
      }
      if (!hit) {
        m_overlay_status = "TRACE — no terrain under pointer";
        return;
      }

      const Vec3 point = origin + direction * hit_t;
      const Vec3 period = m_map->size ();
      const Vec3 scale = m_map->scale ();
      const auto wrap = [] (float value, float size) {
        value = std::fmod (value, size);
        return value < 0.0f ? value + size : value;
      };
      const std::size_t grid_x =
        static_cast<std::size_t> (
          std::floor (wrap (point[0], period[0]) / scale[0])) %
        static_cast<std::size_t> (m_map->unique_width ());
      const std::size_t grid_y =
        static_cast<std::size_t> (
          std::floor (wrap (point[2], period[2]) / scale[2])) %
        static_cast<std::size_t> (m_map->unique_height ());
      m_inspected_cell =
        static_cast<std::uint32_t> (grid_y * m_map->unique_width () + grid_x);
      update_overlay ();
    }

    void TerrainLab::update_overlay () {
      if (!m_renderer || !m_map)
        return;
      if (m_overlay == OverlayMode::None) {
        m_renderer->clear_terrain_overlay ();
        m_overlay_status = "NO READING — terrain materials";
        return;
      }

      const int width = m_map->width ();
      const int height = m_map->height ();
      const std::size_t count = static_cast<std::size_t> (width) * height;
      std::vector<float> values (count, 0.0f);
      render::TerrainOverlayParams params {
        .width = width, .height = height, .minimum = 0.0f, .maximum = 1.0f
      };

      if (m_overlay == OverlayMode::Height) {
        std::copy_n (m_map->raw_heights (), count, values.begin ());
        const auto [minimum, maximum] =
          std::minmax_element (values.begin (), values.end ());
        params.minimum = *minimum;
        params.maximum = *maximum;
        params.ramp = render::TerrainOverlayRamp::Heat;
        m_overlay_status = "HEIGHT — normalized elevation";
      } else if (m_overlay == OverlayMode::HeightDelta) {
        const std::vector<map::TerrainCheckpoint>& checkpoints =
          m_model.checkpoints ();
        if (m_selected_stage < 0 ||
            m_selected_stage >= static_cast<int> (checkpoints.size ())) {
          m_renderer->clear_terrain_overlay ();
          m_overlay_status = "DELTA — select a pipeline stage";
          return;
        }
        const std::vector<float>& before =
          checkpoints[static_cast<std::size_t> (m_selected_stage)].heights;
        const float* after =
          m_selected_stage + 1 < static_cast<int> (checkpoints.size ())
            ? checkpoints[static_cast<std::size_t> (m_selected_stage + 1)]
                .heights.data ()
            : m_map->raw_heights ();
        float magnitude = 0.0f;
        for (std::size_t i = 0; i < count; ++i) {
          values[i] = after[i] - before[i];
          magnitude = std::max (magnitude, std::fabs (values[i]));
        }
        params.minimum = -magnitude;
        params.maximum = magnitude;
        params.ramp = render::TerrainOverlayRamp::Diverging;
        m_overlay_status = "DELTA — blue removal / orange addition";
      } else if (m_overlay == OverlayMode::StandingWater ||
                 m_overlay == OverlayMode::PermanentWater) {
        const terrain::FloodField& flood = standing_water ();
        const std::size_t unique_width = flood.width ();
        const std::size_t unique_height = flood.height ();
        const terrain::ScalarRaster permanent =
          m_overlay == OverlayMode::PermanentWater
            ? terrain::permanent_water_surface (flood, *m_lakes)
            : flood.water_level;
        const std::span<const float> level = permanent.values ();
        const std::span<const float> ground = flood.water_depth.values ();
        std::vector<float> unique (level.size ());
        for (std::size_t i = 0; i < unique.size (); ++i)
          unique[i] = level[i] - (flood.water_level.values ()[i] - ground[i]);
        const float maximum =
          *std::max_element (unique.begin (), unique.end ());
        params.maximum = maximum > 0.0f ? maximum : 1.0f;
        params.ramp = render::TerrainOverlayRamp::Water;
        params.opacity = 0.88f;
        m_overlay_status =
          m_overlay == OverlayMode::PermanentWater
            ? "LAKES — permanent water census | " + m_census_status
            : "WATER — every standing depth w - z | " + m_flood_status;
        for (int y = 0; y < height; ++y)
          for (int x = 0; x < width; ++x)
            values[static_cast<std::size_t> (y) * width + x] =
              unique[(static_cast<std::size_t> (y) % unique_height) *
                       unique_width +
                     static_cast<std::size_t> (x) % unique_width];
      } else if (m_overlay == OverlayMode::Eroded ||
                 m_overlay == OverlayMode::Deposited) {
        // Lifetime sediment ledger; square-root scaling keeps the sparse
        // heavy-tailed cut/fill pattern legible under the heat ramp.
        const float* ledger = m_overlay == OverlayMode::Eroded
                                ? m_map->raw_eroded ()
                                : m_map->raw_deposited ();
        for (std::size_t i = 0; i < count; ++i)
          values[i] = std::sqrt (std::max (0.0f, ledger[i]));
        const float maximum =
          *std::max_element (values.begin (), values.end ());
        params.maximum = maximum > 0.0f ? maximum : 1.0f;
        params.ramp = render::TerrainOverlayRamp::Heat;
        m_overlay_status = m_overlay == OverlayMode::Eroded
                             ? "ERODED — lifetime material removed"
                             : "DEPOSITED — lifetime material settled";
      } else {
        const terrain::DrainageGraph& graph = drainage ();
        const std::size_t unique_width = graph.width ();
        const std::size_t unique_height = graph.height ();
        std::vector<float> unique (unique_width * unique_height, 0.0f);
        if (m_overlay == OverlayMode::Slope) {
          std::copy (graph.slope.values ().begin (),
                     graph.slope.values ().end (),
                     unique.begin ());
          params.maximum = *std::max_element (unique.begin (), unique.end ());
          params.ramp = render::TerrainOverlayRamp::Heat;
          m_overlay_status =
            "SLOPE — physical rise / run | " + m_analysis_status;
        } else if (m_overlay == OverlayMode::Flow ||
                   m_overlay == OverlayMode::Streams) {
          const float cell_area =
            square_meters_value (graph.source_grid.cell_area ());
          float maximum = 0.0f;
          if (m_overlay == OverlayMode::Flow) {
            for (std::size_t i = 0; i < unique.size (); ++i) {
              unique[i] = std::log2 (std::max (
                1.0f, graph.contributing_area.values ()[i] / cell_area));
              maximum = std::max (maximum, unique[i]);
            }
          } else {
            for (const terrain::RiverReach& reach : m_rivers->reaches)
              for (const std::uint32_t cell : reach.cells) {
                unique[cell] = std::log2 (std::max (
                  1.0f, graph.contributing_area.values ()[cell] / cell_area));
                maximum = std::max (maximum, unique[cell]);
              }
          }
          params.minimum = m_overlay == OverlayMode::Streams ? 6.0f : 0.0f;
          params.maximum = maximum;
          params.ramp = m_overlay == OverlayMode::Streams
                          ? render::TerrainOverlayRamp::Streams
                          : render::TerrainOverlayRamp::Flow;
          m_overlay_status = (m_overlay == OverlayMode::Streams
                                ? "STREAMS — dry reaches >= 1,024 cells | "
                                : "FLOW — logarithmic contributing area | ") +
                             m_analysis_status;
        } else if (m_overlay == OverlayMode::Basins) {
          for (std::size_t i = 0; i < unique.size (); ++i)
            unique[i] = static_cast<float> (graph.basin[i]);
          params.ramp = render::TerrainOverlayRamp::Categorical;
          params.opacity = 0.40f;
          m_overlay_status =
            "BASINS — shared outlet catchments | " + m_analysis_status;
        } else if (m_overlay == OverlayMode::Sinks) {
          for (const std::uint32_t sink : graph.sinks) {
            const int sx = static_cast<int> (sink % unique_width);
            const int sy = static_cast<int> (sink / unique_width);
            for (int dy = -2; dy <= 2; ++dy)
              for (int dx = -2; dx <= 2; ++dx) {
                const std::size_t x = static_cast<std::size_t> (
                  (sx + dx + static_cast<int> (unique_width)) %
                  static_cast<int> (unique_width));
                const std::size_t y = static_cast<std::size_t> (
                  (sy + dy + static_cast<int> (unique_height)) %
                  static_cast<int> (unique_height));
                unique[y * unique_width + x] = std::max (
                  unique[y * unique_width + x],
                  1.0f - 0.18f * static_cast<float> (std::hypot (dx, dy)));
              }
          }
          params.ramp = render::TerrainOverlayRamp::Marker;
          params.opacity = 0.95f;
          m_overlay_status =
            "OUTLETS — terminal wet routes | " + m_analysis_status;
        } else if (m_overlay == OverlayMode::Waterfalls) {
          for (const terrain::Waterfall& waterfall : m_rivers->waterfalls) {
            const int fx = static_cast<int> (waterfall.lip_cell % unique_width);
            const int fy = static_cast<int> (waterfall.lip_cell / unique_width);
            for (int dy = -3; dy <= 3; ++dy)
              for (int dx = -3; dx <= 3; ++dx) {
                const std::size_t x = static_cast<std::size_t> (
                  (fx + dx + static_cast<int> (unique_width)) %
                  static_cast<int> (unique_width));
                const std::size_t y = static_cast<std::size_t> (
                  (fy + dy + static_cast<int> (unique_height)) %
                  static_cast<int> (unique_height));
                unique[y * unique_width + x] = std::max (
                  unique[y * unique_width + x],
                  1.0f - 0.14f * static_cast<float> (std::hypot (dx, dy)));
              }
          }
          params.ramp = render::TerrainOverlayRamp::Marker;
          params.opacity = 0.98f;
          m_overlay_status =
            "FALLS — clustered steep high-flow steps | " + m_analysis_status;
        } else {
          if (!m_inspected_cell || *m_inspected_cell >= unique.size ()) {
            m_renderer->clear_terrain_overlay ();
            m_overlay_status =
              "TRACE — click terrain to follow its receiver path";
            return;
          }
          std::uint32_t cell = *m_inspected_cell;
          const std::uint32_t basin_id = graph.basin[cell];
          std::size_t catchment_cells = 0;
          for (std::size_t i = 0; i < unique.size (); ++i)
            if (graph.basin[i] == basin_id) {
              unique[i] = 0.16f;
              ++catchment_cells;
            }
          std::size_t steps = 0;
          while (steps++ < unique.size ()) {
            const int cx = static_cast<int> (cell % unique_width);
            const int cy = static_cast<int> (cell / unique_width);
            for (int dy = -2; dy <= 2; ++dy)
              for (int dx = -2; dx <= 2; ++dx) {
                const std::size_t px = static_cast<std::size_t> (
                  (cx + dx + static_cast<int> (unique_width)) %
                  static_cast<int> (unique_width));
                const std::size_t py = static_cast<std::size_t> (
                  (cy + dy + static_cast<int> (unique_height)) %
                  static_cast<int> (unique_height));
                unique[py * unique_width + px] = 1.0f;
              }
            const std::uint32_t next = graph.receiver[cell];
            if (next == cell)
              break;
            cell = next;
          }
          params.ramp = render::TerrainOverlayRamp::Marker;
          params.opacity = 0.98f;
          const float area =
            graph.contributing_area.values ()[*m_inspected_cell];
          std::ostringstream trace;
          const std::uint32_t body_id = m_lakes->body[*m_inspected_cell];
          if (body_id != terrain::LakeCensus::dry) {
            const terrain::WaterBody& body = m_lakes->bodies[body_id];
            const terrain::WaterBodyFlow& flow =
              m_water_network->bodies[body_id];
            trace << "TRACE — " << water_body_class_name (body.classification)
                  << " #" << body.id << " | " << flow.inlets.size ()
                  << " inlets | mean " << std::fixed << std::setprecision (1)
                  << meters_value (body.mean_depth) << "m | catchment "
                  << std::setprecision (0)
                  << square_meters_value (body.ocean_connected
                                            ? flow.inflow_area
                                            : flow.outflow_area)
                  << " m2";
          } else if (m_rivers->waterfall_by_cell[*m_inspected_cell] !=
                     terrain::Waterfall::no_id) {
            const terrain::Waterfall& waterfall =
              m_rivers
                ->waterfalls[m_rivers->waterfall_by_cell[*m_inspected_cell]];
            trace << "TRACE — FALL #" << waterfall.id << " | drop "
                  << std::fixed << std::setprecision (1)
                  << meters_value (waterfall.drop) << "m | slope "
                  << std::setprecision (2) << waterfall.slope << " | area "
                  << std::setprecision (0)
                  << square_meters_value (waterfall.contributing_area)
                  << " m2 | reach #" << waterfall.reach_id;
          } else if (m_rivers->reach_by_cell[*m_inspected_cell] !=
                     terrain::RiverReach::no_id) {
            const terrain::RiverReach& reach =
              m_rivers->reaches[m_rivers->reach_by_cell[*m_inspected_cell]];
            trace << "TRACE — REACH #" << reach.id << " | "
                  << reach.cells.size () << " cells | area " << std::fixed
                  << std::setprecision (0)
                  << square_meters_value (reach.downstream_area)
                  << " m2 | slope " << std::setprecision (3)
                  << reach.maximum_slope.numerical_value_in (mp_units::one);
            if (reach.downstream_ocean)
              trace << " | to ocean";
            else if (reach.downstream_body != terrain::no_water_body)
              trace << " | to body #" << reach.downstream_body;
            else if (reach.downstream_reach != terrain::RiverReach::no_id)
              trace << " | to reach #" << reach.downstream_reach;
          } else {
            trace << "TRACE — " << steps << " to outlet; " << catchment_cells
                  << "-cell basin | area " << std::fixed
                  << std::setprecision (0) << area << " m2";
          }
          m_overlay_status = trace.str ();
        }
        for (int y = 0; y < height; ++y)
          for (int x = 0; x < width; ++x)
            values[static_cast<std::size_t> (y) * width + x] =
              unique[(static_cast<std::size_t> (y) % unique_height) *
                       unique_width +
                     static_cast<std::size_t> (x) % unique_width];
      }
      m_renderer->set_terrain_overlay (params, values);
    }

    void TerrainLab::append_stage (terrain::TerrainTransform stage) {
      const int first_stage = static_cast<int> (program ().transforms.size ());
      program ().transforms.push_back (std::move (stage));
      m_selected_stage = static_cast<int> (program ().transforms.size ()) - 1;
      ensure_selected_stage_visible ();
      rerun_program_from (first_stage);
    }

    void TerrainLab::move_selected_stage (int direction) {
      const int first_stage =
        std::min (m_selected_stage, m_selected_stage + direction);
      const int target = m_selected_stage + direction;
      if (m_selected_stage < 0 || target < 0 ||
          target >= static_cast<int> (program ().transforms.size ()))
        return;
      std::swap (program ().transforms[m_selected_stage],
                 program ().transforms[target]);
      m_selected_stage = target;
      ensure_selected_stage_visible ();
      rerun_program_from (first_stage);
    }

    void TerrainLab::duplicate_selected_stage () {
      if (m_selected_stage < 0 ||
          m_selected_stage >= static_cast<int> (program ().transforms.size ()))
        return;
      const int first_stage = m_selected_stage + 1;
      const auto position = program ().transforms.begin () + first_stage;
      program ().transforms.insert (position,
                                    program ().transforms[m_selected_stage]);
      ++m_selected_stage;
      ensure_selected_stage_visible ();
      rerun_program_from (first_stage);
    }

    void TerrainLab::remove_selected_stage () {
      if (m_selected_stage < 0 ||
          m_selected_stage >= static_cast<int> (program ().transforms.size ()))
        return;
      const int first_stage = m_selected_stage;
      program ().transforms.erase (program ().transforms.begin () +
                                   m_selected_stage);
      if (program ().transforms.empty ())
        m_selected_stage = -1;
      else
        m_selected_stage =
          std::min (m_selected_stage,
                    static_cast<int> (program ().transforms.size ()) - 1);
      ensure_selected_stage_visible ();
      rerun_program_from (first_stage);
    }

    void TerrainLab::ensure_selected_stage_visible () {
      const int count = static_cast<int> (program ().transforms.size ());
      const int maximum = std::max (0, count - visible_stage_rows);
      if (m_selected_stage >= 0) {
        if (m_selected_stage < m_stage_scroll)
          m_stage_scroll = m_selected_stage;
        else if (m_selected_stage >= m_stage_scroll + visible_stage_rows)
          m_stage_scroll = m_selected_stage - visible_stage_rows + 1;
      }
      m_stage_scroll = std::clamp (m_stage_scroll, 0, maximum);
    }

    float TerrainLab::selected_property_normalized (int row) const {
      const terrain::TerrainProgramEditor editor (program ());
      const std::size_t property = static_cast<std::size_t> (row);
      return m_selected_stage < 0
               ? editor.source ().normalized_property (property)
               : editor.transform (static_cast<std::size_t> (m_selected_stage))
                   .normalized_property (property);
    }

    ParameterDomain TerrainLab::selected_property_domain (int row) const {
      const terrain::TerrainProgramEditor editor (program ());
      const std::size_t property = static_cast<std::size_t> (row);
      return m_selected_stage < 0
               ? editor.source ().property (property).domain
               : editor.transform (static_cast<std::size_t> (m_selected_stage))
                   .property (property)
                   .domain;
    }

    bool TerrainLab::selected_property_drag_enabled (int row) const {
      if (selected_property_domain (row) != ParameterDomain::Continuous)
        return false;
      const terrain::TerrainProgram& lab_program = program ();
      if (m_selected_stage < 0) {
        return std::ranges::all_of (
          lab_program.transforms, [] (const terrain::TerrainTransform& stage) {
            return std::holds_alternative<terrain::NormalizeHeights> (stage) ||
                   std::holds_alternative<terrain::PowerHeights> (stage);
          });
      }
      const terrain::TransformSemantics semantics =
        terrain::terrain_transform_semantics (
          lab_program.transforms[m_selected_stage]);
      return semantics.spatial_scope == terrain::SpatialScope::Pointwise &&
             semantics.evaluation_order == terrain::EvaluationOrder::Direct;
    }

    bool TerrainLab::set_selected_property_normalized (int row, float value) {
      if (selected_property_domain (row) != ParameterDomain::Continuous)
        return false;
      terrain::TerrainProgramEditor editor (program ());
      const std::size_t property = static_cast<std::size_t> (row);
      return m_selected_stage < 0
               ? editor.set_source_normalized_property (property, value)
               : editor.set_transform_normalized_property (
                   static_cast<std::size_t> (m_selected_stage),
                   property,
                   value);
    }

    bool TerrainLab::adjust_selected_continuous (int row, int direction) {
      if (direction == 0 || selected_property_drag_enabled (row))
        return false;
      const float value =
        selected_property_normalized (row) + direction * 0.05f;
      return set_selected_property_normalized (row, value);
    }

    bool TerrainLab::adjust_selected_natural (int row, int direction) {
      if (direction == 0 ||
          selected_property_domain (row) != ParameterDomain::Natural)
        return false;
      terrain::TerrainProgramEditor editor (program ());
      const std::size_t property = static_cast<std::size_t> (row);
      return m_selected_stage < 0
               ? editor.adjust_source_natural_property (property, direction)
               : editor.adjust_transform_natural_property (
                   static_cast<std::size_t> (m_selected_stage),
                   property,
                   direction);
    }

    void TerrainLab::queue_parameter_rebuild () {
      m_parameter_rebuild_pending = true;
      m_parameter_rebuild_stage = m_selected_stage;
    }

    void TerrainLab::run_pending_parameter_rebuild () {
      if (!m_parameter_rebuild_pending)
        return;
      const int stage = m_parameter_rebuild_stage;
      m_parameter_rebuild_pending = false;
      bool iterative = false;
      if (stage >= 0 &&
          stage < static_cast<int> (program ().transforms.size ())) {
        iterative =
          terrain::terrain_transform_semantics (program ().transforms[stage])
            .evaluation_order == terrain::EvaluationOrder::Iterative;
        rerun_program_from (stage);
      } else {
        rebuild_program ();
      }
      m_parameter_rebuild_delay = iterative ? 0.18f : 0.045f;
    }

    void TerrainLab::apply_friendly_preset (int preset) {
      program () = calibrated_world_program (
        program ().seed, terrain::TerrainGenerationProfile::Play);
      auto& recipe = program ().source.recipe;
      auto& orogeny =
        std::get<terrain::OrogenyEvolution> (program ().transforms.front ());
      if (preset == 0) {
        recipe.blend.mountain_weight = 1.35f;
        recipe.warp.amplitude = 0.22f;
        orogeny.evolution.duration =
          180000.0f * mp_units::astronomy::Julian_year;
        orogeny.maximum_uplift_rate =
          0.0027f * mp_units::si::metre / mp_units::astronomy::Julian_year;
      } else if (preset == 1) {
        recipe.blend.mountain_weight = 0.45f;
        recipe.warp.amplitude = 0.08f;
        orogeny.evolution.duration =
          800000.0f * mp_units::astronomy::Julian_year;
        orogeny.maximum_uplift_rate =
          0.0007f * mp_units::si::metre / mp_units::astronomy::Julian_year;
      } else if (preset == 2) {
        recipe.blend.continent_weight = 0.72f;
        recipe.blend.mountain_weight = 1.05f;
        orogeny.evolution.duration =
          650000.0f * mp_units::astronomy::Julian_year;
        orogeny.evolution.reference_incision_rate =
          0.00018f * mp_units::si::metre / mp_units::astronomy::Julian_year;
      } else {
        // Mountain texture becomes a tectonic-rate pattern over a shallow
        // continent seed. Four drainage refreshes keep the first interactive
        // preset responsive; Build mode exposes the full geological span.
        recipe.blend.plains_weight = 0.25f;
        recipe.blend.mountain_weight = 0.9f;
        recipe.warp.amplitude = 0.28f;
        orogeny.evolution.duration =
          200000.0f * mp_units::astronomy::Julian_year;
        orogeny.evolution.time_step =
          50000.0f * mp_units::astronomy::Julian_year;
      }
      m_selected_stage = -1;
      m_stage_scroll = 0;
      m_friendly_preset = preset;
      rebuild_program ();
    }

    void TerrainLab::handle_friendly_click (float x, float y) {
      for (int i = 0; i < 3; ++i) {
        if (!friendly_action_rect (i, m_ui_height).contains (x, y))
          continue;
        if (i == 0) {
          program () =
            calibrated_world_program (terrain::next_seed (program ().seed),
                                      terrain::TerrainGenerationProfile::Play);
          m_selected_stage = -1;
          m_friendly_preset = -1;
          rebuild_program ();
        } else if (i == 1) {
          fit_view ();
        } else {
          m_build_ui = true;
        }
        return;
      }
      for (int i = 0; i < 4; ++i) {
        if (friendly_preset_rect (i, m_ui_height).contains (x, y)) {
          apply_friendly_preset (i);
          return;
        }
      }
      constexpr OverlayMode modes[] = {
        OverlayMode::None,          OverlayMode::Height,
        OverlayMode::Slope,         OverlayMode::Flow,
        OverlayMode::Streams,       OverlayMode::Basins,
        OverlayMode::Sinks,         OverlayMode::Trace,
        OverlayMode::StandingWater, OverlayMode::PermanentWater,
        OverlayMode::Waterfalls,    OverlayMode::HeightDelta,
        OverlayMode::Eroded,        OverlayMode::Deposited
      };
      for (int i = 0; i < 14; ++i) {
        if (!friendly_overlay_rect (i, m_ui_height).contains (x, y))
          continue;
        set_overlay (modes[i]);
        return;
      }
    }

    void TerrainLab::handle_readings_click (float x, float y) {
      constexpr OverlayMode overlay_modes[] = {
        OverlayMode::None,           OverlayMode::Height,
        OverlayMode::Slope,          OverlayMode::Flow,
        OverlayMode::Streams,        OverlayMode::Basins,
        OverlayMode::Sinks,          OverlayMode::HeightDelta,
        OverlayMode::Trace,          OverlayMode::StandingWater,
        OverlayMode::PermanentWater, OverlayMode::Waterfalls,
        OverlayMode::Eroded,         OverlayMode::Deposited
      };
      for (int i = 0; i < 14; ++i)
        if (overlay_rect (i).contains (x, y)) {
          set_overlay (overlay_modes[i]);
          return;
        }
    }

    void TerrainLab::handle_build_click (float x, float y) {
      if (observe_back_rect ().contains (x, y)) {
        m_build_ui = false;
        return;
      }
      if (close_rect ().contains (x, y)) {
        leave ();
        return;
      }
      if (reset_rect ().contains (x, y)) {
        reset_program ();
        return;
      }
      if (seed_rect ().contains (x, y)) {
        const terrain::Seed seed = terrain::next_seed (program ().seed);
        program ().source.recipe.seeds =
          terrain::derive_geological_seeds (seed.value);
        program ().seed = seed;
        m_selected_stage = -1;
        rebuild_program ();
        return;
      }
      if (fit_rect ().contains (x, y)) {
        fit_view ();
        return;
      }
      if (view_rect ().contains (x, y)) {
        cycle_view ();
        return;
      }
      for (int i = 0; i < 7; ++i) {
        if (layer_rect (i).contains (x, y)) {
          select (layers[i]);
          return;
        }
      }
      if (source_rect ().contains (x, y)) {
        m_selected_stage = -1;
        if (m_overlay == OverlayMode::HeightDelta)
          update_overlay ();
        return;
      }
      for (int row = 0; row < visible_stage_rows; ++row) {
        const int stage = m_stage_scroll + row;
        if (stage < static_cast<int> (program ().transforms.size ()) &&
            stage_rect (row).contains (x, y)) {
          m_selected_stage = stage;
          if (m_overlay == OverlayMode::HeightDelta)
            update_overlay ();
          return;
        }
      }
      for (int i = 0; i < 7; ++i) {
        if (!add_stage_rect (i).contains (x, y))
          continue;
        if (i == 0)
          append_stage (terrain::NormalizeHeights {});
        else if (i == 1)
          append_stage (terrain::PowerHeights { 1.15f });
        else if (i == 2)
          append_stage (terrain::AnalyticalErosion {});
        else if (i == 3) {
          program ().source.mode = terrain::GeologicalSource::Mode::Orogeny;
          if (program ().transforms.size () == 1 &&
              std::holds_alternative<terrain::NormalizeHeights> (
                program ().transforms.front ()))
            program ().transforms.clear ();
          program ().transforms.emplace_back (terrain::OrogenyEvolution {});
          m_selected_stage =
            static_cast<int> (program ().transforms.size ()) - 1;
          ensure_selected_stage_visible ();
          rebuild_program ();
        } else if (i == 4)
          append_stage (
            terrain::ThermalErosion { terrain::iteration_count (2), 0.003f });
        else if (i == 5)
          append_stage (terrain::HillslopeDiffusion {});
        else
          append_stage (terrain::TrailFormation {});
        return;
      }
      for (int i = 0; i < 4; ++i) {
        if (!edit_stage_rect (i).contains (x, y))
          continue;
        if (i == 0)
          move_selected_stage (-1);
        else if (i == 1)
          move_selected_stage (1);
        else if (i == 2)
          duplicate_selected_stage ();
        else
          remove_selected_stage ();
        return;
      }

      int property_count = 9;
      if (m_selected_stage >= 0)
        property_count = terrain::TerrainTransformEditor (
                           program ().transforms[m_selected_stage])
                           .property_count ();
      for (int row = 0; row < property_count; ++row) {
        const ParameterDomain domain = selected_property_domain (row);
        const bool stepped_continuous = domain == ParameterDomain::Continuous &&
                                        !selected_property_drag_enabled (row);
        if (domain != ParameterDomain::Natural && !stepped_continuous)
          continue;
        const UiRect bounds = property_rect (row);
        const int direction = counter_minus_rect (bounds).contains (x, y)  ? -1
                              : counter_plus_rect (bounds).contains (x, y) ? 1
                                                                           : 0;
        const bool changed = domain == ParameterDomain::Natural
                               ? adjust_selected_natural (row, direction)
                               : adjust_selected_continuous (row, direction);
        if (changed) {
          queue_parameter_rebuild ();
          run_pending_parameter_rebuild ();
          return;
        }
      }
    }

    attenuation_t TerrainLab::scene_fog (attenuation_t) const {
      // The Lab is an inspection instrument. Atmospheric depth cues are
      // useful while riding, but here they make finite terrain look clipped
      // or imply a boundary where the toroidal world has none.
      return 0.0f / u::m;
    }

    void TerrainLab::refresh (bool inspection_fog) {
      if (!m_renderer || !m_map || !m_terrain || !m_world || !m_graphics)
        return;
      // A pristine map is the game's own terrain with baked normals:
      // render it at full quality.  Preview quality (GPU-derived
      // normals, small shadow map) is for maps the lab has rebuilt and
      // may rebuild again while a parameter drags.
      const bool interactive_preview =
        inspection_fog &&
        (!m_model.map_pristine () || m_view == ViewMode::Torus);
      if (!interactive_preview)
        m_map->recompute_normals ();
      WorldParams display = *m_world;
      if (inspection_fog)
        display.fog_scale = scene_fog (m_world->fog_scale);
      const render::TerrainProjection projection =
        inspection_fog && m_view == ViewMode::Torus
          ? render::TerrainProjection::Torus
          : render::TerrainProjection::Plane;
      const bool repeat = !inspection_fog || m_view == ViewMode::Cover;
      m_terrain->setup (*m_renderer,
                        *m_map,
                        display,
                        *m_graphics,
                        projection,
                        repeat,
                        interactive_preview);
      if (m_model.map_pristine ()) {
        m_path_presentation.reuse_path_payloads (m_saved_trail_influence,
                                                 m_saved_home_base_influence);
      } else if (const auto& network = m_model.trail_network ()) {
        m_path_presentation.refresh_paths (*network);
      } else {
        m_path_presentation.reuse_path_payloads ({}, {});
      }
      m_path_presentation.upload_paths (*m_renderer);
      if (m_graphics->terrain_shadows)
        m_terrain->render_shadow (*m_renderer, *m_map, m_sun_dir);
      update_overlay ();
    }

    void TerrainLab::tick (float dt) {
      MOPPE_PROFILE_ZONE ("TerrainLab::tick");
      if (m_parameter_rebuild_delay > 0.0f)
        m_parameter_rebuild_delay -= dt;
      // Iterative terrain transforms can take longer than a frame on the
      // full-resolution Lab map. Keep the thumb responsive while dragging and
      // evaluate the final value on release instead of repeatedly blocking the
      // event loop with obsolete intermediate rebuilds.
      if (m_parameter_rebuild_pending && m_parameter_rebuild_delay <= 0.0f &&
          !m_parameter_drag)
        run_pending_parameter_rebuild ();

      if (m_history_playing && m_history && m_history->size () > 1) {
        m_history_age += dt;
        if (m_history_age >= 0.85f) {
          m_history_age = 0.0f;
          const std::size_t next = m_history_index + 1;
          if (next < m_history->size ())
            show_history_snapshot (next);
          else
            m_history_playing = false;
        }
      }
      const float orbit =
        (m_orbit_right ? 1.0f : 0.0f) - (m_orbit_left ? 1.0f : 0.0f);
      const float tilt =
        (m_tilt_up ? 1.0f : 0.0f) - (m_tilt_down ? 1.0f : 0.0f);
      const float zoom = (m_zoom_out ? 1.0f : 0.0f) - (m_zoom_in ? 1.0f : 0.0f);

      m_yaw += orbit * dt * 0.85f;
      m_pitch += tilt * dt * 0.55f;
      m_pitch = std::clamp (m_pitch, 0.18f, 1.48f);
      m_scroll_zoom_target *= std::exp (zoom * dt * 1.1f);
      const float maximum_zoom = std::max (1500.0f, m_fit_distance * 1.6f);
      m_scroll_zoom_target =
        std::clamp (m_scroll_zoom_target, 500.0f, maximum_zoom);
      const damping_t zoom_speed = 14.0f / u::s;
      const float zoom_response = smoothing_alpha (zoom_speed, dt * u::s);
      m_distance += (m_scroll_zoom_target - m_distance) * zoom_response;
    }

    void TerrainLab::key (platform::Key key, bool down) {
      using platform::Key;
      if (!m_active)
        return;

      if (key == Key::Left || key == Key::A)
        m_orbit_left = down;
      else if (key == Key::Right || key == Key::D)
        m_orbit_right = down;
      else if (key == Key::Up)
        m_zoom_in = down;
      else if (key == Key::Down)
        m_zoom_out = down;
      else if (key == Key::W)
        m_tilt_up = down;
      else if (key == Key::S)
        m_tilt_down = down;

      if (!down)
        return;

      switch (key) {
      case Key::Space:
        if (m_history && m_history->size () > 1) {
          if (m_history_index + 1 >= m_history->size ())
            show_history_snapshot (0);
          m_history_playing = !m_history_playing;
          m_history_age = 0.0f;
        }
        break;
      case Key::Tab:
        if (m_history && !m_history->empty ()) {
          m_history_playing = false;
          show_history_snapshot ((m_history_index + 1) % m_history->size ());
        }
        break;
      case Key::One:
        if (m_build_ui)
          select (terrain::GeologicalLayer::Combined);
        else
          set_overlay (OverlayMode::None);
        break;
      case Key::Two:
        if (m_build_ui)
          select (terrain::GeologicalLayer::Continent);
        else
          set_overlay (OverlayMode::Slope);
        break;
      case Key::Three:
        if (m_build_ui)
          select (terrain::GeologicalLayer::Plains);
        else
          set_overlay (OverlayMode::StandingWater);
        break;
      case Key::Four:
        if (m_build_ui)
          select (terrain::GeologicalLayer::Mountains);
        else
          set_overlay (OverlayMode::Streams);
        break;
      case Key::Five:
        select (terrain::GeologicalLayer::MountainMask);
        break;
      case Key::Six:
        select (terrain::GeologicalLayer::WarpX);
        break;
      case Key::Seven:
        select (terrain::GeologicalLayer::WarpY);
        break;
      case Key::R:
        reset_program ();
        break;
      case Key::N:
        program ().seed = terrain::next_seed (program ().seed);
        program ().source.recipe.seeds =
          terrain::derive_geological_seeds (program ().seed.value);
        m_selected_stage = -1;
        rebuild_program ();
        break;
      case Key::Y:
        append_stage (
          terrain::ThermalErosion { terrain::iteration_count (2), 0.003f });
        break;
      default:
        break;
      }
    }

    std::string TerrainLab::history_snapshot_name (std::size_t index) const {
      if (index == 0)
        return "MATERIALIZED FIELD";
      if (index <= program ().transforms.size ()) {
        const std::string_view id =
          terrain::terrain_transform_id (program ().transforms[index - 1]);
        std::string name (id);
        std::transform (name.begin (), name.end (), name.begin (), [] (char c) {
          return static_cast<char> (
            std::toupper (static_cast<unsigned char> (c)));
        });
        return name;
      }
      return "FINAL TERRAIN";
    }

    void TerrainLab::show_history_snapshot (std::size_t index) {
      if (!m_history || !m_map || index >= m_history->size ())
        return;
      const std::vector<float>& heights = (*m_history)[index];
      const std::size_t count =
        static_cast<std::size_t> (m_map->width ()) * m_map->height ();
      if (heights.size () != count)
        return;
      std::copy (heights.begin (), heights.end (), m_map->raw_heights ());
      m_map->synchronize_periodic_edges ();
      m_history_index = index;
      m_history_age = 0.0f;
      m_selected_stage = index == 0 ? -1 : static_cast<int> (index - 1);
      m_model.set_map_pristine (index + 1 == m_history->size ());
      invalidate_analysis ();
      refresh ();
    }

    void TerrainLab::pointer_move (float x, float y, float dx, float dy) {
      if (!m_active)
        return;
      m_pointer_x = x;
      m_pointer_y = y;
      if (m_observe_window.dragging ()) {
        m_observe_window.drag_to (x, y, m_ui_width, m_ui_height);
        return;
      }
      if (m_readings_window.dragging ()) {
        m_readings_window.drag_to (x, y, m_ui_width, m_ui_height);
        return;
      }
      if (m_build_window.dragging ()) {
        m_build_window.drag_to (x, y, m_ui_width, m_ui_height);
        return;
      }
      if (m_parameter_drag) {
        const float normalized =
          m_drag_start_normalized + (m_drag_start_y - y) / 180.0f;
        if (set_selected_property_normalized (m_drag_property, normalized))
          queue_parameter_rebuild ();
        return;
      }
      if (m_pan_drag) {
        const Vec3 f = forward ();
        Vec3 right = cross (f, Vec3 (0, 1, 0));
        Vec3 ground_forward (f[0], 0, f[2]);
        normalize (right);
        normalize (ground_forward);
        const float scale = m_distance * 0.0012f;
        m_target -= right * (dx * scale);
        m_target += ground_forward * (dy * scale);
        return;
      }
      if (!m_camera_drag)
        return;
      m_camera_drag_distance += std::hypot (dx, dy);
      m_yaw -= dx * 0.006f;
      m_pitch += dy * 0.006f;
      m_pitch = std::clamp (m_pitch, 0.18f, 1.48f);
    }

    void TerrainLab::pointer_button (platform::PointerButton button,
                                     bool down,
                                     float x,
                                     float y) {
      if (!m_active)
        return;
      m_pointer_x = x;
      m_pointer_y = y;
      if (button == platform::PointerButton::Primary) {
        m_pointer_down = down;
        if (!down) {
          const bool moved_window = m_observe_window.dragging () ||
                                    m_build_window.dragging () ||
                                    m_readings_window.dragging ();
          m_observe_window.end_drag ();
          m_build_window.end_drag ();
          m_readings_window.end_drag ();
          if (moved_window)
            return;
        }
        if (down) {
          if (!m_build_ui) {
            if (m_observe_window.begin_drag (x, y))
              return;
            if (m_observe_window.contains (x, y))
              handle_friendly_click (m_observe_window.local_x (x),
                                     m_observe_window.local_y (y));
            else {
              m_camera_drag = true;
              m_camera_drag_distance = 0.0f;
            }
          } else {
            if (m_readings_window.begin_drag (x, y))
              return;
            const float build_x = m_build_window.local_x (x);
            const float build_y = m_build_window.local_y (y);
            if (m_build_window.contains (x, y) &&
                close_rect ().contains (build_x, build_y)) {
              handle_build_click (build_x, build_y);
              return;
            }
            if (m_build_window.begin_drag (x, y))
              return;
            int property_count = 9;
            if (m_selected_stage >= 0)
              property_count = terrain::TerrainTransformEditor (
                                 program ().transforms[m_selected_stage])
                                 .property_count ();
            if (m_build_window.contains (x, y)) {
              for (int row = 0; row < property_count; ++row) {
                if (!parameter_control_rect (property_rect (row))
                       .contains (build_x, build_y))
                  continue;
                if (!selected_property_drag_enabled (row))
                  continue;
                m_parameter_drag = true;
                m_drag_property = row;
                m_drag_start_y = y;
                m_drag_start_normalized = selected_property_normalized (row);
                return;
              }
            }
            if (m_readings_window.contains (x, y))
              handle_readings_click (m_readings_window.local_x (x),
                                     m_readings_window.local_y (y));
            else if (m_build_window.contains (x, y))
              handle_build_click (build_x, build_y);
            else {
              m_camera_drag = true;
              m_camera_drag_distance = 0.0f;
            }
          }
        } else {
          if (m_parameter_drag) {
            m_parameter_drag = false;
            m_drag_property = -1;
            run_pending_parameter_rebuild ();
          }
          if (m_camera_drag && m_camera_drag_distance < 4.0f) {
            if (m_overlay == OverlayMode::Trace)
              inspect_drainage (x, y);
          }
          m_camera_drag = false;
        }
      } else {
        m_pan_drag = down;
      }
    }

    void TerrainLab::pointer_scroll (float x, float y, float delta) {
      if (!m_active || delta == 0.0f)
        return;
      m_pointer_x = x;
      m_pointer_y = y;
      const bool over_ui = m_build_ui ? m_build_window.contains (x, y) ||
                                          m_readings_window.contains (x, y)
                                      : m_observe_window.contains (x, y);
      if (over_ui) {
        if (m_build_ui && m_build_window.contains (x, y) &&
            stage_list_rect ().contains (m_build_window.local_x (x),
                                         m_build_window.local_y (y)) &&
            program ().transforms.size () > visible_stage_rows) {
          m_stage_scroll += delta > 0.0f ? -1 : 1;
          const int maximum = static_cast<int> (program ().transforms.size ()) -
                              visible_stage_rows;
          m_stage_scroll = std::clamp (m_stage_scroll, 0, maximum);
        }
        return;
      }
      const float amount = std::clamp (delta, -2.0f, 2.0f);
      m_scroll_zoom_target *= std::exp (-amount * 0.028f);
      const float maximum_zoom = std::max (1500.0f, m_fit_distance * 1.6f);
      m_scroll_zoom_target =
        std::clamp (m_scroll_zoom_target, 500.0f, maximum_zoom);
    }

    Vec3 TerrainLab::position () const {
      const float horizontal = std::cos (m_pitch) * m_distance;
      Vec3 camera = m_target + Vec3 (std::sin (m_yaw) * horizontal,
                                     std::sin (m_pitch) * m_distance,
                                     std::cos (m_yaw) * horizontal);
      if (m_map && m_view != ViewMode::Torus) {
        // Orbiting changes x/z as well as altitude.  Keep the camera body
        // above the height field at every frame so neither a manual orbit nor
        // the automatic chase can tunnel through an intervening ridge.
        const float terrain_height =
          m_map->interpolated_height (camera[0], camera[2]);
        camera[1] = std::max (camera[1], terrain_height + 30.0f);
      }
      return camera;
    }

    Vec3 TerrainLab::forward () const {
      return normalized (m_target - position ());
    }

    Mat4 TerrainLab::view_matrix () const {
      return Mat4::look_at (position (), m_target, Vec3 (0, 1, 0));
    }

    void TerrainLab::draw_friendly (render::DrawList& dl,
                                    int width,
                                    int height) const {
      (void)width;
      const auto hot = [this] (const UiRect& bounds) {
        return bounds.contains (m_observe_window.local_x (m_pointer_x),
                                m_observe_window.local_y (m_pointer_y));
      };
      m_ui.begin (dl);
      m_ui.begin_window (dl, m_observe_window, "TERRAIN LAB / OBSERVE");
      const UiRect panel = friendly_panel_rect (height);
      const UiRect content = friendly_content_rect (height);
      m_ui.friendly_section (
        dl, { content.x, panel.y + 110.0f, content.width, 18.0f }, "WORLDS");
      m_ui.friendly_section (
        dl,
        { content.x, panel.y + 252.0f, content.width, 18.0f },
        "OBSERVE / SURFACE BUNDLE");

      constexpr const char* action_labels[] = { "NEW", "CENTER", "BUILD" };
      for (int i = 0; i < 3; ++i) {
        const UiRect bounds = friendly_action_rect (i, height);
        m_ui.friendly_button (dl,
                              bounds,
                              action_labels[i],
                              i == 0   ? "seed"
                              : i == 1 ? "fit"
                                       : "advanced",
                              hot (bounds),
                              m_pointer_down,
                              false,
                              false);
      }
      constexpr const char* preset_titles[] = {
        "YOUNG PEAKS", "OLD HILLS", "RIVER COUNTRY", "OROGENY"
      };
      constexpr const char* preset_details[] = { "sharp relief",
                                                 "aged + relaxed",
                                                 "river alignments",
                                                 "uplift + incision" };
      for (int i = 0; i < 4; ++i) {
        const UiRect bounds = friendly_preset_rect (i, height);
        m_ui.friendly_button (dl,
                              bounds,
                              preset_titles[i],
                              preset_details[i],
                              hot (bounds),
                              m_pointer_down,
                              m_friendly_preset == i,
                              false);
      }

      constexpr const char* overlay_titles[] = {
        "MATERIAL", "HEIGHT", "SLOPE", "FLOW",  "STREAMS", "BASINS", "OUTLETS",
        "TRACE",    "WATER",  "LAKES", "FALLS", "DELTA",   "ERODED", "DEPOSIT"
      };
      constexpr const char* overlay_details[] = {
        "Terrain materials and relief",
        "Normalized terrain elevation",
        "Physical rise over run",
        "Upslope area reaching each cell",
        "Connected extracted river reaches",
        "Cells sharing a terminal outlet",
        "Terminal wet routes",
        "Click terrain to follow its receiver path",
        "Every standing depth to spill level",
        "Persistent water bodies",
        "Steep high-flow steps",
        "Selected stage: before versus after",
        "Lifetime material removed",
        "Lifetime material settled"
      };
      constexpr OverlayMode overlay_modes[] = {
        OverlayMode::None,          OverlayMode::Height,
        OverlayMode::Slope,         OverlayMode::Flow,
        OverlayMode::Streams,       OverlayMode::Basins,
        OverlayMode::Sinks,         OverlayMode::Trace,
        OverlayMode::StandingWater, OverlayMode::PermanentWater,
        OverlayMode::Waterfalls,    OverlayMode::HeightDelta,
        OverlayMode::Eroded,        OverlayMode::Deposited
      };
      int selected = 0;
      for (int i = 0; i < 14; ++i) {
        const UiRect bounds = friendly_overlay_rect (i, height);
        if (m_overlay == overlay_modes[i])
          selected = i;
        m_ui.friendly_button (dl,
                              bounds,
                              overlay_titles[i],
                              "",
                              hot (bounds),
                              m_pointer_down,
                              m_overlay == overlay_modes[i]);
      }

      m_ui.friendly_section (
        dl, { content.x, panel.y + 562.0f, content.width, 18.0f }, "READOUT");
      const std::size_t separator = m_overlay_status.find (" | ");
      std::string primary = separator == std::string::npos
                              ? m_overlay_status
                              : m_overlay_status.substr (0, separator);
      std::string secondary = overlay_details[selected];
      if (separator != std::string::npos)
        secondary = m_overlay_status.substr (separator + 3);
      const auto shorten = [] (std::string& text) {
        if (text.size () > 43)
          text = text.substr (0, 40) + "...";
      };
      shorten (primary);
      shorten (secondary);
      m_ui.paragraph (dl, content.x, panel.y + 606.0f, primary, true);
      m_ui.caption (dl, content.x, panel.y + 628.0f, secondary);

      if (panel.height > 680.0f && m_history && !m_history->empty ()) {
        std::ostringstream playback;
        playback << (m_history_playing ? "PLAYING " : "PAUSED ")
                 << (m_history_index + 1) << '/' << m_history->size () << "  "
                 << history_snapshot_name (m_history_index);
        m_ui.caption (
          dl, content.x, panel.y + panel.height - 39.0f, playback.str ());
      }
      m_ui.caption (dl,
                    content.x,
                    panel.y + panel.height - 17.0f,
                    "DRAG ORBIT   WHEEL ZOOM   SPACE PLAY   T BACK");
      m_ui.end_window (dl);
      m_ui.end (dl);
    }

    void TerrainLab::draw (render::DrawList& dl, int width, int height) {
      m_ui_width = width;
      m_ui_height = height;
      m_observe_window.set_size (friendly_panel_rect (height).width,
                                 friendly_panel_rect (height).height);
      m_observe_window.constrain (width, height);
      m_build_window.constrain (width, height);
      m_readings_window.constrain (width, height);
      if (!m_build_ui)
        draw_friendly (dl, width, height);
      else
        draw_build (dl);
      m_ui.begin (dl);
      draw_compass (dl, width);
      m_ui.end (dl);
    }

    void TerrainLab::draw_compass (render::DrawList& dl, int width) const {
      const UiRect bounds {
        static_cast<float> (width) - 106.0f, 18.0f, 88.0f, 88.0f
      };
      m_ui.surface (dl, bounds);
      const float cx = bounds.x + bounds.width * 0.5f;
      const float cy = bounds.y + bounds.height * 0.5f;
      const float radius = 24.0f;
      const Vec3 north (-std::sin (m_yaw), std::cos (m_yaw), 0.0f);
      const Vec3 east (-std::cos (m_yaw), -std::sin (m_yaw), 0.0f);

      dl.color (0.42f, 0.48f, 0.52f, 0.75f);
      dl.line (cx - east[0] * radius,
               cy - east[1] * radius,
               cx + east[0] * radius,
               cy + east[1] * radius,
               1.0f);
      dl.line (cx - north[0] * radius,
               cy - north[1] * radius,
               cx + north[0] * radius,
               cy + north[1] * radius,
               1.0f);
      dl.color (1.0f, 0.72f, 0.18f, 0.98f);
      dl.line (cx, cy, cx + north[0] * radius, cy + north[1] * radius, 2.5f);
      dl.begin (render::Prim::Triangles);
      dl.vertex (cx + north[0] * (radius + 5.0f),
                 cy + north[1] * (radius + 5.0f));
      dl.vertex (cx + north[0] * (radius - 4.0f) + east[0] * 4.0f,
                 cy + north[1] * (radius - 4.0f) + east[1] * 4.0f);
      dl.vertex (cx + north[0] * (radius - 4.0f) - east[0] * 4.0f,
                 cy + north[1] * (radius - 4.0f) - east[1] * 4.0f);
      dl.end ();
      m_ui.label (dl,
                  cx + north[0] * 35.0f - 4.0f,
                  cy + north[1] * 35.0f + 4.0f,
                  "N",
                  true);
      m_ui.label (
        dl, cx + east[0] * 33.0f - 4.0f, cy + east[1] * 33.0f + 4.0f, "E");
    }

    void TerrainLab::draw_build (render::DrawList& dl) const {
      const auto hot = [this] (const UiRect& bounds) {
        return bounds.contains (m_build_window.local_x (m_pointer_x),
                                m_build_window.local_y (m_pointer_y));
      };
      const terrain::TerrainProgram& lab_program = program ();
      const terrain::TerrainProgramEditor editor (lab_program);

      m_ui.begin (dl);
      m_ui.begin_window (dl, m_build_window, "TERRAIN LAB / BUILD");
      m_ui.button (dl, close_rect (), "X", hot (close_rect ()), m_pointer_down);
      m_ui.button (
        dl, reset_rect (), "RESET", hot (reset_rect ()), m_pointer_down);

      std::ostringstream seed;
      seed << "SEED " << lab_program.seed.value << " >";
      m_ui.button (
        dl, seed_rect (), seed.str (), hot (seed_rect ()), m_pointer_down);
      m_ui.button (dl, fit_rect (), "FIT", hot (fit_rect ()), m_pointer_down);
      const char* view_label = m_view == ViewMode::Tile    ? "VIEW: TILE"
                               : m_view == ViewMode::Cover ? "VIEW: COVER"
                                                           : "VIEW: DONUT";
      m_ui.button (dl,
                   view_rect (),
                   view_label,
                   hot (view_rect ()),
                   m_pointer_down,
                   m_view != ViewMode::Tile);
      m_ui.button (dl,
                   observe_back_rect (),
                   "OBSERVE",
                   hot (observe_back_rect ()),
                   m_pointer_down);
      for (int i = 0; i < 7; ++i) {
        const UiRect bounds = layer_rect (i);
        m_ui.button (dl,
                     bounds,
                     layer_labels[i],
                     hot (bounds),
                     m_pointer_down,
                     lab_program.source.layer == layers[i]);
      }

      m_ui.section_header (dl, pipeline_header_rect (), "PIPELINE");
      m_ui.section_header (dl,
                           property_header_rect (),
                           m_selected_stage < 0 ? "RECIPE PARAMETERS"
                                                : "STAGE PARAMETERS");

      const UiRect source = source_rect ();
      m_ui.pipeline_row (
        dl,
        source,
        "F",
        lab_program.source.mode == terrain::GeologicalSource::Mode::Orogeny
          ? "OROGENY SEED"
          : "GEOLOGICAL FIELD",
        lab_program.source.mode == terrain::GeologicalSource::Mode::Orogeny
          ? "shallow continent / recipe becomes uplift"
          : terrain::geological_layer_name (lab_program.source.layer),
        hot (source),
        m_pointer_down,
        m_selected_stage < 0);

      dl.color (0.37f, 0.55f, 0.37f, 0.9f);
      dl.line (source.x + 17,
               source.y + source.height,
               source.x + 17,
               stage_rect (visible_stage_rows - 1).y + stage_row_height,
               2.0f);
      for (int row = 0; row < visible_stage_rows; ++row) {
        const int stage_index = m_stage_scroll + row;
        if (stage_index >= static_cast<int> (lab_program.transforms.size ()))
          break;
        const UiRect bounds = stage_rect (row);
        m_ui.pipeline_row (dl,
                           bounds,
                           std::to_string (stage_index + 1),
                           stage_name (lab_program.transforms[stage_index]),
                           stage_detail (lab_program.transforms[stage_index]),
                           hot (bounds),
                           m_pointer_down,
                           m_selected_stage == stage_index);
      }

      static const char* add_labels[] = { "+NORM",  "+POWER", "+AGE",  "+OROG",
                                          "+TALUS", "+CREEP", "+TRAIL" };
      static const char* edit_labels[] = { "UP", "DOWN", "COPY", "DEL" };
      for (int i = 0; i < 7; ++i) {
        const UiRect add = add_stage_rect (i);
        m_ui.button (dl, add, add_labels[i], hot (add), m_pointer_down);
      }
      for (int i = 0; i < 4; ++i) {
        const UiRect edit = edit_stage_rect (i);
        m_ui.button (
          dl, edit, edit_labels[i], hot (edit), m_pointer_down, false);
      }

      if (m_selected_stage < 0) {
        for (int row = 0; row < 9; ++row) {
          const UiRect bounds = property_rect (row);
          const PropertyText property =
            editor.source ().property (static_cast<std::size_t> (row));
          if (selected_property_drag_enabled (row)) {
            m_ui.knob (dl,
                       bounds,
                       property.label,
                       property.value,
                       selected_property_normalized (row),
                       hot (parameter_control_rect (bounds)),
                       m_parameter_drag && m_drag_property == row);
          } else {
            m_ui.counter (dl,
                          bounds,
                          property.domain == ParameterDomain::Continuous
                            ? property.label + " [STEP]"
                            : property.label,
                          property.value,
                          hot (counter_minus_rect (bounds)),
                          hot (counter_plus_rect (bounds)),
                          m_pointer_down);
          }
        }
      } else {
        const terrain::TerrainTransform& stage =
          lab_program.transforms[m_selected_stage];
        const terrain::TerrainTransformEditor stage_editor (stage);
        const int count = static_cast<int> (stage_editor.property_count ());
        for (int row = 0; row < count; ++row) {
          const UiRect bounds = property_rect (row);
          const PropertyText property =
            stage_editor.property (static_cast<std::size_t> (row));
          if (selected_property_drag_enabled (row)) {
            m_ui.knob (dl,
                       bounds,
                       property.label,
                       property.value,
                       selected_property_normalized (row),
                       hot (parameter_control_rect (bounds)),
                       m_parameter_drag && m_drag_property == row);
          } else {
            m_ui.counter (dl,
                          bounds,
                          property.domain == ParameterDomain::Continuous
                            ? property.label + " [STEP]"
                            : property.label,
                          property.value,
                          hot (counter_minus_rect (bounds)),
                          hot (counter_plus_rect (bounds)),
                          m_pointer_down);
          }
        }
        if (count <= 3) {
          m_ui.label (dl, right_x + 8, 280, stage_name (stage), true);
          m_ui.label (dl, right_x + 8, 302, stage_detail (stage));
          m_ui.label (dl, right_x + 8, 324, semantics_detail (stage), true);
        }
        if (std::holds_alternative<terrain::NormalizeHeights> (stage)) {
          m_ui.label (
            dl, right_x + 8, 352, "A whole-raster materialization barrier.");
          m_ui.label (
            dl, right_x + 8, 372, "It can be moved, copied, or deleted.");
        }
      }

      std::ostringstream status;
      status << lab_program.transforms.size () << " stages | selected ";
      if (m_selected_stage < 0)
        status << "field recipe";
      else
        status << (m_selected_stage + 1);
      m_ui.label (dl, left_x, 590, status.str (), true);
      m_ui.label (dl,
                  left_x,
                  611,
                  "LEFT DRAG orbit | RIGHT DRAG pan | TERRAIN WHEEL zoom");
      m_ui.label (dl, left_x, 632, "Select or reorder stages | T returns");
      m_ui.key_hint (dl, right_x + 8, 544, "DIAL", "live direct value");
      m_ui.key_hint (dl, right_x + 8, 570, "- / +", "count or costly step");

      m_ui.end_window (dl);
      m_ui.begin_window (dl, m_readings_window, "MAP READINGS");
      const auto readings_hot = [this] (const UiRect& bounds) {
        return bounds.contains (m_readings_window.local_x (m_pointer_x),
                                m_readings_window.local_y (m_pointer_y));
      };
      constexpr float readings_x = 0.0f;
      constexpr float readings_y = 0.0f;
      constexpr const char* overlay_labels[] = {
        "MATERIAL", "HEIGHT", "SLOPE", "FLOW",  "STREAMS", "BASINS", "OUTLETS",
        "DELTA",    "TRACE",  "WATER", "LAKES", "FALLS",   "ERODED", "DEPOSIT"
      };
      constexpr OverlayMode overlay_modes[] = {
        OverlayMode::None,           OverlayMode::Height,
        OverlayMode::Slope,          OverlayMode::Flow,
        OverlayMode::Streams,        OverlayMode::Basins,
        OverlayMode::Sinks,          OverlayMode::HeightDelta,
        OverlayMode::Trace,          OverlayMode::StandingWater,
        OverlayMode::PermanentWater, OverlayMode::Waterfalls,
        OverlayMode::Eroded,         OverlayMode::Deposited
      };
      for (int i = 0; i < 14; ++i) {
        const UiRect bounds = overlay_rect (i);
        m_ui.button (dl,
                     bounds,
                     overlay_labels[i],
                     readings_hot (bounds),
                     m_pointer_down,
                     m_overlay == overlay_modes[i]);
      }
      const std::size_t separator = m_overlay_status.find (" | ");
      m_ui.label (dl,
                  readings_x + 10,
                  readings_y + 186,
                  separator == std::string::npos
                    ? m_overlay_status
                    : m_overlay_status.substr (0, separator),
                  true);
      if (separator != std::string::npos)
        m_ui.label (dl,
                    readings_x + 10,
                    readings_y + 208,
                    m_overlay_status.substr (separator + 3));
      m_ui.label (dl,
                  readings_x + 10,
                  readings_y + 233,
                  "Readings color the surface; geometry stays terrain.");
      const terrain::AnalyticalErosionReport* analytical_report = nullptr;
      const terrain::StreamPowerEvolutionReport* orogeny_report = nullptr;
      const terrain::TrailFormationReport* trail_report = nullptr;
      const std::vector<terrain::TerrainTransformReport>& reports =
        m_model.reports ();
      if (m_selected_stage >= 0 &&
          m_selected_stage < static_cast<int> (reports.size ())) {
        analytical_report = std::get_if<terrain::AnalyticalErosionReport> (
          &reports[static_cast<std::size_t> (m_selected_stage)]);
        orogeny_report = std::get_if<terrain::StreamPowerEvolutionReport> (
          &reports[static_cast<std::size_t> (m_selected_stage)]);
        trail_report = std::get_if<terrain::TrailFormationReport> (
          &reports[static_cast<std::size_t> (m_selected_stage)]);
      }
      if (orogeny_report) {
        const auto& report = *orogeny_report;
        m_ui.label (dl,
                    readings_x + 10,
                    readings_y + 266,
                    "UPLIFT / INCISION QUASI-EQUILIBRIUM",
                    true);
        m_ui.label (
          dl,
          readings_x + 10,
          readings_y + 288,
          "STEPS " + std::to_string (terrain::count_value (report.steps)) +
            "  DIFFUSION " +
            std::to_string (terrain::count_value (report.diffusion_sweeps)) +
            "  FIXED " +
            format_count (static_cast<int> (
              terrain::count_value (report.fixed_boundaries))));
        m_ui.label (
          dl,
          readings_x + 10,
          readings_y + 310,
          "UPLIFT " +
            format_ledger (cubic_meters_value (report.tectonic_uplift_volume)) +
            " M3  INCISED " +
            format_ledger (cubic_meters_value (report.incised_volume)) + " M3");
        m_ui.label (dl,
                    readings_x + 10,
                    readings_y + 332,
                    "MEAN CHANGE " +
                      format_float (static_cast<float> (meters_value (
                                      report.mean_absolute_change)),
                                    2) +
                      " M");
        m_ui.label (dl,
                    readings_x + 10,
                    readings_y + 354,
                    "FINAL STEP MEAN / MAX " +
                      format_float (static_cast<float> (meters_value (
                                      report.final_step_mean_change)),
                                    2) +
                      " / " +
                      format_float (static_cast<float> (meters_value (
                                      report.final_step_maximum_change)),
                                    2) +
                      " M");
      } else if (trail_report) {
        const auto& report = *trail_report;
        m_ui.label (dl,
                    readings_x + 10,
                    readings_y + 266,
                    "RIDER-SCALE VALLEY NETWORK",
                    true);
        m_ui.label (
          dl,
          readings_x + 10,
          readings_y + 288,
          "CENTERLINE " +
            format_count (static_cast<int> (
              terrain::count_value (report.centerline_cells))) +
            "  LOOP " +
            format_float (static_cast<float> (
                            meters_value (report.circuit_length) / 1000.0),
                          1) +
            " KM" + "  SHAPED " +
            format_count (
              static_cast<int> (terrain::count_value (report.shaped_cells))));
        m_ui.label (
          dl,
          readings_x + 10,
          readings_y + 310,
          "CUT " + format_ledger (cubic_meters_value (report.cut_volume)) +
            " M3  FILL " +
            format_ledger (cubic_meters_value (report.fill_volume)) + " M3");
        m_ui.label (
          dl,
          readings_x + 10,
          readings_y + 332,
          "GRADE MEAN / MAX " + format_float (report.mean_centerline_grade, 3) +
            " / " + format_float (report.maximum_centerline_grade, 3) +
            "  EXCEPTIONS " +
            format_count (static_cast<int> (report.grade_exceptions)));
        m_ui.label (
          dl,
          readings_x + 10,
          readings_y + 354,
          "HIGH " +
            format_float (static_cast<float> (meters_value (
                            report.maximum_centerline_height_above_sea)),
                          0) +
            " M  CHANGE MEAN / MAX " +
            format_float (
              static_cast<float> (meters_value (report.mean_absolute_change)),
              2) +
            " / " +
            format_float (static_cast<float> (
                            meters_value (report.maximum_absolute_change)),
                          2) +
            " M");
      } else if (analytical_report) {
        const auto& report = *analytical_report;
        m_ui.label (dl,
                    readings_x + 10,
                    readings_y + 266,
                    "FINITE-TIME STREAM POWER",
                    true);
        m_ui.label (dl,
                    readings_x + 10,
                    readings_y + 288,
                    "FIXED BOUNDARIES " +
                      format_count (static_cast<int> (
                        terrain::count_value (report.fixed_boundaries))) +
                      "  PASSES " +
                      std::to_string (
                        terrain::count_value (report.fixed_point_iterations)));
        m_ui.label (
          dl,
          readings_x + 10,
          readings_y + 310,
          "LOWERED " +
            format_ledger (cubic_meters_value (report.lowered_volume)) +
            " M3  RAISED " +
            format_ledger (cubic_meters_value (report.raised_volume)));
        m_ui.label (dl,
                    readings_x + 10,
                    readings_y + 332,
                    "MEAN CHANGE " +
                      format_float (static_cast<float> (meters_value (
                                      report.mean_absolute_change)),
                                    2) +
                      " M");
        m_ui.label (dl,
                    readings_x + 10,
                    readings_y + 354,
                    "MAX CHANGE " +
                      format_float (static_cast<float> (meters_value (
                                      report.maximum_absolute_change)),
                                    2) +
                      " M");
      }
      m_ui.end_window (dl);
      m_ui.end (dl);
    }
  }
}
