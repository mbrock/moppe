// The game: the port of main.cc's MoppeGLUT application class onto
// the platform/render abstractions.  World generation runs on a
// background thread behind a loading screen; the frame follows the
// exact pass order of the GL build's render_scene().

#include <moppe/platform/platform.hh>
#include <moppe/render/renderer.hh>
#include <moppe/render/text.hh>

#include <moppe/game/world.hh>
#include <moppe/game/terrain.hh>
#include <moppe/game/river_surface.hh>
#include <moppe/game/terrain_lab.hh>
#include <moppe/game/water_capture.hh>
#include <moppe/map/terrain_evaluator.hh>
#include <moppe/game/chase_camera.hh>
#include <moppe/game/vegetation.hh>
#include <moppe/game/stars.hh>
#include <moppe/game/dust.hh>
#include <moppe/game/blob_shadow.hh>
#include <moppe/game/fish.hh>
#include <moppe/game/wildlife.hh>
#include <moppe/game/city.hh>
#include <moppe/game/walker.hh>
#include <moppe/game/vehicle_render.hh>
#include <moppe/game/hud.hh>

#include <moppe/map/generate.hh>
#include <moppe/mov/vehicle.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/moisture.hh>
#include <moppe/terrain/watercourse.hh>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>

namespace moppe {
namespace game {
  // THE sun: one fixed direction shared by the light, the shadow
  // map, the sky's sun disc, and the ocean glint.
  static const float SUN_AZIMUTH = 0.8f;
  static float SUN_HEIGHT = 0.62f;   // low, vivid golden afternoon

  static Vector3D
  sun_direction_for (float height) {
    const float el = (height - 0.5f) * 3.14159f;
    return Vector3D (std::cos (el) * std::sin (SUN_AZIMUTH),
		     std::sin (el),
		     std::cos (el) * std::cos (SUN_AZIMUTH));
  }

