#ifndef MOPPE_GAME_VEGETATION_HH
#define MOPPE_GAME_VEGETATION_HH

#include <moppe/game/world.hh>
#include <moppe/map/generate.hh>
#include <moppe/render/renderer.hh>

#include <vector>

namespace moppe {
namespace game {
  // Scattered trees and bushes, bucketed into 6x6 sector meshes so
  // only the sectors within fog reach are drawn.  Port of main.cc's
  // Vegetation; display lists become baked meshes and the plant
  // list is retained between generate() and load().
  class Vegetation {
  public:
    static const int VSEC = 6;

    // Rejection-samples plant spots (fixed seed 1234, same rules
    // as the GL build).  `count` is the tree count; the bush count
    // follows the original per-mode call site (pico 4000, city
    // 300, else 1500).
    void generate (const map::HeightMap& map,
		   const WorldParams& world, int count);

    // Explicit counts (GL build: pico 6000/4000, city 500/300,
    // default 2200/1500).
    void generate (const map::HeightMap& map,
		   const WorldParams& world,
		   int trees, int bushes);

    // Bakes the 36 sector meshes from the retained plant list.
    void load (render::Renderer& r);

    // Draws the sectors within fog-visibility reach of the camera.
    void render (render::Renderer& r, const FrameEnv& env);

  private:
    struct Plant {
      bool tree;
      float x, y, z, s, tint;
    };

    int sector_of (float x, float z) const;

    std::vector<Plant> m_plants;
    render::MeshPtr m_meshes[VSEC * VSEC];
    Vector3D m_map_size;
  };
}
}

#endif
