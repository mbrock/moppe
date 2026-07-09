// The game: the port of main.cc's MoppeGLUT application class onto
// the platform/render abstractions.  World generation runs on a
// background thread behind a loading screen; the frame follows the
// exact pass order of the GL build's render_scene().

#include <moppe/platform/platform.hh>
#include <moppe/render/renderer.hh>
#include <moppe/render/text.hh>

#include <moppe/game/world.hh>
#include <moppe/game/terrain.hh>
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

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <ctime>
#include <iostream>
#include <memory>
#include <random>

namespace moppe {
namespace game {
  // THE sun: one fixed direction shared by the light, the shadow
  // map, the sky's sun disc, and the ocean glint.
  static const float SUN_AZIMUTH = 0.8f;
  static float SUN_HEIGHT = 0.75f;   // eternal golden afternoon

  static Vector3D
  sun_direction_for (float height) {
    const float el = (height - 0.5f) * 3.14159f;
    return Vector3D (std::cos (el) * std::sin (SUN_AZIMUTH),
		     std::sin (el),
		     std::cos (el) * std::cos (SUN_AZIMUTH));
  }

  // The fixed-function light colors from update_dynamic_lighting at
  // sun_height 0.75: the sunset branch, NOT white.
  static void
  sun_light_colors (float sun_height, Vector3D& diffuse,
		    Vector3D& specular) {
    const Vector3D day_color (1.0f, 0.98f, 0.9f);
    const Vector3D night_color (0.2f, 0.2f, 0.3f);
    const Vector3D sunset_color (1.0f, 0.6f, 0.3f);

    const bool is_sunset =
      (sun_height > 0.1f && sun_height < 0.3f) ||
      (sun_height > 0.7f && sun_height < 0.9f);

    if (is_sunset) {
      float k = (sun_height < 0.5f)
	? 1.0f - std::fabs (sun_height - 0.2f) / 0.1f
	: 1.0f - std::fabs (sun_height - 0.8f) / 0.1f;
      clamp (k, 0.0f, 1.0f);

      const float day_factor = (sun_height < 0.5f)
	? sun_height / 0.5f : 1.0f - (sun_height - 0.5f) / 0.5f;
      const Vector3D base = night_color * (1.0f - day_factor)
	+ day_color * day_factor;

      diffuse = base * (1.0f - k) + sunset_color * k;
      specular = Vector3D (1.0f, 0.9f, 0.8f) * (0.7f + k * 0.3f);
    } else {
      diffuse = night_color * (1.0f - sun_height)
	+ day_color * sun_height;
      specular = Vector3D (0.8f, 0.8f, 0.9f)
	* (0.4f + sun_height * 0.6f);
    }
  }

  // Sky horizon color (port of gfx::Sky::get_horizon_color): the
  // CPU twin of the sky shader's horizon math, used to derive the
  // fog color each tick.
  static Vector3D
  horizon_color_for (float sun_height) {
    const Vector3D day_horizon (0.3f, 0.5f, 0.9f);
    const Vector3D night_horizon (0.05f, 0.05f, 0.1f);
    const Vector3D sunset_horizon (0.7f, 0.4f, 0.3f);

    const bool is_sunset =
      (sun_height > 0.1f && sun_height < 0.3f) ||
      (sun_height > 0.7f && sun_height < 0.9f);

    if (is_sunset) {
      float k = (sun_height < 0.5f)
	? 1.0f - std::fabs (sun_height - 0.2f) / 0.1f
	: 1.0f - std::fabs (sun_height - 0.8f) / 0.1f;
      clamp (k, 0.0f, 1.0f);

      const Vector3D base = night_horizon * (1.0f - sun_height)
	+ day_horizon * sun_height;
      return base * (1.0f - k) + sunset_horizon * k;
    }
    return night_horizon * (1.0f - sun_height)
      + day_horizon * sun_height;
  }

  class MoppeGame: public platform::Game {
  public:
    MoppeGame (const WorldParams& world)
      : m_world (world),
	m_spawn_position (world.spawn_position ()),
	m_map (world.resolution, world.resolution, world.map_size,
	       (int) ::time (0)),
	m_camera (18, 6.5f * one_meter),
	// Dirt-bike figures: 2600 N of launch, 30 kW of engine --
	// hard low-end punch, ~125 km/h against drag (the old
	// 5000 N constant force topped out near 300).
	m_vehicle (world.spawn_position (), 45, m_map, 2600, 30000,
		   150),
	m_car (world.spawn_position (), 45, m_map, 14000, 100000,
	       900),
	m_renderer (0),
	m_ready (false),
	m_gen_stage (0),
	m_total_time (0),
	m_shake (0),
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
	m_fx_rng (7)
    { }

