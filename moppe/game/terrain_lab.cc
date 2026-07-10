#include <moppe/game/terrain_lab.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <variant>

namespace moppe {
namespace game {
  namespace {
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

    UiRect batch_preset_rect (int index) {
      const float gap = 4.0f;
      const float width = (right_width - 3 * gap) / 4.0f;
      return { right_x + index * (width + gap), 350, width, 28 };
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
      const float width = (left_width - 3 * gap) / 4.0f;
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

    std::string stage_name (const terrain::TerrainTransform& stage) {
      if (std::holds_alternative<terrain::NormalizeHeights> (stage))
	return "NORMALIZE";
      if (std::holds_alternative<terrain::PowerHeights> (stage))
	return "POWER CURVE";
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
      if (const auto* hydraulic =
	  std::get_if<terrain::HydraulicErosion> (&stage))
	return format_count (hydraulic->droplets) + " drops / "
	  + format_count (hydraulic->batch_size) + " batch";
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
    };

    PropertyText recipe_property
      (const terrain::GeologicalRecipe& recipe, int row) {
      switch (row) {
      case 0: return { "WARP STRENGTH",
	format_float (recipe.warp.amplitude, 3) };
      case 1: return { "CONTINENT CYCLES",
	std::to_string (recipe.continent.noise.cycles) };
      case 2: return { "PLAINS CYCLES",
	std::to_string (recipe.plains.noise.cycles) };
      case 3: return { "MOUNTAIN CYCLES",
	std::to_string (recipe.mountains.cycles) };
      case 4: return { "MASK START",
	format_float (recipe.blend.mask_low, 3) };
      case 5: return { "MASK END",
	format_float (recipe.blend.mask_high, 3) };
      case 6: return { "CONTINENT MIX",
	format_float (recipe.blend.continent_weight, 2) };
      case 7: return { "PLAINS MIX",
	format_float (recipe.blend.plains_weight, 2) };
      default: return { "MOUNTAIN MIX",
	format_float (recipe.blend.mountain_weight, 2) };
      }
    }

    int stage_property_count (const terrain::TerrainTransform& stage) {
      if (std::holds_alternative<terrain::PowerHeights> (stage))
	return 1;
      if (std::holds_alternative<terrain::HydraulicErosion> (stage))
	return 2;
      if (std::holds_alternative<terrain::ThermalErosion> (stage))
	return 2;
      return 0;
    }

    PropertyText stage_property
      (const terrain::TerrainTransform& stage, int row) {
      if (const auto* power =
	  std::get_if<terrain::PowerHeights> (&stage))
	return { "EXPONENT", format_float (power->exponent, 2) };
      if (const auto* hydraulic =
	  std::get_if<terrain::HydraulicErosion> (&stage)) {
	if (row == 0)
	  return { "DROPLETS", format_count (hydraulic->droplets) };
	return { "BATCH SIZE", format_count (hydraulic->batch_size) };
      }
      const auto& thermal = std::get<terrain::ThermalErosion> (stage);
      if (row == 0)
	return { "PASSES", std::to_string (thermal.iterations) };
      return { "TALUS", format_float (thermal.talus, 4) };
    }
  }

  TerrainLab::TerrainLab ()
    : m_renderer (0), m_map (0), m_terrain (0), m_world (0),
      m_active (false),
      m_program (terrain::make_geological_program (0)),
      m_selected_stage (-1), m_stage_scroll (0),
      m_pointer_x (0), m_pointer_y (0),
      m_pointer_down (false), m_camera_drag (false), m_pan_drag (false),
      m_parameter_drag (false), m_drag_property (-1),
      m_drag_start_y (0.0f), m_drag_start_normalized (0.0f),
      m_parameter_rebuild_pending (false),
      m_parameter_rebuild_stage (-1), m_parameter_rebuild_delay (0.0f),
      m_target (),
      m_yaw (0.72f), m_pitch (0.62f), m_distance (5600.0f),
      m_orbit_left (false), m_orbit_right (false),
      m_zoom_in (false), m_zoom_out (false),
      m_tilt_up (false), m_tilt_down (false),
      m_shadow_refresh_delay (0.0f),
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
    m_selected_stage = -1;
    m_stage_scroll = 0;
    m_pointer_down = false;
    m_camera_drag = false;
    m_pan_drag = false;
    m_parameter_drag = false;
    m_drag_property = -1;
    m_parameter_rebuild_pending = false;
    m_parameter_rebuild_delay = 0.0f;
    m_view = ViewMode::Cover;
    m_orbit_left = m_orbit_right = false;
    m_zoom_in = m_zoom_out = false;
    m_tilt_up = m_tilt_down = false;
    m_shadow_refresh_delay = 0.0f;
    fit_view ();

