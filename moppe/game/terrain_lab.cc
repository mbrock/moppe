#include <moppe/game/terrain_lab.hh>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace moppe {
namespace game {
  TerrainLab::TerrainLab ()
    : m_renderer (0), m_map (0), m_terrain (0), m_world (0),
      m_active (false), m_seed (0),
      m_field (map::GeologicalField::Combined),
      m_droplets (0), m_thermal_passes (0),
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
    m_seed = seed;
    m_field = map::GeologicalField::Combined;
    m_orbit_left = m_orbit_right = false;
    m_zoom_in = m_zoom_out = false;
    m_tilt_up = m_tilt_down = false;

    const size_t count = (size_t) map.width () * map.height ();
    m_saved_heights.assign (map.raw_heights (),
			    map.raw_heights () + count);
    m_active = true;
    reset_field ();
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
  TerrainLab::select (map::GeologicalField field)
  {
    if (field == m_field && m_droplets == 0 && m_thermal_passes == 0)
      return;
    m_field = field;
    reset_field ();
  }

  void
  TerrainLab::reset_field ()
  {
    if (!m_map)
      return;
    m_map->reseed (m_seed);
    m_map->randomize_geologically (m_field);
    m_droplets = 0;
    m_thermal_passes = 0;
    refresh ();
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
    m_pitch = std::max (0.18f, std::min (1.28f, m_pitch));
    m_distance *= std::exp (zoom * dt * 1.1f);
    m_distance = std::max (700.0f, std::min (9000.0f, m_distance));
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
    case Key::One:   select (map::GeologicalField::Combined); break;
    case Key::Two:   select (map::GeologicalField::Continent); break;
    case Key::Three: select (map::GeologicalField::Plains); break;
    case Key::Four:  select (map::GeologicalField::Mountains); break;
    case Key::Five:  select (map::GeologicalField::MountainMask); break;
    case Key::Six:   select (map::GeologicalField::WarpX); break;
    case Key::Seven: select (map::GeologicalField::WarpY); break;
    case Key::R:
      reset_field ();
      break;
    case Key::N:
      ++m_seed;
      reset_field ();
      break;
    case Key::E:
      m_map->erode_hydraulically (100000);
      m_droplets += 100000;
      refresh ();
      break;
    case Key::Y:
      m_map->erode_thermally (2, 0.003f);
      m_thermal_passes += 2;
      refresh ();
      break;
    default:
      break;
    }
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
    m_ui.begin (dl);
    m_ui.panel (dl, 14, 14, 350, 316, "TERRAIN LAB");

    std::ostringstream seed;
    seed << "Seed " << m_seed;
    m_ui.label (dl, 28, 67, seed.str ());
    m_ui.label (dl, 28, 88, map::geological_field_name (m_field), true);

    std::ostringstream transforms;
    transforms << "Transforms: " << m_droplets << " droplets, "
	       << m_thermal_passes << " thermal passes";
    m_ui.label (dl, 28, 109, transforms.str ());

    m_ui.key_hint (dl, 28, 139, "1-7", "noise fields / combined");
    m_ui.key_hint (dl, 28, 165, "R", "reset this field");
    m_ui.key_hint (dl, 28, 191, "N", "next deterministic seed");
    m_ui.key_hint (dl, 28, 217, "E", "+100,000 erosion droplets");
    m_ui.key_hint (dl, 28, 243, "Y", "+2 thermal passes");
    m_ui.key_hint (dl, 28, 269, "arrows", "orbit / zoom");
    m_ui.key_hint (dl, 28, 295, "W/S", "tilt camera");
    m_ui.key_hint (dl, 226, 295, "T", "return to game");
    m_ui.end (dl);
  }
}
}
