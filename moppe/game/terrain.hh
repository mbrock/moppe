#ifndef MOPPE_GAME_TERRAIN_HH
#define MOPPE_GAME_TERRAIN_HH

#include <moppe/game/graphics_settings.hh>
#include <moppe/game/world.hh>
#include <moppe/map/surface.hh>
#include <moppe/render/renderer.hh>
#include <moppe/terrain/watercourse.hh>

#include <array>
#include <optional>
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
      // after the surface changes.  The surface owns the typed
      // elevation and normal columns uploaded here. The optional water
      // sheet shares its lattice and extends the bounds, not the ground.
      void setup (render::Renderer& r,
                  const map::SurfaceGeometry& surface,
                  const WorldParams& world,
                  const GraphicsSettings& graphics,
                  const terrain::WaterSheets* water = nullptr);

      // Where a cascade of the sun's map is centred: a point that follows
      // the camera in whole texels, kept in double precision so that its
      // walk does not itself wobble the map.
      using ShadowAnchor = std::optional<std::array<meters_f64_t, 3>>;

      // Updates the production camera-local shadow level. The focus and
      // radius remain dimensional until the renderer boundary. `sky_pole`
      // is the axis the key light turns about through the day; the map is
      // squared to it rather than to the zenith.
      void render_local_shadow (render::Renderer& r,
                                position_t camera,
                                const direction_t& view_dir,
                                const direction_t& sun_dir,
                                const direction_t& sky_pole);

      // Emits culled chunk draws: distance cull against max_dist plus
      // the view frustum's side planes.  Five nested LODs run from a
      // smoothly subdivided near field to a stride-8 haze ring; a chunk
      // that reaches the near field is split into patches so only the
      // near ones pay for subdivision.
      void render (render::Renderer& r,
                   position_t cam,
                   const Mat4& view_proj,
                   meters_t max_dist);

    private:
      static constexpr int PATCHES =
        render::terrain_chunk_cells / render::terrain_patch_cells;

      struct Chunk {
        int x0, z0; // grid origin
        position_t center;
        meters_t radius;
        // Horizontal Gerstner excursion; zero without waves on water.
        meters_t wave_reach = 0.0f * u::m;
        // Lowest ground and highest wave crest over the chunk.
        terrain::SurfaceElevation ymin, ymax;
        // Height range of each patch, row-major.
        terrain::SurfaceElevation patch_ymin[PATCHES * PATCHES];
        terrain::SurfaceElevation patch_ymax[PATCHES * PATCHES];
      };

      std::vector<Chunk> m_chunks;
      std::vector<render::ChunkDraw> m_draws;
      // One lattice step along each axis.
      meters_t m_cell_x = 1.0f * u::m;
      meters_t m_cell_z = 1.0f * u::m;
      plan_extent_t m_period;
      // The coarser step: what turns the LOD distances, counted in source
      // cells, into metres.
      meters_t m_lod_scale = 1.0f * u::m;
      std::array<ShadowAnchor, 2> m_shadow_anchors;
      render::TexturePtr m_grass;
    };
  }
}

#endif
