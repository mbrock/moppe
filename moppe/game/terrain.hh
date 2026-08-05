#ifndef MOPPE_GAME_TERRAIN_HH
#define MOPPE_GAME_TERRAIN_HH

#include <moppe/game/graphics_settings.hh>
#include <moppe/game/world.hh>
#include <moppe/map/surface.hh>
#include <moppe/render/renderer.hh>

#include <vector>

namespace moppe {
  namespace game {
    // Game-side terrain: uploads the height/normal arrays (the same
    // ones physics samples), builds chunk bounding spheres, culls
    // chunks per frame, and computes the one-time sun-shadow matrix.
    // Replaces gfx::TerrainRenderer + gfx::ShadowMap.
    class Terrain {
    public:
      // Uploads heights/normals and the splat textures; call again
      // after the surface changes (e.g. city baking).  Takes the
      // Surface owns the typed elevation and normal columns uploaded here.
      void setup (render::Renderer& r,
                  const map::SurfaceGeometry& surface,
                  const WorldParams& world,
                  const GraphicsSettings& graphics);

      // Renders the one-time shadow map.  sun_dir points toward the
      // sun, world space.
      void render_shadow (render::Renderer& r, const Vec3& sun_dir);

      // Updates the production camera-local shadow level. The focus and
      // radius remain dimensional until the renderer boundary.
      void render_local_shadow (render::Renderer& r,
                                position_t camera,
                                const Vec3& view_dir,
                                const Vec3& sun_dir);

      // Emits culled chunk draws: distance cull against max_dist plus
      // a conservative behind-camera test.  Five nested LODs run from
      // a bilinearly subdivided near field to a stride-8 haze ring.
      void render (render::Renderer& r,
                   const Vec3& cam,
                   const Vec3& view_dir,
                   float max_dist);

    private:
      struct Chunk {
        int x0, z0; // grid origin
        Vec3 center;
        float radius;
      };

      std::vector<Chunk> m_chunks;
      std::vector<render::ChunkDraw> m_draws;
      Vec3 m_scale;
      Vec3 m_period;
      Vec3 m_extent;
      float m_lod_scale = 1;
      render::TexturePtr m_grass, m_dirt, m_rock, m_snow;
      bool m_textures_loaded = false;
    };
  }
}

#endif
