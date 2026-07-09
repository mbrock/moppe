#ifndef MOPPE_GAME_WORLD_HH
#define MOPPE_GAME_WORLD_HH

#include <moppe/gfx/math.hh>

namespace moppe {
namespace game {
  // World configuration -- the port of main.cc's file-scope mutable
  // globals (map_size, resolution, water_level, fog_scale, mode
  // flags), now passed explicitly.
  struct WorldParams {
    Vector3D map_size;          // world meters (x, height, z)
    int resolution;             // heightmap samples per side
    float water_level;          // meters
    float fog_scale;            // haze density per meter
    bool pico_mode;
    bool city_mode;

    WorldParams ()
      : map_size (5000 * one_meter, 650 * one_meter,
		  5000 * one_meter),
	resolution (2049),
	water_level (50.0f),
	fog_scale (0.0004f),
	pico_mode (false),
	city_mode (false)
    { }

    Vector3D spawn_position () const {
      if (city_mode)
	return Vector3D (map_size.x * 0.5f, 0, map_size.z * 0.5f);
      if (pico_mode)
	return Vector3D (map_size.x * 0.47f, 0, map_size.z * 0.40f);
      return Vector3D (map_size.x * 0.5f, 0, map_size.z * 0.35f);
    }
  };

  // Per-frame shared context handed to every system.  fog_color is
  // the hidden idle()->display() channel from the GL build, made
  // explicit; the camera basis replaces GL_MODELVIEW_MATRIX
  // readback for billboards (it already includes camera shake).
  struct FrameEnv {
    Vector3D fog_color;
    float fog_scale;
    Vector3D sun_dir;           // world space, toward the sun
    Vector3D camera_pos;
    Vector3D cam_right, cam_up, cam_forward;
    float time;                 // total game time (animation clock)
  };
}
}

#endif
