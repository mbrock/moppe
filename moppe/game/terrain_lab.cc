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

    std::string stage_name (const terrain::PipelineStage& stage) {
      if (std::holds_alternative<terrain::NormalizeHeights> (stage))
	return "NORMALIZE";
      if (std::holds_alternative<terrain::PowerHeights> (stage))
	return "POWER CURVE";
      if (std::holds_alternative<terrain::HydraulicErosion> (stage))
	return "WATER EROSION";
      return "TALUS RELAX";
    }

    std::string stage_detail (const terrain::PipelineStage& stage) {
      if (std::holds_alternative<terrain::NormalizeHeights> (stage))
	return "map sampled range to 0..1";
      if (const auto* power =
	  std::get_if<terrain::PowerHeights> (&stage))
	return "height ^ " + format_float (power->exponent, 2);
      if (const auto* hydraulic =
	  std::get_if<terrain::HydraulicErosion> (&stage))
	return format_count (hydraulic->droplets) + " droplets";
      const auto& thermal = std::get<terrain::ThermalErosion> (stage);
      return std::to_string (thermal.iterations) + " passes @ "
	+ format_float (thermal.talus, 4);
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
      case 1: return { "CONTINENT FREQ",
	format_float (recipe.continent.noise.frequency, 2) };
      case 2: return { "PLAINS FREQ",
	format_float (recipe.plains.noise.frequency, 2) };
      case 3: return { "MOUNTAIN FREQ",
	format_float (recipe.mountains.frequency, 2) };
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

    int stage_property_count (const terrain::PipelineStage& stage) {
      if (std::holds_alternative<terrain::PowerHeights> (stage)
	  || std::holds_alternative<terrain::HydraulicErosion> (stage))
	return 1;
      if (std::holds_alternative<terrain::ThermalErosion> (stage))
	return 2;
      return 0;
    }

    PropertyText stage_property
      (const terrain::PipelineStage& stage, int row) {
      if (const auto* power =
	  std::get_if<terrain::PowerHeights> (&stage))
	return { "EXPONENT", format_float (power->exponent, 2) };
      if (const auto* hydraulic =
	  std::get_if<terrain::HydraulicErosion> (&stage))
	return { "DROPLETS", format_count (hydraulic->droplets) };
      const auto& thermal = std::get<terrain::ThermalErosion> (stage);
      if (row == 0)
	return { "PASSES", std::to_string (thermal.iterations) };
      return { "TALUS", format_float (thermal.talus, 4) };
    }
  }

  TerrainLab::TerrainLab ()
    : m_renderer (0), m_map (0), m_terrain (0), m_world (0),
      m_active (false),
      m_pipeline (terrain::make_geological_pipeline (0)),
      m_selected_stage (-1), m_stage_scroll (0),
      m_pointer_x (0), m_pointer_y (0),
      m_pointer_down (false), m_camera_drag (false),
      m_yaw (0.72f), m_pitch (0.62f), m_distance (5600.0f),
      m_orbit_left (false), m_orbit_right (false),
      m_zoom_in (false), m_zoom_out (false),
      m_tilt_up (false), m_tilt_down (false)
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
    m_terrain = &terrain;
    m_world = &world;
    m_sun_dir = sun_dir;
    m_pipeline = terrain::make_geological_pipeline
      (static_cast<std::uint32_t> (seed));
    m_selected_stage = -1;
    m_stage_scroll = 0;
    m_pointer_down = false;
    m_camera_drag = false;
    m_orbit_left = m_orbit_right = false;
    m_zoom_in = m_zoom_out = false;
    m_tilt_up = m_tilt_down = false;

    const size_t count = (size_t) map.width () * map.height ();
    m_saved_heights.assign (map.raw_heights (),
			    map.raw_heights () + count);
    m_active = true;
    rerun_pipeline ();
  }

  void
  TerrainLab::leave ()
  {
    if (!m_active)
      return;
    restore_game_map ();
    m_saved_heights.clear ();
    m_saved_heights.shrink_to_fit ();
    m_orbit_left = m_orbit_right = false;
    m_zoom_in = m_zoom_out = false;
    m_tilt_up = m_tilt_down = false;
    m_pointer_down = false;
    m_camera_drag = false;
    m_active = false;
    m_map = 0;
    m_terrain = 0;
    m_world = 0;
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
    if (layer == m_pipeline.layer)
      return;
    m_pipeline.layer = layer;
    m_selected_stage = -1;
    rerun_pipeline ();
  }

  void
  TerrainLab::reset_pipeline ()
  {
    m_pipeline = terrain::make_geological_pipeline
      (m_pipeline.randomness.seed, m_pipeline.layer);
    m_selected_stage = -1;
    m_stage_scroll = 0;
    rerun_pipeline ();
  }

  void
  TerrainLab::rerun_pipeline ()
  {
    if (!m_map)
      return;
    m_map->run_pipeline (m_pipeline);
    refresh ();
  }

  void
  TerrainLab::append_stage (terrain::PipelineStage stage)
  {
    m_pipeline.stages.push_back (std::move (stage));
    m_selected_stage = static_cast<int> (m_pipeline.stages.size ()) - 1;
    ensure_selected_stage_visible ();
    rerun_pipeline ();
  }

  void
  TerrainLab::move_selected_stage (int direction)
  {
    const int target = m_selected_stage + direction;
    if (m_selected_stage < 0 || target < 0
	|| target >= static_cast<int> (m_pipeline.stages.size ()))
      return;
    std::swap (m_pipeline.stages[m_selected_stage],
	       m_pipeline.stages[target]);
    m_selected_stage = target;
    ensure_selected_stage_visible ();
    rerun_pipeline ();
  }

  void
  TerrainLab::duplicate_selected_stage ()
  {
    if (m_selected_stage < 0
	|| m_selected_stage >= static_cast<int> (m_pipeline.stages.size ()))
      return;
    const auto position = m_pipeline.stages.begin () + m_selected_stage + 1;
    m_pipeline.stages.insert
      (position, m_pipeline.stages[m_selected_stage]);
    ++m_selected_stage;
    ensure_selected_stage_visible ();
    rerun_pipeline ();
  }

  void
  TerrainLab::remove_selected_stage ()
  {
    if (m_selected_stage < 0
	|| m_selected_stage >= static_cast<int> (m_pipeline.stages.size ()))
      return;
    m_pipeline.stages.erase
      (m_pipeline.stages.begin () + m_selected_stage);
    if (m_pipeline.stages.empty ())
      m_selected_stage = -1;
    else
      m_selected_stage = std::min
	(m_selected_stage, static_cast<int> (m_pipeline.stages.size ()) - 1);
    ensure_selected_stage_visible ();
    rerun_pipeline ();
  }

  void
  TerrainLab::ensure_selected_stage_visible ()
  {
    const int count = static_cast<int> (m_pipeline.stages.size ());
    const int maximum = std::max (0, count - visible_stage_rows);
    if (m_selected_stage >= 0) {
      if (m_selected_stage < m_stage_scroll)
	m_stage_scroll = m_selected_stage;
      else if (m_selected_stage >= m_stage_scroll + visible_stage_rows)
	m_stage_scroll = m_selected_stage - visible_stage_rows + 1;
    }
    m_stage_scroll = std::clamp (m_stage_scroll, 0, maximum);
  }

  void
  TerrainLab::adjust_selected_property (int row, int direction)
  {
    if (direction == 0)
      return;
    if (m_selected_stage < 0) {
      terrain::GeologicalRecipe& recipe = m_pipeline.recipe;
      switch (row) {
      case 0:
	recipe.warp.amplitude = std::clamp
	  (recipe.warp.amplitude + direction * 0.025f, 0.0f, 0.6f);
	break;
      case 1:
	recipe.continent.noise.frequency = std::clamp
	  (recipe.continent.noise.frequency + direction * 0.25f,
	   0.25f, 16.0f);
	break;
      case 2:
	recipe.plains.noise.frequency = std::clamp
	  (recipe.plains.noise.frequency + direction * 0.5f,
	   0.5f, 32.0f);
	break;
      case 3:
	recipe.mountains.frequency = std::clamp
	  (recipe.mountains.frequency + direction * 0.25f,
	   0.25f, 24.0f);
	break;
      case 4:
	recipe.blend.mask_low = std::clamp
	  (recipe.blend.mask_low + direction * 0.025f,
	   0.0f, recipe.blend.mask_high - 0.025f);
	break;
      case 5:
	recipe.blend.mask_high = std::clamp
	  (recipe.blend.mask_high + direction * 0.025f,
	   recipe.blend.mask_low + 0.025f, 1.0f);
	break;
      case 6:
	recipe.blend.continent_weight = std::clamp
	  (recipe.blend.continent_weight + direction * 0.05f,
	   0.0f, 1.5f);
	break;
      case 7:
	recipe.blend.plains_weight = std::clamp
	  (recipe.blend.plains_weight + direction * 0.025f,
	   0.0f, 1.0f);
	break;
      case 8:
	recipe.blend.mountain_weight = std::clamp
	  (recipe.blend.mountain_weight + direction * 0.05f,
	   0.0f, 1.5f);
	break;
      default:
	return;
      }
      rerun_pipeline ();
      return;
    }

    terrain::PipelineStage& stage =
      m_pipeline.stages[m_selected_stage];
    if (auto* power = std::get_if<terrain::PowerHeights> (&stage)) {
      power->exponent = std::clamp
	(power->exponent + direction * 0.05f, 0.1f, 4.0f);
    } else if (auto* hydraulic =
	       std::get_if<terrain::HydraulicErosion> (&stage)) {
      hydraulic->droplets = std::clamp
	(hydraulic->droplets + direction * 25000, 0, 2000000);
    } else if (auto* thermal =
	       std::get_if<terrain::ThermalErosion> (&stage)) {
      if (row == 0)
	thermal->iterations = std::clamp
	  (thermal->iterations + direction, 0, 20);
      else
	thermal->talus = std::clamp
	  (thermal->talus + direction * 0.0005f, 0.0f, 0.05f);
    } else {
      return;
    }
    rerun_pipeline ();
  }

  void
  TerrainLab::handle_click (float x, float y)
  {
    if (close_rect ().contains (x, y)) {
      leave ();
      return;
    }
    if (reset_rect ().contains (x, y)) {
      reset_pipeline ();
      return;
    }
    if (seed_rect ().contains (x, y)) {
      const std::uint32_t seed = m_pipeline.randomness.seed + 1;
      m_pipeline.recipe.seeds = terrain::derive_geological_seeds (seed);
      m_pipeline.randomness = { .seed = seed, .offset = 3 };
      m_selected_stage = -1;
      rerun_pipeline ();
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
      if (stage < static_cast<int> (m_pipeline.stages.size ())
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

    int property_count = 9;
    if (m_selected_stage >= 0)
      property_count = stage_property_count
	(m_pipeline.stages[m_selected_stage]);
    for (int row = 0; row < property_count; ++row) {
      const UiRect bounds = property_rect (row);
      if (stepper_minus_rect (bounds).contains (x, y)) {
	adjust_selected_property (row, -1);
	return;
      }
      if (stepper_plus_rect (bounds).contains (x, y)) {
	adjust_selected_property (row, 1);
	return;
      }
    }
  }

  void
  TerrainLab::refresh (bool inspection_fog)
  {
    if (!m_renderer || !m_map || !m_terrain || !m_world)
      return;
    m_map->recompute_normals ();
    WorldParams display = *m_world;
    if (inspection_fog)
      display.fog_scale *= 0.18f;
    m_terrain->setup (*m_renderer, *m_map, display);
    m_terrain->render_shadow (*m_renderer, *m_map, m_sun_dir);
  }

  void
  TerrainLab::tick (float dt)
  {
    const float orbit = (m_orbit_right ? 1.0f : 0.0f)
      - (m_orbit_left ? 1.0f : 0.0f);
    const float tilt = (m_tilt_up ? 1.0f : 0.0f)
      - (m_tilt_down ? 1.0f : 0.0f);
    const float zoom = (m_zoom_out ? 1.0f : 0.0f)
      - (m_zoom_in ? 1.0f : 0.0f);

    m_yaw += orbit * dt * 0.85f;
    m_pitch += tilt * dt * 0.55f;
    m_pitch = std::clamp (m_pitch, 0.18f, 1.28f);
    m_distance *= std::exp (zoom * dt * 1.1f);
    m_distance = std::clamp (m_distance, 700.0f, 9000.0f);
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
      reset_pipeline ();
      break;
    case Key::N:
      m_pipeline.randomness.seed += 1;
      m_pipeline.recipe.seeds = terrain::derive_geological_seeds
	(m_pipeline.randomness.seed);
      m_pipeline.randomness.offset = 3;
      m_selected_stage = -1;
      rerun_pipeline ();
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
	if (window_rect ().contains (x, y))
	  handle_click (x, y);
	else
	  m_camera_drag = true;
      } else {
	m_camera_drag = false;
      }
    } else {
      m_camera_drag = down;
    }
  }

  void
  TerrainLab::pointer_scroll (float x, float y, float delta)
  {
    if (!m_active || delta == 0.0f)
      return;
    m_pointer_x = x;
    m_pointer_y = y;
    if (stage_list_rect ().contains (x, y)
	&& m_pipeline.stages.size () > visible_stage_rows) {
      m_stage_scroll += delta > 0.0f ? -1 : 1;
      const int maximum = static_cast<int> (m_pipeline.stages.size ())
	- visible_stage_rows;
      m_stage_scroll = std::clamp (m_stage_scroll, 0, maximum);
      return;
    }
    const float amount = std::clamp (delta, -8.0f, 8.0f);
    m_distance *= std::exp (-amount * 0.055f);
    m_distance = std::clamp (m_distance, 700.0f, 9000.0f);
  }

  Vector3D
  TerrainLab::position () const
  {
    const Vector3D target
      (m_world->map_size.x * 0.5f, m_world->map_size.y * 0.12f,
       m_world->map_size.z * 0.5f);
    const float horizontal = std::cos (m_pitch) * m_distance;
    return target + Vector3D
      (std::sin (m_yaw) * horizontal,
       std::sin (m_pitch) * m_distance,
       std::cos (m_yaw) * horizontal);
  }

  Vector3D
  TerrainLab::forward () const
  {
    const Vector3D target
      (m_world->map_size.x * 0.5f, m_world->map_size.y * 0.12f,
       m_world->map_size.z * 0.5f);
    return (target - position ()).normalized ();
  }

  Mat4
  TerrainLab::view_matrix () const
  {
    const Vector3D target
      (m_world->map_size.x * 0.5f, m_world->map_size.y * 0.12f,
       m_world->map_size.z * 0.5f);
    return Mat4::look_at (position (), target, Vector3D (0, 1, 0));
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
    seed << "SEED " << m_pipeline.randomness.seed << " >";
    m_ui.button (dl, seed_rect (), seed.str (), hot (seed_rect ()),
		 m_pointer_down);

    m_ui.label (dl, 262, 71, "TERRAIN LAB / LIVE RECIPE", true);
    for (int i = 0; i < 7; ++i) {
      const UiRect bounds = layer_rect (i);
      m_ui.button (dl, bounds, layer_labels[i], hot (bounds),
		   m_pointer_down, m_pipeline.layer == layers[i]);
    }

    m_ui.section_header
      (dl, pipeline_header_rect (), "PIPELINE");
    m_ui.section_header
      (dl, property_header_rect (),
	 m_selected_stage < 0 ? "RECIPE PARAMETERS" : "STAGE PARAMETERS");

    const UiRect source = source_rect ();
    m_ui.pipeline_row
      (dl, source, "F", "GEOLOGICAL FIELD",
       terrain::geological_layer_name (m_pipeline.layer), hot (source),
       m_pointer_down, m_selected_stage < 0);

    dl.color (0.37f, 0.55f, 0.37f, 0.9f);
    dl.line (source.x + 17, source.y + source.height,
	     source.x + 17,
	     stage_rect (visible_stage_rows - 1).y
	       + stage_row_height, 2.0f);
    for (int row = 0; row < visible_stage_rows; ++row) {
      const int stage_index = m_stage_scroll + row;
      if (stage_index >= static_cast<int> (m_pipeline.stages.size ()))
	break;
      const UiRect bounds = stage_rect (row);
      m_ui.pipeline_row
	(dl, bounds, std::to_string (stage_index + 1),
	 stage_name (m_pipeline.stages[stage_index]),
	 stage_detail (m_pipeline.stages[stage_index]), hot (bounds),
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
	  (m_pipeline.recipe, row);
	m_ui.stepper
	  (dl, bounds, property.label, property.value,
	   hot (stepper_minus_rect (bounds)),
	   hot (stepper_plus_rect (bounds)), m_pointer_down);
      }
    } else {
      const terrain::PipelineStage& stage =
	m_pipeline.stages[m_selected_stage];
      const int count = stage_property_count (stage);
      for (int row = 0; row < count; ++row) {
	const UiRect bounds = property_rect (row);
	const PropertyText property = stage_property (stage, row);
	m_ui.stepper
	  (dl, bounds, property.label, property.value,
	   hot (stepper_minus_rect (bounds)),
	   hot (stepper_plus_rect (bounds)), m_pointer_down);
      }
      m_ui.label (dl, right_x + 8, 270,
		  stage_name (stage), true);
      m_ui.label (dl, right_x + 8, 292,
		  stage_detail (stage));
      if (std::holds_alternative<terrain::NormalizeHeights> (stage)) {
	m_ui.label (dl, right_x + 8, 322,
		    "A whole-raster materialization barrier.");
	m_ui.label (dl, right_x + 8, 342,
		    "It can be moved, copied, or deleted.");
      }
    }

    std::ostringstream status;
    status << m_pipeline.stages.size () << " stages | selected ";
    if (m_selected_stage < 0)
      status << "field recipe";
    else
      status << (m_selected_stage + 1);
    m_ui.label (dl, left_x, 586, status.str (), true);
    m_ui.label
      (dl, left_x, 613,
       "CLICK controls | DRAG landscape to orbit | WHEEL to zoom");
    m_ui.label
      (dl, left_x, 636,
       "Pipeline rows are selectable and reorderable | T returns to game");
    m_ui.end (dl);
  }
}
}
