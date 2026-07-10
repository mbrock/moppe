#ifndef MOPPE_GAME_TERRAIN_LAB_HH
#define MOPPE_GAME_TERRAIN_LAB_HH

#include <moppe/game/inspector_ui.hh>
#include <moppe/game/terrain.hh>
#include <moppe/game/world.hh>
#include <moppe/gfx/mat4.hh>
#include <moppe/map/generate.hh>
#include <moppe/platform/platform.hh>

#include <vector>

namespace moppe {
namespace game {
  // Interactive, renderer-backed workbench for heightmap generators and
  // transforms.  It temporarily borrows the game's map but snapshots and
  // restores it so experiments cannot alter the playable world.
  class TerrainLab {
  public:
    TerrainLab ();

    void load (render::Renderer& renderer);
    void enter (render::Renderer& renderer, map::RandomHeightMap& map,
		Terrain& terrain, const WorldParams& world, int seed,
		const Vector3D& sun_dir);
    void leave ();

    bool active () const { return m_active; }
    void tick (float dt);
    void key (platform::Key key, bool down);

    Vector3D position () const;
    Vector3D forward () const;
    Mat4 view_matrix () const;

    void draw (render::DrawList& dl, int width_pts,
	       int height_pts) const;

  private:
    void select (terrain::GeologicalLayer layer);
    void reset_field ();
    void refresh (bool inspection_fog = true);
    void restore_game_map ();

    InspectorUi m_ui;
    render::Renderer* m_renderer;
    map::RandomHeightMap* m_map;
    Terrain* m_terrain;
    const WorldParams* m_world;
    Vector3D m_sun_dir;
    std::vector<float> m_saved_heights;

    bool m_active;
    int m_seed;
    terrain::GeologicalLayer m_layer;
    int m_droplets;
    int m_thermal_passes;

    float m_yaw;
    float m_pitch;
    float m_distance;
    bool m_orbit_left;
    bool m_orbit_right;
    bool m_zoom_in;
    bool m_zoom_out;
    bool m_tilt_up;
    bool m_tilt_down;
  };
}
}

#endif