  static float
  smooth_curve (float edge0, float edge1, float x) {
    float t = (x - edge0) / (edge1 - edge0);
    clamp (t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
  }

  static void
  fill_rounded_rect (render::DrawList& dl, float x, float y,
		     float width, float height, float radius) {
    radius = std::clamp (radius, 0.0f, std::min (width, height) * 0.5f);
    constexpr int corner_steps = 6;
    dl.begin (render::Prim::TriangleFan);
    dl.vertex (x + width * 0.5f, y + height * 0.5f);
    for (int corner = 0; corner < 4; ++corner) {
      const float cx = corner == 0 || corner == 3
	? x + radius : x + width - radius;
      const float cy = corner < 2 ? y + radius : y + height - radius;
      const float start = 3.14159265f + corner * 1.57079633f;
      for (int i = 0; i <= corner_steps; ++i) {
	const float angle = start + i * 1.57079633f / corner_steps;
	dl.vertex (cx + std::cos (angle) * radius,
		   cy + std::sin (angle) * radius);
      }
    }
    dl.vertex (x, y + radius);
    dl.end ();
  }

  static std::string
  terrain_cache_path (const WorldParams& world,
		      terrain::TerrainGenerationProfile profile,
		      int seed) {
    std::ostringstream name;
    name << "terrain-" << platform::executable_build_id () << '-'
	 << terrain::profile_id (profile) << '-' << world.resolution << '-'
	 << seed << (world.toroidal () ? "-torus.map" : "-bounded.map");
    return platform::cache_path (name.str ());
  }

  static std::string
  last_seed_path (const WorldParams& world,
		  terrain::TerrainGenerationProfile profile) {
    std::ostringstream name;
    name << "last-seed-" << platform::executable_build_id () << '-'
	 << terrain::profile_id (profile) << '-' << world.resolution << ".txt";
    return platform::cache_path (name.str ());
  }

  static void
  remember_seed (const WorldParams& world,
		 terrain::TerrainGenerationProfile profile, int seed) {
    std::ofstream output (last_seed_path (world, profile));
    if (output)
      output << seed << '\n';
  }

  static int
  remembered_seed (const WorldParams& world,
		   terrain::TerrainGenerationProfile profile) {
    std::ifstream input (last_seed_path (world, profile));
    int seed = -1;
    if (input >> seed && seed >= 0)
      return seed;
    return static_cast<int> (::time (0));
  }

  static void
  prune_obsolete_terrain_caches () {
    const std::string build_id = platform::executable_build_id ();
    std::error_code error;
    const std::filesystem::path root (platform::cache_path (""));
    for (const std::filesystem::directory_entry& entry :
	 std::filesystem::directory_iterator (root, error)) {
      if (error || !entry.is_regular_file ())
	continue;
      const std::string name = entry.path ().filename ().string ();
      const bool terrain_file = name.starts_with ("terrain-")
	|| name.starts_with ("last-seed-");
      if (terrain_file && name.find (build_id) == std::string::npos)
	std::filesystem::remove (entry.path (), error);
    }
  }

  static float
  sun_elevation_for (float sun_height) {
    return std::sin ((sun_height - 0.5f) * 3.14159f);
  }

  static float
  daylight_for (float sun_height) {
    return smooth_curve (-0.08f, 0.18f,
                         sun_elevation_for (sun_height));
  }

  static float
  golden_light_for (float sun_height) {
    const float elevation = sun_elevation_for (sun_height);
    return daylight_for (sun_height)
      * (1.0f - smooth_curve (0.15f, 0.65f, elevation));
  }

  // Sunlight is warm near the horizon and becomes a soft ivory as
  // it rises.  Intensity follows the real elevation rather than the
  // old cyclic sun-height color branches.
  static void
  sun_light_colors (float sun_height, Vector3D& diffuse,
		    Vector3D& specular) {
    const float daylight = daylight_for (sun_height);
    const float golden = golden_light_for (sun_height);
    const Vector3D warm (1.0f, 0.60f, 0.30f);
    const Vector3D ivory (1.0f, 0.96f, 0.84f);
    const Vector3D sun_color = ivory * (1.0f - golden)
      + warm * golden;

    diffuse = sun_color * (0.10f + 0.98f * daylight);
    specular = (Vector3D (1.0f, 0.86f, 0.70f) * golden
                + Vector3D (0.92f, 0.95f, 1.0f) * (1.0f - golden))
      * (0.15f + 0.85f * daylight);
  }

  // Sky horizon color (port of gfx::Sky::get_horizon_color): the
  // CPU twin of the sky shader's horizon math, used to derive the
  // fog color each tick.
  static Vector3D
  horizon_color_for (float sun_height) {
    const float daylight = daylight_for (sun_height);
    const float warmth = golden_light_for (sun_height) * 0.16f;
    // Must track sky.metal's day_horizon so distant terrain fades
    // into exactly the color the sky shows at the horizon.
    const Vector3D day_horizon (0.55f, 0.68f, 0.84f);
    const Vector3D night_horizon (0.035f, 0.045f, 0.09f);
    const Vector3D warm_horizon (0.92f, 0.58f, 0.32f);
    const Vector3D base = night_horizon * (1.0f - daylight)
      + day_horizon * daylight;
    return base * (1.0f - warmth) + warm_horizon * warmth;
  }

  class MoppeGame: public platform::Game {
  public:
    MoppeGame (const WorldParams& world, bool start_in_terrain_lab,
	       bool terrain_lab_preview, int seed,
	       std::string screenshot_path,
	       std::optional<WaterShot> water_shot,
	       terrain::TerrainGenerationProfile generation_profile)
      : m_world (world),
	m_spawn_position (world.spawn_position ()),
	m_seed (seed),
	m_generation_profile (generation_profile),
	m_map (world.resolution, world.resolution, world.map_size,
	       m_seed, world.topology ()),
	m_loading_map (world.resolution, world.resolution, world.map_size,
		       m_seed, world.topology ()),
	m_camera (18, 6.5f * one_meter),
	// Dirt-bike figures: 2600 N of launch, 30 kW of engine --
	// hard low-end punch, ~125 km/h against drag (the old
	// 5000 N constant force topped out near 300).
	m_vehicle (world.spawn_position (), 45, m_map, 2600, 30000,
		   150),
	m_car (world.spawn_position (), 45, m_map, 14000, 100000,
	       900),
	m_renderer (0),
	m_start_in_terrain_lab (start_in_terrain_lab),
	m_terrain_lab_preview (terrain_lab_preview),
	m_screenshot_path (std::move (screenshot_path)),
	m_water_shot (water_shot),
	m_screenshot_frames (0),
	m_ready (false),
	m_gen_stage (0),
	m_total_time (0),
	m_frame_time (1.0f / 60.0f),
	m_shake (0),
	m_shake_time (0),
	m_health (100.0f),
	m_fov_k (0),
	m_lives (10),
	m_game_over (false),
	m_fuel (100.0f),
	m_odometer (0),
	m_turn_input (0),
	m_go_input (0),
	m_boost_input (0),
	m_mode (M_BIKE),
	m_cam_mode (CAM_CHASE),
	m_car_exists (false),
	m_combo (0),
	m_score (0),
	m_jump_airtime (0),
	m_landed_airtime (0),
	m_landed_points (0),
	m_landed_age (10),
	m_fx_rng (7)
    { }

    // -- lifecycle ---------------------------------------------------

    void setup (render::Renderer& r, int, int) override {
      m_renderer = &r;

      // Fast, main-thread resource setup; the heavy world build
      // runs behind the loading screen.
      m_hud.load (r);
      m_terrain_lab.load (r);
      m_loading_title_font.reset
	(new render::FontAtlas (r, "AvenirNext-DemiBold", 36,
				r.scale_factor ()));
      m_loading_font.reset
	(new render::FontAtlas (r, "AvenirNext-Medium", 23,
				r.scale_factor ()));
      m_loading_detail_font.reset
	(new render::FontAtlas (r, "AvenirNext-Medium", 14,
				r.scale_factor ()));
      m_dust.load (r);
      m_blob.load (r);

      platform::async (&MoppeGame::generate_thunk,
		       &MoppeGame::finish_thunk, this);
    }

    static void generate_thunk (void* self)
    { ((MoppeGame*) self)->generate_world (); }
    static void finish_thunk (void* self)
    { ((MoppeGame*) self)->finish_setup (); }

    Vector3D choose_landscape_spawn () {
      // The generated landscape has no authored start.  Sample the
      // finished terrain for a dry, grassy, locally flat patch rather
      // than trusting the old fixed coordinate near the map corner.
      const float margin_x = 0.08f * m_world.map_size.x;
      const float margin_z = 0.08f * m_world.map_size.z;
      const float patch = 20.0f * one_meter;
      const float min_ground = m_world.water_level + 25.0f * one_meter;
      const float max_ground = 0.32f * m_world.map_size.y;

      std::uniform_real_distribution<float> random_x
	(margin_x, m_world.map_size.x - margin_x);
      std::uniform_real_distribution<float> random_z
	(margin_z, m_world.map_size.z - margin_z);

      Vector3D chosen;
      Vector3D fallback;
      int good_count = 0;
      float fallback_score = -1000000.0f;

      const auto standing_depth = [this] (float x, float z) {
	if (!m_standing_water)
	  return 0.0f;
	const terrain::TerrainGrid& grid = m_standing_water->source_grid;
	const auto wrap = [] (float value, float period) {
	  value = std::fmod (value, period);
	  return value < 0.0f ? value + period : value;
	};
	const std::size_t gx = static_cast<std::size_t>
	  (wrap (x, m_world.map_size.x) / grid.spacing_x)
	  % m_standing_water->width ();
	const std::size_t gz = static_cast<std::size_t>
	  (wrap (z, m_world.map_size.z) / grid.spacing_y)
	  % m_standing_water->height ();
	return m_standing_water->water_depth.at (gx, gz)
	  * m_world.map_size.y;
      };

      for (int i = 0; i < 6000; ++i) {
	const float x = random_x (m_fx_rng);
	const float z = random_z (m_fx_rng);
	const float h = m_map.interpolated_height (x, z);
	const float hx0 = m_map.interpolated_height (x - patch, z);
	const float hx1 = m_map.interpolated_height (x + patch, z);
	const float hz0 = m_map.interpolated_height (x, z - patch);
	const float hz1 = m_map.interpolated_height (x, z + patch);
	const float low = std::min
	  (h, std::min (std::min (hx0, hx1), std::min (hz0, hz1)));
	const float high = std::max
	  (h, std::max (std::max (hx0, hx1), std::max (hz0, hz1)));
	const float relief = high - low;
	const float up = m_map.interpolated_normal (x, z).y;
	const float lake_depth = std::max
	  ({ standing_depth (x, z), standing_depth (x - patch, z),
	     standing_depth (x + patch, z), standing_depth (x, z - patch),
	     standing_depth (x, z + patch) });

	// Always retain the best fallback.  The large shore penalty makes
	// even an unusual generated map prefer dry ground over a flat seabed.
	const float shore_penalty =
	  std::max (0.0f, min_ground - low) * 2.0f;
	const float alpine_penalty =
	  std::max (0.0f, h - max_ground) * 0.03f;
	const float score = up * 20.0f - relief * 0.2f
	  - shore_penalty - alpine_penalty
	  - (lake_depth > 0.1f ? 10000.0f : 0.0f);
	if (score > fallback_score) {
	  fallback_score = score;
	  fallback = Vector3D (x, h + 1.2f, z);
	}

	if (lake_depth > 0.1f || low < min_ground || high > max_ground ||
	    up < 0.94f || relief > 3.5f * one_meter)
	  continue;

	// Reservoir sampling chooses uniformly among all suitable sites,
	// so different generated worlds do not always start at the first
	// acceptable patch encountered.
	++good_count;
	std::uniform_int_distribution<int> keep (1, good_count);
	if (keep (m_fx_rng) == 1)
	  chosen = Vector3D (x, h + 1.2f, z);
      }

      return good_count > 0 ? chosen : fallback;
    }

    void generate_world () {
      // Exceptions must not escape the GCD block (std::terminate).
      // A world that failed to generate is a broken build or broken
      // inputs, not a state to idle in: log and exit with failure.
      try {
	generate_world_inner ();
      } catch (const std::exception& e) {
	std::cerr << "world generation failed: " << e.what ()
		  << std::endl;
	std::_Exit (-1);
      }
    }

    void generate_world_inner () {
      if (m_terrain_lab_preview) {
	m_gen_stage = 3;
	const terrain::TerrainProgram program =
	  terrain::make_geological_program (m_seed);
	map::TerrainEvaluator (m_map).evaluate (program);
      } else if (m_world.city_mode) {
	m_gen_stage = 1;
	m_city.generate (m_map, m_world);
      } else if (m_world.pico_mode) {
	m_gen_stage = 2;
	m_map.load_raw_u16 (platform::asset_path ("data/pico.u16"),
			    0.1f, m_world.map_size.y);
      } else {
	// Reuse the automatic build/profile/seed cache when possible.
	// MOPPE_MAPCACHE=<file> remains an explicit experiment override.
	const char* cache_override = ::getenv ("MOPPE_MAPCACHE");
	const std::string automatic_cache = terrain_cache_path
	  (m_world, m_generation_profile, m_seed);
	const char* cache = cache_override ? cache_override
	  : automatic_cache.c_str ();
	if (cache && m_map.try_load_cache (cache)) {
	  m_gen_stage = 7;
	} else {
	  m_gen_stage = 3;
	  const terrain::TerrainProgram program = terrain::make_world_program
	    (m_seed, m_generation_profile);
	  map::TerrainEvaluator evaluator (m_map);
	  evaluator.evaluate
	    (program, [this] (std::size_t,
			      const terrain::TerrainTransform& transform) {
	      // Normalization and lowland shaping have run when the first erosion
	      // stage is announced.  Publish that coherent newborn landform for
	      // the loading-screen renderer rather than racing the GPU against
	      // the map as erosion mutates it on this worker thread.
	      if (std::holds_alternative<terrain::AnalyticalErosion>
		  (transform)) {
		const std::size_t count = static_cast<std::size_t>
		  (m_map.width ()) * m_map.height ();
		auto heights = std::make_shared<const std::vector<float>>
		  (m_map.raw_heights (), m_map.raw_heights () + count);
		const std::lock_guard<std::mutex> lock (m_loading_mutex);
		m_loading_heights = std::move (heights);
	      }
	      if (std::holds_alternative<terrain::HydraulicErosion>
		  (transform))
		m_gen_stage = 4;
	    });
	  if (cache)
	    m_map.save_cache (cache);
	}
      }
      m_gen_stage = 5;
      m_map.recompute_normals ();

      // The random world's sea and lakes are one priority-flood surface.
      // Keep this as a reading: terrain and erosion remain authoritative.
      if (!m_terrain_lab_preview
	  && !m_world.city_mode && !m_world.pico_mode) {
	const float sea_level = m_world.water_level / m_world.map_size.y;
	m_standing_water = terrain::analyze_standing_water
	  (m_map.terrain_view (), sea_level);
	m_lake_census = terrain::census_lakes (*m_standing_water);
	{
	  // A one-line hydrology reading at load: pond explosions from
	  // erosion regressions show up here before any capture does.
	  std::size_t wet = 0;
	  for (const terrain::WaterBody& body : m_lake_census->bodies)
	    wet += body.cells;
	  std::cerr << "standing water: " << m_lake_census->bodies.size ()
		    << " bodies, " << wet << " wet cells\n";
	}
	m_drainage = terrain::analyze_wet_drainage
	  (m_map.terrain_view (), *m_standing_water, *m_lake_census);
	m_water_network = terrain::analyze_water_network
	  (*m_standing_water, *m_lake_census, *m_drainage);
	m_rivers = terrain::extract_river_network
	  (*m_standing_water, *m_lake_census, *m_drainage,
	   visible_river_minimum_area (m_drainage->source_grid));
	if (m_water_shot) {
	  m_water_inspection = choose_water_inspection
	    (*m_water_shot, m_map, *m_standing_water, *m_lake_census,
	     *m_drainage, *m_rivers);
	  if (!m_water_inspection)
	    throw std::runtime_error
	      ("no " + std::string (water_shot_name (*m_water_shot))
	       + " available for water screenshot");
	  std::cerr << "water screenshot: "
		    << water_shot_name (*m_water_shot)
		    << " cell=" << m_water_inspection->cell
		    << " score=" << m_water_inspection->score << '\n';
	}
      }

      if (!m_terrain_lab_preview) {
	m_vegetation.generate
	  (m_map, m_world, Vegetation::population_for (m_world));
	m_stars.generate (m_map, m_world,
			  m_world.pico_mode ? 250
			  : m_world.city_mode ? 130 : 80);
	m_fish.generate (m_map, m_world);
	m_wildlife.generate (m_map, m_world);
      }
      m_gen_stage = 6;
    }

    // The recipe behind the current map: entering the lab shows the
    // world's own pipeline instead of rebuilding a bare geological
    // field.  City and pico maps are not program outputs; they get
    // the geological recipe as a starting point for experiments.
    terrain::TerrainProgram lab_program () const {
      if (m_terrain_lab_preview || m_world.city_mode || m_world.pico_mode)
	return terrain::make_geological_program (m_seed);
      return terrain::make_world_program (m_seed, m_generation_profile);
    }

    void finish_setup () {
      render::Renderer& r = *m_renderer;

      m_terrain.setup (r, m_map, m_world);
      // Rivers are painted into the water sheets below; the ribbon
      // meshes stay available behind an env var while the painted
      // rendering proves itself against the reference captures.
      if (m_rivers && ::getenv ("MOPPE_RIVER_RIBBONS"))
	m_river_surface.rebuild
	  (r, m_map, *m_standing_water, *m_lake_census,
	   *m_drainage, *m_rivers);
      if (!m_terrain_lab_preview) {
	m_terrain.render_shadow (r, m_map,
				 sun_direction_for (SUN_HEIGHT));
	m_vegetation.load (r);
      }
      if (m_world.city_mode)
	m_city.load (r);

      render::OceanSetup ocean;
      ocean.level = m_world.water_level;
      ocean.center = Vector3D (m_world.map_size.x / 2, 0,
			       m_world.map_size.z / 2);
      ocean.half_extent = m_world.pico_mode
	? 0.55f * m_world.map_size.x : 5500 * one_meter;
      ocean.cells = 300;
      std::vector<float> water_levels;
      std::vector<float> water_flow;
      if (m_standing_water && m_drainage && m_rivers) {
	// The complete waterscape painted onto the terrain lattice:
	// lakes, sea, and rivers in one surface sheet, per-body wave
	// amplitude, and a flow arrow in every wet cell. Rivers render
	// through the same lattice water pass as the lakes; the flow
	// sheet is what carries their motion.
	const terrain::WaterSheets sheets = terrain::paint_watercourses
	  (m_map.terrain_view (), *m_standing_water, *m_lake_census,
	   *m_drainage, *m_rivers);
	water_levels.resize
	  (2 * static_cast<std::size_t> (m_map.width ()) * m_map.height ());
	water_flow.resize (water_levels.size ());
	const std::span<const float> unique = sheets.surface.values ();
	const std::size_t unique_width = m_standing_water->width ();
	const std::size_t unique_height = m_standing_water->height ();
	for (int y = 0; y < m_map.height (); ++y)
	  for (int x = 0; x < m_map.width (); ++x) {
	    const std::size_t cell =
	      (static_cast<std::size_t> (y) % unique_height) * unique_width
	      + static_cast<std::size_t> (x) % unique_width;
	    const std::size_t out = 2
	      * (static_cast<std::size_t> (y) * m_map.width () + x);
	    water_levels[out] = unique[cell];
	    water_levels[out + 1] = sheets.amplitude[cell];
	    water_flow[out] = sheets.flow[2 * cell];
	    water_flow[out + 1] = sheets.flow[2 * cell + 1];
	  }
      }
      r.set_ocean (ocean, water_levels);
      r.set_water_flow (water_flow);

      if (m_standing_water && m_drainage) {
	// Ground moisture from the hydrology: vegetation reads it for
	// blade height, color, and density.
	const terrain::ScalarRaster moisture = terrain::analyze_moisture
	  (*m_standing_water, *m_lake_census, *m_drainage);
	std::vector<float> expanded
	  (static_cast<std::size_t> (m_map.width ()) * m_map.height ());
	const std::span<const float> unique = moisture.values ();
	const std::size_t unique_width = m_standing_water->width ();
	const std::size_t unique_height = m_standing_water->height ();
	for (int y = 0; y < m_map.height (); ++y)
	  for (int x = 0; x < m_map.width (); ++x)
	    expanded[static_cast<std::size_t> (y) * m_map.width () + x]
	      = unique[(static_cast<std::size_t> (y) % unique_height)
		       * unique_width
		       + static_cast<std::size_t> (x) % unique_width];
	r.set_terrain_moisture (expanded);
      }

      m_vehicle.set_water_level (m_world.water_level);
      m_car.set_water_level (m_world.water_level);
      m_vehicle.set_obstacles (&m_city.obstacles ());
      m_car.set_obstacles (&m_city.obstacles ());

      if (!m_terrain_lab_preview
	  && !m_world.city_mode && !m_world.pico_mode) {
	m_spawn_position = choose_landscape_spawn ();
	m_vehicle.reset (m_spawn_position);

	std::uniform_real_distribution<float> heading
	  (0.0f, 2.0f * 3.14159f);
	const float a = heading (m_fx_rng);
	m_vehicle.set_heading (Vector3D (std::sin (a), 0,
				       std::cos (a)));
      }
      if (m_water_inspection)
	m_camera.place
	  (m_water_inspection->eye, m_water_inspection->target);

      m_ready = true;
      remember_seed (m_world, m_generation_profile, m_seed);
      if (m_start_in_terrain_lab) {
	m_terrain_lab.enter
	  (r, m_map, m_terrain, m_world, lab_program (),
	   sun_direction_for (SUN_HEIGHT));
	m_start_in_terrain_lab = false;
      }
      if (::getenv ("MOPPE_REGENERATE_ONCE")
	  && !m_automated_regeneration_done) {
	m_automated_regeneration_done = true;
	regenerate_world ();
      }
    }

    // -- simulation --------------------------------------------------

    void tick (float dt) override {
      m_frame_time = dt;
      if (!m_ready || m_game_over)
	return;

      m_total_time += dt;
      const float total_time = m_total_time;

      // Weather remains part of the world while actors are paused.  This
      // also initializes the shared horizon color when the game starts
      // directly in Terrain Lab.
      float cloudiness =
	std::sin (total_time * 0.0003f) * 0.4f + 0.5f
	+ 0.3f * std::pow (std::sin (total_time * 0.0008f), 2.0f)
	+ std::sin (total_time * 0.02f) * 0.05f;
      clamp (cloudiness, 0.0f, 1.0f);
      m_cloudiness = cloudiness;

      // Fog stays mostly sky-blue.  Directional warmth is added in
      // the shaders only when looking toward the sun.
      const Vector3D horizon = horizon_color_for (SUN_HEIGHT);
      m_fog = horizon * 0.82f
        + Vector3D (0.90f, 0.94f, 1.0f) * 0.18f;

      // Terrain inspection pauses actors and vehicle physics, but keeps the
      // visual clock, weather, and fog alive so sky and water remain a
      // useful frame of reference around the heightmap.
      if (m_terrain_lab.active ()) {
	m_terrain_lab.tick (dt);
	return;
      }

      // Screenshot autopilot for headless verification: rides in a
      // lazy arc with periodic boost-assisted leaps.
      static const bool demo = ::getenv ("MOPPE_DEMO") != 0;
      if (demo && !m_water_inspection) {
	input_go (1.0f);
	input_turn (0.35f * std::sin (total_time * 0.25f));
	input_boost (std::fmod (total_time, 11.0f) < 1.35f ? 1.0f : 0.0f);
      }

      m_vehicle.update (dt);
      if (m_car_exists)
	m_car.update (dt);
      if (m_mode == M_FOOT)
	m_walker.update (dt, m_map, m_city.obstacles (), m_world);

      const Vector3D vpos =
	(m_mode == M_FOOT) ? m_walker.position ()
	: (m_mode == M_CAR) ? m_car.position ()
	: m_vehicle.position ();
      mov::Vehicle& av = active_vehicle ();

      // Parked vehicles' impacts shouldn't linger until remount.
      if (m_mode != M_BIKE) {
	m_vehicle.pop_impact ();
	m_vehicle.pop_fall_drop ();
      }
      if (m_car_exists && m_mode != M_CAR) {
	m_car.pop_impact ();
	m_car.pop_fall_drop ();
      }

      const bool in_water = vpos.y < m_world.water_level + 1.0f;
      const bool driving = (m_mode != M_FOOT);

      // Long jumps become score events after three seconds. Keep the last
      // airborne time locally because Vehicle clears its timer on touchdown.
      if (driving && av.airtime () > 0.0f) {
	m_jump_airtime = av.airtime ();
	m_landed_age += dt;
      } else {
	if (driving && m_jump_airtime >= 3.0f) {
	  m_landed_airtime = m_jump_airtime;
	  m_landed_points = (int) std::round
	    (100.0f * m_jump_airtime * m_jump_airtime);
	  m_score += m_landed_points;
	  m_landed_age = 0.0f;
	}
	m_jump_airtime = 0.0f;
	m_landed_age += dt;
      }
      const Vector3D dust_color (0.60f, 0.52f, 0.40f);
      const Vector3D clod_color (0.42f, 0.34f, 0.24f);
      const Vector3D spray_color (0.85f, 0.92f, 1.0f);
      const Vector3D fwd = av.orientation ();
      const Vector3D rear_wheel =
	vpos - fwd * 1.4f + Vector3D (0, -0.7f, 0);

      // Drift kicks up dirt from the rear wheel (or spray).
      if (driving && av.grounded () && av.drift_speed () > 6.0f) {
	int n = std::min (4, (int) (av.drift_speed () * 0.2f));
	m_dust.emit (rear_wheel, av.velocity () * 0.15f, n,
		     in_water ? spray_color : dust_color);
      }

      // Roost: hard throttle sprays an arc of dirt clods backward
      // off the rear knobby, heaviest when the engine is winning
      // against the ground (launches, corner exits).
      if (driving && av.grounded () && !in_water
	  && av.thrust () > 0.6f) {
	const float speed = av.velocity ().length ();
	const float slip = av.thrust ()
	  * (1.0f - std::min (1.0f, speed / 30.0f));
	if (slip > 0.15f) {
	  Dust::Style roost;
	  roost.size = 0.45f;
	  roost.life = 0.9f;
	  roost.gravity = 12.0f;
	  roost.spread = 0.5f;
	  m_dust.emit (rear_wheel,
		       fwd * (-6.0f - 14.0f * slip)
		       + Vector3D (0, 3.5f + 3.0f * slip, 0),
		       1 + (int) (slip * 3.0f), clod_color, roost);
	}
      }

      // Jet embers: hot additive sparks streaming out of the
      // nozzles while the jets burn, arcing down and dying fast.
      if (driving && av.boost_level () > 0.05f) {
	std::uniform_real_distribution<float> chance (0.0f, 1.0f);
	if (chance (m_fx_rng) < 34.0f * av.boost_level () * dt) {
	  Dust::Style ember;
	  ember.size = 0.15f;
	  ember.life = 0.45f;
	  ember.gravity = 6.0f;
	  ember.spread = 0.3f;
	  ember.additive = true;
	  m_dust.emit (vpos - fwd * 0.5f + Vector3D (0, -0.5f, 0),
		       av.velocity () * 0.5f - fwd * 2.0f
		       + Vector3D (0, -4.0f, 0),
		       1, Vector3D (1.0f, 0.55f, 0.18f), ember);
	}
      }

      // Exhaust smoke: faint gray puffs that rise off the muffler
      // while the throttle is open.
      if (driving && std::abs (av.thrust ()) > 0.3f
	  && m_mode == M_BIKE) {
	std::uniform_real_distribution<float> chance (0.0f, 1.0f);
	if (chance (m_fx_rng) < 14.0f * dt) {
	  Dust::Style smoke;
	  smoke.size = 0.35f;
	  smoke.life = 0.8f;
	  smoke.gravity = -2.5f;   // buoyant
	  smoke.spread = 0.25f;
	  m_dust.emit (vpos - fwd * 1.2f + Vector3D (0, -0.4f, 0),
		       av.velocity () * 0.25f, 1,
		       Vector3D (0.45f, 0.45f, 0.48f), smoke);
	}
      }

      // Wading fast throws up a bow wave.
      if (driving && in_water && av.velocity ().length () > 15.0f)
	m_dust.emit (vpos + Vector3D (0, -0.5f, 0),
		     av.velocity () * 0.3f, 3, spray_color);

      // Hard landings shake the camera and burst dirt outward:
      // a low pancake of dust plus a ring of ballistic clods.
      const float impact = driving ? av.pop_impact () : 0.0f;
      if (impact > 8.0f) {
	m_shake = std::min (0.28f, 0.010f * impact);
	m_shake_time = 0.0f;
	m_dust.emit (vpos + Vector3D (0, -0.7f, 0),
		     av.velocity () * 0.2f, 12,
		     in_water ? spray_color : dust_color);
	if (!in_water) {
	  Dust::Style burst;
	  burst.size = 0.5f;
	  burst.life = 1.1f;
	  burst.gravity = 10.0f;
	  burst.spread = 1.4f;
	  m_dust.emit (vpos + Vector3D (0, -0.6f, 0),
		       av.velocity () * 0.15f
		       + Vector3D (0, 2.0f + 0.15f * impact, 0),
		       (int) std::min (10.0f, impact * 0.5f),
		       clod_color, burst);
	}
      }

      // Crashes hurt; health trickles back slowly.  Falls from
      // above a hundred meters are simply fatal -- house rule.
      if (impact > 9.0f)
	m_health -= (impact - 9.0f) * 4.5f;
      if (driving && av.pop_fall_drop () > 100.0f)
	m_health = 0.0f;
      m_health = std::min (100.0f, m_health + 1.5f * dt);
      if (m_health <= 0.0f) {
	m_dust.emit (vpos, Vector3D (0, 6, 0), 40,
		     Vector3D (1.0f, 0.5f, 0.1f));
	--m_lives;
	if (m_lives <= 0) {
	  m_game_over = true;
	} else {
	  // Halfway through the hearts, the game offers its
	  // sympathies out loud.
	  if (m_lives == 5)
	    platform::say ("Ouchies. That hurts.");

	  // Respawn where you crashed, upright on the ground.
	  // (Deaths used to teleport you 600 m above the map
	  // corner -- it read as falling through the cosmos.)
	  const float ground =
	    m_map.interpolated_height (vpos.x, vpos.z);
	  av.reset (Vector3D (vpos.x, ground + 1.2f, vpos.z));
	  m_health = 100.0f;
	  m_shake = 1.0f;
	  m_shake_time = 0.0f;
	}
      }

      // Pedestrians get bowled over, with a puff of alarm.
      if (m_world.city_mode) {
	if (m_city.update_people (vpos,
				  driving ? av.velocity ()
				  : Vector3D (),
				  m_total_time) > 0)
	  m_dust.emit (m_city.last_hit (), Vector3D (0, 4, 0), 10,
		       Vector3D (0.95f, 0.85f, 0.4f));
      }

      // Star pickups sparkle gold and top up fuel and boost reserves.
      {
	const int picked = m_stars.update (vpos, m_total_time, dt);
	if (picked > 0) {
	  Dust::Style sparkle;
	  sparkle.size = 0.38f;
	  sparkle.life = 0.85f;
	  sparkle.gravity = -1.5f;
	  sparkle.spread = 1.7f;
	  sparkle.additive = true;
	  m_dust.emit (m_stars.last_pos (), Vector3D (0, 4, 0), 32,
		       Vector3D (1.0f, 0.72f, 0.12f), sparkle);
	  Dust::Style flash;
	  flash.size = 0.9f;
	  flash.life = 0.35f;
	  flash.spread = 0.25f;
	  flash.additive = true;
	  m_dust.emit (m_stars.last_pos (), Vector3D (), 5,
		       Vector3D (1.0f, 0.95f, 0.55f), flash);
	  m_fuel = std::min (100.0f, m_fuel + 25.0f * picked);
	  av.replenish_boost (0.25f * picked);
	}
      }

      // Fuel: the throttle burns it; an empty tank limps along at
      // a third power (never fully stranded).
      if (driving) {
	m_fuel = std::max
	  (0.0f, m_fuel - std::abs (av.thrust ()) * 0.9f * dt);
	m_odometer += av.velocity ().length () * dt;

	const float want =
	  m_go_input * ((m_fuel <= 0.5f && m_go_input > 0)
			? 0.3f : 1.0f);
	av.set_thrust (want);
      }

      m_dust.update (dt);
      m_shake_time += dt;
      m_shake *= std::exp (-7.0f * dt);

      if (m_cam_mode == CAM_HELMET) {
	// Ride inside the rider's head; lightly smoothed so
	// terrain bumps don't rattle the eyeballs.
	Vector3D eye, look;
	if (m_mode == M_FOOT) {
	  eye = m_walker.position () + Vector3D (0, 1.55f, 0);
	  look = m_walker.heading ();
	} else {
	  eye = av.position () + Vector3D (0, 0.95f, 0)
	    + av.orientation () * 0.4f;
	  look = av.orientation ();
	}
	m_fp_eye = m_fp_eye
	  + (eye - m_fp_eye) * (1.0f - std::exp (-25.0f * dt));
	m_camera.place (m_fp_eye, m_fp_eye + look * 10.0f);
      } else {
	const float flip = (m_cam_mode == CAM_FRONT) ? -1.0f : 1.0f;
	if (m_mode == M_FOOT)
	  m_camera.update (m_walker.position () + Vector3D (0, 1, 0),
			   m_walker.heading () * flip, Vector3D (),
			   dt);
	else
	  m_camera.update (av.position (),
			   av.orientation () * flip,
			   av.velocity (), dt);
	m_camera.limit (m_map);
      }
      if (m_water_inspection) {
	m_camera.place
	  (m_water_inspection->eye, m_water_inspection->target);
	m_camera.limit (m_map);
      }

      // Speed widens the field of view a touch.
      {
	const float kmh = driving
	  ? av.velocity ().length () * 3.6f : 0.0f;
	const float k = std::min
	  (1.0f, std::max (0.0f, (kmh - 70.0f) / 180.0f));
	m_fov_k += (k - m_fov_k) * (1.0f - std::exp (-5.0f * dt));
      }
    }

    // -- rendering ---------------------------------------------------

    void render (render::Renderer& r) override {
      if (!m_ready) {
	render_loading (r);
	return;
      }
      if (m_game_over) {
	render_game_over (r);
	return;
      }

      render::FrameParams fp;
      const float aspect =
	(float) r.width_pts () / std::max (1, r.height_pts ());
      const bool terrain_lab = m_terrain_lab.active ();
      const float fov = terrain_lab || m_water_inspection
	? 70.0f : 100.0f + 9.0f * m_fov_k;
      fp.proj = Mat4::perspective_reversed
	(degrees_to_radians (fov), aspect, 0.5f,
	 m_world.pico_mode ? 30000.0f : 9000.0f);

      // Hard landings produce a brief continuous vibration. Randomizing the
      // rotations each frame looked like violent camera teleportation.
      Mat4 view = terrain_lab
	? m_terrain_lab.view_matrix () : m_camera.view_matrix ();
      if (!terrain_lab && !m_water_inspection && m_shake > 0.005f) {
	const Vector3D cam = m_camera.position ();
	const float ground =
	  m_map.interpolated_height (cam.x, cam.z);
	const float clearance = cam.y - ground;
	const float room = std::min
	  (1.0f, std::max (0.0f, (clearance - 2.0f) / 8.0f));

	const float pulse = m_shake * room;
	const float roll = pulse * std::sin (2.0f * PI * 15.0f
					    * m_shake_time);
	const float pitch = pulse * 0.55f
	  * std::sin (2.0f * PI * 19.0f * m_shake_time + 0.7f);
	view = view
	  * Mat4::rotation (degrees_to_radians
			    (roll),
			    Vector3D (0, 0, 1))
	  * Mat4::rotation (degrees_to_radians
			    (pitch),
			    Vector3D (1, 0, 0));
      }
      fp.view = view;

      const Vector3D cam = terrain_lab
	? m_terrain_lab.position () : m_camera.position ();
      fp.camera_pos = cam;
      fp.cam_right = Vector3D (view.m[0], view.m[4], view.m[8]);
      fp.cam_up = Vector3D (view.m[1], view.m[5], view.m[9]);
      fp.cam_forward = Vector3D (-view.m[2], -view.m[6], -view.m[10]);
      // The lab's plane views render the game's own sky and haze; the
      // torus is an abstract inspection object on a dark backdrop.
      fp.clear_color = terrain_lab && m_terrain_lab.torus_view ()
	? Vector3D (0.012f, 0.016f, 0.022f)
	: m_fog;
      const float scene_fog = terrain_lab
	? m_terrain_lab.scene_fog (m_world.fog_scale)
	: m_world.fog_scale;
      fp.fog_scale = scene_fog;
      fp.sun_dir = sun_direction_for (SUN_HEIGHT);
      sun_light_colors (SUN_HEIGHT, fp.sun_diffuse, fp.sun_specular);
      fp.sun_specular = fp.sun_specular * 0.5f;   // material specular
      const float daylight = daylight_for (SUN_HEIGHT);
      // Open terrain receives substantial skylight even when a mountain
      // blocks the sun. Keep the fill cool so cast shadows retain shape and
      // color instead of collapsing into near-black silhouettes.
      fp.ambient = Vector3D (0.39f, 0.43f, 0.49f)
        * (0.35f + 0.65f * daylight);
      if (terrain_lab) {
	// Keep the same time of day while giving the high overview a small
	// legibility lift after automatic exposure.
	fp.ambient = fp.ambient * 1.15f;
	fp.exposure_bias = 0.88f;
      }
      fp.time = m_total_time;
      fp.profile = true;

      // Lens-flare occlusion: march toward the sun through the
      // heightmap; any ridge above the ray kills the flare.  Cloud
      // cover and submersion dim it; smoothed so it doesn't blink
      // as hills sweep past the sun.
      {
	float vis = 1.0f;
	if (cam.y < m_world.water_level)
	  vis = 0.0f;
	else {
	  for (int i = 1; i <= 40; ++i) {
	    const float t = 90.0f * i;
	    const Vector3D p = cam + fp.sun_dir * t;
	    if (!m_map.in_bounds (p.x, p.z))
	      break;
	    if (m_map.interpolated_height (p.x, p.z) > p.y) {
	      vis = 0.0f;
	      break;
	    }
	  }
	}
	vis *= 1.0f - 0.65f * m_cloudiness;
	m_flare += (vis - m_flare) * 0.12f;
	fp.sun_visibility = terrain_lab ? 0.0f : m_flare;
      }

      const bool captured = !m_screenshot_path.empty ()
	&& ++m_screenshot_frames >= 30;
      if (captured) {
	if (m_water_inspection)
	  std::cerr << "water screenshot camera: eye="
		    << m_camera.position () << " target="
		    << m_water_inspection->target << '\n';
	r.request_screenshot (m_screenshot_path);
      }
      if (!r.begin_frame (fp))
	return;

      FrameEnv env;
      env.fog_color = m_fog;
      env.fog_scale = scene_fog;
      env.sun_dir = fp.sun_dir;
      env.camera_pos = cam;
      env.cam_right = fp.cam_right;
      env.cam_up = fp.cam_up;
      env.cam_forward = fp.cam_forward;
      env.time = m_total_time;

      const auto draw_world_sky = [&] {
	render::SkyParams sky;
	sky.time = m_total_time;
	sky.sun_height = SUN_HEIGHT;
	// A world-shaping overview should keep the game world's moving sky,
	// without letting a passing front hide the land being edited.
	sky.cloudiness = terrain_lab
	  ? std::min (m_cloudiness, 0.35f) : m_cloudiness;
	sky.sun_dir = fp.sun_dir;
	sky.fog_color = m_fog;
	r.draw_sky (sky);
      };

      // At this extreme altitude, drawing the far-plane dome after terrain
      // exposes depth precision at the horizon.  Paint it first in the lab;
      // terrain then covers it deterministically.  Gameplay retains the
      // cheaper depth-culled order below.
      if (terrain_lab && !m_terrain_lab.torus_view ())
	draw_world_sky ();

      // Terrain first, chunk-culled to the haze horizon.
      m_terrain.render (r, cam,
		terrain_lab ? m_terrain_lab.forward () : m_camera.forward (),
			terrain_lab ? 12000.0f
			: 3.0f / m_world.fog_scale);

      // Sky AFTER the terrain: depth testing kills the expensive
      // cloud shader wherever terrain covers it.
      if (!terrain_lab)
	draw_world_sky ();

      if (!terrain_lab && !m_water_inspection) {
	// The world draw list, in the GL build's draw order.  Terrain lab
	// deliberately hides every placed object so generator differences are
	// not confused with stale vegetation or actor positions.
	m_world_dl.clear ();
	if (m_world.city_mode)
	  m_city.render (r, m_world_dl, env);
	m_vegetation.render (r, env);
	m_stars.render (m_world_dl, env);

	// Soft blob shadows under the movers.
	m_blob.draw (m_world_dl, m_map, m_vehicle.position (), 2.2f);
	if (m_car_exists)
	  m_blob.draw (m_world_dl, m_map, m_car.position (), 2.9f);
	if (m_mode == M_FOOT)
	  m_blob.draw (m_world_dl, m_map,
		       m_walker.position () + Vector3D (0, 0.5f, 0),
		       0.8f);

	// In helmet cam you ARE the rider: don't draw yourself.
	const bool helmet = (m_cam_mode == CAM_HELMET);
	if (!(helmet && m_mode == M_BIKE))
	  render_vehicle (m_world_dl, m_vehicle, m_total_time);
	if (m_car_exists && !(helmet && m_mode == M_CAR))
	  render_vehicle (m_world_dl, m_car, m_total_time);
	if (m_mode == M_FOOT && !helmet)
	  m_walker.render (m_world_dl, m_total_time);

	m_fish.render (m_world_dl, env);
	m_wildlife.render (m_world_dl, env);
	r.draw_list (m_world_dl);
      }

      // Translucent water late so the seabed and fish show
      // through; dust last so spray sits atop the surface.
      if (terrain_lab)
	m_terrain_lab.render_rivers (r, cam);
      else
	m_river_surface.draw (r, cam);

      // The lab keeps the game's painted water while the map is the
      // game's own; a rebuilt map invalidates the water sheets, so
      // they disappear until the lab's own analysis draws ribbons.
      if (!terrain_lab
	  || (!m_terrain_lab.torus_view ()
	      && m_terrain_lab.map_pristine ())) {
	render::OceanParams ocean;
	ocean.time = m_total_time;
	ocean.fog_color = m_fog;
	ocean.fog_scale = scene_fog;
	if (m_world.toroidal ()) {
	  const Vector3D center (0.5f * m_world.map_size.x, 0,
				 0.5f * m_world.map_size.z);
	  ocean.world_offset.x = cam.x - center.x;
	  ocean.world_offset.z = cam.z - center.z;
	}
	r.draw_ocean (ocean);
      }

      if (!terrain_lab) {
	m_dust_dl.clear ();
	m_dust.render (m_dust_dl, env);
	r.draw_list (m_dust_dl);
      }

      // Post effects.
      if (!terrain_lab && cam.y < m_world.water_level)
	r.apply_underwater (m_total_time);
      if (!terrain_lab) {
	const float kmh = (m_mode == M_FOOT)
	  ? 0.0f : active_vehicle ().velocity ().length () * 3.6f;
	float k = (kmh - 90.0f) / 160.0f;
	clamp (k, 0.0f, 1.0f);
	if (k > 0.01f)
	  r.apply_motion_blur (k);
      }

      // HUD, kept inside the safe area (notch / home indicator).
      m_hud_dl.clear ();
      const platform::Insets si = platform::safe_insets ();
      m_hud_dl.translate (si.left, si.top, 0);
      const int hud_width =
	r.width_pts () - (int) (si.left + si.right);
      const int hud_height =
	r.height_pts () - (int) (si.top + si.bottom);
      if (terrain_lab) {
	m_terrain_lab.draw (m_hud_dl, hud_width, hud_height);
      } else if (!m_water_inspection) {
	HudState hs;
	hs.speed_kmh = (m_mode == M_FOOT)
	  ? 0.0f : active_vehicle ().velocity ().length () * 3.6f;
	hs.fuel = m_fuel;
	hs.boost_ready01 = (m_mode == M_FOOT)
	  ? 1.0f : active_vehicle ().boost_charge ();
	hs.health01 = m_health / 100.0f;
	hs.odometer_m = (float) m_odometer;
	hs.lives = m_lives;
	hs.stars = m_stars.collected ();
	hs.score = m_score;
	hs.airtime_s = m_jump_airtime;
	hs.landed_airtime_s = m_landed_airtime;
	hs.landed_points = m_landed_points;
	hs.landed_age_s = m_landed_age;
	hs.on_foot = (m_mode == M_FOOT);
	hs.frame_time_s = m_frame_time;
	const Vector3D heading = m_mode == M_FOOT
	  ? m_walker.heading () : active_vehicle ().orientation ();
	hs.heading_radians = std::atan2 (heading.x, heading.z);
	m_hud.draw (m_hud_dl, hs, hud_width, hud_height);
      }
      // Even a clean inspection capture needs this empty HUD pass: it is
      // also the final post-chain composite into the drawable.
      r.draw_hud (m_hud_dl);

      r.end_frame ();
      if (captured) {
	m_screenshot_path.clear ();
	platform::request_quit ();
      }
    }

    void render_loading (render::Renderer& r) {
      const float w = (float) r.width_pts ();
      const float h = (float) r.height_pts ();
      const float aspect = w / std::max (1.0f, h);
      const float sky_time = (float) platform::now ();
      std::shared_ptr<const std::vector<float>> loading_heights;
      {
	const std::lock_guard<std::mutex> lock (m_loading_mutex);
	loading_heights = m_loading_heights;
      }

      // Give Metal one frame with a quiet low plain, then upload the newborn
      // geological field.  Interactive-preview terrain retains the previous
      // height texture and grows smoothly between the two without repeated
      // CPU uploads.
      if (loading_heights && m_loading_terrain_state == 0) {
	const float floor = *std::min_element
	  (loading_heights->begin (), loading_heights->end ());
	std::fill (m_loading_map.raw_heights (),
		   m_loading_map.raw_heights () + loading_heights->size (),
		   floor);
	m_terrain.setup
	  (r, m_loading_map, m_world, render::TerrainProjection::Plane,
	   false, true);
	m_loading_terrain_state = 1;
	m_loading_terrain_reveal = sky_time;
      } else if (loading_heights && m_loading_terrain_state == 1) {
	std::copy (loading_heights->begin (), loading_heights->end (),
		   m_loading_map.raw_heights ());
	m_terrain.setup
	  (r, m_loading_map, m_world, render::TerrainProjection::Plane,
	   false, true);
	m_loading_terrain_state = 2;
	const std::lock_guard<std::mutex> lock (m_loading_mutex);
	m_loading_heights.reset ();
      }

      const bool show_terrain = m_loading_terrain_state > 0;
      Vector3D eye (0, 34, 0);
      Vector3D target (0, 27, -100);
      if (show_terrain) {
	const float orbit = sky_time * 0.035f;
	const float radius = m_world.map_size.x * 0.64f;
	target = Vector3D (m_world.map_size.x * 0.5f,
			   m_world.map_size.y * 0.12f,
			   m_world.map_size.z * 0.5f);
	eye = target + Vector3D (std::sin (orbit) * radius,
				m_world.map_size.y * 0.70f,
				std::cos (orbit) * radius);
      }
      const Vector3D forward = (target - eye).normalized ();

      render::FrameParams fp;
      fp.clear_color = horizon_color_for (SUN_HEIGHT);
      fp.view = Mat4::look_at (eye, target, Vector3D (0, 1, 0));
      fp.proj = Mat4::perspective_reversed
	(degrees_to_radians (show_terrain ? 52.0f : 64.0f), aspect,
	 0.5f, std::max (9000.0f, m_world.map_size.x * 2.0f));
      fp.camera_pos = eye;
      fp.sun_dir = sun_direction_for (SUN_HEIGHT);
      sun_light_colors (SUN_HEIGHT, fp.sun_diffuse, fp.sun_specular);
      fp.ambient = Vector3D (0.45f, 0.49f, 0.55f);
      fp.fog_scale = show_terrain ? m_world.fog_scale * 1.35f : 0.0f;
      fp.time = sky_time;
      fp.exposure_bias = 0.88f;
      if (!r.begin_frame (fp))
	return;

      render::SkyParams sky;
      sky.time = sky_time;
      sky.sun_height = SUN_HEIGHT;
      sky.cloudiness = 0.32f;
      sky.sun_dir = fp.sun_dir;
      sky.fog_color = fp.clear_color;
      r.draw_sky (sky);

      if (show_terrain) {
	const float reveal_age = sky_time - m_loading_terrain_reveal;
	// Let the first low plain breathe for a moment before it rises.  The
	// terrain transition itself lasts about a second in the Metal backend.
	if (m_loading_terrain_state == 2 || reveal_age > 0.0f)
	  m_terrain.render
	    (r, eye, forward, m_world.map_size.x * 1.8f);
      }

      m_hud_dl.clear ();
      render::DrawState s;
      s.blend = true;
      s.depth_test = false;
      s.depth_write = false;
      s.cull = false;
      m_hud_dl.state (s);
      m_hud_dl.lit (false);
      m_hud_dl.fogged (false);

      static const char* stages[] = {
	"Starting up...",
	"Building city...",
	"Loading Pico Island DEM...",
	"Generating terrain...",
	"Eroding terrain...",
	"Computing normals, planting things...",
	"Uploading to the GPU...",
	"Loading cached terrain...",
      };
      const int stage = m_gen_stage;
      const char* text = stages[stage < 0 ? 0 : stage > 7 ? 7 : stage];
      std::string erosion_text;
      if (stage == 4) {
	erosion_text = "Eroding (" + std::to_string
	  (terrain::profile_droplet_count (m_generation_profile))
	  + " droplets, "
	  + std::string (terrain::profile_id (m_generation_profile)) + ")...";
	text = erosion_text.c_str ();
      }

      const float panel_width = std::min (620.0f, w - 48.0f);
      const float panel_height = 206.0f;
      const float panel_x = (w - panel_width) * 0.5f;
      const float panel_y = std::min
	(h - panel_height - 28.0f, std::max (32.0f, h * 0.56f));

      // Terrain Lab's cool instrument surface, allowed to float over the
      // world sky which is being prepared behind it.
      m_hud_dl.color (0.0f, 0.008f, 0.01f, 0.28f);
      fill_rounded_rect (m_hud_dl, panel_x + 6, panel_y + 9,
			 panel_width, panel_height, 17);
      m_hud_dl.color (0.44f, 0.39f, 0.24f, 0.92f);
      fill_rounded_rect (m_hud_dl, panel_x, panel_y,
			 panel_width, panel_height, 16);
      m_hud_dl.color (0.08f, 0.25f, 0.24f, 0.99f);
      fill_rounded_rect (m_hud_dl, panel_x + 1, panel_y + 1,
			 panel_width - 2, panel_height - 2, 15);
      m_hud_dl.color (0.025f, 0.065f, 0.065f, 0.97f);
      fill_rounded_rect (m_hud_dl, panel_x + 3, panel_y + 3,
			 panel_width - 6, panel_height - 6, 13);
      m_hud_dl.color (0.39f, 0.78f, 0.68f, 0.42f);
      m_hud_dl.line (panel_x + 20, panel_y + 3,
		     panel_x + panel_width - 20, panel_y + 3, 1.0f);

      if (m_loading_title_font && m_loading_title_font->ok ()) {
	m_hud_dl.color (0.91f, 0.98f, 0.90f, 1.0f);
	m_loading_title_font->draw
	  (m_hud_dl, panel_x + 30, panel_y + 53, "MAKING A WORLD");
      }
      if (m_loading_detail_font && m_loading_detail_font->ok ()) {
	m_hud_dl.color (0.47f, 0.72f, 0.68f, 0.98f);
	const std::string detail = "SEED " + std::to_string (m_seed)
	  + "  /  "
	  + std::string (terrain::profile_id (m_generation_profile));
	m_loading_detail_font->draw
	  (m_hud_dl, panel_x + 32, panel_y + 80, detail);
      }
      if (m_loading_font && m_loading_font->ok ()) {
	m_hud_dl.color (0.84f, 0.94f, 0.92f, 1.0f);
	m_loading_font->draw
	  (m_hud_dl, panel_x + 31, panel_y + 126, text);
      }

      // Stage lights communicate real progress; a soft traveling highlight
      // keeps long erosion steps visibly alive without pretending to know a
      // percentage within the stage.
      const double t = platform::now ();
      const float pulse =
	0.5f + 0.5f * (float) std::sin (t * 2.6);
      const float track_x = panel_x + 32;
      const float track_y = panel_y + 161;
      const float track_width = panel_width - 64;
      m_hud_dl.color (0.02f, 0.12f, 0.12f, 0.98f);
      fill_rounded_rect
	(m_hud_dl, track_x, track_y, track_width, 8, 4);
      const float completed = track_width * (stage + 1) / 8.0f;
      m_hud_dl.color (0.32f, 0.70f, 0.61f, 0.92f);
      fill_rounded_rect
	(m_hud_dl, track_x, track_y, completed, 8, 4);
      const float scan_x = track_x + std::max
	(0.0f, completed - 18.0f - 10.0f * pulse);
      m_hud_dl.color (0.76f, 0.92f, 0.66f, 0.42f + 0.32f * pulse);
      fill_rounded_rect (m_hud_dl, scan_x, track_y - 1,
			 std::min (24.0f, completed), 10, 5);

      if (m_loading_detail_font && m_loading_detail_font->ok ()) {
	m_hud_dl.color (0.42f, 0.67f, 0.63f, 0.9f);
	m_loading_detail_font->draw
	  (m_hud_dl, track_x, panel_y + 190,
	   "FORMING LAND  /  MOVING WATER  /  GROWING A PLACE");
      }

      r.draw_hud (m_hud_dl);
      r.end_frame ();
    }

    void render_game_over (render::Renderer& r) {
      render::FrameParams fp;
      fp.clear_color = Vector3D (0, 0, 0);
      fp.view = Mat4 ();
      fp.proj = Mat4 ();
      if (!r.begin_frame (fp))
	return;

      m_hud_dl.clear ();
      m_hud.draw_game_over (m_hud_dl, r.width_pts (),
			    r.height_pts ());
      r.draw_hud (m_hud_dl);
      r.end_frame ();
    }

    // -- input -------------------------------------------------------

    void controls (const platform::ControlState& state) override {
      if (m_game_over || m_terrain_lab.active ())
	return;
      input_turn (state.steer);
      input_go (state.drive);
      input_boost (state.boost);
    }

    void pointer_move (float x, float y, float dx, float dy) override {
      if (m_terrain_lab.active ())
	m_terrain_lab.pointer_move (x, y, dx, dy);
    }

    void pointer_button
      (platform::PointerButton button, bool down,
       float x, float y) override {
      if (m_terrain_lab.active ())
	m_terrain_lab.pointer_button (button, down, x, y);
    }

    void pointer_scroll
      (float x, float y, float delta) override {
      if (m_terrain_lab.active ())
	m_terrain_lab.pointer_scroll (x, y, delta);
    }

    void key (platform::Key k, bool down) override {
      using platform::Key;
      const float factor = down ? 1.0f : 0.0f;

      // In great pain, only R (ride again) and ESC work.
      if (m_game_over) {
	if (k == Key::R && down)
	  revive ();
	else if (k == Key::Escape && down)
	  platform::request_quit ();
	return;
      }

      if (m_terrain_lab.active ()) {
	if (k == Key::T && down)
	  m_terrain_lab.leave ();
	else
	  m_terrain_lab.key (k, down);
	return;
      }

      if (k == Key::T && down && m_ready) {
	input_turn (0);
	input_go (0);
	input_boost (0);
	m_terrain_lab.enter (*m_renderer, m_map, m_terrain, m_world,
			     lab_program (),
			     sun_direction_for (SUN_HEIGHT));
	return;
      }

      if (k == Key::N && down && m_ready) {
	regenerate_world ();
	return;
      }

      // The secret dismount combo: 7, then 5, then R.  Arrow keys
      // bypass it (they were "special" codes dispatched before the
      // combo machine in the GLUT build).
      const bool arrow = (k == Key::Left || k == Key::Right
			  || k == Key::Up || k == Key::Down);
      if (down && !arrow) {
	static const Key want[3] = { Key::Seven, Key::Five, Key::R };
	if (k == want[m_combo]) {
	  if (++m_combo == 3) {
	    m_combo = 0;
	    toggle_mount ();
	  }
	} else
	  m_combo = (k == Key::Seven) ? 1 : 0;
      }

      switch (k) {
      case Key::Left:
      case Key::A:
	input_turn (-1 * factor);
	break;
      case Key::Right:
      case Key::D:
	input_turn (1 * factor);
	break;
      case Key::Up:
      case Key::W:
	input_go (1 * factor);
	break;
      case Key::Down:
      case Key::S:
	input_go (-1 * factor);
	break;

      case Key::Tab:
	if (down) {
	  m_cam_mode = (CamMode) ((m_cam_mode + 1) % 3);
	  if (m_cam_mode == CAM_HELMET)
	    m_fp_eye = m_camera.position ();   // glide in
	}
	break;

      case Key::Space:
	if (m_ready)
	  input_boost (factor);
	break;

      case Key::Escape:
	if (down)
	  platform::request_quit ();
	break;

      default:
	break;
      }
    }

  private:
    enum Mode { M_BIKE, M_FOOT, M_CAR };
    enum CamMode { CAM_CHASE, CAM_FRONT, CAM_HELMET };

    mov::Vehicle& active_vehicle ()
    { return m_mode == M_CAR ? m_car : m_vehicle; }

    void regenerate_world () {
      input_turn (0.0f);
      input_go (0.0f);
      input_boost (0.0f);
      m_ready = false;
      m_gen_stage = 0;
      m_standing_water.reset ();
      m_lake_census.reset ();
      m_drainage.reset ();
      m_water_network.reset ();
      m_rivers.reset ();
      m_river_surface.clear ();
      ++m_seed;
      m_mode = M_BIKE;
      m_car_exists = false;
      m_game_over = false;
      m_health = 100.0f;
      m_fuel = 100.0f;
      platform::async (&MoppeGame::generate_thunk,
		       &MoppeGame::finish_thunk, this);
    }

    void input_turn (float v) {
      m_turn_input = v;
      if (m_mode == M_FOOT)
	m_walker.set_turn (v);
      else
	active_vehicle ().set_yaw (90 * v);
    }

    void input_go (float v) {
      m_go_input = v;
      if (m_mode == M_FOOT)
	m_walker.set_walk (v > 0 ? v : v * 0.6f);
      else {
	active_vehicle ().set_thrust (v);
	active_vehicle ().set_boost (m_boost_input, m_go_input);
      }
    }

    void input_boost (float v) {
      const float previous = m_boost_input;
      m_boost_input = std::max (0.0f, std::min (1.0f, v));
      if (m_mode == M_FOOT) {
	if (m_boost_input > 0.1f && previous <= 0.1f)
	  m_walker.jump ();
      } else
	active_vehicle ().set_boost (m_boost_input, m_go_input);
    }

    void toggle_mount () {
      if (!m_ready)
	return;

      if (m_mode != M_FOOT) {
	// Step off to the side of whatever we're driving.
	mov::Vehicle& av = active_vehicle ();
	const Vector3D h = av.orientation ();
	const Vector3D side (h.z, 0, -h.x);
	m_walker.spawn (av.position ()
			+ side * (m_mode == M_CAR ? 2.4f : 1.8f),
			h);
	av.set_thrust (0);
	av.set_yaw (0);
	av.set_boost (0, 0);
	m_mode = M_FOOT;
	input_turn (m_turn_input);
	input_go (m_go_input);
	return;
      }

      // On foot: bike first, then our parked car, then grand theft.
      if ((m_walker.position () - m_vehicle.position ()).length2 ()
	  < 5.0f * 5.0f) {
	m_vehicle.set_thrust (0);
	m_vehicle.set_yaw (0);
	m_mode = M_BIKE;
	input_turn (m_turn_input);
	input_go (m_go_input);
	input_boost (m_boost_input);
	return;
      }

      if (m_car_exists &&
	  (m_walker.position () - m_car.position ()).length2 ()
	  < 6.0f * 6.0f) {
	m_car.set_thrust (0);
	m_car.set_yaw (0);
	m_mode = M_CAR;
	input_turn (m_turn_input);
	input_go (m_go_input);
	input_boost (m_boost_input);
	return;
      }

      Vector3D cpos, cdir, ccolor;
      const int kind = m_city.take_car_near
	(m_walker.position (), 7.0f, m_total_time, cpos, cdir,
	 ccolor);
      if (kind >= 0) {
	m_car.reset (cpos);
	m_car.set_heading (cdir);
	m_car.set_body_style (kind + 1, ccolor);
	m_car_exists = true;
	m_mode = M_CAR;
	input_turn (m_turn_input);
	input_go (m_go_input);
	input_boost (m_boost_input);
      }
    }

    void revive () {
      m_lives = 10;
      m_health = 100.0f;
      m_fuel = 100.0f;
      m_shake = 0.0f;
      m_shake_time = 0.0f;
      m_jump_airtime = 0.0f;
      m_landed_age = 10.0f;
      m_mode = M_BIKE;
      // Back to the start, but ON the ground rather than 600 m
      // over it.
      const float ground = m_map.interpolated_height
	(m_spawn_position.x, m_spawn_position.z);
      m_vehicle.reset (Vector3D (m_spawn_position.x, ground + 1.2f,
				 m_spawn_position.z));
      // Key releases were swallowed during the game-over screen;
      // don't resume with the throttle stuck open.
      m_turn_input = 0;
      m_go_input = 0;
      m_boost_input = 0;
      m_vehicle.set_thrust (0);
      m_vehicle.set_yaw (0);
      m_vehicle.set_boost (0, 0);
      m_game_over = false;
    }

    WorldParams m_world;
    Vector3D m_spawn_position;
    int m_seed;
    terrain::TerrainGenerationProfile m_generation_profile;
    map::RandomHeightMap m_map;
    map::RandomHeightMap m_loading_map;
    std::mutex m_loading_mutex;
    std::shared_ptr<const std::vector<float>> m_loading_heights;
    int m_loading_terrain_state = 0;
    float m_loading_terrain_reveal = 0.0f;
    std::optional<terrain::FloodField> m_standing_water;
    std::optional<terrain::LakeCensus> m_lake_census;
    std::optional<terrain::DrainageGraph> m_drainage;
    std::optional<terrain::WaterNetwork> m_water_network;
    std::optional<terrain::RiverNetwork> m_rivers;
    RiverSurface m_river_surface;
    Terrain m_terrain;
    TerrainLab m_terrain_lab;
    ChaseCamera m_camera;
    mov::Vehicle m_vehicle;
    mov::Vehicle m_car;
    Vegetation m_vegetation;
    Stars m_stars;
    Dust m_dust;
    BlobShadow m_blob;
    Fish m_fish;
    Wildlife m_wildlife;
    City m_city;
    Walker m_walker;
    Hud m_hud;
    std::unique_ptr<render::FontAtlas> m_loading_title_font;
    std::unique_ptr<render::FontAtlas> m_loading_font;
    std::unique_ptr<render::FontAtlas> m_loading_detail_font;

    render::Renderer* m_renderer;
    bool m_start_in_terrain_lab;
    bool m_terrain_lab_preview;
    bool m_automated_regeneration_done = false;
    std::string m_screenshot_path;
    std::optional<WaterShot> m_water_shot;
    std::optional<WaterInspection> m_water_inspection;
    int m_screenshot_frames;
    std::atomic<bool> m_ready;
    std::atomic<int> m_gen_stage;

    render::DrawList m_world_dl;
    render::DrawList m_dust_dl;
    render::DrawList m_hud_dl;

    // double: a float accumulator quantizes 60 Hz ticks after ~18 h
    // and stops advancing entirely after ~24 days.
    double m_total_time;
    float m_frame_time;
    float m_cloudiness = 0.5f;
    float m_flare = 0.0f;
    Vector3D m_fog;
    float m_shake;
    float m_shake_time;
    float m_health;
    float m_fov_k;
    int m_lives;
    bool m_game_over;
    float m_fuel;
    double m_odometer;
    float m_turn_input;
    float m_go_input;
    float m_boost_input;
    Mode m_mode;
    CamMode m_cam_mode;
    Vector3D m_fp_eye;
    bool m_car_exists;
    int m_combo;
    int m_score;
    float m_jump_airtime;
    float m_landed_airtime;
    int m_landed_points;
    float m_landed_age;
    std::mt19937 m_fx_rng;
  };
}
}

int
main (int argc, char** argv) {
  using namespace moppe;

  game::WorldParams world;
  platform::Config config;
  bool start_in_terrain_lab = false;
  bool terrain_lab_preview = false;
  std::string screenshot_path;
  std::optional<game::WaterShot> water_shot;
  int seed = -1;
  terrain::TerrainGenerationProfile generation_profile =
    terrain::TerrainGenerationProfile::Play;
  config.title = "Moppe";
  config.fullscreen = true;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--pico") {
      // The real Pico Island: 49.4 km square, summit 2333m, sea
      // level slightly raised so the coast reads as ocean.
      world.pico_mode = true;
      world.map_size = Vector3D (49400 * one_meter,
				 2400 * one_meter,
				 49400 * one_meter);
      world.water_level = 15 * one_meter;
      world.fog_scale = 0.00013f;   // clear island air
    } else if (arg == "--city") {
      // Urban stunt island in a shallow sea.
      world.city_mode = true;
      world.water_level = 15 * one_meter;
    } else if (arg == "--fullscreen") {
      config.fullscreen = true;
    } else if (arg == "--windowed") {
      config.fullscreen = false;
    } else if (arg == "--fast") {
      generation_profile = terrain::TerrainGenerationProfile::Fast;
    } else if (arg == "--terrain-quality") {
      if (i + 1 >= argc) {
	std::cerr << "--terrain-quality requires fast, play, or research\n";
	return -1;
      }
      const std::string quality = argv[++i];
      if (quality == "fast")
	generation_profile = terrain::TerrainGenerationProfile::Fast;
      else if (quality == "play")
	generation_profile = terrain::TerrainGenerationProfile::Play;
      else if (quality == "research")
	generation_profile = terrain::TerrainGenerationProfile::Research;
      else {
	std::cerr << "unknown terrain quality: " << quality << '\n';
	return -1;
      }
    } else if (arg == "--terrain-lab") {
      start_in_terrain_lab = true;
    } else if (arg == "--terrain-lab-preview") {
      start_in_terrain_lab = true;
      terrain_lab_preview = true;
      config.fullscreen = false;
      world.resolution = 1025;
    } else if (arg == "--terrain-lab-screenshot") {
      if (i + 1 >= argc) {
	std::cerr << "--terrain-lab-screenshot requires a PNG path\n";
	return -1;
      }
      screenshot_path = argv[++i];
      start_in_terrain_lab = true;
      terrain_lab_preview = true;
      config.fullscreen = false;
      world.resolution = 1025;
    } else if (arg == "--screenshot") {
      if (i + 1 >= argc) {
	std::cerr << "--screenshot requires a PNG path\n";
	return -1;
      }
      screenshot_path = argv[++i];
      config.fullscreen = false;
    } else if (arg == "--water-screenshot") {
      if (i + 2 >= argc) {
	std::cerr << "--water-screenshot requires a feature and PNG path\n";
	return -1;
      }
      const std::string feature = argv[++i];
      water_shot = game::parse_water_shot (feature);
      if (!water_shot) {
	std::cerr << "unknown water feature: " << feature
		  << " (use river, confluence, mouth, waterfall, or lake)\n";
	return -1;
      }
      screenshot_path = argv[++i];
      config.fullscreen = false;
    } else if (arg == "--seed") {
      if (i + 1 >= argc) {
	std::cerr << "--seed requires an integer\n";
	return -1;
      }
      seed = std::atoi (argv[++i]);
    }
  }
  if (terrain_lab_preview)
    config.fullscreen = false;
  if (generation_profile == terrain::TerrainGenerationProfile::Fast
      && !world.city_mode && !world.pico_mode && !terrain_lab_preview)
    world.resolution = 1025;
  config.capture_frames = !screenshot_path.empty ();
  if (!screenshot_path.empty () && seed < 0)
    seed = 123;
  game::prune_obsolete_terrain_caches ();
  if (seed < 0)
    seed = game::remembered_seed (world, generation_profile);

  // Debug: override the sun height (e.g. 0.55 for long shadows).
  if (const char* sh = ::getenv ("MOPPE_SUNHEIGHT"))
    game::SUN_HEIGHT = (float) ::atof (sh);

  game::MoppeGame game
    (world, start_in_terrain_lab, terrain_lab_preview, seed,
	     std::move (screenshot_path), water_shot, generation_profile);

  try {
    return platform::run (game, config);
  } catch (const std::exception& e) {
    std::cerr << "\nError: " << e.what () << "\n";
    return -1;
  }
}