    // -- lifecycle ---------------------------------------------------

    void setup (render::Renderer& r, int, int) override {
      m_renderer = &r;

      // Fast, main-thread resource setup; the heavy world build
      // runs behind the loading screen.
      m_hud.load (r);
      m_loading_font.reset
	(new render::FontAtlas (r, "Helvetica", 14,
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

	// Always retain the best fallback.  The large shore penalty makes
	// even an unusual generated map prefer dry ground over a flat seabed.
	const float shore_penalty =
	  std::max (0.0f, min_ground - low) * 2.0f;
	const float alpine_penalty =
	  std::max (0.0f, h - max_ground) * 0.03f;
	const float score = up * 20.0f - relief * 0.2f
	  - shore_penalty - alpine_penalty;
	if (score > fallback_score) {
	  fallback_score = score;
	  fallback = Vector3D (x, h + 1.2f, z);
	}

	if (low < min_ground || high > max_ground ||
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
      // Exceptions must not escape the GCD block (std::terminate);
      // surface them on the loading screen instead.
      try {
	generate_world_inner ();
      } catch (const std::exception& e) {
	std::cerr << "world generation failed: " << e.what ()
		  << std::endl;
	m_gen_stage = 7;
      }
    }

    void generate_world_inner () {
      if (m_world.city_mode) {
	m_gen_stage = 1;
	m_city.generate (m_map, m_world);
      } else if (m_world.pico_mode) {
	m_gen_stage = 2;
	m_map.load_raw_u16 (platform::asset_path ("data/pico.u16"),
			    0.1f, m_world.map_size.y);
      } else {
	m_gen_stage = 3;
	m_map.randomize_geologically ();
	// Slight lowland squash; ~10-15% ends up as ocean
	m_map.exponentiate (1.15);
	m_gen_stage = 4;
	m_map.erode_hydraulically (1500000);
	// Talus angle ~40 degrees at 2.4m cells, 650m height scale
	m_map.erode_thermally (2, 0.003f);
      }
      m_gen_stage = 5;
      m_map.recompute_normals ();

      m_vegetation.generate (m_map, m_world,
			     m_world.pico_mode ? 6000
			     : m_world.city_mode ? 500 : 2200,
			     m_world.pico_mode ? 4000
			     : m_world.city_mode ? 300 : 1500);
      m_stars.generate (m_map, m_world,
			m_world.pico_mode ? 250
			: m_world.city_mode ? 130 : 80);
      m_fish.generate (m_map, m_world);
      m_wildlife.generate (m_map, m_world);
      m_gen_stage = 6;
    }

    void finish_setup () {
      if (m_gen_stage == 7)
	return;   // generation failed; loading screen shows why
      render::Renderer& r = *m_renderer;

      m_terrain.setup (r, m_map, m_world);
      m_terrain.render_shadow (r, m_map,
			       sun_direction_for (SUN_HEIGHT));
      m_vegetation.load (r);
      if (m_world.city_mode)
	m_city.load (r);

      render::OceanSetup ocean;
      ocean.level = m_world.water_level;
      ocean.center = Vector3D (m_world.map_size.x / 2, 0,
			       m_world.map_size.z / 2);
      ocean.half_extent = m_world.pico_mode
	? 0.55f * m_world.map_size.x : 5500 * one_meter;
      ocean.cells = 300;
      r.set_ocean (ocean);

      m_vehicle.set_water_level (m_world.water_level);
      m_car.set_water_level (m_world.water_level);
      m_vehicle.set_obstacles (&m_city.obstacles ());
      m_car.set_obstacles (&m_city.obstacles ());

      if (!m_world.city_mode && !m_world.pico_mode) {
	m_spawn_position = choose_landscape_spawn ();
	m_vehicle.reset (m_spawn_position);

	std::uniform_real_distribution<float> heading
	  (0.0f, 2.0f * 3.14159f);
	const float a = heading (m_fx_rng);
	m_vehicle.set_heading (Vector3D (std::sin (a), 0,
				       std::cos (a)));
      }

      m_ready = true;
    }

    // -- simulation --------------------------------------------------

    void tick (float dt) override {
      if (!m_ready || m_game_over)
	return;

      m_total_time += dt;
      const float total_time = m_total_time;

      // Screenshot autopilot for headless verification: rides in a
      // lazy arc with periodic boost-assisted leaps.
      static const bool demo = ::getenv ("MOPPE_DEMO") != 0;
      if (demo) {
	input_go (1.0f);
	input_turn (0.35f * std::sin (total_time * 0.25f));
	input_boost (std::fmod (total_time, 11.0f) < 1.35f ? 1.0f : 0.0f);
      }

      // Weather: slowly drifting cloudiness with passing fronts.
      float cloudiness =
	std::sin (total_time * 0.0003f) * 0.4f + 0.5f
	+ 0.3f * std::pow (std::sin (total_time * 0.0008f), 2.0f)
	+ std::sin (total_time * 0.02f) * 0.05f;
      clamp (cloudiness, 0.0f, 1.0f);
      m_cloudiness = cloudiness;

      // Fog color: sky horizon shifted toward a milky pale blue.
      const Vector3D horizon = horizon_color_for (SUN_HEIGHT);
      m_fog = horizon * 0.75f + Vector3D (0.93f, 0.95f, 1.0f) * 0.25f;

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
      const Vector3D dust_color (0.72f, 0.63f, 0.48f);
      const Vector3D spray_color (0.85f, 0.92f, 1.0f);

      // Drift kicks up dirt from the rear wheel (or spray).
      if (driving && av.grounded () && av.drift_speed () > 6.0f) {
	Vector3D back = vpos - av.orientation () * 1.4f;
	back.y = vpos.y - 0.7f;
	int n = std::min (6, (int) (av.drift_speed () * 0.25f));
	m_dust.emit (back, av.velocity () * 0.15f, n,
		     in_water ? spray_color : dust_color);
      }

      // Wading fast throws up a bow wave.
      if (driving && in_water && av.velocity ().length () > 15.0f)
	m_dust.emit (vpos + Vector3D (0, -0.5f, 0),
		     av.velocity () * 0.3f, 3, spray_color);

      // Hard landings shake the camera and burst dirt outward.
      const float impact = driving ? av.pop_impact () : 0.0f;
      if (impact > 8.0f) {
	m_shake = std::min (0.45f, 0.018f * impact);
	m_dust.emit (vpos + Vector3D (0, -0.7f, 0),
		     av.velocity () * 0.2f, 18,
		     in_water ? spray_color : dust_color);
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
	  m_dust.emit (m_stars.last_pos (), Vector3D (0, 3, 0), 16,
		       Vector3D (1.0f, 0.85f, 0.2f));
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
      m_shake *= std::exp (-4.0f * dt);

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
      fp.proj = Mat4::perspective_reversed
	(degrees_to_radians (100.0f + 9.0f * m_fov_k), aspect, 0.5f,
	 m_world.pico_mode ? 30000.0f : 9000.0f);

      // Hard-landing camera shake, faded near the ground so the
      // view can't dip below the terrain.
      Mat4 view = m_camera.view_matrix ();
      if (m_shake > 0.005f) {
	const Vector3D cam = m_camera.position ();
	const float ground =
	  m_map.interpolated_height (cam.x, cam.z);
	const float clearance = cam.y - ground;
	const float room = std::min
	  (1.0f, std::max (0.0f, (clearance - 2.0f) / 8.0f));

	std::uniform_real_distribution<float> u (-1.0f, 1.0f);
	view = view
	  * Mat4::rotation (degrees_to_radians
			    (m_shake * room * u (m_fx_rng)),
			    Vector3D (0, 0, 1))
	  * Mat4::rotation (degrees_to_radians
			    (m_shake * room * u (m_fx_rng)),
			    Vector3D (1, 0, 0));
      }
      fp.view = view;

      const Vector3D cam = m_camera.position ();
      fp.camera_pos = cam;
      fp.cam_right = Vector3D (view.m[0], view.m[4], view.m[8]);
      fp.cam_up = Vector3D (view.m[1], view.m[5], view.m[9]);
      fp.cam_forward = Vector3D (-view.m[2], -view.m[6], -view.m[10]);
      fp.clear_color = m_fog;
      fp.fog_scale = m_world.fog_scale;
      fp.sun_dir = sun_direction_for (SUN_HEIGHT);
      sun_light_colors (SUN_HEIGHT, fp.sun_diffuse, fp.sun_specular);
      fp.sun_specular = fp.sun_specular * 0.5f;   // material specular
      fp.ambient = Vector3D (0.2f, 0.2f, 0.2f);   // GL light-model default
      fp.time = m_total_time;

      if (!r.begin_frame (fp))
	return;

      FrameEnv env;
      env.fog_color = m_fog;
      env.fog_scale = m_world.fog_scale;
      env.sun_dir = fp.sun_dir;
      env.camera_pos = cam;
      env.cam_right = fp.cam_right;
      env.cam_up = fp.cam_up;
      env.cam_forward = fp.cam_forward;
      env.time = m_total_time;

      // Terrain first, chunk-culled to the haze horizon.
      m_terrain.render (r, cam, m_camera.forward (),
			3.0f / m_world.fog_scale);

      // Sky AFTER the terrain: depth testing kills the expensive
      // cloud shader wherever terrain covers it.
      render::SkyParams sky;
      sky.time = m_total_time;
      sky.sun_height = SUN_HEIGHT;
      sky.cloudiness = m_cloudiness;
      sky.sun_dir = fp.sun_dir;
      sky.fog_color = m_fog;
      r.draw_sky (sky);

      // The world draw list, in the GL build's draw order.
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

      // Translucent water late so the seabed and fish show
      // through; dust last so spray sits atop the surface.
      render::OceanParams ocean;
      ocean.time = m_total_time;
      ocean.fog_color = m_fog;
      ocean.fog_scale = m_world.fog_scale;
      r.draw_ocean (ocean);

      m_dust_dl.clear ();
      m_dust.render (m_dust_dl, env);
      r.draw_list (m_dust_dl);

      // Post effects.
      if (cam.y < m_world.water_level)
	r.apply_underwater (m_total_time);
      {
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
      hs.on_foot = (m_mode == M_FOOT);
      m_hud.draw (m_hud_dl, hs,
		  r.width_pts () - (int) (si.left + si.right),
		  r.height_pts () - (int) (si.top + si.bottom));
      r.draw_hud (m_hud_dl);

      r.end_frame ();
    }

    void render_loading (render::Renderer& r) {
      render::FrameParams fp;
      fp.clear_color = Vector3D (0.06f, 0.07f, 0.09f);
      fp.view = Mat4 ();
      fp.proj = Mat4 ();
      if (!r.begin_frame (fp))
	return;

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
	"Eroding (1.5 million droplets)...",
	"Computing normals, planting things...",
	"Uploading to the GPU...",
	"World generation failed -- see the log.",
      };
      const int stage = m_gen_stage;
      const char* text = stages[stage < 0 ? 0 : stage > 7 ? 7 : stage];

      const float w = (float) r.width_pts ();
      const float h = (float) r.height_pts ();
      if (m_loading_font && m_loading_font->ok ()) {
	m_hud_dl.color (0.85f, 0.87f, 0.9f);
	const float tw = m_loading_font->measure (text);
	m_loading_font->draw (m_hud_dl, (w - tw) / 2, h / 2, text);
      }

      // A pulsing bar so the screen visibly lives during erosion.
      const double t = platform::now ();
      const float pulse =
	0.5f + 0.5f * (float) std::sin (t * 2.6);
      m_hud_dl.color (0.35f + 0.3f * pulse, 0.5f, 0.85f, 0.9f);
      m_hud_dl.begin (render::Prim::Quads);
      m_hud_dl.vertex (w / 2 - 120, h / 2 + 24);
      m_hud_dl.vertex (w / 2 + 120, h / 2 + 24);
      m_hud_dl.vertex (w / 2 + 120, h / 2 + 30);
      m_hud_dl.vertex (w / 2 - 120, h / 2 + 30);
      m_hud_dl.end ();

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
      if (m_game_over)
	return;
      input_turn (state.steer);
      input_go (state.drive);
      input_boost (state.boost);
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
    map::RandomHeightMap m_map;
    Terrain m_terrain;
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
    std::unique_ptr<render::FontAtlas> m_loading_font;

    render::Renderer* m_renderer;
    std::atomic<bool> m_ready;
    std::atomic<int> m_gen_stage;

    render::DrawList m_world_dl;
    render::DrawList m_dust_dl;
    render::DrawList m_hud_dl;

    // double: a float accumulator quantizes 60 Hz ticks after ~18 h
    // and stops advancing entirely after ~24 days.
    double m_total_time;
    float m_cloudiness = 0.5f;
    Vector3D m_fog;
    float m_shake;
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
    std::mt19937 m_fx_rng;
  };
}
}

int
main (int argc, char** argv) {
  using namespace moppe;

  game::WorldParams world;
  platform::Config config;
  config.title = "Moppe";

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
    }
  }

  // Debug: override the sun height (e.g. 0.55 for long shadows).
  if (const char* sh = ::getenv ("MOPPE_SUNHEIGHT"))
    game::SUN_HEIGHT = (float) ::atof (sh);

  game::MoppeGame game (world);

  try {
    return platform::run (game, config);
  } catch (const std::exception& e) {
    std::cerr << "\nError: " << e.what () << "\n";
    return -1;
  }
}
