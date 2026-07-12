#ifndef MOPPE_GAME_WORLD_HH
#define MOPPE_GAME_WORLD_HH

#include <moppe/color.hh>
#include <moppe/gfx/math.hh>
#include <moppe/terrain/topology.hh>

namespace moppe {
  namespace game {
    // World configuration -- the port of main.cc's file-scope mutable
    // globals (map_size, resolution, water_level, fog_scale, mode
    // flags), now passed explicitly.
    struct WorldParams {
      Vec3 map_size;        // world metres (x, height, z)
      int resolution;       // heightmap samples per side
      meters_t water_level; // sea level above the model zero datum
      float fog_scale;      // haze density per metre
      WorldParams ()
          : map_size (5000, 650, 5000), resolution (2049),
            water_level (50 * u::m), fog_scale (0.0004f) {}

      Vec3 spawn_position () const {
        return Vec3 (50, 600, 50);
      }

      bool toroidal () const {
        return true;
      }
      terrain::Topology topology () const {
        return toroidal () ? terrain::Topology::Torus
                           : terrain::Topology::Bounded;
      }
    };

    // Per-frame shared context handed to every system.  fog_color is
    // the hidden idle()->display() channel from the GL build, made
    // explicit; the camera basis replaces GL_MODELVIEW_MATRIX
    // readback for billboards (it already includes camera shake).
    struct FrameEnv {
      DisplayColor fog_color;
      float fog_scale;
      Vec3 sun_dir; // world space, toward the sun
      Vec3 camera_pos;
      Vec3 cam_right, cam_up, cam_forward;
      float time; // total game time (animation clock)
    };
  }
}

#endif
