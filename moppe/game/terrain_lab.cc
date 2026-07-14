#include <moppe/game/terrain_lab.hh>
#include <moppe/profile.hh>

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

      constexpr float window_x = 14.0f;
      constexpr float window_y = 14.0f;
      constexpr float window_width = 520.0f;
      constexpr float window_height = 640.0f;
      constexpr float left_x = 28.0f;
      constexpr float left_width = 226.0f;
      constexpr float right_x = 262.0f;
      constexpr float right_width = 258.0f;
      constexpr float stage_row_height = 38.0f;
      constexpr float stage_row_stride = 41.0f;
      constexpr int visible_stage_rows = 7;
      constexpr float readings_x = 548.0f;
      constexpr float readings_y = 14.0f;
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

      UiRect window_rect () {
        return { window_x, window_y, window_width, window_height };
      }

      UiRect readings_rect () {
        return { readings_x, readings_y, readings_width, readings_height };
      }

      bool ui_contains (float x, float y) {
        return window_rect ().contains (x, y) ||
               readings_rect ().contains (x, y);
      }

      UiRect friendly_panel_rect (int height) {
        return { 18,
                 18,
                 426,
                 static_cast<float> (
                   std::min (862, std::max (620, height - 36))) };
      }

      float friendly_vertical_scale (int height) {
        return std::min (1.0f, friendly_panel_rect (height).height / 862.0f);
      }

      float friendly_scaled_y (float target, int height) {
        return 18.0f + (target - 18.0f) * friendly_vertical_scale (height);
      }

      UiRect friendly_action_rect (int index, int height) {
        constexpr float gap = 12.0f;
        constexpr float width = (378.0f - gap * 2.0f) / 3.0f;
        const float scale = friendly_vertical_scale (height);
        return { 42 + index * (width + gap),
                 friendly_scaled_y (58, height),
                 width,
                 72 * scale };
      }

      UiRect friendly_preset_rect (int index, int height) {
        constexpr float gap = 12.0f;
        constexpr float width = (378.0f - gap) / 2.0f;
        const int row = index / 2;
        const int column = index % 2;
        const float scale = friendly_vertical_scale (height);
        return { 42 + column * (width + gap),
                 friendly_scaled_y (190 + row * 128.0f, height),
                 width,
                 116 * scale };
      }

      UiRect friendly_slider_rect (int index, int height) {
        const float scale = friendly_vertical_scale (height);
        return {
          50, friendly_scaled_y (512 + index * 69.0f, height), 362, 58 * scale
        };
      }

      UiRect friendly_lens_rect (int index, int width) {
        constexpr float button_width = 92.0f;
        constexpr float gap = 10.0f;
        const float start = std::max (474.0f, width - 434.0f);
        return { start + index * (button_width + gap), 54, button_width, 104 };
      }

      UiRect friendly_lens_surface_rect (int width) {
        const UiRect first = friendly_lens_rect (0, width);
        const UiRect last = friendly_lens_rect (3, width);
        return {
          first.x - 18.0f, 18.0f, last.x + last.width - first.x + 36.0f, 184.0f
        };
      }

      bool friendly_ui_contains (float x, float y, int width, int height) {
        if (friendly_panel_rect (height).contains (x, y))
          return true;
        return friendly_lens_surface_rect (width).contains (x, y);
      }

      UiRect expert_back_rect () {
        return { 448, 52, 72, 28 };
      }

      UiRect overlay_rect (int index) {
        constexpr float gap = 4.0f;
        constexpr float margin = 10.0f;
        constexpr float width = (readings_width - 2 * margin - 3 * gap) / 4;
        const int row = index / 4;
        const int column = index % 4;
        return { readings_x + margin + column * (width + gap),
                 readings_y + 42 + row * 34,
                 width,
                 28 };
      }

      UiRect close_rect () {
        return { 505, 20, 22, 22 };
      }

      UiRect reset_rect () {
        return { left_x, 52, 84, 28 };
      }

      UiRect seed_rect () {
        return { left_x + 90, 52, 128, 28 };
      }

      UiRect fit_rect () {
        return { 252, 52, 72, 28 };
      }

      UiRect view_rect () {
        return { 330, 52, 112, 28 };
      }

      UiRect hydraulic_preset_rect (int group, int index) {
        const float gap = 4.0f;
        const float width = (right_width - 3 * gap) / 4.0f;
        return {
          right_x + index * (width + gap), 378.0f + group * 56.0f, width, 28
        };
      }

      UiRect layer_rect (int index) {
        const float gap = 3.0f;
        const float width = (492.0f - 6 * gap) / 7.0f;
        return { left_x + index * (width + gap), 89, width, 29 };
      }

      UiRect pipeline_header_rect () {
        return { left_x, 128, left_width, 24 };
      }

      UiRect property_header_rect () {
        return { right_x, 128, right_width, 24 };
      }

      UiRect source_rect () {
        return { left_x, 157, left_width, stage_row_height };
      }

      UiRect stage_rect (int visible_index) {
        return { left_x,
                 201 + visible_index * stage_row_stride,
                 left_width,
                 stage_row_height };
      }

      UiRect stage_list_rect () {
        return {
          left_x, 201, left_width, visible_stage_rows * stage_row_stride
        };
      }

      UiRect add_stage_rect (int index) {
        const float gap = 3.0f;
        const float width = (left_width - 8 * gap) / 9.0f;
        return { left_x + index * (width + gap), 497, width, 29 };
      }

      UiRect edit_stage_rect (int index) {
        const float gap = 3.0f;
        const float width = (left_width - 3 * gap) / 4.0f;
        return { left_x + index * (width + gap), 532, width, 29 };
      }

      UiRect property_rect (int index) {
        return { right_x, 157 + index * 41.0f, right_width, 39.0f };
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
        if (std::holds_alternative<terrain::NormalizeHeights> (stage))
          return "NORMALIZE";
        if (std::holds_alternative<terrain::PowerHeights> (stage))
          return "POWER CURVE";
        if (std::holds_alternative<terrain::AnalyticalErosion> (stage))
          return "STREAM POWER AGE";
        if (std::holds_alternative<terrain::OrogenyEvolution> (stage))
          return "OROGENY EVOLUTION";
        if (std::holds_alternative<terrain::HydraulicErosion> (stage))
          return "WATER EROSION";
        if (std::holds_alternative<terrain::ChannelCarving> (stage))
          return "CHANNEL CARVE";
        if (std::holds_alternative<terrain::TrailFormation> (stage))
          return "MOTORCYCLE CIRCUIT";
        if (std::holds_alternative<terrain::HillslopeDiffusion> (stage))
          return "SOIL CREEP";
        return "TALUS RELAX";
      }

      std::string stage_detail (const terrain::TerrainTransform& stage) {
        if (std::holds_alternative<terrain::NormalizeHeights> (stage))
          return "map sampled range to 0..1";
        if (const auto* power = std::get_if<terrain::PowerHeights> (&stage))
          return "height ^ " + format_float (power->exponent, 2);
        if (const auto* analytical =
              std::get_if<terrain::AnalyticalErosion> (&stage))
          return format_float (
                   julian_years_value (analytical->duration) / 1000.0f, 0) +
                 " ky / " +
                 std::to_string (
                   terrain::count_value (analytical->fixed_point_iterations)) +
                 " routing passes";
        if (const auto* orogeny =
              std::get_if<terrain::OrogenyEvolution> (&stage)) {
          const float duration =
            julian_years_value (orogeny->evolution.duration);
          const float dt = julian_years_value (orogeny->evolution.time_step);
          return format_float (duration / 1000.0f, 0) + " ky / " +
                 format_count (static_cast<int> (std::ceil (duration / dt))) +
                 " geological steps";
        }
        if (const auto* hydraulic =
              std::get_if<terrain::HydraulicErosion> (&stage))
          return format_count (terrain::count_value (hydraulic->droplets)) +
                 " drops / " +
                 format_count (terrain::count_value (hydraulic->batch_size)) +
                 " batch / " +
                 format_count (terrain::count_value (hydraulic->max_steps)) +
                 " steps";
        if (const auto* carving = std::get_if<terrain::ChannelCarving> (&stage))
          return format_float (meters_value (carving->minimum_depth), 1) +
                 ".." +
                 format_float (meters_value (carving->maximum_depth), 1) +
                 " m beds @ " +
                 format_count (static_cast<int> (carving->minimum_area_cells)) +
                 " cells";
        if (const auto* trails = std::get_if<terrain::TrailFormation> (&stage))
          return format_float (
                   meters_value (trails->desired_circuit_radius) / 1000.0f, 1) +
                 " km radius / " +
                 format_float (meters_value (trails->width), 1) + " m path / " +
                 format_float (
                   trails->maximum_grade.numerical_value_in (mp_units::one) *
                     100.0f,
                   0) +
                 "% grade";
        if (const auto* diffusion =
              std::get_if<terrain::HillslopeDiffusion> (&stage))
          return format_float (
                   julian_years_value (diffusion->duration) / 1000.0f, 1) +
                 " ky @ D " +
                 format_float (
                   square_meters_per_julian_year_value (diffusion->diffusivity),
                   3);
        const auto& thermal = std::get<terrain::ThermalErosion> (stage);
        return std::to_string (terrain::count_value (thermal.iterations)) +
               " passes @ " + format_float (thermal.talus, 4);
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

      struct PropertyText {
        std::string label;
        std::string value;
        ParameterDomain domain;
      };

      PropertyText recipe_property (const terrain::GeologicalRecipe& recipe,
                                    int row) {
        switch (row) {
        case 0:
          return { "WARP STRENGTH",
                   format_float (recipe.warp.amplitude, 3),
                   ParameterDomain::Continuous };
        case 1:
          return { "CONTINENT WAVES",
                   std::to_string (recipe.continent.noise.cycles),
                   ParameterDomain::Natural };
        case 2:
          return { "PLAINS WAVES",
                   std::to_string (recipe.plains.noise.cycles),
                   ParameterDomain::Natural };
        case 3:
          return { "RIDGE WAVES",
                   std::to_string (recipe.mountains.cycles),
                   ParameterDomain::Natural };
        case 4:
          return { "MASK START",
                   format_float (recipe.blend.mask_low, 3),
                   ParameterDomain::Continuous };
        case 5:
          return { "MASK END",
                   format_float (recipe.blend.mask_high, 3),
                   ParameterDomain::Continuous };
        case 6:
          return { "CONTINENT MIX",
                   format_float (recipe.blend.continent_weight, 2),
                   ParameterDomain::Continuous };
        case 7:
          return { "PLAINS MIX",
                   format_float (recipe.blend.plains_weight, 2),
                   ParameterDomain::Continuous };
        default:
          return { "MOUNTAIN MIX",
                   format_float (recipe.blend.mountain_weight, 2),
                   ParameterDomain::Continuous };
        }
      }

      int stage_property_count (const terrain::TerrainTransform& stage) {
        if (std::holds_alternative<terrain::PowerHeights> (stage))
          return 1;
        if (std::holds_alternative<terrain::AnalyticalErosion> (stage))
          return 6;
        if (std::holds_alternative<terrain::OrogenyEvolution> (stage))
          return 7;
        if (std::holds_alternative<terrain::HydraulicErosion> (stage))
          return 3;
        if (std::holds_alternative<terrain::ThermalErosion> (stage))
          return 2;
        if (std::holds_alternative<terrain::ChannelCarving> (stage))
          return 5;
        if (std::holds_alternative<terrain::TrailFormation> (stage))
          return 10;
        if (std::holds_alternative<terrain::HillslopeDiffusion> (stage))
          return 2;
        return 0;
      }

      PropertyText stage_property (const terrain::TerrainTransform& stage,
                                   int row) {
        if (const auto* power = std::get_if<terrain::PowerHeights> (&stage))
          return { "EXPONENT",
                   format_float (power->exponent, 2),
                   ParameterDomain::Continuous };
        if (const auto* analytical =
              std::get_if<terrain::AnalyticalErosion> (&stage)) {
          if (row == 0)
            return { "AGE (KY)",
                     format_float (
                       julian_years_value (analytical->duration) / 1000.0f, 0),
                     ParameterDomain::Continuous };
          if (row == 1)
            return { "UPLIFT (MM/Y)",
                     format_float (
                       meters_per_julian_year_value (analytical->uplift_rate) *
                         1000.0f,
                       2),
                     ParameterDomain::Continuous };
          if (row == 2)
            return { "ERODIBILITY",
                     format_ledger (analytical->erodibility),
                     ParameterDomain::Continuous };
          if (row == 3)
            return { "AREA EXPONENT",
                     format_float (analytical->area_exponent, 2),
                     ParameterDomain::Continuous };
          if (row == 4)
            return { "ROUTING PASSES",
                     std::to_string (terrain::count_value (
                       analytical->fixed_point_iterations)),
                     ParameterDomain::Natural };
          return { "RELAXATION",
                   format_float (analytical->relaxation, 2),
                   ParameterDomain::Continuous };
        }
        if (const auto* orogeny =
              std::get_if<terrain::OrogenyEvolution> (&stage)) {
          if (row == 0)
            return {
              "DURATION (KY)",
              format_float (
                julian_years_value (orogeny->evolution.duration) / 1000.0f, 0),
              ParameterDomain::Continuous
            };
          if (row == 1)
            return {
              "STEP (KY)",
              format_float (
                julian_years_value (orogeny->evolution.time_step) / 1000.0f, 0),
              ParameterDomain::Continuous
            };
          if (row == 2)
            return { "UPLIFT (MM/Y)",
                     format_float (meters_per_julian_year_value (
                                     orogeny->maximum_uplift_rate) *
                                     1000.0f,
                                   2),
                     ParameterDomain::Continuous };
          if (row == 3)
            return { "ERODIBILITY",
                     format_ledger (orogeny->evolution.erodibility),
                     ParameterDomain::Continuous };
          if (row == 4)
            return { "AREA EXPONENT",
                     format_float (orogeny->evolution.area_exponent, 2),
                     ParameterDomain::Continuous };
          if (row == 5)
            return { "DIFFUSIVITY",
                     format_float (square_meters_per_julian_year_value (
                                     orogeny->evolution.diffusivity),
                                   5),
                     ParameterDomain::Continuous };
          return { "SEA LEVEL",
                   format_float (orogeny->evolution.sea_level, 3),
                   ParameterDomain::Continuous };
        }
        if (const auto* hydraulic =
              std::get_if<terrain::HydraulicErosion> (&stage)) {
          if (row == 0)
            return { "DROPLETS",
                     format_count (terrain::count_value (hydraulic->droplets)),
                     ParameterDomain::Natural };
          if (row == 1)
            return { "BATCH SIZE",
                     format_count (
                       terrain::count_value (hydraulic->batch_size)),
                     ParameterDomain::Natural };
          return { "MAX STEPS",
                   format_count (terrain::count_value (hydraulic->max_steps)),
                   ParameterDomain::Natural };
        }
        if (const auto* carving =
              std::get_if<terrain::ChannelCarving> (&stage)) {
          if (row == 0)
            return { "MIN AREA",
                     format_count (
                       static_cast<int> (carving->minimum_area_cells)),
                     ParameterDomain::Continuous };
          if (row == 1)
            return { "DEPTH SCALE",
                     format_float (carving->depth_per_sqrt_m2 * 1000.0f, 2),
                     ParameterDomain::Continuous };
          if (row == 2)
            return { "MIN DEPTH (M)",
                     format_float (meters_value (carving->minimum_depth), 2),
                     ParameterDomain::Continuous };
          if (row == 3)
            return { "MAX DEPTH (M)",
                     format_float (meters_value (carving->maximum_depth), 2),
                     ParameterDomain::Continuous };
          return { "BANK BLEND (M)",
                   format_float (meters_value (carving->bank_blend), 1),
                   ParameterDomain::Continuous };
        }
        if (const auto* trails =
              std::get_if<terrain::TrailFormation> (&stage)) {
          if (row == 0)
            return { "MIN CATCHMENT (M2)",
                     format_count (static_cast<int> (
                       square_meters_value (trails->minimum_catchment_area))),
                     ParameterDomain::Continuous };
          if (row == 1)
            return { "MAX CATCHMENT (M2)",
                     format_count (static_cast<int> (
                       square_meters_value (trails->maximum_catchment_area))),
                     ParameterDomain::Continuous };
          if (row == 2)
            return { "PATH WIDTH (M)",
                     format_float (meters_value (trails->width), 1),
                     ParameterDomain::Continuous };
          if (row == 3)
            return { "SHOULDER (M)",
                     format_float (meters_value (trails->shoulder_blend), 1),
                     ParameterDomain::Continuous };
          if (row == 4)
            return { "MAX CUT (M)",
                     format_float (meters_value (trails->maximum_cut), 2),
                     ParameterDomain::Continuous };
          if (row == 5)
            return { "MAX FILL (M)",
                     format_float (meters_value (trails->maximum_fill), 2),
                     ParameterDomain::Continuous };
          if (row == 6)
            return { "MAX GRADE",
                     format_float (
                       trails->maximum_grade.numerical_value_in (mp_units::one),
                       2),
                     ParameterDomain::Continuous };
          if (row == 7)
            return { "BASE TO WATER (M)",
                     format_float (
                       meters_value (trails->home_base_water_distance), 0),
                     ParameterDomain::Continuous };
          if (row == 8)
            return { "BASE PAD (M)",
                     format_float (meters_value (trails->home_base_pad_radius),
                                   0),
                     ParameterDomain::Continuous };
          return { "CIRCUIT RADIUS (M)",
                   format_float (meters_value (trails->desired_circuit_radius),
                                 0),
                   ParameterDomain::Continuous };
        }
        if (const auto* diffusion =
              std::get_if<terrain::HillslopeDiffusion> (&stage)) {
          if (row == 0)
            return { "DURATION (KY)",
                     format_float (
                       julian_years_value (diffusion->duration) / 1000.0f, 1),
                     ParameterDomain::Continuous };
          return { "DIFFUSIVITY",
                   format_float (square_meters_per_julian_year_value (
                                   diffusion->diffusivity),
                                 3),
                   ParameterDomain::Continuous };
        }
        const auto& thermal = std::get<terrain::ThermalErosion> (stage);
        if (row == 0)
          return { "PASSES",
                   std::to_string (terrain::count_value (thermal.iterations)),
                   ParameterDomain::Natural };
        return { "TALUS",
                 format_float (thermal.talus, 4),
                 ParameterDomain::Continuous };
      }
    }

    TerrainLab::TerrainLab ()
        : m_renderer (0), m_map (0), m_terrain (0), m_world (0), m_graphics (0),
          m_history (nullptr), m_history_index (0), m_history_age (0.0f),
          m_history_playing (false), m_active (false), m_map_pristine (false),
          m_program (terrain::make_geological_program (0)),
          m_droplet_overlay_points (0), m_droplet_progress (0.0f),
          m_droplet_settle (0.0f), m_droplet_armed (false),
          m_droplet_follow (false), m_time (0.0f),
          m_overlay (OverlayMode::None), m_selected_stage (-1),
          m_stage_scroll (0), m_pointer_x (0), m_pointer_y (0),
          m_pointer_down (false), m_camera_drag (false),
          m_camera_drag_distance (0.0f), m_pan_drag (false),
          m_parameter_drag (false), m_friendly_drag (false),
          m_friendly_drag_control (-1), m_expert_ui (false),
          m_friendly_preset (-1), m_ui_width (960), m_ui_height (640),
          m_drag_property (-1), m_drag_start_y (0.0f),
          m_drag_start_normalized (0.0f), m_parameter_rebuild_pending (false),
          m_parameter_rebuild_stage (-1), m_parameter_rebuild_delay (0.0f),
          m_target (), m_yaw (PI), m_pitch (1.48f), m_distance (4200.0f),
          m_fit_distance (4200.0f), m_orbit_left (false), m_orbit_right (false),
          m_zoom_in (false), m_zoom_out (false), m_tilt_up (false),
          m_tilt_down (false), m_scroll_zoom_target (4200.0f),
          m_view (ViewMode::Tile) {}

    void TerrainLab::load (render::Renderer& renderer) {
      m_ui.load (renderer);
    }

    void TerrainLab::enter (render::Renderer& renderer,
                            map::RandomHeightMap& map,
                            Terrain& terrain,
                            const WorldParams& world,
                            const GraphicsSettings& graphics,
                            const terrain::TerrainProgram& program,
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
      m_evaluator = std::make_unique<map::TerrainEvaluator> (
        map, m_source_evaluator.get ());
      m_terrain = &terrain;
      m_world = &world;
      m_graphics = &graphics;
      m_sun_dir = sun_dir;
      m_program = program;
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
        m_program = terrain::make_orogeny_program (
          m_program.randomness.seed.value,
          terrain::TerrainGenerationProfile::Fast);
        env_stages = true;
      }
      if (const char* erosion = std::getenv ("MOPPE_LAB_EROSION")) {
        std::istringstream input (erosion);
        std::string part;
        int values[] = { 100000, 256, 512 };
        for (int i = 0; i < 3 && std::getline (input, part, ','); ++i)
          values[i] = std::stoi (part);
        m_program.transforms.emplace_back (terrain::HydraulicErosion {
          .droplets = terrain::droplet_count (values[0]),
          .batch_size = terrain::batch_size (values[1]),
          .max_steps = terrain::step_count (values[2]),
          .minimum_water = 0.01f,
          .sediment_at_termination = terrain::SedimentDisposition::Deposit });
        env_stages = true;
      }
      if (std::getenv ("MOPPE_LAB_ANALYTICAL")) {
        m_program.transforms.emplace_back (terrain::AnalyticalErosion {});
        env_stages = true;
      }
      m_selected_stage = -1;
      m_stage_scroll = 0;
      m_pointer_down = false;
      m_camera_drag = false;
      m_pan_drag = false;
      m_parameter_drag = false;
      m_friendly_drag = false;
      m_friendly_drag_control = -1;
      m_expert_ui = false;
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
      m_water_network.reset ();
      m_rivers.reset ();
      m_river_surface.clear ();
      m_flood.reset ();
      m_lakes.reset ();
      m_inspected_cell.reset ();
      m_droplet_trace = {};
      m_droplet_target.reset ();
      m_droplet_overlay.clear ();
      m_droplet_overlay_points = 0;
      m_droplet_progress = 0.0f;
      m_droplet_settle = 0.0f;
      m_droplet_armed = false;
      m_droplet_follow = false;
      m_overlay_status = "NO READING — terrain materials";
      m_view = ViewMode::Tile;
      m_orbit_left = m_orbit_right = false;
      m_zoom_in = m_zoom_out = false;
      m_tilt_up = m_tilt_down = false;
      fit_view ();

      const size_t count = (size_t)map.width () * map.height ();
      m_saved_heights.assign (map.raw_heights (), map.raw_heights () + count);
      m_active = true;
      // The map on screen IS this program's output (or the world it was
      // loaded from): entering the lab is just another view of it, so
      // don't rebuild.  The pipeline reruns only once something is
      // edited.  Env-var stages are additions and do need a run.
      m_checkpoints.clear ();
      m_reports.clear ();
      m_map_pristine = true;
      if (env_stages)
        rebuild_program ();
      else
        refresh ();
      if (std::getenv ("MOPPE_LAB_RIVERS"))
        drainage ();
      if (const char* stage = std::getenv ("MOPPE_LAB_STAGE")) {
        const int selected = std::atoi (stage);
        if (selected >= 0 &&
            selected < static_cast<int> (m_program.transforms.size ())) {
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
      if (const char* drop = std::getenv ("MOPPE_LAB_DROP_SCREEN")) {
        std::istringstream input (drop);
        std::string sx, sy;
        if (std::getline (input, sx, ',') && std::getline (input, sy))
          launch_droplet (std::stof (sx), std::stof (sy));
      }
      if (const char* arm = std::getenv ("MOPPE_LAB_ARM_SCREEN")) {
        std::istringstream input (arm);
        std::string sx, sy;
        if (std::getline (input, sx, ',') && std::getline (input, sy)) {
          m_droplet_armed = true;
          m_droplet_target =
            terrain_point_at_screen (std::stof (sx), std::stof (sy));
        }
      }
    }

    void TerrainLab::leave () {
      if (!m_active)
        return;
      m_overlay = OverlayMode::None;
      if (m_renderer)
        m_renderer->clear_terrain_overlay ();
      restore_game_map ();
      m_saved_heights.clear ();
      m_saved_heights.shrink_to_fit ();
      m_saved_trail_influence.clear ();
      m_saved_trail_influence.shrink_to_fit ();
      m_saved_home_base_influence.clear ();
      m_saved_home_base_influence.shrink_to_fit ();
      m_checkpoints.clear ();
      m_checkpoints.shrink_to_fit ();
      m_reports.clear ();
      m_reports.shrink_to_fit ();
      m_river_surface.clear ();
      m_droplet_trace = {};
      m_droplet_target.reset ();
      m_droplet_overlay.clear ();
      m_droplet_overlay_points = 0;
      m_droplet_draw.clear ();
      m_droplet_settle = 0.0f;
      m_droplet_armed = false;
      m_droplet_follow = false;
      m_orbit_left = m_orbit_right = false;
      m_zoom_in = m_zoom_out = false;
      m_tilt_up = m_tilt_down = false;
      m_pointer_down = false;
      m_camera_drag = false;
      m_pan_drag = false;
      m_parameter_drag = false;
      m_parameter_rebuild_pending = false;
      m_active = false;
      m_evaluator.reset ();
      m_history = nullptr;
      m_map = 0;
      m_terrain = 0;
      m_world = 0;
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
      if (!m_map || m_saved_heights.empty ())
        return;
      size_t i = 0;
      for (int y = 0; y < m_map->height (); ++y)
        for (int x = 0; x < m_map->width (); ++x)
          m_map->set (x, y, m_saved_heights[i++]);
      m_map_pristine = true;
      refresh (false);
    }

    void TerrainLab::select (terrain::GeologicalLayer layer) {
      if (layer == m_program.source.layer)
        return;
      m_program.source.layer = layer;
      m_selected_stage = -1;
      rebuild_program ();
    }

    void TerrainLab::reset_program () {
      m_program = terrain::make_geological_program (
        m_program.randomness.seed.value, m_program.source.layer);
      m_selected_stage = -1;
      m_stage_scroll = 0;
      rebuild_program ();
    }

    void TerrainLab::rebuild_program () {
      if (!m_evaluator)
        return;
      m_map_pristine = false;
      m_evaluator->begin (m_program);
      m_checkpoints.clear ();
      m_reports.clear ();
      for (const terrain::TerrainTransform& transform : m_program.transforms) {
        // A checkpoint is the input to a stage.  The final output is already
        // resident in m_map, so copying it would only duplicate 16 MiB at the
        // default lab resolution on every recipe edit.
        m_checkpoints.push_back (m_evaluator->checkpoint ());
        m_reports.push_back (m_evaluator->apply (transform));
      }
      invalidate_analysis ();
      refresh ();
    }

    void TerrainLab::rerun_program_from (int first_stage) {
      const int stage_count = static_cast<int> (m_program.transforms.size ());
      if (!m_evaluator || first_stage < 0 || first_stage > stage_count ||
          first_stage > static_cast<int> (m_checkpoints.size ())) {
        rebuild_program ();
        return;
      }

      m_map_pristine = false;
      if (first_stage < static_cast<int> (m_checkpoints.size ()))
        m_evaluator->restore (m_checkpoints[first_stage]);

      // Appending a stage starts from the current final map.  Other edits start
      // from their saved input, and discard only the now-invalid suffix.
      const std::size_t retained =
        std::min (static_cast<std::size_t> (stage_count),
                  static_cast<std::size_t> (first_stage + 1));
      if (m_checkpoints.size () > retained)
        m_checkpoints.resize (retained);
      if (m_reports.size () > static_cast<std::size_t> (first_stage))
        m_reports.resize (static_cast<std::size_t> (first_stage));
      for (std::size_t i = static_cast<std::size_t> (first_stage);
           i < m_program.transforms.size ();
           ++i) {
        if (i >= m_checkpoints.size ())
          m_checkpoints.push_back (m_evaluator->checkpoint ());
        const terrain::TerrainTransformReport report =
          m_evaluator->apply (m_program.transforms[i]);
        if (i >= m_reports.size ())
          m_reports.push_back (report);
        else
          m_reports[i] = report;
      }
      invalidate_analysis ();
      refresh ();
    }

    void TerrainLab::invalidate_analysis () {
      m_drainage.reset ();
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
        m_rivers = terrain::extract_river_network (
          *m_flood,
          *m_lakes,
          *m_drainage,
          visible_river_minimum_area (m_drainage->source_grid));
        m_river_surface.rebuild (
          *m_renderer, *m_map, *m_flood, *m_lakes, *m_drainage, *m_rivers);
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
      // A pristine map still renders the game's painted water sheets;
      // the analysis ribbons would only double-draw on top of them.
      if (m_map_pristine)
        return;
      m_river_surface.draw (renderer, camera);
    }

    Vec3 TerrainLab::droplet_world_position (std::size_t index) const {
      if (!m_map || index >= m_droplet_trace.points.size ())
        return {};
      const map::HydraulicDropletPoint& point = m_droplet_trace.points[index];
      const Vec3 scale = m_map->scale ();
      return Vec3 (
        point.x * scale[0], point.height * scale[1] + 2.2f, point.y * scale[2]);
    }

    Vec3 TerrainLab::droplet_world_position (float progress) const {
      if (m_droplet_trace.points.empty ())
        return {};
      const float last =
        static_cast<float> (m_droplet_trace.points.size () - 1);
      const float position = std::clamp (progress, 0.0f, last);
      const std::size_t before = static_cast<std::size_t> (position);
      const std::size_t after =
        std::min (before + 1, m_droplet_trace.points.size () - 1);
      const float fraction = position - static_cast<float> (before);
      const Vec3 a = droplet_world_position (before);
      return a + (droplet_world_position (after) - a) * fraction;
    }

    float TerrainLab::visible_droplet_pitch (const Vec3& droplet) const {
      if (!m_map)
        return m_pitch;

      // Preserve the player's orbit when it already has a clear view.  In a
      // valley, progressively lift the camera until every sample between it
      // and the bead clears the terrain.  The clearance tapers toward the
      // droplet, which is deliberately drawn just above the surface.
      constexpr float maximum_pitch = 1.48f;
      constexpr float pitch_step = 0.035f;
      const float first_pitch = std::clamp (m_pitch, 0.18f, maximum_pitch);
      for (float pitch = first_pitch; pitch <= maximum_pitch;
           pitch += pitch_step) {
        const float horizontal = std::cos (pitch) * m_distance;
        const Vec3 camera = m_target + Vec3 (std::sin (m_yaw) * horizontal,
                                             std::sin (pitch) * m_distance,
                                             std::cos (m_yaw) * horizontal);
        bool visible = true;
        for (int sample = 1; sample < 32; ++sample) {
          const float t = static_cast<float> (sample) / 32.0f;
          const Vec3 point = camera + (droplet - camera) * t;
          const float clearance = 5.0f * (1.0f - t);
          if (point[1] <
              m_map->interpolated_height (point[0], point[2]) + clearance) {
            visible = false;
            break;
          }
        }
        if (visible)
          return pitch;
      }
      return maximum_pitch;
    }

    void TerrainLab::update_droplet_overlay (bool force) {
      if (!m_renderer || !m_map || m_droplet_trace.points.empty () ||
          m_overlay != OverlayMode::None)
        return;
      const int width = m_map->width ();
      const int height = m_map->height ();
      const std::size_t count = static_cast<std::size_t> (width) * height;
      if (m_droplet_overlay.size () != count) {
        m_droplet_overlay.assign (count, 0.0f);
        m_droplet_overlay_points = 0;
        force = true;
      }
      const std::size_t visible = std::min (
        m_droplet_trace.points.size (),
        static_cast<std::size_t> (std::max (m_droplet_progress, 0.0f)) + 1);
      const int unique_width = m_map->unique_width ();
      const int unique_height = m_map->unique_height ();
      constexpr int radius = 4;
      constexpr float radius_f = 3.5f;
      const auto stamp = [&] (int x, int y, float value) {
        if (m_map->periodic ()) {
          x = terrain::wrap_index (x, unique_width);
          y = terrain::wrap_index (y, unique_height);
        } else if (x < 0 || x >= width || y < 0 || y >= height) {
          return;
        }
        const auto set = [&] (int sx, int sy) {
          float& cell =
            m_droplet_overlay[static_cast<std::size_t> (sy) * width + sx];
          cell = std::max (cell, value);
        };
        set (x, y);
        if (m_map->periodic () && x == 0)
          set (width - 1, y);
        if (m_map->periodic () && y == 0)
          set (x, height - 1);
        if (m_map->periodic () && x == 0 && y == 0)
          set (width - 1, height - 1);
      };
      for (; m_droplet_overlay_points < visible; ++m_droplet_overlay_points) {
        const map::HydraulicDropletPoint& point =
          m_droplet_trace.points[m_droplet_overlay_points];
        const int center_x = static_cast<int> (std::floor (point.x));
        const int center_y = static_cast<int> (std::floor (point.y));
        for (int dy = -radius; dy <= radius; ++dy)
          for (int dx = -radius; dx <= radius; ++dx) {
            const float distance =
              std::hypot (static_cast<float> (center_x + dx) - point.x,
                          static_cast<float> (center_y + dy) - point.y);
            const float falloff = std::max (0.0f, 1.0f - distance / radius_f);
            stamp (center_x + dx, center_y + dy, falloff * falloff);
          }
        force = true;
      }
      if (!force)
        return;
      m_renderer->set_terrain_overlay (
        { .width = width,
          .height = height,
          .minimum = 0.0f,
          .maximum = 1.0f,
          .opacity = 0.96f,
          .ramp = render::TerrainOverlayRamp::Droplet },
        m_droplet_overlay);
    }

    void TerrainLab::render_droplet (render::Renderer& renderer,
                                     const Vec3& camera) {
      if (!m_map)
        return;

      m_droplet_draw.clear ();
      render::DrawState state;
      state.blend = true;
      state.additive = true;
      state.depth_write = false;
      state.cull = false;
      m_droplet_draw.state (state);
      m_droplet_draw.lit (false);
      // Unfogged: the target dot and bead are interface cues that must
      // stay legible even from the kilometres-out overview orbit.
      m_droplet_draw.fogged (false);

      if (m_droplet_armed && m_droplet_target) {
        // A camera-facing glow point: a draped ring smears into a huge
        // oval on steep slopes, but a billboard disc stays a dot from
        // every angle.  Sized by distance so it reads the same zoomed
        // out or close in, with a soft breathing pulse while armed.
        const Vec3 ground ((*m_droplet_target)[0],
                           m_map->interpolated_height ((*m_droplet_target)[0],
                                                       (*m_droplet_target)[2]),
                           (*m_droplet_target)[2]);
        Vec3 to_camera = camera - ground;
        const float distance = length (to_camera);
        if (distance > 1.0f) {
          to_camera = to_camera * (1.0f / distance);
          Vec3 side = cross (to_camera, Vec3 (0, 1, 0));
          if (length2 (side) < 1e-6f)
            side = Vec3 (1, 0, 0);
          normalize (side);
          const Vec3 lift = normalized (cross (side, to_camera));
          const float pulse = 0.85f + 0.15f * std::sin (m_time * 4.2f);
          // Proportional to distance, so the dot keeps a constant
          // on-screen size from valley floor to orbital overview.
          const float size =
            std::clamp (distance * 0.02f, 4.0f, 320.0f) * pulse;
          const Vec3 center = ground + to_camera * (3.0f + size * 0.35f);
          const auto glow = [&] (float radius, float core_alpha) {
            m_droplet_draw.begin (render::Prim::TriangleFan);
            m_droplet_draw.color (0.55f, 0.97f, 1.0f, core_alpha);
            m_droplet_draw.vertex (center);
            m_droplet_draw.color (0.25f, 0.94f, 1.0f, 0.0f);
            for (int i = 0; i <= 24; ++i) {
              const float angle = PI2 * static_cast<float> (i) / 24.0f;
              m_droplet_draw.vertex (
                center +
                (side * std::cos (angle) + lift * std::sin (angle)) * radius);
            }
            m_droplet_draw.end ();
          };
          glow (size, 0.55f);
          glow (size * 0.4f, 0.95f);
        }
      }

      // When the drop arrives it wobbles like landing jelly, then melts
      // into a spreading puddle and fades, so the end of the run reads
      // as an ending rather than the bead just freezing in place.
      constexpr float melt_duration = 1.8f;
      const float melt =
        m_droplet_progress >=
            static_cast<float> (m_droplet_trace.points.size ()) - 1.0f
          ? std::clamp (m_droplet_settle / melt_duration, 0.0f, 1.0f)
          : 0.0f;
      if (m_droplet_trace.points.size () >= 2 && melt < 1.0f) {
        const Vec3 bead = droplet_world_position (m_droplet_progress) -
                          Vec3 (0.0f, melt * 1.8f, 0.0f);
        const float fade = 1.0f - melt;
        // The wake and nose cap describe motion; snuff them quickly
        // once the drop has stopped.
        const float motion_fade = std::max (0.0f, 1.0f - melt * 4.0f);
        const float wobble = melt > 0.0f ? std::exp (-m_droplet_settle * 2.4f) *
                                             std::sin (m_droplet_settle * 16.0f)
                                         : 0.0f;
        const float squash = (1.0f + 0.45f * wobble) * (1.0f - 0.85f * melt);
        const float spread = (1.0f - 0.25f * wobble) * (1.0f + 1.6f * melt);

        Vec3 direction = bead - droplet_world_position (
                                  std::max (0.0f, m_droplet_progress - 0.65f));
        if (length2 (direction) < 1e-6f)
          direction = Vec3 (0, -1, 0);
        normalize (direction);
        Vec3 wake_side = cross (direction, camera - bead);
        if (length2 (wake_side) < 1e-6f)
          wake_side = cross (direction, Vec3 (0, 1, 0));
        if (length2 (wake_side) < 1e-6f)
          wake_side = Vec3 (1, 0, 0);
        normalize (wake_side);
        const auto wake = [&] (float length, float width, float alpha) {
          if (alpha <= 0.0f)
            return;
          m_droplet_draw.color (0.12f, 0.72f, 1.0f, alpha);
          m_droplet_draw.begin (render::Prim::Triangles);
          m_droplet_draw.vertex (bead - direction * length);
          m_droplet_draw.vertex (bead - direction * 2.0f - wake_side * width);
          m_droplet_draw.vertex (bead - direction * 2.0f + wake_side * width);
          m_droplet_draw.end ();
        };
        wake (30.0f, 8.0f, 0.10f * motion_fade);
        wake (19.0f, 3.8f, 0.42f * motion_fade);

        // A small pointed cap makes the direction legible even in a still
        // frame; the ellipsoid behind it supplies the rounded water volume.
        Vec3 nose_up = normalized (cross (direction, wake_side));
        if (motion_fade > 0.0f) {
          m_droplet_draw.color (0.34f, 0.92f, 1.0f, 0.82f * motion_fade);
          m_droplet_draw.begin (render::Prim::TriangleFan);
          m_droplet_draw.vertex (bead + direction * 11.0f);
          for (int i = 0; i <= 10; ++i) {
            const float angle = PI2 * static_cast<float> (i) / 10.0f;
            const Vec3 radial =
              wake_side * std::cos (angle) + nose_up * std::sin (angle);
            m_droplet_draw.vertex (bead - direction * 1.5f + radial * 5.2f);
          }
          m_droplet_draw.end ();
        }

        const Vec3 up (0, 1, 0);
        // While melting, the blob relaxes upright regardless of its
        // final travel direction.
        Vec3 lean = direction + (up - direction) * melt;
        if (length2 (lean) < 1e-6f)
          lean = up;
        normalize (lean);
        Vec3 axis = cross (up, lean);
        const radians_t angle =
          std::acos (std::clamp (dot (up, lean), -1.0f, 1.0f)) * u::rad;
        m_droplet_draw.push ();
        m_droplet_draw.translate (bead);
        if (length2 (axis) > 1e-6f)
          m_droplet_draw.rotate (angle, normalized (axis));
        m_droplet_draw.color (0.18f, 0.82f, 1.0f, 0.92f * fade);
        m_droplet_draw.scale (5.5f * spread, 10.0f * squash, 5.5f * spread);
        m_droplet_draw.sphere (1.0f, 14, 9);
        m_droplet_draw.pop ();
        m_droplet_draw.push ();
        m_droplet_draw.translate (bead + Vec3 (-2.0f, 2.0f, 0));
        m_droplet_draw.color (0.88f, 1.0f, 1.0f, 0.50f * fade);
        m_droplet_draw.sphere (2.2f * (0.4f + 0.6f * fade), 8, 6);
        m_droplet_draw.pop ();
      }
      if (!m_droplet_draw.empty ())
        renderer.draw_list (m_droplet_draw);
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

    void TerrainLab::launch_droplet (float x, float y) {
      const std::optional<Vec3> hit = terrain_point_at_screen (x, y);
      if (!hit || !m_map) {
        m_overlay_status = "DROP — no terrain under pointer";
        return;
      }
      const Vec3 scale = m_map->scale ();
      m_droplet_trace =
        m_map->trace_hydraulic_droplet ((*hit)[0] / scale[0],
                                        (*hit)[2] / scale[2],
                                        512,
                                        0.01f,
                                        terrain::SedimentDisposition::Deposit,
                                        terrain::CarvingRule::PathMonotone);
      m_droplet_overlay.assign (
        static_cast<std::size_t> (m_map->width ()) * m_map->height (), 0.0f);
      m_droplet_overlay_points = 0;
      // Hold briefly on the release point so a click on a summit visibly
      // begins there before the erosion step starts running downhill.
      m_droplet_progress = -8.0f;
      m_droplet_settle = 0.0f;
      m_droplet_armed = false;
      m_droplet_target.reset ();
      m_droplet_follow = m_droplet_trace.points.size () > 1;
      m_map_pristine = false;
      invalidate_analysis ();
      refresh ();
      std::ostringstream status;
      const float eroded_volume =
        m_droplet_trace.eroded * scale[0] * scale[1] * scale[2];
      status << "DROP — " << m_droplet_trace.points.size () - 1 << " steps, "
             << std::fixed << std::setprecision (3) << eroded_volume
             << " m3 moved";
      m_overlay_status = status.str ();
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
        if (!m_droplet_trace.points.empty ())
          update_droplet_overlay (true);
        else
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
        if (m_selected_stage < 0 ||
            m_selected_stage >= static_cast<int> (m_checkpoints.size ())) {
          m_renderer->clear_terrain_overlay ();
          m_overlay_status = "DELTA — select a pipeline stage";
          return;
        }
        const std::vector<float>& before =
          m_checkpoints[static_cast<std::size_t> (m_selected_stage)].heights;
        const float* after =
          m_selected_stage + 1 < static_cast<int> (m_checkpoints.size ())
            ? m_checkpoints[static_cast<std::size_t> (m_selected_stage + 1)]
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
      const int first_stage = static_cast<int> (m_program.transforms.size ());
      m_program.transforms.push_back (std::move (stage));
      m_selected_stage = static_cast<int> (m_program.transforms.size ()) - 1;
      ensure_selected_stage_visible ();
      rerun_program_from (first_stage);
    }

    void TerrainLab::move_selected_stage (int direction) {
      const int first_stage =
        std::min (m_selected_stage, m_selected_stage + direction);
      const int target = m_selected_stage + direction;
      if (m_selected_stage < 0 || target < 0 ||
          target >= static_cast<int> (m_program.transforms.size ()))
        return;
      std::swap (m_program.transforms[m_selected_stage],
                 m_program.transforms[target]);
      m_selected_stage = target;
      ensure_selected_stage_visible ();
      rerun_program_from (first_stage);
    }

    void TerrainLab::duplicate_selected_stage () {
      if (m_selected_stage < 0 ||
          m_selected_stage >= static_cast<int> (m_program.transforms.size ()))
        return;
      const int first_stage = m_selected_stage + 1;
      const auto position = m_program.transforms.begin () + first_stage;
      m_program.transforms.insert (position,
                                   m_program.transforms[m_selected_stage]);
      ++m_selected_stage;
      ensure_selected_stage_visible ();
      rerun_program_from (first_stage);
    }

    void TerrainLab::remove_selected_stage () {
      if (m_selected_stage < 0 ||
          m_selected_stage >= static_cast<int> (m_program.transforms.size ()))
        return;
      const int first_stage = m_selected_stage;
      m_program.transforms.erase (m_program.transforms.begin () +
                                  m_selected_stage);
      if (m_program.transforms.empty ())
        m_selected_stage = -1;
      else
        m_selected_stage =
          std::min (m_selected_stage,
                    static_cast<int> (m_program.transforms.size ()) - 1);
      ensure_selected_stage_visible ();
      rerun_program_from (first_stage);
    }

    void TerrainLab::ensure_selected_stage_visible () {
      const int count = static_cast<int> (m_program.transforms.size ());
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
      const auto unit = [] (float value, float low, float high) {
        return high > low
                 ? std::clamp ((value - low) / (high - low), 0.0f, 1.0f)
                 : 0.0f;
      };
      if (m_selected_stage < 0) {
        const terrain::GeologicalRecipe& recipe = m_program.source.recipe;
        switch (row) {
        case 0:
          return unit (recipe.warp.amplitude, 0.0f, 0.6f);
        case 1:
          return unit (recipe.continent.noise.cycles, 1.0f, 16.0f);
        case 2:
          return unit (recipe.plains.noise.cycles, 1.0f, 32.0f);
        case 3:
          return unit (recipe.mountains.cycles, 1.0f, 8.0f);
        case 4:
          return unit (
            recipe.blend.mask_low, 0.0f, recipe.blend.mask_high - 0.001f);
        case 5:
          return unit (
            recipe.blend.mask_high, recipe.blend.mask_low + 0.001f, 1.0f);
        case 6:
          return unit (recipe.blend.continent_weight, 0.0f, 1.5f);
        case 7:
          return unit (recipe.blend.plains_weight, 0.0f, 1.0f);
        case 8:
          return unit (recipe.blend.mountain_weight, 0.0f, 1.5f);
        default:
          return 0.0f;
        }
      }

      const terrain::TerrainTransform& stage =
        m_program.transforms[m_selected_stage];
      if (const auto* power = std::get_if<terrain::PowerHeights> (&stage))
        return unit (power->exponent, 0.1f, 4.0f);
      if (const auto* analytical =
            std::get_if<terrain::AnalyticalErosion> (&stage)) {
        if (row == 0)
          return unit (
            julian_years_value (analytical->duration), 0.0f, 1600000.0f);
        if (row == 1)
          return unit (meters_per_julian_year_value (analytical->uplift_rate),
                       0.0f,
                       0.003f);
        if (row == 2)
          return unit (std::log10 (analytical->erodibility), -6.0f, -3.0f);
        if (row == 3)
          return unit (analytical->area_exponent, 0.0f, 1.0f);
        if (row == 4)
          return unit (
            terrain::count_value (analytical->fixed_point_iterations),
            1.0f,
            12.0f);
        return unit (analytical->relaxation, 0.1f, 1.0f);
      }
      if (const auto* orogeny =
            std::get_if<terrain::OrogenyEvolution> (&stage)) {
        if (row == 0)
          return unit (
            julian_years_value (orogeny->evolution.duration), 0.0f, 5000000.0f);
        if (row == 1)
          return unit (julian_years_value (orogeny->evolution.time_step),
                       1000.0f,
                       250000.0f);
        if (row == 2)
          return unit (
            meters_per_julian_year_value (orogeny->maximum_uplift_rate),
            0.0f,
            0.003f);
        if (row == 3)
          return unit (
            std::log10 (orogeny->evolution.erodibility), -7.0f, -3.0f);
        if (row == 4)
          return unit (orogeny->evolution.area_exponent, 0.0f, 1.0f);
        if (row == 5)
          return unit (square_meters_per_julian_year_value (
                         orogeny->evolution.diffusivity),
                       0.0f,
                       0.005f);
        return unit (orogeny->evolution.sea_level, 0.0f, 0.3f);
      }
      if (const auto* hydraulic =
            std::get_if<terrain::HydraulicErosion> (&stage))
        return row == 0   ? unit (terrain::count_value (hydraulic->droplets),
                                0.0f,
                                5000000.0f)
               : row == 1 ? unit (terrain::count_value (hydraulic->batch_size),
                                  1.0f,
                                  4096.0f)
                          : unit (terrain::count_value (hydraulic->max_steps),
                                  1.0f,
                                  2048.0f);
      if (const auto* carving = std::get_if<terrain::ChannelCarving> (&stage)) {
        if (row == 0)
          return unit (carving->minimum_area_cells, 64.0f, 16384.0f);
        if (row == 1)
          return unit (carving->depth_per_sqrt_m2, 0.0f, 0.01f);
        if (row == 2)
          return unit (meters_value (carving->minimum_depth), 0.0f, 4.0f);
        if (row == 3)
          return unit (meters_value (carving->maximum_depth), 0.0f, 12.0f);
        return unit (meters_value (carving->bank_blend), 0.0f, 30.0f);
      }
      if (const auto* trails = std::get_if<terrain::TrailFormation> (&stage)) {
        if (row == 0)
          return unit (square_meters_value (trails->minimum_catchment_area),
                       100.0f,
                       20000.0f);
        if (row == 1)
          return unit (square_meters_value (trails->maximum_catchment_area),
                       20000.0f,
                       250000.0f);
        if (row == 2)
          return unit (meters_value (trails->width), 1.0f, 12.0f);
        if (row == 3)
          return unit (meters_value (trails->shoulder_blend), 0.0f, 20.0f);
        if (row == 4)
          return unit (meters_value (trails->maximum_cut), 0.0f, 5.0f);
        if (row == 5)
          return unit (meters_value (trails->maximum_fill), 0.0f, 5.0f);
        if (row == 6)
          return unit (trails->maximum_grade.numerical_value_in (mp_units::one),
                       0.05f,
                       0.6f);
        if (row == 7)
          return unit (
            meters_value (trails->home_base_water_distance), 20.0f, 250.0f);
        if (row == 8)
          return unit (
            meters_value (trails->home_base_pad_radius), 8.0f, 45.0f);
        return unit (
          meters_value (trails->desired_circuit_radius), 250.0f, 1800.0f);
      }
      if (const auto* thermal = std::get_if<terrain::ThermalErosion> (&stage))
        return row == 0 ? unit (terrain::count_value (thermal->iterations),
                                0.0f,
                                20.0f)
                        : unit (thermal->talus, 0.0f, 0.05f);
      if (const auto* diffusion =
            std::get_if<terrain::HillslopeDiffusion> (&stage))
        return row == 0 ? unit (julian_years_value (diffusion->duration),
                                0.0f,
                                20000.0f)
                        : unit (square_meters_per_julian_year_value (
                                  diffusion->diffusivity),
                                0.0f,
                                0.1f);
      return 0.0f;
    }

    ParameterDomain TerrainLab::selected_property_domain (int row) const {
      if (m_selected_stage < 0)
        return recipe_property (m_program.source.recipe, row).domain;
      return stage_property (m_program.transforms[m_selected_stage], row)
        .domain;
    }

    bool TerrainLab::set_selected_property_normalized (int row, float value) {
      if (selected_property_domain (row) != ParameterDomain::Continuous)
        return false;
      value = std::clamp (value, 0.0f, 1.0f);
      const auto mix = [value] (float low, float high) {
        return low + value * (high - low);
      };
      if (m_selected_stage < 0) {
        terrain::GeologicalRecipe& recipe = m_program.source.recipe;
        switch (row) {
        case 0: {
          const float old = recipe.warp.amplitude;
          recipe.warp.amplitude = mix (0.0f, 0.6f);
          return recipe.warp.amplitude != old;
        }
        case 1: {
          const int old = recipe.continent.noise.cycles;
          recipe.continent.noise.cycles = std::lround (mix (1.0f, 16.0f));
          return recipe.continent.noise.cycles != old;
        }
        case 2: {
          const int old = recipe.plains.noise.cycles;
          recipe.plains.noise.cycles = std::lround (mix (1.0f, 32.0f));
          return recipe.plains.noise.cycles != old;
        }
        case 3: {
          const int old = recipe.mountains.cycles;
          recipe.mountains.cycles = std::lround (mix (1.0f, 8.0f));
          return recipe.mountains.cycles != old;
        }
        case 4: {
          const float old = recipe.blend.mask_low;
          recipe.blend.mask_low = mix (0.0f, recipe.blend.mask_high - 0.001f);
          return recipe.blend.mask_low != old;
        }
        case 5: {
          const float old = recipe.blend.mask_high;
          recipe.blend.mask_high = mix (recipe.blend.mask_low + 0.001f, 1.0f);
          return recipe.blend.mask_high != old;
        }
        case 6: {
          const float old = recipe.blend.continent_weight;
          recipe.blend.continent_weight = mix (0.0f, 1.5f);
          return recipe.blend.continent_weight != old;
        }
        case 7: {
          const float old = recipe.blend.plains_weight;
          recipe.blend.plains_weight = mix (0.0f, 1.0f);
          return recipe.blend.plains_weight != old;
        }
        case 8: {
          const float old = recipe.blend.mountain_weight;
          recipe.blend.mountain_weight = mix (0.0f, 1.5f);
          return recipe.blend.mountain_weight != old;
        }
        default:
          return false;
        }
      }

      terrain::TerrainTransform& stage = m_program.transforms[m_selected_stage];
      if (auto* power = std::get_if<terrain::PowerHeights> (&stage)) {
        const float old = power->exponent;
        power->exponent = mix (0.1f, 4.0f);
        return power->exponent != old;
      }
      if (auto* analytical = std::get_if<terrain::AnalyticalErosion> (&stage)) {
        if (row == 0) {
          const auto old = analytical->duration;
          analytical->duration =
            mix (0.0f, 1600000.0f) * mp_units::astronomy::Julian_year;
          return analytical->duration != old;
        }
        if (row == 1) {
          const auto old = analytical->uplift_rate;
          analytical->uplift_rate = mix (0.0f, 0.003f) * mp_units::si::metre /
                                    mp_units::astronomy::Julian_year;
          return analytical->uplift_rate != old;
        }
        if (row == 2) {
          const float old = analytical->erodibility;
          analytical->erodibility = std::pow (10.0f, mix (-6.0f, -3.0f));
          return analytical->erodibility != old;
        }
        if (row == 3) {
          const float old = analytical->area_exponent;
          analytical->area_exponent = mix (0.0f, 1.0f);
          return analytical->area_exponent != old;
        }
        if (row == 5) {
          const float old = analytical->relaxation;
          analytical->relaxation = mix (0.1f, 1.0f);
          return analytical->relaxation != old;
        }
        return false;
      }
      if (auto* orogeny = std::get_if<terrain::OrogenyEvolution> (&stage)) {
        if (row == 0) {
          const auto old = orogeny->evolution.duration;
          orogeny->evolution.duration =
            mix (0.0f, 5000000.0f) * mp_units::astronomy::Julian_year;
          return orogeny->evolution.duration != old;
        }
        if (row == 1) {
          const auto old = orogeny->evolution.time_step;
          orogeny->evolution.time_step =
            mix (1000.0f, 250000.0f) * mp_units::astronomy::Julian_year;
          return orogeny->evolution.time_step != old;
        }
        if (row == 2) {
          const auto old = orogeny->maximum_uplift_rate;
          orogeny->maximum_uplift_rate = mix (0.0f, 0.003f) *
                                         mp_units::si::metre /
                                         mp_units::astronomy::Julian_year;
          return orogeny->maximum_uplift_rate != old;
        }
        if (row == 3) {
          const float old = orogeny->evolution.erodibility;
          orogeny->evolution.erodibility = std::pow (10.0f, mix (-7.0f, -3.0f));
          return orogeny->evolution.erodibility != old;
        }
        if (row == 4) {
          const float old = orogeny->evolution.area_exponent;
          orogeny->evolution.area_exponent = mix (0.0f, 1.0f);
          return orogeny->evolution.area_exponent != old;
        }
        if (row == 5) {
          const auto old = orogeny->evolution.diffusivity;
          orogeny->evolution.diffusivity =
            mix (0.0f, 0.005f) * mp_units::si::metre * mp_units::si::metre /
            mp_units::astronomy::Julian_year;
          return orogeny->evolution.diffusivity != old;
        }
        const float old = orogeny->evolution.sea_level;
        orogeny->evolution.sea_level = mix (0.0f, 0.3f);
        m_program.source.sea_level = orogeny->evolution.sea_level;
        return orogeny->evolution.sea_level != old;
      }
      if (auto* hydraulic = std::get_if<terrain::HydraulicErosion> (&stage)) {
        if (row == 0) {
          const auto old = hydraulic->droplets;
          hydraulic->droplets =
            terrain::droplet_count (std::lround (mix (0.0f, 5000000.0f)));
          return hydraulic->droplets != old;
        }
        if (row == 1) {
          const auto old = hydraulic->batch_size;
          hydraulic->batch_size =
            terrain::batch_size (std::lround (mix (1.0f, 4096.0f)));
          return hydraulic->batch_size != old;
        }
        const auto old = hydraulic->max_steps;
        hydraulic->max_steps =
          terrain::step_count (std::lround (mix (1.0f, 2048.0f)));
        return hydraulic->max_steps != old;
      }
      if (auto* carving = std::get_if<terrain::ChannelCarving> (&stage)) {
        if (row == 0) {
          const float old = carving->minimum_area_cells;
          carving->minimum_area_cells = mix (64.0f, 16384.0f);
          return carving->minimum_area_cells != old;
        }
        if (row == 1) {
          const float old = carving->depth_per_sqrt_m2;
          carving->depth_per_sqrt_m2 = mix (0.0f, 0.01f);
          return carving->depth_per_sqrt_m2 != old;
        }
        if (row == 2) {
          const auto old = carving->minimum_depth;
          carving->minimum_depth =
            mix (0.0f, std::min (4.0f, meters_value (carving->maximum_depth))) *
            mp_units::si::metre;
          return carving->minimum_depth != old;
        }
        if (row == 3) {
          const auto old = carving->maximum_depth;
          carving->maximum_depth =
            mix (std::max (0.0f, meters_value (carving->minimum_depth)),
                 12.0f) *
            mp_units::si::metre;
          return carving->maximum_depth != old;
        }
        const auto old = carving->bank_blend;
        carving->bank_blend = mix (0.0f, 30.0f) * mp_units::si::metre;
        return carving->bank_blend != old;
      }
      if (auto* trails = std::get_if<terrain::TrailFormation> (&stage)) {
        if (row == 0) {
          const auto old = trails->minimum_catchment_area;
          trails->minimum_catchment_area =
            std::min (mix (100.0f, 20000.0f),
                      square_meters_value (trails->maximum_catchment_area)) *
            mp_units::si::metre * mp_units::si::metre;
          return trails->minimum_catchment_area != old;
        }
        if (row == 1) {
          const auto old = trails->maximum_catchment_area;
          trails->maximum_catchment_area =
            std::max (mix (20000.0f, 250000.0f),
                      square_meters_value (trails->minimum_catchment_area)) *
            mp_units::si::metre * mp_units::si::metre;
          return trails->maximum_catchment_area != old;
        }
        if (row == 2) {
          const auto old = trails->width;
          trails->width = mix (1.0f, 12.0f) * mp_units::si::metre;
          return trails->width != old;
        }
        if (row == 3) {
          const auto old = trails->shoulder_blend;
          trails->shoulder_blend = mix (0.0f, 20.0f) * mp_units::si::metre;
          return trails->shoulder_blend != old;
        }
        if (row == 4) {
          const auto old = trails->maximum_cut;
          trails->maximum_cut = mix (0.0f, 5.0f) * mp_units::si::metre;
          return trails->maximum_cut != old;
        }
        if (row == 5) {
          const auto old = trails->maximum_fill;
          trails->maximum_fill = mix (0.0f, 5.0f) * mp_units::si::metre;
          return trails->maximum_fill != old;
        }
        if (row == 6) {
          const auto old = trails->maximum_grade;
          trails->maximum_grade =
            mix (0.05f, 0.6f) * terrain::terrain_slope[mp_units::one];
          return trails->maximum_grade != old;
        }
        if (row == 7) {
          const auto old = trails->home_base_water_distance;
          trails->home_base_water_distance =
            mix (20.0f, 250.0f) * mp_units::si::metre;
          return trails->home_base_water_distance != old;
        }
        if (row == 8) {
          const auto old = trails->home_base_pad_radius;
          trails->home_base_pad_radius =
            mix (8.0f, 45.0f) * mp_units::si::metre;
          return trails->home_base_pad_radius != old;
        }
        const auto old = trails->desired_circuit_radius;
        trails->desired_circuit_radius =
          mix (250.0f, 1800.0f) * mp_units::si::metre;
        return trails->desired_circuit_radius != old;
      }
      if (auto* thermal = std::get_if<terrain::ThermalErosion> (&stage)) {
        if (row == 0) {
          const auto old = thermal->iterations;
          thermal->iterations =
            terrain::iteration_count (std::lround (mix (0.0f, 20.0f)));
          return thermal->iterations != old;
        }
        const float old = thermal->talus;
        thermal->talus = mix (0.0f, 0.05f);
        return thermal->talus != old;
      }
      if (auto* diffusion = std::get_if<terrain::HillslopeDiffusion> (&stage)) {
        if (row == 0) {
          const auto old = diffusion->duration;
          diffusion->duration =
            mix (0.0f, 20000.0f) * mp_units::astronomy::Julian_year;
          return diffusion->duration != old;
        }
        const auto old = diffusion->diffusivity;
        diffusion->diffusivity = mix (0.0f, 0.1f) * mp_units::si::metre *
                                 mp_units::si::metre /
                                 mp_units::astronomy::Julian_year;
        return diffusion->diffusivity != old;
      }
      return false;
    }

    bool TerrainLab::adjust_selected_natural (int row, int direction) {
      if (direction == 0 ||
          selected_property_domain (row) != ParameterDomain::Natural)
        return false;
      if (m_selected_stage < 0) {
        terrain::GeologicalRecipe& recipe = m_program.source.recipe;
        int* value = row == 1   ? &recipe.continent.noise.cycles
                     : row == 2 ? &recipe.plains.noise.cycles
                                : &recipe.mountains.cycles;
        const int maximum = row == 1 ? 16 : row == 2 ? 32 : 8;
        const int changed = std::clamp (*value + direction, 1, maximum);
        if (changed == *value)
          return false;
        *value = changed;
        return true;
      }

      terrain::TerrainTransform& stage = m_program.transforms[m_selected_stage];
      if (auto* analytical = std::get_if<terrain::AnalyticalErosion> (&stage)) {
        const int changed = std::clamp (
          terrain::count_value (analytical->fixed_point_iterations) + direction,
          1,
          12);
        if (changed ==
            terrain::count_value (analytical->fixed_point_iterations))
          return false;
        analytical->fixed_point_iterations = terrain::iteration_count (changed);
        return true;
      }
      if (auto* hydraulic = std::get_if<terrain::HydraulicErosion> (&stage)) {
        const int value = row == 0 ? terrain::count_value (hydraulic->droplets)
                          : row == 1
                            ? terrain::count_value (hydraulic->batch_size)
                            : terrain::count_value (hydraulic->max_steps);
        int changed = value;
        if (row == 0) {
          constexpr int choices[] = { 0,       10000,   30000,   100000,
                                      300000,  500000,  1000000, 1500000,
                                      2000000, 3000000, 5000000 };
          if (direction > 0) {
            for (int choice : choices)
              if (choice > value) {
                changed = choice;
                break;
              }
          } else {
            for (auto i = std::rbegin (choices); i != std::rend (choices); ++i)
              if (*i < value) {
                changed = *i;
                break;
              }
          }
        } else if (row == 1) {
          changed = std::clamp (value + direction * 64, 1, 4096);
        } else {
          constexpr int choices[] = {
            8, 16, 32, 64, 128, 256, 512, 1024, 2048
          };
          if (direction > 0) {
            for (int choice : choices)
              if (choice > value) {
                changed = choice;
                break;
              }
          } else {
            for (auto i = std::rbegin (choices); i != std::rend (choices); ++i)
              if (*i < value) {
                changed = *i;
                break;
              }
          }
        }
        if (changed == value)
          return false;
        if (row == 0)
          hydraulic->droplets = terrain::droplet_count (changed);
        else if (row == 1)
          hydraulic->batch_size = terrain::batch_size (changed);
        else
          hydraulic->max_steps = terrain::step_count (changed);
        return true;
      }
      if (auto* thermal = std::get_if<terrain::ThermalErosion> (&stage)) {
        const int changed = std::clamp (
          terrain::count_value (thermal->iterations) + direction, 0, 20);
        if (changed == terrain::count_value (thermal->iterations))
          return false;
        thermal->iterations = terrain::iteration_count (changed);
        return true;
      }
      return false;
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
          stage < static_cast<int> (m_program.transforms.size ())) {
        iterative =
          terrain::terrain_transform_semantics (m_program.transforms[stage])
            .evaluation_order == terrain::EvaluationOrder::Iterative;
        rerun_program_from (stage);
      } else {
        rebuild_program ();
      }
      m_parameter_rebuild_delay = iterative ? 0.18f : 0.045f;
    }

    float TerrainLab::friendly_control_normalized (int control) const {
      if (control == 0)
        return std::clamp (
          m_program.source.recipe.blend.mountain_weight / 1.5f, 0.0f, 1.0f);
      if (control == 1)
        return std::clamp (
          m_program.source.recipe.warp.amplitude / 0.6f, 0.0f, 1.0f);
      for (const terrain::TerrainTransform& stage : m_program.transforms) {
        if (control == 2) {
          if (const auto* orogeny =
                std::get_if<terrain::OrogenyEvolution> (&stage))
            return std::clamp (
              julian_years_value (orogeny->evolution.duration) / 800000.0f,
              0.0f,
              1.0f);
          if (const auto* age =
                std::get_if<terrain::AnalyticalErosion> (&stage))
            return std::clamp (
              julian_years_value (age->duration) / 800000.0f, 0.0f, 1.0f);
        } else if (control == 3) {
          if (const auto* rain =
                std::get_if<terrain::HydraulicErosion> (&stage))
            return std::clamp (
              terrain::count_value (rain->droplets) / 300000.0f, 0.0f, 1.0f);
        }
      }
      return 0.0f;
    }

    bool TerrainLab::set_friendly_control_normalized (int control,
                                                      float value) {
      value = std::clamp (value, 0.0f, 1.0f);
      int changed_stage = -1;
      bool changed = false;
      if (control == 0) {
        float& weight = m_program.source.recipe.blend.mountain_weight;
        const float next = value * 1.5f;
        changed = next != weight;
        weight = next;
      } else if (control == 1) {
        float& warp = m_program.source.recipe.warp.amplitude;
        const float next = value * 0.6f;
        changed = next != warp;
        warp = next;
      } else {
        for (std::size_t i = 0; i < m_program.transforms.size (); ++i) {
          terrain::TerrainTransform& stage = m_program.transforms[i];
          if (control == 2) {
            if (auto* orogeny =
                  std::get_if<terrain::OrogenyEvolution> (&stage)) {
              const auto next =
                value * 800000.0f * mp_units::astronomy::Julian_year;
              changed = next != orogeny->evolution.duration;
              orogeny->evolution.duration = next;
              changed_stage = static_cast<int> (i);
              break;
            }
            if (auto* age = std::get_if<terrain::AnalyticalErosion> (&stage)) {
              const auto next =
                value * 800000.0f * mp_units::astronomy::Julian_year;
              changed = next != age->duration;
              age->duration = next;
              changed_stage = static_cast<int> (i);
              break;
            }
          } else if (auto* rain =
                       std::get_if<terrain::HydraulicErosion> (&stage)) {
            const int next = static_cast<int> (std::lround (value * 300000.0f));
            changed = next != terrain::count_value (rain->droplets);
            rain->droplets = terrain::droplet_count (next);
            changed_stage = static_cast<int> (i);
            break;
          }
        }
      }
      if (!changed)
        return false;
      m_friendly_preset = -1;
      m_parameter_rebuild_pending = true;
      m_parameter_rebuild_stage = changed_stage;
      return true;
    }

    void TerrainLab::apply_friendly_preset (int preset) {
      const std::uint32_t seed = m_program.randomness.seed.value;
      m_program = preset == 3 ? terrain::make_orogeny_program (seed)
                              : terrain::make_geological_program (seed);
      auto& recipe = m_program.source.recipe;
      if (preset == 0) {
        recipe.blend.mountain_weight = 1.35f;
        recipe.warp.amplitude = 0.22f;
        m_program.transforms.emplace_back (terrain::PowerHeights { 1.35f });
      } else if (preset == 1) {
        recipe.blend.mountain_weight = 0.45f;
        recipe.warp.amplitude = 0.08f;
        m_program.transforms.emplace_back (terrain::PowerHeights { 0.78f });
        m_program.transforms.emplace_back (terrain::AnalyticalErosion {
          .duration = 500000.0f * mp_units::astronomy::Julian_year,
          .fixed_point_iterations = terrain::iteration_count (3) });
        m_program.transforms.emplace_back (
          terrain::ThermalErosion { terrain::iteration_count (4), 0.004f });
      } else if (preset == 2) {
        recipe.blend.continent_weight = 0.72f;
        recipe.blend.mountain_weight = 1.05f;
        m_program.transforms.emplace_back (terrain::PowerHeights { 1.12f });
        m_program.transforms.emplace_back (terrain::HydraulicErosion {
          .droplets = terrain::droplet_count (100000),
          .batch_size = terrain::batch_size (256),
          .max_steps = terrain::step_count (256),
          .minimum_water = 0.01f,
          .sediment_at_termination = terrain::SedimentDisposition::Deposit });
      } else {
        // Mountain texture becomes a tectonic-rate pattern over a shallow
        // continent seed. Four drainage refreshes keep the first interactive
        // preset responsive; Expert mode exposes the full geological span.
        recipe.blend.plains_weight = 0.25f;
        recipe.blend.mountain_weight = 0.9f;
        recipe.warp.amplitude = 0.28f;
        auto& orogeny =
          std::get<terrain::OrogenyEvolution> (m_program.transforms.front ());
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
          const std::uint32_t seed =
            terrain::next_seed (m_program.randomness.seed).value;
          m_program = terrain::make_geological_program (seed);
          m_selected_stage = -1;
          m_friendly_preset = -1;
          rebuild_program ();
        } else if (i == 1) {
          fit_view ();
        } else {
          m_expert_ui = true;
        }
        return;
      }
      for (int i = 0; i < 4; ++i) {
        if (friendly_preset_rect (i, m_ui_height).contains (x, y)) {
          apply_friendly_preset (i);
          return;
        }
      }
      constexpr OverlayMode modes[] = { OverlayMode::None,
                                        OverlayMode::Slope,
                                        OverlayMode::StandingWater,
                                        OverlayMode::Trace };
      for (int i = 0; i < 4; ++i) {
        if (friendly_lens_rect (i, m_ui_width).contains (x, y)) {
          if (i == 3) {
            m_droplet_armed = true;
            m_droplet_follow = false;
            m_droplet_target.reset ();
            set_overlay (OverlayMode::None);
            m_overlay_status = "DROP — click land to release";
          } else {
            m_droplet_armed = false;
            m_droplet_target.reset ();
            set_overlay (modes[i]);
          }
          return;
        }
      }
    }

    void TerrainLab::handle_click (float x, float y) {
      if (m_expert_ui && expert_back_rect ().contains (x, y)) {
        m_expert_ui = false;
        return;
      }
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
      if (close_rect ().contains (x, y)) {
        leave ();
        return;
      }
      if (reset_rect ().contains (x, y)) {
        reset_program ();
        return;
      }
      if (seed_rect ().contains (x, y)) {
        const terrain::Seed seed =
          terrain::next_seed (m_program.randomness.seed);
        m_program.source.recipe.seeds =
          terrain::derive_geological_seeds (seed.value);
        m_program.randomness = { .seed = seed,
                                 .offset = terrain::SequenceOffset { 3 } };
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
        if (stage < static_cast<int> (m_program.transforms.size ()) &&
            stage_rect (row).contains (x, y)) {
          m_selected_stage = stage;
          if (m_overlay == OverlayMode::HeightDelta)
            update_overlay ();
          return;
        }
      }
      for (int i = 0; i < 9; ++i) {
        if (!add_stage_rect (i).contains (x, y))
          continue;
        if (i == 0)
          append_stage (terrain::NormalizeHeights {});
        else if (i == 1)
          append_stage (terrain::PowerHeights { 1.15f });
        else if (i == 2)
          append_stage (terrain::AnalyticalErosion {});
        else if (i == 3) {
          m_program.source.mode = terrain::GeologicalSource::Mode::Orogeny;
          if (m_program.transforms.size () == 1 &&
              std::holds_alternative<terrain::NormalizeHeights> (
                m_program.transforms.front ()))
            m_program.transforms.clear ();
          m_program.transforms.emplace_back (terrain::OrogenyEvolution {});
          m_selected_stage =
            static_cast<int> (m_program.transforms.size ()) - 1;
          ensure_selected_stage_visible ();
          rebuild_program ();
        } else if (i == 4)
          append_stage (terrain::HydraulicErosion {
            .droplets = terrain::droplet_count (100000),
            .batch_size = terrain::batch_size (256),
            .max_steps = terrain::step_count (512),
            .minimum_water = 0.01f,
            .sediment_at_termination = terrain::SedimentDisposition::Deposit });
        else if (i == 5)
          append_stage (
            terrain::ThermalErosion { terrain::iteration_count (2), 0.003f });
        else if (i == 6)
          append_stage (terrain::HillslopeDiffusion {});
        else if (i == 7)
          append_stage (terrain::TrailFormation {});
        else
          append_stage (terrain::ChannelCarving {});
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
        property_count =
          stage_property_count (m_program.transforms[m_selected_stage]);
      for (int row = 0; row < property_count; ++row) {
        if (selected_property_domain (row) != ParameterDomain::Natural)
          continue;
        const UiRect bounds = property_rect (row);
        const int direction = counter_minus_rect (bounds).contains (x, y)  ? -1
                              : counter_plus_rect (bounds).contains (x, y) ? 1
                                                                           : 0;
        if (adjust_selected_natural (row, direction)) {
          queue_parameter_rebuild ();
          run_pending_parameter_rebuild ();
          return;
        }
      }

      if (m_selected_stage >= 0) {
        terrain::TerrainTransform& stage =
          m_program.transforms[m_selected_stage];
        if (auto* hydraulic = std::get_if<terrain::HydraulicErosion> (&stage)) {
          constexpr int presets[3][4] = { { 100000, 300000, 1000000, 1500000 },
                                          { 64, 128, 256, 512 },
                                          { 1, 64, 256, 1024 } };
          for (int group = 0; group < 3; ++group)
            for (int i = 0; i < 4; ++i)
              if (hydraulic_preset_rect (group, i).contains (x, y)) {
                const int value =
                  group == 0   ? terrain::count_value (hydraulic->droplets)
                  : group == 1 ? terrain::count_value (hydraulic->max_steps)
                               : terrain::count_value (hydraulic->batch_size);
                if (value != presets[group][i]) {
                  if (group == 0)
                    hydraulic->droplets =
                      terrain::droplet_count (presets[group][i]);
                  else if (group == 1)
                    hydraulic->max_steps =
                      terrain::step_count (presets[group][i]);
                  else
                    hydraulic->batch_size =
                      terrain::batch_size (presets[group][i]);
                  rerun_program_from (m_selected_stage);
                }
                return;
              }
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
        inspection_fog && (!m_map_pristine || m_view == ViewMode::Torus);
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
      if (m_map_pristine) {
        m_renderer->set_terrain_paths (m_saved_trail_influence,
                                       m_saved_home_base_influence);
      } else if (m_evaluator && m_evaluator->trail_network ()) {
        m_renderer->set_terrain_paths (
          terrain::expand_trail_influence (*m_evaluator->trail_network ()),
          terrain::expand_home_base_influence (*m_evaluator->trail_network ()));
      } else {
        m_renderer->set_terrain_paths ({});
      }
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
          !m_friendly_drag && !m_parameter_drag)
        run_pending_parameter_rebuild ();

      m_time += dt;
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
      if (m_droplet_trace.points.size () > 1) {
        const float last =
          static_cast<float> (m_droplet_trace.points.size () - 1);
        m_droplet_progress = std::min (last, m_droplet_progress + dt * 16.0f);
        const bool arrived = m_droplet_progress >= last;
        if (arrived)
          m_droplet_settle += dt;
        if (m_droplet_follow) {
          // Begin from the view in which the drop was placed, then ease
          // toward its route.  Chase a blend of the bead and a point a
          // little further along the trace: the lead frames where the
          // drop is heading and averages out the per-cell zigzag.
          const Vec3 bead = droplet_world_position (m_droplet_progress);
          const Vec3 lead =
            droplet_world_position (std::min (last, m_droplet_progress + 6.0f));
          const Vec3 desired = bead + (lead - bead) * 0.45f;
          const float follow_response =
            smoothing_alpha (2.6f / u::s, dt * u::s);
          m_target += (desired - m_target) * follow_response;
          m_scroll_zoom_target = std::min (m_scroll_zoom_target, 650.0f);
          const float clear_pitch = visible_droplet_pitch (bead);
          if (clear_pitch > m_pitch)
            m_pitch += (clear_pitch - m_pitch) * follow_response;
          // Linger through the settle animation before releasing the
          // camera, so the melt is watched rather than abandoned.
          if (arrived && m_droplet_settle > 2.0f)
            m_droplet_follow = false;
        }
        update_droplet_overlay ();
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
      const damping_t zoom_speed = (m_droplet_follow ? 2.2f : 14.0f) / u::s;
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
        if (m_expert_ui)
          select (terrain::GeologicalLayer::Combined);
        else {
          m_droplet_armed = false;
          set_overlay (OverlayMode::None);
        }
        break;
      case Key::Two:
        if (m_expert_ui)
          select (terrain::GeologicalLayer::Continent);
        else {
          m_droplet_armed = false;
          set_overlay (OverlayMode::Slope);
        }
        break;
      case Key::Three:
        if (m_expert_ui)
          select (terrain::GeologicalLayer::Plains);
        else {
          m_droplet_armed = false;
          set_overlay (OverlayMode::StandingWater);
        }
        break;
      case Key::Four:
        if (m_expert_ui)
          select (terrain::GeologicalLayer::Mountains);
        else {
          m_droplet_armed = true;
          m_droplet_follow = false;
          set_overlay (OverlayMode::None);
          m_overlay_status = "DROP — click land to release";
        }
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
        m_program.randomness.seed =
          terrain::next_seed (m_program.randomness.seed);
        m_program.source.recipe.seeds =
          terrain::derive_geological_seeds (m_program.randomness.seed.value);
        m_program.randomness.offset = terrain::SequenceOffset { 3 };
        m_selected_stage = -1;
        rebuild_program ();
        break;
      case Key::E:
        append_stage (
          terrain::HydraulicErosion { terrain::droplet_count (100000) });
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
      if (index <= m_program.transforms.size ()) {
        const std::string_view id =
          terrain::terrain_transform_id (m_program.transforms[index - 1]);
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
      m_map_pristine = index + 1 == m_history->size ();
      invalidate_analysis ();
      refresh ();
    }

    void TerrainLab::pointer_move (float x, float y, float dx, float dy) {
      if (!m_active)
        return;
      m_pointer_x = x;
      m_pointer_y = y;
      if (m_droplet_armed) {
        const bool over_ui =
          m_expert_ui ? ui_contains (x, y)
                      : friendly_ui_contains (x, y, m_ui_width, m_ui_height);
        m_droplet_target =
          over_ui ? std::nullopt : terrain_point_at_screen (x, y);
      }
      if (m_friendly_drag) {
        const UiRect bounds =
          friendly_slider_rect (m_friendly_drag_control, m_ui_height);
        const UiRect rail = friendly_slider_rail_rect (bounds);
        const float rail_x = rail.x;
        const float rail_width = rail.width;
        const float normalized =
          rail_width > 0.0f ? (x - rail_x) / rail_width : 0.0f;
        if (set_friendly_control_normalized (m_friendly_drag_control,
                                             normalized))
          m_parameter_rebuild_delay = 0.045f;
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
      if (std::hypot (dx, dy) > 0.0f)
        m_droplet_follow = false;
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
        if (down) {
          if (!m_expert_ui) {
            for (int control = 0; control < 4; ++control) {
              const UiRect bounds = friendly_slider_rect (control, m_ui_height);
              if (!bounds.contains (x, y))
                continue;
              const bool has_age = std::any_of (
                m_program.transforms.begin (),
                m_program.transforms.end (),
                [] (const terrain::TerrainTransform& stage) {
                  return std::holds_alternative<terrain::AnalyticalErosion> (
                           stage) ||
                         std::holds_alternative<terrain::OrogenyEvolution> (
                           stage);
                });
              const bool has_rain = std::any_of (
                m_program.transforms.begin (),
                m_program.transforms.end (),
                [] (const terrain::TerrainTransform& stage) {
                  return std::holds_alternative<terrain::HydraulicErosion> (
                    stage);
                });
              if (control == 2 && !has_age)
                append_stage (terrain::AnalyticalErosion {
                  .duration = 0.0f * mp_units::astronomy::Julian_year });
              else if (control == 3 && !has_rain)
                append_stage (terrain::HydraulicErosion {
                  .droplets = terrain::droplet_count (0),
                  .batch_size = terrain::batch_size (256),
                  .max_steps = terrain::step_count (256),
                  .minimum_water = 0.01f,
                  .sediment_at_termination =
                    terrain::SedimentDisposition::Deposit });
              m_friendly_drag = true;
              m_friendly_drag_control = control;
              const UiRect rail = friendly_slider_rail_rect (bounds);
              const float rail_x = rail.x;
              const float rail_width = rail.width;
              set_friendly_control_normalized (control,
                                               (x - rail_x) / rail_width);
              return;
            }
            if (friendly_ui_contains (x, y, m_ui_width, m_ui_height))
              handle_friendly_click (x, y);
            else {
              m_camera_drag = true;
              m_camera_drag_distance = 0.0f;
            }
          } else {
            int property_count = 9;
            if (m_selected_stage >= 0)
              property_count =
                stage_property_count (m_program.transforms[m_selected_stage]);
            for (int row = 0; row < property_count; ++row) {
              if (!parameter_control_rect (property_rect (row)).contains (x, y))
                continue;
              if (selected_property_domain (row) != ParameterDomain::Continuous)
                continue;
              m_parameter_drag = true;
              m_drag_property = row;
              m_drag_start_y = y;
              m_drag_start_normalized = selected_property_normalized (row);
              return;
            }
            if (ui_contains (x, y))
              handle_click (x, y);
            else {
              m_camera_drag = true;
              m_camera_drag_distance = 0.0f;
            }
          }
        } else {
          if (m_friendly_drag) {
            m_friendly_drag = false;
            m_friendly_drag_control = -1;
            run_pending_parameter_rebuild ();
          }
          if (m_parameter_drag) {
            m_parameter_drag = false;
            m_drag_property = -1;
            run_pending_parameter_rebuild ();
          }
          if (m_camera_drag && m_camera_drag_distance < 4.0f) {
            if (m_droplet_armed)
              launch_droplet (x, y);
            else if (m_overlay == OverlayMode::Trace)
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
      const bool over_ui =
        m_expert_ui ? ui_contains (x, y)
                    : friendly_ui_contains (x, y, m_ui_width, m_ui_height);
      if (over_ui) {
        if (m_expert_ui && stage_list_rect ().contains (x, y) &&
            m_program.transforms.size () > visible_stage_rows) {
          m_stage_scroll += delta > 0.0f ? -1 : 1;
          const int maximum = static_cast<int> (m_program.transforms.size ()) -
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
      const auto hot = [this] (const UiRect& bounds) {
        return bounds.contains (m_pointer_x, m_pointer_y);
      };
      m_ui.begin (dl);
      const UiRect panel = friendly_panel_rect (height);
      const float vertical_scale = friendly_vertical_scale (height);
      m_ui.surface (dl, panel);
      m_ui.friendly_section (
        dl,
        { 28, friendly_scaled_y (26, height), 388, 18 * vertical_scale },
        "SESSION");
      m_ui.friendly_section (
        dl,
        { 28, friendly_scaled_y (158, height), 388, 18 * vertical_scale },
        "WORLD PRESETS");
      m_ui.friendly_section (
        dl,
        { 28, friendly_scaled_y (458, height), 388, 18 * vertical_scale },
        "TERRAIN PARAMETERS");

      constexpr const char* action_labels[] = { "NEW WORLD",
                                                "CENTER",
                                                "EXPERT" };
      for (int i = 0; i < 3; ++i) {
        const UiRect bounds = friendly_action_rect (i, height);
        m_ui.session_button (
          dl, bounds, action_labels[i], hot (bounds), m_pointer_down, i);
      }
      constexpr const char* preset_titles[] = {
        "YOUNG PEAKS", "OLD HILLS", "RAINY ISLAND", "OROGENY"
      };
      for (int i = 0; i < 4; ++i) {
        const UiRect bounds = friendly_preset_rect (i, height);
        m_ui.preset_card (dl,
                          bounds,
                          preset_titles[i],
                          hot (bounds),
                          m_pointer_down,
                          m_friendly_preset == i,
                          i + 3);
      }

      const UiRect parameter_surface {
        38, friendly_scaled_y (490, height), 390, 304 * vertical_scale
      };
      m_ui.surface (dl, parameter_surface);

      constexpr const char* slider_titles[] = {
        "MOUNTAINS", "WILD RIDGES", "AGE", "RAINFALL"
      };
      constexpr const char* slider_low[] = {
        "GENTLE", "SMOOTH", "YOUNG", "DRY"
      };
      constexpr const char* slider_high[] = {
        "TALL", "TWISTED", "ANCIENT", "SOAKED"
      };
      for (int i = 0; i < 4; ++i) {
        const UiRect bounds = friendly_slider_rect (i, height);
        m_ui.friendly_slider (dl,
                              bounds,
                              slider_titles[i],
                              slider_low[i],
                              slider_high[i],
                              friendly_control_normalized (i),
                              hot (bounds),
                              m_friendly_drag && m_friendly_drag_control == i);
      }

      m_ui.caption (dl,
                    37,
                    panel.y + panel.height - 21,
                    "DRAG: ORBIT   SCROLL: ZOOM   T: BACK");

      constexpr const char* lens_titles[] = {
        "LAND", "STEEP", "WATER", "DROP RAIN"
      };
      constexpr OverlayMode lens_modes[] = { OverlayMode::None,
                                             OverlayMode::Slope,
                                             OverlayMode::StandingWater,
                                             OverlayMode::Trace };
      const UiRect lens_surface = friendly_lens_surface_rect (width);
      m_ui.surface (dl, lens_surface);
      m_ui.friendly_section (dl,
                             { lens_surface.x + 10.0f,
                               lens_surface.y + 7.0f,
                               lens_surface.width - 20.0f,
                               18.0f },
                             "TOOLS");
      constexpr const char* lens_keys[] = { "1", "2", "3", "4" };
      for (int i = 0; i < 4; ++i) {
        const UiRect bounds = friendly_lens_rect (i, width);
        m_ui.tool_button (dl,
                          bounds,
                          lens_titles[i],
                          lens_keys[i],
                          hot (bounds),
                          m_pointer_down,
                          i == 3 ? m_droplet_armed : m_overlay == lens_modes[i],
                          i + 11);
      }
      if (m_history && !m_history->empty ()) {
        std::ostringstream playback;
        playback << (m_history_playing ? "PLAYING " : "PAUSED ")
                 << (m_history_index + 1) << '/' << m_history->size () << "  "
                 << history_snapshot_name (m_history_index)
                 << "   SPACE: PLAY   TAB: STEP";
        m_ui.caption (dl,
                      lens_surface.x + 12.0f,
                      lens_surface.y + lens_surface.height - 12.0f,
                      playback.str ());
      }
      if (m_droplet_armed &&
          !friendly_ui_contains (
            m_pointer_x, m_pointer_y, m_ui_width, m_ui_height))
        m_ui.friendly_tool_cursor (dl, m_pointer_x, m_pointer_y, 14);
      m_ui.end (dl);
    }

    void TerrainLab::draw (render::DrawList& dl, int width, int height) const {
      m_ui_width = width;
      m_ui_height = height;
      if (!m_expert_ui)
        draw_friendly (dl, width, height);
      else
        draw_expert (dl);
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

    void TerrainLab::draw_expert (render::DrawList& dl) const {
      const auto hot = [this] (const UiRect& bounds) {
        return bounds.contains (m_pointer_x, m_pointer_y);
      };

      m_ui.begin (dl);
      m_ui.panel (dl,
                  window_x,
                  window_y,
                  window_width,
                  window_height,
                  "FIELD ALGEBRA TYCOON");
      m_ui.button (dl, close_rect (), "X", hot (close_rect ()), m_pointer_down);
      m_ui.button (
        dl, reset_rect (), "RESET", hot (reset_rect ()), m_pointer_down);

      std::ostringstream seed;
      seed << "SEED " << m_program.randomness.seed.value << " >";
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
                   expert_back_rect (),
                   "FRIENDLY",
                   hot (expert_back_rect ()),
                   m_pointer_down);
      for (int i = 0; i < 7; ++i) {
        const UiRect bounds = layer_rect (i);
        m_ui.button (dl,
                     bounds,
                     layer_labels[i],
                     hot (bounds),
                     m_pointer_down,
                     m_program.source.layer == layers[i]);
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
        m_program.source.mode == terrain::GeologicalSource::Mode::Orogeny
          ? "OROGENY SEED"
          : "GEOLOGICAL FIELD",
        m_program.source.mode == terrain::GeologicalSource::Mode::Orogeny
          ? "shallow continent / recipe becomes uplift"
          : terrain::geological_layer_name (m_program.source.layer),
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
        if (stage_index >= static_cast<int> (m_program.transforms.size ()))
          break;
        const UiRect bounds = stage_rect (row);
        m_ui.pipeline_row (dl,
                           bounds,
                           std::to_string (stage_index + 1),
                           stage_name (m_program.transforms[stage_index]),
                           stage_detail (m_program.transforms[stage_index]),
                           hot (bounds),
                           m_pointer_down,
                           m_selected_stage == stage_index);
      }

      static const char* add_labels[] = { "+NORM",  "+POWER", "+AGE",
                                          "+OROG",  "+DROP",  "+TALUS",
                                          "+CREEP", "+TRAIL", "+CARVE" };
      static const char* edit_labels[] = { "UP", "DOWN", "COPY", "DEL" };
      for (int i = 0; i < 9; ++i) {
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
            recipe_property (m_program.source.recipe, row);
          if (property.domain == ParameterDomain::Continuous) {
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
                          property.label,
                          property.value,
                          hot (counter_minus_rect (bounds)),
                          hot (counter_plus_rect (bounds)),
                          m_pointer_down);
          }
        }
      } else {
        const terrain::TerrainTransform& stage =
          m_program.transforms[m_selected_stage];
        const int count = stage_property_count (stage);
        for (int row = 0; row < count; ++row) {
          const UiRect bounds = property_rect (row);
          const PropertyText property = stage_property (stage, row);
          if (property.domain == ParameterDomain::Continuous) {
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
                          property.label,
                          property.value,
                          hot (counter_minus_rect (bounds)),
                          hot (counter_plus_rect (bounds)),
                          m_pointer_down);
          }
        }
        if (count <= 3) {
          m_ui.label (dl, right_x + 8, 294, stage_name (stage), true);
          m_ui.label (dl, right_x + 8, 316, stage_detail (stage));
          m_ui.label (dl, right_x + 8, 338, semantics_detail (stage), true);
        }
        if (std::holds_alternative<terrain::NormalizeHeights> (stage)) {
          m_ui.label (
            dl, right_x + 8, 366, "A whole-raster materialization barrier.");
          m_ui.label (
            dl, right_x + 8, 386, "It can be moved, copied, or deleted.");
        } else if (std::holds_alternative<terrain::HydraulicErosion> (stage)) {
          const auto& hydraulic = std::get<terrain::HydraulicErosion> (stage);
          constexpr const char* headings[] = { "DROP COUNT",
                                               "MAXIMUM LIFETIME",
                                               "BATCH SIZE" };
          constexpr const char* labels[3][4] = {
            { "100K", "300K", "1M", "1.5M" },
            { "64", "128", "256", "512" },
            { "1", "64", "256", "1024" }
          };
          constexpr int presets[3][4] = { { 100000, 300000, 1000000, 1500000 },
                                          { 64, 128, 256, 512 },
                                          { 1, 64, 256, 1024 } };
          for (int group = 0; group < 3; ++group) {
            m_ui.label (
              dl, right_x + 8, 362 + group * 56, headings[group], true);
            const int value =
              group == 0   ? terrain::count_value (hydraulic.droplets)
              : group == 1 ? terrain::count_value (hydraulic.max_steps)
                           : terrain::count_value (hydraulic.batch_size);
            for (int i = 0; i < 4; ++i) {
              const UiRect bounds = hydraulic_preset_rect (group, i);
              m_ui.button (dl,
                           bounds,
                           labels[group][i],
                           hot (bounds),
                           m_pointer_down,
                           value == presets[group][i]);
            }
          }
        }
      }

      std::ostringstream status;
      status << m_program.transforms.size () << " stages | selected ";
      if (m_selected_stage < 0)
        status << "field recipe";
      else
        status << (m_selected_stage + 1);
      m_ui.label (dl, left_x, 586, status.str (), true);
      m_ui.label (dl,
                  left_x,
                  613,
                  "LEFT DRAG orbit | RIGHT DRAG pan | TERRAIN WHEEL zoom");
      m_ui.label (
        dl,
        left_x,
        636,
        "Pipeline rows are selectable and reorderable | T returns to game");
      m_ui.key_hint (dl, right_x + 8, 558, "DIAL", "continuous value");
      m_ui.key_hint (dl, right_x + 8, 584, "- / +", "whole-number count");

      m_ui.panel (dl,
                  readings_x,
                  readings_y,
                  readings_width,
                  readings_height,
                  "MAP READINGS");
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
                     hot (bounds),
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
      const terrain::HydraulicErosionReport* erosion_report = nullptr;
      const terrain::HydraulicErosion* erosion_stage = nullptr;
      const terrain::AnalyticalErosionReport* analytical_report = nullptr;
      const terrain::StreamPowerEvolutionReport* orogeny_report = nullptr;
      const terrain::TrailFormationReport* trail_report = nullptr;
      if (m_selected_stage >= 0 &&
          m_selected_stage < static_cast<int> (m_reports.size ())) {
        erosion_report = std::get_if<terrain::HydraulicErosionReport> (
          &m_reports[static_cast<std::size_t> (m_selected_stage)]);
        analytical_report = std::get_if<terrain::AnalyticalErosionReport> (
          &m_reports[static_cast<std::size_t> (m_selected_stage)]);
        orogeny_report = std::get_if<terrain::StreamPowerEvolutionReport> (
          &m_reports[static_cast<std::size_t> (m_selected_stage)]);
        trail_report = std::get_if<terrain::TrailFormationReport> (
          &m_reports[static_cast<std::size_t> (m_selected_stage)]);
        erosion_stage = std::get_if<terrain::HydraulicErosion> (
          &m_program.transforms[static_cast<std::size_t> (m_selected_stage)]);
      }
      if (erosion_report) {
        const auto& report = *erosion_report;
        m_ui.label (dl,
                    readings_x + 10,
                    readings_y + 266,
                    erosion_stage && erosion_stage->sediment_at_termination ==
                                       terrain::SedimentDisposition::Deposit
                      ? "SEDIMENT LEDGER — SETTLE AT DEATH"
                      : "SEDIMENT LEDGER — DISCARD AT DEATH",
                    true);
        m_ui.label (dl,
                    readings_x + 10,
                    readings_y + 288,
                    "ERODED " + format_ledger (report.eroded) + "  DEPOSITED " +
                      format_ledger (report.deposited));
        m_ui.label (
          dl,
          readings_x + 10,
          readings_y + 310,
          "LOST " + format_ledger (report.discarded_sediment) + "  (" +
            format_float (
              static_cast<float> (100.0 * report.discarded_fraction ()), 1) +
            "%)");
        m_ui.label (
          dl,
          readings_x + 10,
          readings_y + 332,
          "MEAN LIFE " +
            format_float (static_cast<float> (report.mean_steps ()), 1) +
            "  FINAL WATER " +
            format_float (static_cast<float> (report.mean_final_water ()), 3));
        m_ui.label (
          dl,
          readings_x + 10,
          readings_y + 354,
          "CAP " +
            format_count (static_cast<int> (report.stopped_at_step_limit)) +
            "  WATER " +
            format_count (static_cast<int> (report.stopped_at_water_cutoff)) +
            "  FLAT " + format_count (static_cast<int> (report.stopped_flat)));
      } else if (orogeny_report) {
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
        m_ui.label (dl,
                    readings_x + 10,
                    readings_y + 332,
                    "MEAN / MAX CHANGE " +
                      format_float (static_cast<float> (meters_value (
                                      report.mean_absolute_change)),
                                    2) +
                      " / " +
                      format_float (static_cast<float> (meters_value (
                                      report.maximum_absolute_change)),
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
      m_ui.end (dl);
    }
  }
}
