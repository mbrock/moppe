#include <moppe/game/terrain_lab.hh>

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
    std::string_view water_body_class_name
      (terrain::WaterBodyClass classification)
    {
      switch (classification) {
      case terrain::WaterBodyClass::Puddle: return "PUDDLE";
      case terrain::WaterBodyClass::Pond: return "POND";
      case terrain::WaterBodyClass::Lake: return "LAKE";
      case terrain::WaterBodyClass::Sea: return "SEA";
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
    constexpr float readings_height = 360.0f;

    constexpr terrain::GeologicalLayer layers[] = {
      terrain::GeologicalLayer::Combined,
      terrain::GeologicalLayer::Continent,
      terrain::GeologicalLayer::Plains,
      terrain::GeologicalLayer::Mountains,
      terrain::GeologicalLayer::MountainMask,
      terrain::GeologicalLayer::WarpX,
      terrain::GeologicalLayer::WarpY
    };

    constexpr const char* layer_labels[] = {
      "ALL", "LAND", "PLAINS", "RIDGES", "MASK", "WARP X", "WARP Y"
    };

    UiRect window_rect ()
    { return { window_x, window_y, window_width, window_height }; }

    UiRect readings_rect ()
    { return { readings_x, readings_y, readings_width, readings_height }; }

    bool ui_contains (float x, float y) {
      return window_rect ().contains (x, y)
	|| readings_rect ().contains (x, y);
    }

    UiRect overlay_rect (int index) {
      constexpr float gap = 4.0f;
      constexpr float margin = 10.0f;
      constexpr float width = (readings_width - 2 * margin - 3 * gap) / 4;
      const int row = index / 4;
      const int column = index % 4;
      return { readings_x + margin + column * (width + gap),
	       readings_y + 42 + row * 34, width, 28 };
    }

    UiRect close_rect ()
    { return { 505, 20, 22, 22 }; }

    UiRect reset_rect ()
    { return { left_x, 52, 84, 28 }; }

    UiRect seed_rect ()
    { return { left_x + 90, 52, 128, 28 }; }

    UiRect fit_rect ()
    { return { 252, 52, 72, 28 }; }

    UiRect view_rect ()
    { return { 330, 52, 112, 28 }; }

    UiRect hydraulic_preset_rect (int group, int index) {
      const float gap = 4.0f;
      const float width = (right_width - 3 * gap) / 4.0f;
      return { right_x + index * (width + gap),
	       378.0f + group * 56.0f, width, 28 };
    }

    UiRect layer_rect (int index) {
      const float gap = 3.0f;
      const float width = (492.0f - 6 * gap) / 7.0f;
      return { left_x + index * (width + gap), 89, width, 29 };
    }

    UiRect pipeline_header_rect ()
    { return { left_x, 128, left_width, 24 }; }

    UiRect property_header_rect ()
    { return { right_x, 128, right_width, 24 }; }

    UiRect source_rect ()
    { return { left_x, 157, left_width, stage_row_height }; }

    UiRect stage_rect (int visible_index) {
      return { left_x, 201 + visible_index * stage_row_stride,
	       left_width, stage_row_height };
    }

    UiRect stage_list_rect () {
      return { left_x, 201, left_width,
	       visible_stage_rows * stage_row_stride };
    }

    UiRect add_stage_rect (int index) {
      const float gap = 3.0f;
      const float width = (left_width - 4 * gap) / 5.0f;
      return { left_x + index * (width + gap), 497, width, 29 };
    }

    UiRect edit_stage_rect (int index) {
      const float gap = 3.0f;
      const float width = (left_width - 3 * gap) / 4.0f;
      return { left_x + index * (width + gap), 532, width, 29 };
    }

    UiRect property_rect (int index) {
      return { right_x, 157 + index * 41.0f,
	       right_width, 39.0f };
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
      if (std::holds_alternative<terrain::HydraulicErosion> (stage))
	return "WATER EROSION";
      return "TALUS RELAX";
    }

    std::string stage_detail (const terrain::TerrainTransform& stage) {
      if (std::holds_alternative<terrain::NormalizeHeights> (stage))
	return "map sampled range to 0..1";
      if (const auto* power =
	  std::get_if<terrain::PowerHeights> (&stage))
	return "height ^ " + format_float (power->exponent, 2);
      if (const auto* analytical =
	  std::get_if<terrain::AnalyticalErosion> (&stage))
	return format_float (analytical->time_years / 1000.0f, 0)
	  + " ky / " + std::to_string (analytical->fixed_point_iterations)
	  + " routing passes";
      if (const auto* hydraulic =
	  std::get_if<terrain::HydraulicErosion> (&stage))
	return format_count (hydraulic->droplets) + " drops / "
	  + format_count (hydraulic->batch_size) + " batch / "
	  + format_count (hydraulic->max_steps) + " steps";
      const auto& thermal = std::get<terrain::ThermalErosion> (stage);
      return std::to_string (thermal.iterations) + " passes @ "
	+ format_float (thermal.talus, 4);
    }

    std::string semantics_detail
      (const terrain::TerrainTransform& transform) {
      const terrain::TransformSemantics semantics =
	terrain::terrain_transform_semantics (transform);
      const char* spatial = semantics.spatial_scope
	== terrain::SpatialScope::Pointwise ? "POINTWISE"
	: semantics.spatial_scope == terrain::SpatialScope::Neighborhood
	? "NEIGHBORHOOD" : "GLOBAL";
      const char* order = semantics.evaluation_order
	== terrain::EvaluationOrder::Direct ? "DIRECT"
	: semantics.evaluation_order == terrain::EvaluationOrder::Reduction
	? "REDUCTION" : "ITERATIVE";
      return std::string (spatial) + " / " + order;
    }

    struct PropertyText {
      std::string label;
      std::string value;
      ParameterDomain domain;
    };

    PropertyText recipe_property
      (const terrain::GeologicalRecipe& recipe, int row) {
      switch (row) {
      case 0: return { "WARP STRENGTH",
	format_float (recipe.warp.amplitude, 3), ParameterDomain::Continuous };
      case 1: return { "CONTINENT WAVES",
	std::to_string (recipe.continent.noise.cycles), ParameterDomain::Natural };
      case 2: return { "PLAINS WAVES",
	std::to_string (recipe.plains.noise.cycles), ParameterDomain::Natural };
      case 3: return { "RIDGE WAVES",
	std::to_string (recipe.mountains.cycles), ParameterDomain::Natural };
      case 4: return { "MASK START",
	format_float (recipe.blend.mask_low, 3), ParameterDomain::Continuous };
      case 5: return { "MASK END",
	format_float (recipe.blend.mask_high, 3), ParameterDomain::Continuous };
      case 6: return { "CONTINENT MIX",
	format_float (recipe.blend.continent_weight, 2),
	ParameterDomain::Continuous };
      case 7: return { "PLAINS MIX",
	format_float (recipe.blend.plains_weight, 2),
	ParameterDomain::Continuous };
      default: return { "MOUNTAIN MIX",
	format_float (recipe.blend.mountain_weight, 2),
	ParameterDomain::Continuous };
      }
    }

    int stage_property_count (const terrain::TerrainTransform& stage) {
      if (std::holds_alternative<terrain::PowerHeights> (stage))
	return 1;
      if (std::holds_alternative<terrain::AnalyticalErosion> (stage))
	return 6;
      if (std::holds_alternative<terrain::HydraulicErosion> (stage))
	return 3;
      if (std::holds_alternative<terrain::ThermalErosion> (stage))
	return 2;
      return 0;
    }

    PropertyText stage_property
      (const terrain::TerrainTransform& stage, int row) {
      if (const auto* power =
	  std::get_if<terrain::PowerHeights> (&stage))
	return { "EXPONENT", format_float (power->exponent, 2),
	  ParameterDomain::Continuous };
      if (const auto* analytical =
	  std::get_if<terrain::AnalyticalErosion> (&stage)) {
	if (row == 0)
	  return { "AGE (KY)",
	    format_float (analytical->time_years / 1000.0f, 0),
	    ParameterDomain::Continuous };
	if (row == 1)
	  return { "UPLIFT (MM/Y)",
	    format_float (analytical->uplift_m_per_year * 1000.0f, 2),
	    ParameterDomain::Continuous };
	if (row == 2)
	  return { "ERODIBILITY", format_ledger (analytical->erodibility),
	    ParameterDomain::Continuous };
	if (row == 3)
	  return { "AREA EXPONENT",
	    format_float (analytical->area_exponent, 2),
	    ParameterDomain::Continuous };
	if (row == 4)
	  return { "ROUTING PASSES",
	    std::to_string (analytical->fixed_point_iterations),
	    ParameterDomain::Natural };
	return { "RELAXATION", format_float (analytical->relaxation, 2),
	  ParameterDomain::Continuous };
      }
      if (const auto* hydraulic =
	  std::get_if<terrain::HydraulicErosion> (&stage)) {
	if (row == 0)
	  return { "DROPLETS", format_count (hydraulic->droplets),
	    ParameterDomain::Natural };
	if (row == 1)
	  return { "BATCH SIZE", format_count (hydraulic->batch_size),
	    ParameterDomain::Natural };
	return { "MAX STEPS", format_count (hydraulic->max_steps),
	  ParameterDomain::Natural };
      }
      const auto& thermal = std::get<terrain::ThermalErosion> (stage);
      if (row == 0)
	return { "PASSES", std::to_string (thermal.iterations),
	  ParameterDomain::Natural };
      return { "TALUS", format_float (thermal.talus, 4),
	ParameterDomain::Continuous };
    }
  }

  TerrainLab::TerrainLab ()
    : m_renderer (0), m_map (0), m_terrain (0), m_world (0),
      m_active (false),
      m_program (terrain::make_geological_program (0)),
      m_overlay (OverlayMode::None),
      m_selected_stage (-1), m_stage_scroll (0),
      m_pointer_x (0), m_pointer_y (0),
      m_pointer_down (false), m_camera_drag (false),
      m_camera_drag_distance (0.0f), m_pan_drag (false),
      m_parameter_drag (false), m_drag_property (-1),
      m_drag_start_y (0.0f), m_drag_start_normalized (0.0f),
      m_parameter_rebuild_pending (false),
      m_parameter_rebuild_stage (-1), m_parameter_rebuild_delay (0.0f),
      m_target (),
      m_yaw (0.72f), m_pitch (0.62f), m_distance (5600.0f),
      m_orbit_left (false), m_orbit_right (false),
      m_zoom_in (false), m_zoom_out (false),
      m_tilt_up (false), m_tilt_down (false),
      m_scroll_zoom_target (5600.0f),
      m_view (ViewMode::Cover)
  { }

  void
  TerrainLab::load (render::Renderer& renderer)
  {
    m_ui.load (renderer);
  }

  void
  TerrainLab::enter (render::Renderer& renderer,
		     map::RandomHeightMap& map, Terrain& terrain,
		     const WorldParams& world, int seed,
		     const Vector3D& sun_dir)
  {
    if (m_active)
      return;

    m_renderer = &renderer;
    m_map = &map;
    if (!m_source_evaluator)
      m_source_evaluator = platform::create_field_evaluator ();
    m_evaluator = std::make_unique<map::TerrainEvaluator>
      (map, m_source_evaluator.get ());
    m_terrain = &terrain;
    m_world = &world;
    m_sun_dir = sun_dir;
    m_program = terrain::make_geological_program
      (static_cast<std::uint32_t> (seed));
    if (const char* erosion = std::getenv ("MOPPE_LAB_EROSION")) {
      std::istringstream input (erosion);
      std::string part;
      int values[] = { 100000, 256, 512 };
      for (int i = 0; i < 3 && std::getline (input, part, ','); ++i)
	values[i] = std::stoi (part);
      m_program.transforms.emplace_back (terrain::HydraulicErosion {
	.droplets = values[0],
	.batch_size = values[1],
	.max_steps = values[2],
	.minimum_water = 0.01f,
	.sediment_at_termination = terrain::SedimentDisposition::Deposit
      });
    }
    if (std::getenv ("MOPPE_LAB_ANALYTICAL"))
      m_program.transforms.emplace_back (terrain::AnalyticalErosion { });
    m_selected_stage = -1;
    m_stage_scroll = 0;
    m_pointer_down = false;
    m_camera_drag = false;
    m_pan_drag = false;
    m_parameter_drag = false;
    m_drag_property = -1;
    m_parameter_rebuild_pending = false;
    m_parameter_rebuild_delay = 0.0f;
    m_overlay = OverlayMode::None;
    if (const char* overlay = std::getenv ("MOPPE_LAB_OVERLAY")) {
      const std::string name (overlay);
      if (name == "height") m_overlay = OverlayMode::Height;
      else if (name == "slope") m_overlay = OverlayMode::Slope;
      else if (name == "flow") m_overlay = OverlayMode::Flow;
      else if (name == "streams") m_overlay = OverlayMode::Streams;
      else if (name == "basins") m_overlay = OverlayMode::Basins;
      else if (name == "sinks") m_overlay = OverlayMode::Sinks;
      else if (name == "delta") m_overlay = OverlayMode::HeightDelta;
      else if (name == "trace") m_overlay = OverlayMode::Trace;
      else if (name == "water")
	m_overlay = OverlayMode::StandingWater;
      else if (name == "lakes") m_overlay = OverlayMode::PermanentWater;
    }
    m_drainage.reset ();
    m_water_network.reset ();
    m_rivers.reset ();
    m_river_surface.clear ();
    m_flood.reset ();
    m_lakes.reset ();
    m_inspected_cell.reset ();
    m_overlay_status = "NO READING — terrain materials";
    m_view = ViewMode::Cover;
    m_orbit_left = m_orbit_right = false;
    m_zoom_in = m_zoom_out = false;
    m_tilt_up = m_tilt_down = false;
    fit_view ();

    const size_t count = (size_t) map.width () * map.height ();
    m_saved_heights.assign (map.raw_heights (),
			    map.raw_heights () + count);
    m_active = true;
    rebuild_program ();
    if (std::getenv ("MOPPE_LAB_RIVERS"))
      drainage ();
    if (const char* stage = std::getenv ("MOPPE_LAB_STAGE")) {
      const int selected = std::atoi (stage);
      if (selected >= 0
	  && selected < static_cast<int> (m_program.transforms.size ())) {
	m_selected_stage = selected;
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

  void
  TerrainLab::leave ()
  {
    if (!m_active)
      return;
    m_overlay = OverlayMode::None;
    if (m_renderer)
      m_renderer->clear_terrain_overlay ();
    restore_game_map ();
    m_saved_heights.clear ();
    m_saved_heights.shrink_to_fit ();
    m_checkpoints.clear ();
    m_checkpoints.shrink_to_fit ();
    m_reports.clear ();
    m_reports.shrink_to_fit ();
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
    m_evaluator.reset ();
    m_map = 0;
    m_terrain = 0;
    m_world = 0;
  }

  void
  TerrainLab::fit_view ()
  {
    if (!m_world)
      return;
    m_target = Vector3D
      (m_world->map_size.x * 0.5f,
	 m_view == ViewMode::Torus
	 ? 0.0f : m_world->map_size.y * 0.10f,
	 m_world->map_size.z * 0.5f);
    m_yaw = 0.72f;
    if (m_view == ViewMode::Torus) {
      m_pitch = 0.48f;
      m_distance = 4700.0f;
    } else if (m_view == ViewMode::Cover) {
      m_pitch = 0.88f;
      m_distance = 7200.0f;
    } else {
      m_pitch = 1.22f;
      m_distance = 6500.0f;
    }
    m_scroll_zoom_target = m_distance;
  }

  void
  TerrainLab::cycle_view ()
  {
    if (m_view == ViewMode::Tile)
      m_view = ViewMode::Cover;
    else if (m_view == ViewMode::Cover)
      m_view = ViewMode::Torus;
    else
      m_view = ViewMode::Tile;
    fit_view ();
    refresh ();
  }

  void
  TerrainLab::restore_game_map ()
  {
    if (!m_map || m_saved_heights.empty ())
      return;
    size_t i = 0;
    for (int y = 0; y < m_map->height (); ++y)
      for (int x = 0; x < m_map->width (); ++x)
	m_map->set (x, y, m_saved_heights[i++]);
    refresh (false);
  }

  void
  TerrainLab::select (terrain::GeologicalLayer layer)
  {
    if (layer == m_program.source.layer)
      return;
    m_program.source.layer = layer;
    m_selected_stage = -1;
    rebuild_program ();
  }

  void
  TerrainLab::reset_program ()
  {
    m_program = terrain::make_geological_program
      (m_program.randomness.seed, m_program.source.layer);
    m_selected_stage = -1;
    m_stage_scroll = 0;
    rebuild_program ();
  }

  void
  TerrainLab::rebuild_program ()
  {
    if (!m_evaluator)
      return;
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

  void
  TerrainLab::rerun_program_from (int first_stage)
  {
    const int stage_count =
      static_cast<int> (m_program.transforms.size ());
    if (!m_evaluator || first_stage < 0 || first_stage > stage_count
	|| first_stage > static_cast<int> (m_checkpoints.size ())) {
      rebuild_program ();
      return;
    }

    if (first_stage < static_cast<int> (m_checkpoints.size ()))
      m_evaluator->restore (m_checkpoints[first_stage]);

    // Appending a stage starts from the current final map.  Other edits start
    // from their saved input, and discard only the now-invalid suffix.
    const std::size_t retained = std::min
      (static_cast<std::size_t> (stage_count),
	static_cast<std::size_t> (first_stage + 1));
    if (m_checkpoints.size () > retained)
      m_checkpoints.resize (retained);
    if (m_reports.size () > static_cast<std::size_t> (first_stage))
      m_reports.resize (static_cast<std::size_t> (first_stage));
    for (std::size_t i = static_cast<std::size_t> (first_stage);
	 i < m_program.transforms.size (); ++i) {
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

  void
  TerrainLab::invalidate_analysis ()
  {
    m_drainage.reset ();
    m_water_network.reset ();
    m_rivers.reset ();
    m_river_surface.clear ();
    m_flood.reset ();
    m_lakes.reset ();
    m_inspected_cell.reset ();
  }

  const terrain::FloodField&
  TerrainLab::standing_water ()
  {
    if (!m_map || !m_world)
      throw std::logic_error ("standing water requested without terrain");
    if (!m_flood) {
      const auto start = std::chrono::steady_clock::now ();
      const float sea_level = m_world->water_level / m_world->map_size.y;
      m_flood = terrain::analyze_standing_water
	(m_map->terrain_view (), sea_level);
      m_lakes = terrain::census_lakes (*m_flood);
      const double milliseconds = std::chrono::duration<double, std::milli>
	(std::chrono::steady_clock::now () - start).count ();
      std::size_t wet_cells = 0;
      float maximum_depth = 0.0f;
      for (const float depth : m_flood->water_depth.values ()) {
	if (depth > 1e-7f)
	  ++wet_cells;
	maximum_depth = std::max (maximum_depth, depth);
      }
      std::ostringstream status;
      status << format_count (static_cast<int> (wet_cells))
	     << " wet cells | " << std::fixed << std::setprecision (1)
	     << maximum_depth * m_world->map_size.y << "m max | "
	     << std::setprecision (0) << milliseconds << "ms flood";
      m_flood_status = status.str ();

      int puddles = 0, ponds = 0, lakes = 0;
      for (const terrain::WaterBody& body : m_lakes->bodies)
	switch (body.classification) {
	case terrain::WaterBodyClass::Puddle: ++puddles; break;
	case terrain::WaterBodyClass::Pond: ++ponds; break;
	case terrain::WaterBodyClass::Lake: ++lakes; break;
	case terrain::WaterBodyClass::Sea: break;
	}
      std::ostringstream census_status;
      census_status << format_count (puddles) << " puddles, "
		    << format_count (ponds) << " ponds, "
		    << format_count (lakes) << " lakes";
      m_census_status = census_status.str ();
    }
    return *m_flood;
  }

  const terrain::DrainageGraph&
  TerrainLab::drainage ()
  {
    if (!m_map)
      throw std::logic_error ("drainage requested without terrain");
    if (!m_drainage) {
      const auto start = std::chrono::steady_clock::now ();
      m_drainage = terrain::analyze_wet_drainage
	(m_map->terrain_view (), standing_water (), *m_lakes);
      m_water_network = terrain::analyze_water_network
	(*m_flood, *m_lakes, *m_drainage);
      m_rivers = terrain::extract_river_network
	(*m_flood, *m_lakes, *m_drainage,
	 visible_river_minimum_area (m_drainage->source_grid));
      m_river_surface.rebuild
	(*m_renderer, *m_map, *m_flood, *m_lakes, *m_drainage, *m_rivers);
      const double milliseconds = std::chrono::duration<double, std::milli>
	(std::chrono::steady_clock::now () - start).count ();
      std::ostringstream status;
      std::size_t inlets = 0;
      for (const terrain::WaterBodyFlow& flow : m_water_network->bodies)
	inlets += flow.inlets.size ();
      status << m_drainage->sinks.size () << " outlets, "
	     << format_count (static_cast<int> (inlets))
	     << " entries, " << m_rivers->reaches.size () << " reaches | "
	     << std::fixed << std::setprecision (0) << milliseconds
	     << " ms analysis";
      m_analysis_status = status.str ();
    }
    return *m_drainage;
  }

  void
  TerrainLab::set_overlay (OverlayMode mode)
  {
    if (m_overlay == mode)
      return;
    m_overlay = mode;
    update_overlay ();
  }

  void
  TerrainLab::render_rivers (render::Renderer& renderer,
			     const Vector3D& camera) const
  {
    m_river_surface.draw (renderer, camera);
  }

  void
  TerrainLab::inspect_drainage (float x, float y)
  {
    if (!m_map || !m_renderer || m_view == ViewMode::Torus) {
      m_overlay_status = "TRACE — use Tile or Cover view";
      return;
    }
    const float width = static_cast<float> (m_renderer->width_pts ());
    const float height = static_cast<float> (m_renderer->height_pts ());
    const float aspect = width / std::max (1.0f, height);
    const float tangent = std::tan (degrees_to_radians (70.0f) * 0.5f);
    const float screen_x = 2.0f * x / width - 1.0f;
    const float screen_y = 1.0f - 2.0f * y / height;
    const Vector3D direction_forward = forward ();
    const Vector3D direction_right = direction_forward
      .cross (Vector3D (0, 1, 0)).normalized ();
    const Vector3D direction_up = direction_right
      .cross (direction_forward).normalized ();
    const Vector3D direction =
      (direction_forward + direction_right * (screen_x * aspect * tangent)
	+ direction_up * (screen_y * tangent)).normalized ();
    const Vector3D origin = position ();

    float previous_t = 0.0f;
    float previous_clearance = origin.y
      - m_map->interpolated_height (origin.x, origin.z);
    bool hit = false;
    float hit_t = 0.0f;
    for (float t = 20.0f; t <= 14000.0f; t += 20.0f) {
      const Vector3D point = origin + direction * t;
      const float clearance = point.y
	- m_map->interpolated_height (point.x, point.z);
      if (previous_clearance > 0.0f && clearance <= 0.0f) {
	float low = previous_t, high = t;
	for (int i = 0; i < 10; ++i) {
	  const float middle = 0.5f * (low + high);
	  const Vector3D candidate = origin + direction * middle;
	  if (candidate.y > m_map->interpolated_height
	      (candidate.x, candidate.z))
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

    const Vector3D point = origin + direction * hit_t;
    const Vector3D period = m_map->size ();
    const Vector3D scale = m_map->scale ();
    const auto wrap = [] (float value, float size) {
      value = std::fmod (value, size);
      return value < 0.0f ? value + size : value;
    };
    const std::size_t grid_x = static_cast<std::size_t>
      (std::floor (wrap (point.x, period.x) / scale.x))
      % static_cast<std::size_t> (m_map->unique_width ());
    const std::size_t grid_y = static_cast<std::size_t>
      (std::floor (wrap (point.z, period.z) / scale.z))
      % static_cast<std::size_t> (m_map->unique_height ());
    m_inspected_cell = static_cast<std::uint32_t>
      (grid_y * m_map->unique_width () + grid_x);
    update_overlay ();
  }

  void
  TerrainLab::update_overlay ()
  {
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
      .width = width,
      .height = height,
      .minimum = 0.0f,
      .maximum = 1.0f
    };

    if (m_overlay == OverlayMode::Height) {
      std::copy_n (m_map->raw_heights (), count, values.begin ());
      const auto [minimum, maximum] = std::minmax_element
	(values.begin (), values.end ());
      params.minimum = *minimum;
      params.maximum = *maximum;
      params.ramp = render::TerrainOverlayRamp::Heat;
      m_overlay_status = "HEIGHT — normalized elevation";
    } else if (m_overlay == OverlayMode::HeightDelta) {
      if (m_selected_stage < 0
	  || m_selected_stage >= static_cast<int> (m_checkpoints.size ())) {
	m_renderer->clear_terrain_overlay ();
	m_overlay_status = "DELTA — select a pipeline stage";
	return;
      }
      const std::vector<float>& before =
	m_checkpoints[static_cast<std::size_t> (m_selected_stage)].heights;
      const float* after = m_selected_stage + 1
	< static_cast<int> (m_checkpoints.size ())
	? m_checkpoints[static_cast<std::size_t> (m_selected_stage + 1)]
	    .heights.data () : m_map->raw_heights ();
      float magnitude = 0.0f;
      for (std::size_t i = 0; i < count; ++i) {
	values[i] = after[i] - before[i];
	magnitude = std::max (magnitude, std::fabs (values[i]));
      }
      params.minimum = -magnitude;
      params.maximum = magnitude;
      params.ramp = render::TerrainOverlayRamp::Diverging;
      m_overlay_status = "DELTA — blue removal / orange addition";
    } else if (m_overlay == OverlayMode::StandingWater
	       || m_overlay == OverlayMode::PermanentWater) {
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
	unique[i] = level[i]
	  - (flood.water_level.values ()[i] - ground[i]);
      const float maximum = *std::max_element (unique.begin (), unique.end ());
      params.maximum = maximum > 0.0f ? maximum : 1.0f;
      params.ramp = render::TerrainOverlayRamp::Water;
      params.opacity = 0.88f;
      m_overlay_status = m_overlay == OverlayMode::PermanentWater
	? "LAKES — permanent water census | " + m_census_status
	: "WATER — every standing depth w - z | " + m_flood_status;
      for (int y = 0; y < height; ++y)
	for (int x = 0; x < width; ++x)
	  values[static_cast<std::size_t> (y) * width + x] = unique
	    [(static_cast<std::size_t> (y) % unique_height) * unique_width
	     + static_cast<std::size_t> (x) % unique_width];
    } else {
      const terrain::DrainageGraph& graph = drainage ();
      const std::size_t unique_width = graph.width ();
      const std::size_t unique_height = graph.height ();
      std::vector<float> unique (unique_width * unique_height, 0.0f);
      if (m_overlay == OverlayMode::Slope) {
	std::copy (graph.slope.values ().begin (), graph.slope.values ().end (),
		   unique.begin ());
	params.maximum = *std::max_element (unique.begin (), unique.end ());
	params.ramp = render::TerrainOverlayRamp::Heat;
	m_overlay_status = "SLOPE — physical rise / run | " + m_analysis_status;
      } else if (m_overlay == OverlayMode::Flow
		 || m_overlay == OverlayMode::Streams) {
	const float cell_area = graph.source_grid.spacing_x
	  * graph.source_grid.spacing_y;
	float maximum = 0.0f;
	if (m_overlay == OverlayMode::Flow) {
	  for (std::size_t i = 0; i < unique.size (); ++i) {
	    unique[i] = std::log2
	      (std::max (1.0f, graph.contributing_area.values ()[i]
				 / cell_area));
	    maximum = std::max (maximum, unique[i]);
	  }
	} else {
	  for (const terrain::RiverReach& reach : m_rivers->reaches)
	    for (const std::uint32_t cell : reach.cells) {
	      unique[cell] = std::log2
		(std::max (1.0f, graph.contributing_area.values ()[cell]
				 / cell_area));
	      maximum = std::max (maximum, unique[cell]);
	    }
	}
	params.minimum = m_overlay == OverlayMode::Streams ? 6.0f : 0.0f;
	params.maximum = maximum;
	params.ramp = m_overlay == OverlayMode::Streams
	  ? render::TerrainOverlayRamp::Streams
	  : render::TerrainOverlayRamp::Flow;
	m_overlay_status = (m_overlay == OverlayMode::Streams
	  ? "STREAMS — dry reaches >= 64 cells | "
	  : "FLOW — logarithmic contributing area | ") + m_analysis_status;
      } else if (m_overlay == OverlayMode::Basins) {
	for (std::size_t i = 0; i < unique.size (); ++i)
	  unique[i] = static_cast<float> (graph.basin[i]);
	params.ramp = render::TerrainOverlayRamp::Categorical;
	params.opacity = 0.40f;
	m_overlay_status = "BASINS — shared outlet catchments | "
	  + m_analysis_status;
      } else if (m_overlay == OverlayMode::Sinks) {
	for (const std::uint32_t sink : graph.sinks) {
	  const int sx = static_cast<int> (sink % unique_width);
	  const int sy = static_cast<int> (sink / unique_width);
	  for (int dy = -2; dy <= 2; ++dy)
	    for (int dx = -2; dx <= 2; ++dx) {
	      const std::size_t x = static_cast<std::size_t>
		((sx + dx + static_cast<int> (unique_width))
		 % static_cast<int> (unique_width));
	      const std::size_t y = static_cast<std::size_t>
		((sy + dy + static_cast<int> (unique_height))
		 % static_cast<int> (unique_height));
	      unique[y * unique_width + x] =
		std::max (unique[y * unique_width + x],
			  1.0f - 0.18f * static_cast<float>
			    (std::hypot (dx, dy)));
	    }
	}
	params.ramp = render::TerrainOverlayRamp::Marker;
	params.opacity = 0.95f;
	m_overlay_status = "OUTLETS — terminal wet routes | "
	  + m_analysis_status;
      } else {
	if (!m_inspected_cell || *m_inspected_cell >= unique.size ()) {
	  m_renderer->clear_terrain_overlay ();
	  m_overlay_status = "TRACE — click terrain to follow its receiver path";
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
	      const std::size_t px = static_cast<std::size_t>
		((cx + dx + static_cast<int> (unique_width))
		 % static_cast<int> (unique_width));
	      const std::size_t py = static_cast<std::size_t>
		((cy + dy + static_cast<int> (unique_height))
		 % static_cast<int> (unique_height));
	      unique[py * unique_width + px] = 1.0f;
	    }
	  const std::uint32_t next = graph.receiver[cell];
	  if (next == cell)
	    break;
	  cell = next;
	}
	params.ramp = render::TerrainOverlayRamp::Marker;
	params.opacity = 0.98f;
	const float area = graph.contributing_area.values ()[*m_inspected_cell];
	std::ostringstream trace;
	const std::uint32_t body_id =
	  m_lakes->body[*m_inspected_cell];
	if (body_id != terrain::LakeCensus::dry) {
	  const terrain::WaterBody& body = m_lakes->bodies[body_id];
	  const terrain::WaterBodyFlow& flow =
	    m_water_network->bodies[body_id];
	  trace << "TRACE — " << water_body_class_name (body.classification)
		<< " #" << body.id << " | " << flow.inlets.size ()
		<< " inlets | mean " << std::fixed << std::setprecision (1)
		<< body.mean_depth_m << "m | catchment "
		<< std::setprecision (0)
		<< (body.ocean_connected ? flow.inflow_area_m2
		    : flow.outflow_area_m2) << " m2";
	} else if (m_rivers->reach_by_cell[*m_inspected_cell]
		   != terrain::RiverReach::no_id) {
	  const terrain::RiverReach& reach = m_rivers->reaches
	    [m_rivers->reach_by_cell[*m_inspected_cell]];
	  trace << "TRACE — REACH #" << reach.id << " | "
		<< reach.cells.size () << " cells | area " << std::fixed
		<< std::setprecision (0) << reach.downstream_area_m2
		<< " m2 | slope " << std::setprecision (3)
		<< reach.maximum_slope;
	  if (reach.downstream_ocean)
	    trace << " | to ocean";
	  else if (reach.downstream_body != terrain::RiverReach::no_id)
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
	  values[static_cast<std::size_t> (y) * width + x] = unique
	    [(static_cast<std::size_t> (y) % unique_height) * unique_width
	     + static_cast<std::size_t> (x) % unique_width];
    }
    m_renderer->set_terrain_overlay (params, values);
  }

  void
  TerrainLab::append_stage (terrain::TerrainTransform stage)
  {
    const int first_stage = static_cast<int> (m_program.transforms.size ());
    m_program.transforms.push_back (std::move (stage));
    m_selected_stage = static_cast<int> (m_program.transforms.size ()) - 1;
    ensure_selected_stage_visible ();
    rerun_program_from (first_stage);
  }

  void
  TerrainLab::move_selected_stage (int direction)
  {
    const int first_stage = std::min
      (m_selected_stage, m_selected_stage + direction);
    const int target = m_selected_stage + direction;
    if (m_selected_stage < 0 || target < 0
	|| target >= static_cast<int> (m_program.transforms.size ()))
      return;
    std::swap (m_program.transforms[m_selected_stage],
	       m_program.transforms[target]);
    m_selected_stage = target;
    ensure_selected_stage_visible ();
    rerun_program_from (first_stage);
  }

  void
  TerrainLab::duplicate_selected_stage ()
  {
    if (m_selected_stage < 0
	|| m_selected_stage >= static_cast<int> (m_program.transforms.size ()))
      return;
    const int first_stage = m_selected_stage + 1;
    const auto position = m_program.transforms.begin () + first_stage;
    m_program.transforms.insert
      (position, m_program.transforms[m_selected_stage]);
    ++m_selected_stage;
    ensure_selected_stage_visible ();
    rerun_program_from (first_stage);
  }

  void
  TerrainLab::remove_selected_stage ()
  {
    if (m_selected_stage < 0
	|| m_selected_stage >= static_cast<int> (m_program.transforms.size ()))
      return;
    const int first_stage = m_selected_stage;
    m_program.transforms.erase
      (m_program.transforms.begin () + m_selected_stage);
    if (m_program.transforms.empty ())
      m_selected_stage = -1;
    else
      m_selected_stage = std::min
	(m_selected_stage, static_cast<int> (m_program.transforms.size ()) - 1);
    ensure_selected_stage_visible ();
    rerun_program_from (first_stage);
  }

  void
  TerrainLab::ensure_selected_stage_visible ()
  {
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

  float
  TerrainLab::selected_property_normalized (int row) const
  {
    const auto unit = [] (float value, float low, float high) {
      return high > low ? std::clamp
	((value - low) / (high - low), 0.0f, 1.0f) : 0.0f;
    };
    if (m_selected_stage < 0) {
      const terrain::GeologicalRecipe& recipe = m_program.source.recipe;
      switch (row) {
      case 0: return unit (recipe.warp.amplitude, 0.0f, 0.6f);
      case 1: return unit (recipe.continent.noise.cycles, 1.0f, 16.0f);
      case 2: return unit (recipe.plains.noise.cycles, 1.0f, 32.0f);
      case 3: return unit (recipe.mountains.cycles, 1.0f, 8.0f);
      case 4: return unit
	(recipe.blend.mask_low, 0.0f, recipe.blend.mask_high - 0.001f);
      case 5: return unit
	(recipe.blend.mask_high, recipe.blend.mask_low + 0.001f, 1.0f);
      case 6: return unit (recipe.blend.continent_weight, 0.0f, 1.5f);
      case 7: return unit (recipe.blend.plains_weight, 0.0f, 1.0f);
      case 8: return unit (recipe.blend.mountain_weight, 0.0f, 1.5f);
      default: return 0.0f;
      }
    }

    const terrain::TerrainTransform& stage =
      m_program.transforms[m_selected_stage];
    if (const auto* power = std::get_if<terrain::PowerHeights> (&stage))
      return unit (power->exponent, 0.1f, 4.0f);
    if (const auto* analytical =
	std::get_if<terrain::AnalyticalErosion> (&stage)) {
      if (row == 0)
	return unit (analytical->time_years, 0.0f, 1600000.0f);
      if (row == 1)
	return unit (analytical->uplift_m_per_year, 0.0f, 0.003f);
      if (row == 2)
	return unit (std::log10 (analytical->erodibility), -6.0f, -3.0f);
      if (row == 3)
	return unit (analytical->area_exponent, 0.0f, 1.0f);
      if (row == 4)
	return unit (analytical->fixed_point_iterations, 1.0f, 12.0f);
      return unit (analytical->relaxation, 0.1f, 1.0f);
    }
    if (const auto* hydraulic =
	std::get_if<terrain::HydraulicErosion> (&stage))
      return row == 0 ? unit (hydraulic->droplets, 0.0f, 5000000.0f)
	: row == 1 ? unit (hydraulic->batch_size, 1.0f, 4096.0f)
	: unit (hydraulic->max_steps, 1.0f, 2048.0f);
    if (const auto* thermal =
	std::get_if<terrain::ThermalErosion> (&stage))
      return row == 0 ? unit (thermal->iterations, 0.0f, 20.0f)
	: unit (thermal->talus, 0.0f, 0.05f);
    return 0.0f;
  }

  ParameterDomain
  TerrainLab::selected_property_domain (int row) const
  {
    if (m_selected_stage < 0)
      return recipe_property (m_program.source.recipe, row).domain;
    return stage_property
      (m_program.transforms[m_selected_stage], row).domain;
  }

  bool
  TerrainLab::set_selected_property_normalized (int row, float value)
  {
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
	recipe.blend.mask_low = mix
	  (0.0f, recipe.blend.mask_high - 0.001f);
	return recipe.blend.mask_low != old;
      }
      case 5: {
	const float old = recipe.blend.mask_high;
	recipe.blend.mask_high = mix
	  (recipe.blend.mask_low + 0.001f, 1.0f);
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
      default: return false;
      }
    }

    terrain::TerrainTransform& stage =
      m_program.transforms[m_selected_stage];
    if (auto* power = std::get_if<terrain::PowerHeights> (&stage)) {
      const float old = power->exponent;
      power->exponent = mix (0.1f, 4.0f);
      return power->exponent != old;
    }
    if (auto* analytical =
	std::get_if<terrain::AnalyticalErosion> (&stage)) {
      if (row == 0) {
	const float old = analytical->time_years;
	analytical->time_years = mix (0.0f, 1600000.0f);
	return analytical->time_years != old;
      }
      if (row == 1) {
	const float old = analytical->uplift_m_per_year;
	analytical->uplift_m_per_year = mix (0.0f, 0.003f);
	return analytical->uplift_m_per_year != old;
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
    if (auto* hydraulic =
	std::get_if<terrain::HydraulicErosion> (&stage)) {
      if (row == 0) {
	const int old = hydraulic->droplets;
	hydraulic->droplets = std::lround (mix (0.0f, 5000000.0f));
	return hydraulic->droplets != old;
      }
      if (row == 1) {
	const int old = hydraulic->batch_size;
	hydraulic->batch_size = std::lround (mix (1.0f, 4096.0f));
	return hydraulic->batch_size != old;
      }
      const int old = hydraulic->max_steps;
      hydraulic->max_steps = std::lround (mix (1.0f, 2048.0f));
      return hydraulic->max_steps != old;
    }
    if (auto* thermal = std::get_if<terrain::ThermalErosion> (&stage)) {
      if (row == 0) {
	const int old = thermal->iterations;
	thermal->iterations = std::lround (mix (0.0f, 20.0f));
	return thermal->iterations != old;
      }
      const float old = thermal->talus;
      thermal->talus = mix (0.0f, 0.05f);
      return thermal->talus != old;
    }
    return false;
  }

  bool
  TerrainLab::adjust_selected_natural (int row, int direction)
  {
    if (direction == 0
	|| selected_property_domain (row) != ParameterDomain::Natural)
      return false;
    if (m_selected_stage < 0) {
      terrain::GeologicalRecipe& recipe = m_program.source.recipe;
      int* value = row == 1 ? &recipe.continent.noise.cycles
	: row == 2 ? &recipe.plains.noise.cycles
	: &recipe.mountains.cycles;
      const int maximum = row == 1 ? 16 : row == 2 ? 32 : 8;
      const int changed = std::clamp (*value + direction, 1, maximum);
      if (changed == *value)
	return false;
      *value = changed;
      return true;
    }

    terrain::TerrainTransform& stage =
      m_program.transforms[m_selected_stage];
    if (auto* analytical =
	std::get_if<terrain::AnalyticalErosion> (&stage)) {
      const int changed = std::clamp
	(analytical->fixed_point_iterations + direction, 1, 12);
      if (changed == analytical->fixed_point_iterations)
	return false;
      analytical->fixed_point_iterations = changed;
      return true;
    }
    if (auto* hydraulic =
	std::get_if<terrain::HydraulicErosion> (&stage)) {
      int* value = row == 0 ? &hydraulic->droplets
	: row == 1 ? &hydraulic->batch_size : &hydraulic->max_steps;
      int changed = *value;
      if (row == 0) {
	constexpr int choices[] = {
	  0, 10000, 30000, 100000, 300000, 500000, 1000000,
	  1500000, 2000000, 3000000, 5000000
	};
	if (direction > 0) {
	  for (int choice : choices)
	    if (choice > *value) { changed = choice; break; }
	} else {
	  for (auto i = std::rbegin (choices); i != std::rend (choices); ++i)
	    if (*i < *value) { changed = *i; break; }
	}
      } else if (row == 1) {
	changed = std::clamp (*value + direction * 64, 1, 4096);
      } else {
	constexpr int choices[] = {
	  8, 16, 32, 64, 128, 256, 512, 1024, 2048
	};
	if (direction > 0) {
	  for (int choice : choices)
	    if (choice > *value) { changed = choice; break; }
	} else {
	  for (auto i = std::rbegin (choices); i != std::rend (choices); ++i)
	    if (*i < *value) { changed = *i; break; }
	}
      }
      if (changed == *value)
	return false;
      *value = changed;
      return true;
    }
    if (auto* thermal = std::get_if<terrain::ThermalErosion> (&stage)) {
      const int changed = std::clamp
	(thermal->iterations + direction, 0, 20);
      if (changed == thermal->iterations)
	return false;
      thermal->iterations = changed;
      return true;
    }
    return false;
  }

  void
  TerrainLab::queue_parameter_rebuild ()
  {
    m_parameter_rebuild_pending = true;
    m_parameter_rebuild_stage = m_selected_stage;
  }

  void
  TerrainLab::run_pending_parameter_rebuild ()
  {
    if (!m_parameter_rebuild_pending)
      return;
    const int stage = m_parameter_rebuild_stage;
    m_parameter_rebuild_pending = false;
    bool iterative = false;
    if (stage >= 0
	&& stage < static_cast<int> (m_program.transforms.size ())) {
      iterative = terrain::terrain_transform_semantics
	(m_program.transforms[stage]).evaluation_order
	== terrain::EvaluationOrder::Iterative;
      rerun_program_from (stage);
    } else {
      rebuild_program ();
    }
    m_parameter_rebuild_delay = iterative ? 0.18f : 0.045f;
  }

  void
  TerrainLab::handle_click (float x, float y)
  {
    constexpr OverlayMode overlay_modes[] = {
      OverlayMode::None, OverlayMode::Height, OverlayMode::Slope,
      OverlayMode::Flow, OverlayMode::Streams, OverlayMode::Basins,
      OverlayMode::Sinks, OverlayMode::HeightDelta, OverlayMode::Trace,
      OverlayMode::StandingWater, OverlayMode::PermanentWater
    };
    for (int i = 0; i < 11; ++i)
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
      const std::uint32_t seed = m_program.randomness.seed + 1;
      m_program.source.recipe.seeds = terrain::derive_geological_seeds (seed);
      m_program.randomness = { .seed = seed, .offset = 3 };
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
      if (stage < static_cast<int> (m_program.transforms.size ())
	  && stage_rect (row).contains (x, y)) {
	m_selected_stage = stage;
	if (m_overlay == OverlayMode::HeightDelta)
	  update_overlay ();
	return;
      }
    }
    for (int i = 0; i < 5; ++i) {
      if (!add_stage_rect (i).contains (x, y))
	continue;
      if (i == 0)
	append_stage (terrain::NormalizeHeights { });
      else if (i == 1)
	append_stage (terrain::PowerHeights { 1.15f });
      else if (i == 2)
	append_stage (terrain::AnalyticalErosion { });
      else if (i == 3)
	append_stage (terrain::HydraulicErosion {
	  .droplets = 100000,
	  .batch_size = 256,
	  .max_steps = 512,
	  .minimum_water = 0.01f,
	  .sediment_at_termination =
	    terrain::SedimentDisposition::Deposit
	});
      else
	append_stage (terrain::ThermalErosion { 2, 0.003f });
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
      property_count = stage_property_count
	(m_program.transforms[m_selected_stage]);
    for (int row = 0; row < property_count; ++row) {
      if (selected_property_domain (row) != ParameterDomain::Natural)
	continue;
      const UiRect bounds = property_rect (row);
      const int direction = counter_minus_rect (bounds).contains (x, y)
	? -1 : counter_plus_rect (bounds).contains (x, y) ? 1 : 0;
      if (adjust_selected_natural (row, direction)) {
	queue_parameter_rebuild ();
	run_pending_parameter_rebuild ();
	return;
      }
    }

    if (m_selected_stage >= 0) {
      terrain::TerrainTransform& stage =
	m_program.transforms[m_selected_stage];
      if (auto* hydraulic =
	  std::get_if<terrain::HydraulicErosion> (&stage)) {
	constexpr int presets[3][4] = {
	  { 100000, 300000, 1000000, 1500000 },
	  { 64, 128, 256, 512 },
	  { 1, 64, 256, 1024 }
	};
	for (int group = 0; group < 3; ++group)
	  for (int i = 0; i < 4; ++i)
	    if (hydraulic_preset_rect (group, i).contains (x, y)) {
	      int& value = group == 0 ? hydraulic->droplets
		: group == 1 ? hydraulic->max_steps
		: hydraulic->batch_size;
	      if (value != presets[group][i]) {
		value = presets[group][i];
		rerun_program_from (m_selected_stage);
	      }
	      return;
	    }
      }
    }
  }

  void
  TerrainLab::refresh (bool inspection_fog)
  {
    if (!m_renderer || !m_map || !m_terrain || !m_world)
      return;
    const bool interactive_preview = inspection_fog;
    if (!interactive_preview)
      m_map->recompute_normals ();
    WorldParams display = *m_world;
    if (inspection_fog) {
      display.fog_scale = m_view == ViewMode::Cover
	? 0.00011f : 0.0f;
    }
    const render::TerrainProjection projection =
      inspection_fog && m_view == ViewMode::Torus
      ? render::TerrainProjection::Torus
      : render::TerrainProjection::Plane;
    const bool repeat = !inspection_fog || m_view == ViewMode::Cover;
    m_terrain->setup
      (*m_renderer, *m_map, display, projection, repeat,
	interactive_preview);
    m_terrain->render_shadow (*m_renderer, *m_map, m_sun_dir);
    update_overlay ();
  }

  void
  TerrainLab::tick (float dt)
  {
    if (m_parameter_rebuild_delay > 0.0f)
      m_parameter_rebuild_delay -= dt;
    if (m_parameter_rebuild_pending && m_parameter_rebuild_delay <= 0.0f)
      run_pending_parameter_rebuild ();

    const float orbit = (m_orbit_right ? 1.0f : 0.0f)
      - (m_orbit_left ? 1.0f : 0.0f);
    const float tilt = (m_tilt_up ? 1.0f : 0.0f)
      - (m_tilt_down ? 1.0f : 0.0f);
    const float zoom = (m_zoom_out ? 1.0f : 0.0f)
      - (m_zoom_in ? 1.0f : 0.0f);

    m_yaw += orbit * dt * 0.85f;
    m_pitch += tilt * dt * 0.55f;
    m_pitch = std::clamp (m_pitch, 0.18f, 1.28f);
    m_scroll_zoom_target *= std::exp (zoom * dt * 1.1f);
    m_scroll_zoom_target = std::clamp
      (m_scroll_zoom_target, 500.0f, 16000.0f);
    const float zoom_response = 1.0f - std::exp (-dt * 14.0f);
    m_distance += (m_scroll_zoom_target - m_distance) * zoom_response;
  }

  void
  TerrainLab::key (platform::Key key, bool down)
  {
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
    case Key::One:   select (terrain::GeologicalLayer::Combined); break;
    case Key::Two:   select (terrain::GeologicalLayer::Continent); break;
    case Key::Three: select (terrain::GeologicalLayer::Plains); break;
    case Key::Four:  select (terrain::GeologicalLayer::Mountains); break;
    case Key::Five:  select (terrain::GeologicalLayer::MountainMask); break;
    case Key::Six:   select (terrain::GeologicalLayer::WarpX); break;
    case Key::Seven: select (terrain::GeologicalLayer::WarpY); break;
    case Key::R:
      reset_program ();
      break;
    case Key::N:
      m_program.randomness.seed += 1;
      m_program.source.recipe.seeds = terrain::derive_geological_seeds
	(m_program.randomness.seed);
      m_program.randomness.offset = 3;
      m_selected_stage = -1;
      rebuild_program ();
      break;
    case Key::E:
      append_stage (terrain::HydraulicErosion { 100000 });
      break;
    case Key::Y:
      append_stage (terrain::ThermalErosion { 2, 0.003f });
      break;
    default:
      break;
    }
  }

  void
  TerrainLab::pointer_move (float x, float y, float dx, float dy)
  {
    if (!m_active)
      return;
    m_pointer_x = x;
    m_pointer_y = y;
    if (m_parameter_drag) {
      const float normalized = m_drag_start_normalized
	+ (m_drag_start_y - y) / 180.0f;
      if (set_selected_property_normalized (m_drag_property, normalized))
	queue_parameter_rebuild ();
      return;
    }
    if (m_pan_drag) {
      const Vector3D f = forward ();
      Vector3D right = f.cross (Vector3D (0, 1, 0));
      Vector3D ground_forward (f.x, 0, f.z);
      right.normalize ();
      ground_forward.normalize ();
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
    m_pitch = std::clamp (m_pitch, 0.18f, 1.28f);
  }

  void
  TerrainLab::pointer_button
    (platform::PointerButton button, bool down, float x, float y)
  {
    if (!m_active)
      return;
    m_pointer_x = x;
    m_pointer_y = y;
    if (button == platform::PointerButton::Primary) {
      m_pointer_down = down;
      if (down) {
	int property_count = 9;
	if (m_selected_stage >= 0)
	  property_count = stage_property_count
	    (m_program.transforms[m_selected_stage]);
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
	if (ui_contains (x, y)) {
	  handle_click (x, y);
	} else {
	  m_camera_drag = true;
	  m_camera_drag_distance = 0.0f;
	}
      } else {
	if (m_parameter_drag) {
	  m_parameter_drag = false;
	  m_drag_property = -1;
	  run_pending_parameter_rebuild ();
	}
	if (m_camera_drag && m_camera_drag_distance < 4.0f
	    && m_overlay == OverlayMode::Trace)
	  inspect_drainage (x, y);
	m_camera_drag = false;
      }
    } else {
      m_pan_drag = down;
    }
  }

  void
  TerrainLab::pointer_scroll (float x, float y, float delta)
  {
    if (!m_active || delta == 0.0f)
      return;
    m_pointer_x = x;
    m_pointer_y = y;
    if (ui_contains (x, y)) {
      if (stage_list_rect ().contains (x, y)
	  && m_program.transforms.size () > visible_stage_rows) {
	m_stage_scroll += delta > 0.0f ? -1 : 1;
	const int maximum = static_cast<int> (m_program.transforms.size ())
	  - visible_stage_rows;
	m_stage_scroll = std::clamp (m_stage_scroll, 0, maximum);
      }
      return;
    }
    const float amount = std::clamp (delta, -2.0f, 2.0f);
    m_scroll_zoom_target *= std::exp (-amount * 0.028f);
    m_scroll_zoom_target = std::clamp
      (m_scroll_zoom_target, 500.0f, 16000.0f);
  }

  Vector3D
  TerrainLab::position () const
  {
    const float horizontal = std::cos (m_pitch) * m_distance;
    return m_target + Vector3D
      (std::sin (m_yaw) * horizontal,
       std::sin (m_pitch) * m_distance,
       std::cos (m_yaw) * horizontal);
  }

  Vector3D
  TerrainLab::forward () const
  {
    return (m_target - position ()).normalized ();
  }

  Mat4
  TerrainLab::view_matrix () const
  {
    return Mat4::look_at (position (), m_target, Vector3D (0, 1, 0));
  }

  void
  TerrainLab::draw (render::DrawList& dl, int, int) const
  {
    const auto hot = [this] (const UiRect& bounds) {
      return bounds.contains (m_pointer_x, m_pointer_y);
    };

    m_ui.begin (dl);
    m_ui.panel
      (dl, window_x, window_y, window_width, window_height,
       "FIELD ALGEBRA TYCOON");
    m_ui.button (dl, close_rect (), "X", hot (close_rect ()),
		 m_pointer_down);
    m_ui.button (dl, reset_rect (), "RESET", hot (reset_rect ()),
		 m_pointer_down);

    std::ostringstream seed;
    seed << "SEED " << m_program.randomness.seed << " >";
    m_ui.button (dl, seed_rect (), seed.str (), hot (seed_rect ()),
		 m_pointer_down);
    m_ui.button (dl, fit_rect (), "FIT", hot (fit_rect ()),
		 m_pointer_down);
    const char* view_label = m_view == ViewMode::Tile ? "VIEW: TILE"
      : m_view == ViewMode::Cover ? "VIEW: COVER" : "VIEW: DONUT";
    m_ui.button (dl, view_rect (), view_label,
		 hot (view_rect ()), m_pointer_down,
		 m_view != ViewMode::Tile);
    for (int i = 0; i < 7; ++i) {
      const UiRect bounds = layer_rect (i);
      m_ui.button (dl, bounds, layer_labels[i], hot (bounds),
		   m_pointer_down, m_program.source.layer == layers[i]);
    }

    m_ui.section_header
      (dl, pipeline_header_rect (), "PIPELINE");
    m_ui.section_header
      (dl, property_header_rect (),
	 m_selected_stage < 0 ? "RECIPE PARAMETERS" : "STAGE PARAMETERS");

    const UiRect source = source_rect ();
    m_ui.pipeline_row
      (dl, source, "F", "GEOLOGICAL FIELD",
       terrain::geological_layer_name (m_program.source.layer), hot (source),
       m_pointer_down, m_selected_stage < 0);

    dl.color (0.37f, 0.55f, 0.37f, 0.9f);
    dl.line (source.x + 17, source.y + source.height,
	     source.x + 17,
	     stage_rect (visible_stage_rows - 1).y
	       + stage_row_height, 2.0f);
    for (int row = 0; row < visible_stage_rows; ++row) {
      const int stage_index = m_stage_scroll + row;
      if (stage_index >= static_cast<int> (m_program.transforms.size ()))
	break;
      const UiRect bounds = stage_rect (row);
      m_ui.pipeline_row
	(dl, bounds, std::to_string (stage_index + 1),
	 stage_name (m_program.transforms[stage_index]),
	 stage_detail (m_program.transforms[stage_index]), hot (bounds),
	 m_pointer_down, m_selected_stage == stage_index);
    }

    static const char* add_labels[] = {
      "+NORM", "+POWER", "+AGE", "+DROP", "+TALUS"
    };
    static const char* edit_labels[] = {
      "UP", "DOWN", "COPY", "DEL"
    };
    for (int i = 0; i < 5; ++i) {
      const UiRect add = add_stage_rect (i);
      m_ui.button (dl, add, add_labels[i], hot (add), m_pointer_down);
    }
    for (int i = 0; i < 4; ++i) {
      const UiRect edit = edit_stage_rect (i);
      m_ui.button (dl, edit, edit_labels[i], hot (edit),
		   m_pointer_down, false);
    }

    if (m_selected_stage < 0) {
      for (int row = 0; row < 9; ++row) {
	const UiRect bounds = property_rect (row);
	const PropertyText property = recipe_property
	  (m_program.source.recipe, row);
	if (property.domain == ParameterDomain::Continuous) {
	  m_ui.knob
	    (dl, bounds, property.label, property.value,
	     selected_property_normalized (row),
	     hot (parameter_control_rect (bounds)),
	     m_parameter_drag && m_drag_property == row);
	} else {
	  m_ui.counter
	    (dl, bounds, property.label, property.value,
	     hot (counter_minus_rect (bounds)),
	     hot (counter_plus_rect (bounds)), m_pointer_down);
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
	  m_ui.knob
	    (dl, bounds, property.label, property.value,
	     selected_property_normalized (row),
	     hot (parameter_control_rect (bounds)),
	     m_parameter_drag && m_drag_property == row);
	} else {
	  m_ui.counter
	    (dl, bounds, property.label, property.value,
	     hot (counter_minus_rect (bounds)),
	     hot (counter_plus_rect (bounds)), m_pointer_down);
	}
      }
      if (count <= 3) {
	m_ui.label (dl, right_x + 8, 294,
		    stage_name (stage), true);
	m_ui.label (dl, right_x + 8, 316,
		    stage_detail (stage));
	m_ui.label (dl, right_x + 8, 338,
		    semantics_detail (stage), true);
      }
      if (std::holds_alternative<terrain::NormalizeHeights> (stage)) {
	m_ui.label (dl, right_x + 8, 366,
		    "A whole-raster materialization barrier.");
	m_ui.label (dl, right_x + 8, 386,
		    "It can be moved, copied, or deleted.");
      } else if (std::holds_alternative<terrain::HydraulicErosion> (stage)) {
	const auto& hydraulic = std::get<terrain::HydraulicErosion> (stage);
	constexpr const char* headings[] = {
	  "DROP COUNT", "MAXIMUM LIFETIME", "BATCH SIZE"
	};
	constexpr const char* labels[3][4] = {
	  { "100K", "300K", "1M", "1.5M" },
	  { "64", "128", "256", "512" },
	  { "1", "64", "256", "1024" }
	};
	constexpr int presets[3][4] = {
	  { 100000, 300000, 1000000, 1500000 },
	  { 64, 128, 256, 512 },
	  { 1, 64, 256, 1024 }
	};
	for (int group = 0; group < 3; ++group) {
	  m_ui.label (dl, right_x + 8, 362 + group * 56,
		      headings[group], true);
	  const int value = group == 0 ? hydraulic.droplets
	    : group == 1 ? hydraulic.max_steps : hydraulic.batch_size;
	  for (int i = 0; i < 4; ++i) {
	    const UiRect bounds = hydraulic_preset_rect (group, i);
	    m_ui.button (dl, bounds, labels[group][i], hot (bounds),
			 m_pointer_down, value == presets[group][i]);
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
    m_ui.label
      (dl, left_x, 613,
	"LEFT DRAG orbit | RIGHT DRAG pan | TERRAIN WHEEL zoom");
    m_ui.label
      (dl, left_x, 636,
       "Pipeline rows are selectable and reorderable | T returns to game");
    m_ui.key_hint
      (dl, right_x + 8, 558, "DIAL", "continuous value");
    m_ui.key_hint
      (dl, right_x + 8, 584, "- / +", "whole-number count");

    m_ui.panel
      (dl, readings_x, readings_y, readings_width, readings_height,
       "MAP READINGS");
    constexpr const char* overlay_labels[] = {
      "MATERIAL", "HEIGHT", "SLOPE", "FLOW",
      "STREAMS", "BASINS", "OUTLETS", "DELTA", "TRACE", "WATER",
      "LAKES"
    };
    constexpr OverlayMode overlay_modes[] = {
      OverlayMode::None, OverlayMode::Height, OverlayMode::Slope,
      OverlayMode::Flow, OverlayMode::Streams, OverlayMode::Basins,
      OverlayMode::Sinks, OverlayMode::HeightDelta, OverlayMode::Trace,
      OverlayMode::StandingWater, OverlayMode::PermanentWater
    };
    for (int i = 0; i < 11; ++i) {
      const UiRect bounds = overlay_rect (i);
      m_ui.button (dl, bounds, overlay_labels[i], hot (bounds),
		   m_pointer_down, m_overlay == overlay_modes[i]);
    }
    const std::size_t separator = m_overlay_status.find (" | ");
    m_ui.label
      (dl, readings_x + 10, readings_y + 152,
       separator == std::string::npos
	? m_overlay_status : m_overlay_status.substr (0, separator), true);
    if (separator != std::string::npos)
      m_ui.label
	(dl, readings_x + 10, readings_y + 174,
	 m_overlay_status.substr (separator + 3));
    m_ui.label
      (dl, readings_x + 10, readings_y + 199,
       "Readings color the surface; geometry stays terrain.");
    const terrain::HydraulicErosionReport* erosion_report = nullptr;
    const terrain::HydraulicErosion* erosion_stage = nullptr;
    const terrain::AnalyticalErosionReport* analytical_report = nullptr;
    if (m_selected_stage >= 0
	&& m_selected_stage < static_cast<int> (m_reports.size ())) {
      erosion_report = std::get_if<terrain::HydraulicErosionReport>
	(&m_reports[static_cast<std::size_t> (m_selected_stage)]);
      analytical_report = std::get_if<terrain::AnalyticalErosionReport>
	(&m_reports[static_cast<std::size_t> (m_selected_stage)]);
      erosion_stage = std::get_if<terrain::HydraulicErosion>
	(&m_program.transforms[static_cast<std::size_t> (m_selected_stage)]);
    }
    if (erosion_report) {
      const auto& report = *erosion_report;
      m_ui.label
	(dl, readings_x + 10, readings_y + 232,
	 erosion_stage && erosion_stage->sediment_at_termination
	   == terrain::SedimentDisposition::Deposit
	 ? "SEDIMENT LEDGER — SETTLE AT DEATH"
	 : "SEDIMENT LEDGER — DISCARD AT DEATH", true);
      m_ui.label
	(dl, readings_x + 10, readings_y + 254,
	 "ERODED " + format_ledger (report.eroded)
	 + "  DEPOSITED " + format_ledger (report.deposited));
      m_ui.label
	(dl, readings_x + 10, readings_y + 276,
	 "LOST " + format_ledger (report.discarded_sediment)
	 + "  (" + format_float
	   (static_cast<float> (100.0 * report.discarded_fraction ()), 1)
	 + "%)");
      m_ui.label
	(dl, readings_x + 10, readings_y + 298,
	 "MEAN LIFE " + format_float
	   (static_cast<float> (report.mean_steps ()), 1)
	 + "  FINAL WATER " + format_float
	   (static_cast<float> (report.mean_final_water ()), 3));
      m_ui.label
	(dl, readings_x + 10, readings_y + 320,
	 "CAP " + format_count
	   (static_cast<int> (report.stopped_at_step_limit))
	 + "  WATER " + format_count
	   (static_cast<int> (report.stopped_at_water_cutoff))
	 + "  FLAT " + format_count
	   (static_cast<int> (report.stopped_flat)));
    } else if (analytical_report) {
      const auto& report = *analytical_report;
      m_ui.label
	(dl, readings_x + 10, readings_y + 232,
	 "FINITE-TIME STREAM POWER", true);
      m_ui.label
	(dl, readings_x + 10, readings_y + 254,
	 "FIXED BOUNDARIES "
	 + format_count (static_cast<int> (report.fixed_boundaries))
	 + "  PASSES " + std::to_string (report.fixed_point_iterations));
      m_ui.label
	(dl, readings_x + 10, readings_y + 276,
	 "LOWERED " + format_ledger (report.lowered_volume_m3)
	 + " M3  RAISED " + format_ledger (report.raised_volume_m3));
      m_ui.label
	(dl, readings_x + 10, readings_y + 298,
	 "MEAN CHANGE " + format_float
	   (static_cast<float> (report.mean_absolute_change_m), 2) + " M");
      m_ui.label
	(dl, readings_x + 10, readings_y + 320,
	 "MAX CHANGE " + format_float
	   (static_cast<float> (report.maximum_absolute_change_m), 2) + " M");
    }
    m_ui.end (dl);
  }
}
}