    const size_t count = (size_t) map.width () * map.height ();
    m_saved_heights.assign (map.raw_heights (),
			    map.raw_heights () + count);
    m_active = true;
    rebuild_program ();
  }

  void
  TerrainLab::leave ()
  {
    if (!m_active)
      return;
    restore_game_map ();
    m_saved_heights.clear ();
    m_saved_heights.shrink_to_fit ();
    m_checkpoints.clear ();
    m_checkpoints.shrink_to_fit ();
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
    for (const terrain::TerrainTransform& transform : m_program.transforms) {
      // A checkpoint is the input to a stage.  The final output is already
      // resident in m_map, so copying it would only duplicate 16 MiB at the
      // default lab resolution on every recipe edit.
      m_checkpoints.push_back (m_evaluator->checkpoint ());
      m_evaluator->apply (transform);
    }
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
    for (std::size_t i = static_cast<std::size_t> (first_stage);
	 i < m_program.transforms.size (); ++i) {
      if (i >= m_checkpoints.size ())
	m_checkpoints.push_back (m_evaluator->checkpoint ());
      m_evaluator->apply (m_program.transforms[i]);
    }
    refresh ();
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
    if (const auto* hydraulic =
	std::get_if<terrain::HydraulicErosion> (&stage))
      return row == 0 ? unit (hydraulic->droplets, 0.0f, 2000000.0f)
	: unit (hydraulic->batch_size, 1.0f, 4096.0f);
    if (const auto* thermal =
	std::get_if<terrain::ThermalErosion> (&stage))
      return row == 0 ? unit (thermal->iterations, 0.0f, 20.0f)
	: unit (thermal->talus, 0.0f, 0.05f);
    return 0.0f;
  }

