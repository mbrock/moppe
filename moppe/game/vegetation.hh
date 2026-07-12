#ifndef MOPPE_GAME_VEGETATION_HH
#define MOPPE_GAME_VEGETATION_HH

#include <moppe/game/world.hh>
#include <moppe/map/generate.hh>
#include <moppe/render/renderer.hh>

#include <vector>

namespace moppe {
  namespace game {
    // Scattered flora, baked into sector meshes so only what the haze
    // lets the camera see is drawn.  Two granularities: structural
    // growth (trunks, canopies, bush volumes, contact shadows) on the
    // coarse structure grid drawn out to the haze horizon, and fine detail
    // (leaf quads, grass tufts, flowers, shoreline reeds) on a finer grid
    // drawn only near the camera, where it registers.  All
    // of it sways: vertices carry wind weights the scene vertex shader
    // animates, so the baked meshes stay static buffers.
    class Vegetation {
    public:
      // Named population settings keep world-mode tuning out of the
      // generation algorithm and make custom worlds straightforward.
      struct Population {
        int trees;
        int bushes;
        int max_tufts;
        int max_flowers;
        int reeds;

        Population (int tree_count,
                    int bush_count,
                    int tuft_limit,
                    int flower_limit,
                    int reed_count)
            : trees (tree_count), bushes (bush_count), max_tufts (tuft_limit),
              max_flowers (flower_limit), reeds (reed_count) {}
      };

      static Population population_for (const WorldParams& world);

      // Retains the terrain inputs needed by procedural grass even when the
      // placed vegetation component is disabled.
      void prepare (const map::HeightMap& map, const WorldParams& world);

      void generate (const map::HeightMap& map,
                     const WorldParams& world,
                     const Population& population);

      // Bakes the sector and detail-cell meshes from the plant list.
      void load (render::Renderer& r);

      // Draws the sectors within fog reach, detail cells within
      // detail reach.
      void render (render::Renderer& r,
                   const FrameEnv& env,
                   bool draw_vegetation,
                   float grass_density,
                   const Vector3D& grass_scale = Vector3D (1, 1, 1));

    private:
      static const int STRUCTURE_GRID = 6;
      static const int DETAIL_GRID = 14;

      enum class Species : uint8_t {
        Broadleaf,
        Conifer,
        Birch,
        Bush,
        Tuft,
        Flower,
        Reed
      };

      struct Plant {
        Species species;
        uint8_t palette;   // flower/bush bloom color index
        Vector3D position; // ground point
        Vector3D ground_normal;
        float scale;
        float tint;    // per-plant color variance
        uint32_t seed; // deterministic per-plant detail
      };

      void append_plant (Species species,
                         const Vector3D& position,
                         const Vector3D& normal,
                         float scale,
                         float tint,
                         uint32_t seed,
                         uint8_t palette = 0);
      int cell_of (const Vector3D& position, int grid) const;
      void record_structure (render::DrawList& dl, const Plant& plant) const;
      void record_shadow (render::DrawList& dl, const Plant& plant) const;
      void record_detail (render::DrawList& dl, const Plant& plant) const;
      void render_grid (render::Renderer& r,
                        const render::MeshPtr* meshes,
                        int grid,
                        float reach,
                        const Vector3D& camera) const;
      void record_near_grass (const FrameEnv& env);

      std::vector<Plant> m_plants;
      render::MeshPtr m_meshes[STRUCTURE_GRID * STRUCTURE_GRID];
      render::MeshPtr m_detail[DETAIL_GRID * DETAIL_GRID];
      Vector3D m_map_size;
      bool m_lean; // pico mode: cheaper per-plant geometry
      bool m_periodic = false;

      // Near-field grass: a deterministic hash grid of tufts around
      // the camera, re-recorded each frame so there's always grass at
      // the wheels without baking millions of blades.
      const map::HeightMap* m_map = nullptr;
      float m_water = 0;
      float m_height = 1;
      render::DrawList m_near;
    };
  }
}

#endif