  bool
  TerrainLab::set_selected_property_normalized (int row, float value)
  {
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
    if (auto* hydraulic =
	std::get_if<terrain::HydraulicErosion> (&stage)) {
      if (row == 0) {
	const int old = hydraulic->droplets;
	hydraulic->droplets = std::lround (mix (0.0f, 2000000.0f));
	return hydraulic->droplets != old;
      }
      const int old = hydraulic->batch_size;
      hydraulic->batch_size = std::lround (mix (1.0f, 4096.0f));
      return hydraulic->batch_size != old;
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
      return;
    }
    for (int row = 0; row < visible_stage_rows; ++row) {
      const int stage = m_stage_scroll + row;
      if (stage < static_cast<int> (m_program.transforms.size ())
	  && stage_rect (row).contains (x, y)) {
	m_selected_stage = stage;
	return;
      }
    }
    for (int i = 0; i < 4; ++i) {
      if (!add_stage_rect (i).contains (x, y))
	continue;
      if (i == 0)
	append_stage (terrain::NormalizeHeights { });
      else if (i == 1)
	append_stage (terrain::PowerHeights { 1.15f });
      else if (i == 2)
	append_stage (terrain::HydraulicErosion { 100000 });
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

    if (m_selected_stage >= 0) {
      terrain::TerrainTransform& stage =
	m_program.transforms[m_selected_stage];
      if (auto* hydraulic =
	  std::get_if<terrain::HydraulicErosion> (&stage)) {
	constexpr int presets[] = { 1, 64, 256, 1024 };
	for (int i = 0; i < 4; ++i)
	  if (batch_preset_rect (i).contains (x, y)) {
	    hydraulic->batch_size = presets[i];
	    rerun_program_from (m_selected_stage);
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
    if (interactive_preview)
      m_shadow_refresh_delay = 0.12f;
    else
      m_terrain->render_shadow (*m_renderer, *m_map, m_sun_dir);
  }

  void
  TerrainLab::tick (float dt)
  {
    if (m_parameter_rebuild_delay > 0.0f)
      m_parameter_rebuild_delay -= dt;
    if (m_parameter_rebuild_pending && m_parameter_rebuild_delay <= 0.0f)
      run_pending_parameter_rebuild ();

    if (m_shadow_refresh_delay > 0.0f) {
      m_shadow_refresh_delay -= dt;
      if (m_shadow_refresh_delay <= 0.0f && m_renderer && m_map && m_terrain)
	m_terrain->render_shadow (*m_renderer, *m_map, m_sun_dir);
    }
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
	  m_parameter_drag = true;
	  m_drag_property = row;
	  m_drag_start_y = y;
	  m_drag_start_normalized = selected_property_normalized (row);
	  return;
	}
	if (window_rect ().contains (x, y)) {
	  handle_click (x, y);
	} else {
	  m_camera_drag = true;
	}
      } else {
	if (m_parameter_drag) {
	  m_parameter_drag = false;
	  m_drag_property = -1;
	  run_pending_parameter_rebuild ();
	}
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
    if (window_rect ().contains (x, y)) {
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
      "+NORM", "+POWER", "+WATER", "+TALUS"
    };
    static const char* edit_labels[] = {
      "UP", "DOWN", "COPY", "DEL"
    };
    for (int i = 0; i < 4; ++i) {
      const UiRect add = add_stage_rect (i);
      const UiRect edit = edit_stage_rect (i);
      m_ui.button (dl, add, add_labels[i], hot (add), m_pointer_down);
      m_ui.button (dl, edit, edit_labels[i], hot (edit),
		   m_pointer_down, false);
    }

    if (m_selected_stage < 0) {
      for (int row = 0; row < 9; ++row) {
	const UiRect bounds = property_rect (row);
	const PropertyText property = recipe_property
	  (m_program.source.recipe, row);
	m_ui.knob
	  (dl, bounds, property.label, property.value,
	   selected_property_normalized (row),
	   hot (parameter_control_rect (bounds)),
	   m_parameter_drag && m_drag_property == row);
      }
    } else {
      const terrain::TerrainTransform& stage =
	m_program.transforms[m_selected_stage];
      const int count = stage_property_count (stage);
      for (int row = 0; row < count; ++row) {
	const UiRect bounds = property_rect (row);
	const PropertyText property = stage_property (stage, row);
	m_ui.knob
	  (dl, bounds, property.label, property.value,
	   selected_property_normalized (row),
	   hot (parameter_control_rect (bounds)),
	   m_parameter_drag && m_drag_property == row);
      }
      m_ui.label (dl, right_x + 8, 270,
		  stage_name (stage), true);
      m_ui.label (dl, right_x + 8, 292,
		  stage_detail (stage));
      m_ui.label (dl, right_x + 8, 316,
		  semantics_detail (stage), true);
      if (std::holds_alternative<terrain::NormalizeHeights> (stage)) {
	m_ui.label (dl, right_x + 8, 344,
		    "A whole-raster materialization barrier.");
	m_ui.label (dl, right_x + 8, 364,
		    "It can be moved, copied, or deleted.");
      } else if (std::holds_alternative<terrain::HydraulicErosion> (stage)) {
	m_ui.label (dl, right_x + 8, 342, "BATCH EXPERIMENT", true);
	constexpr const char* labels[] = { "1", "64", "256", "1024" };
	const int batch =
	  std::get<terrain::HydraulicErosion> (stage).batch_size;
	constexpr int presets[] = { 1, 64, 256, 1024 };
	for (int i = 0; i < 4; ++i) {
	  const UiRect bounds = batch_preset_rect (i);
	  m_ui.button (dl, bounds, labels[i], hot (bounds),
		       m_pointer_down, batch == presets[i]);
	}
	m_ui.label (dl, right_x + 8, 394,
		    "A batch advances together, then commits.");
	m_ui.label (dl, right_x + 8, 414,
		    "Larger batches make erosion more simultaneous.");
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
	"DRAG KNOBS vertically | LEFT DRAG orbit | RIGHT DRAG pan");
    m_ui.label
      (dl, left_x, 636,
       "Pipeline rows are selectable and reorderable | T returns to game");
    m_ui.end (dl);
  }
}
}
