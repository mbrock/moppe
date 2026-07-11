#ifndef MOPPE_GAME_TERRAIN_LAB_HH
#define MOPPE_GAME_TERRAIN_LAB_HH

#include <moppe/game/inspector_ui.hh>
#include <moppe/game/terrain.hh>
#include <moppe/game/world.hh>
#include <moppe/gfx/mat4.hh>
#include <moppe/map/generate.hh>
#include <moppe/map/terrain_evaluator.hh>
#include <moppe/platform/platform.hh>
#include <moppe/terrain/drainage.hh>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace moppe {
namespace game {
  enum class ParameterDomain {
    Continuous,
    Natural
  };

  // Interactive, renderer-backed workbench for heightmap generators and
  // transforms.  It temporarily borrows the game's map but snapshots and
  // restores it so experiments cannot alter the playable world.
  class TerrainLab {
  public:
    enum class ViewMode {
      Tile,
      Cover,
      Torus
    };

    TerrainLab ();

    void load (render::Renderer& renderer);
    void enter (render::Renderer& renderer, map::RandomHeightMap& map,
		Terrain& terrain, const WorldParams& world, int seed,
		const Vector3D& sun_dir);
    void leave ();

    bool active () const { return m_active; }
    bool cover_view () const { return m_view == ViewMode::Cover; }
    bool torus_view () const { return m_view == ViewMode::Torus; }
    void tick (float dt);
    void key (platform::Key key, bool down);
    void pointer_move (float x, float y, float dx, float dy);
    void pointer_button (platform::PointerButton button, bool down,
			 float x, float y);
    void pointer_scroll (float x, float y, float delta);

    Vector3D position () const;
    Vector3D forward () const;
    Mat4 view_matrix () const;

    void draw (render::DrawList& dl, int width_pts,
	       int height_pts) const;

  private:
    enum class OverlayMode {
      None,
      Height,
      Slope,
      Flow,
      Streams,
      Basins,
      Sinks,
      HeightDelta,
      Trace
    };

    void select (terrain::GeologicalLayer layer);
    void reset_program ();
    void rebuild_program ();
    void rerun_program_from (int first_stage);
    void append_stage (terrain::TerrainTransform stage);
    void move_selected_stage (int direction);
    void duplicate_selected_stage ();
    void remove_selected_stage ();
    float selected_property_normalized (int row) const;
    ParameterDomain selected_property_domain (int row) const;
    bool set_selected_property_normalized (int row, float value);
    bool adjust_selected_natural (int row, int direction);
    void queue_parameter_rebuild ();
    void run_pending_parameter_rebuild ();
    void handle_click (float x, float y);
    void ensure_selected_stage_visible ();
    void refresh (bool inspection_fog = true);
    void restore_game_map ();
    void fit_view ();
    void cycle_view ();
    void set_overlay (OverlayMode mode);
    void update_overlay ();
    void invalidate_analysis ();
    const terrain::DrainageGraph& drainage ();
    void inspect_drainage (float x, float y);

    InspectorUi m_ui;
    render::Renderer* m_renderer;
    map::RandomHeightMap* m_map;
    std::unique_ptr<terrain::FieldEvaluator> m_source_evaluator;
    std::unique_ptr<map::TerrainEvaluator> m_evaluator;
    Terrain* m_terrain;
    const WorldParams* m_world;
    Vector3D m_sun_dir;
    std::vector<float> m_saved_heights;

    bool m_active;
    terrain::TerrainProgram m_program;
    std::vector<map::TerrainCheckpoint> m_checkpoints;
    std::optional<terrain::DrainageGraph> m_drainage;
    OverlayMode m_overlay;
    bool m_analysis_dirty;
    std::string m_overlay_status;
    std::string m_analysis_status;
    std::optional<std::uint32_t> m_inspected_cell;
    int m_selected_stage;
    int m_stage_scroll;

    float m_pointer_x;
    float m_pointer_y;
    bool m_pointer_down;
    bool m_camera_drag;
    float m_camera_drag_distance;
    bool m_pan_drag;
    bool m_parameter_drag;
    int m_drag_property;
    float m_drag_start_y;
    float m_drag_start_normalized;
    bool m_parameter_rebuild_pending;
    int m_parameter_rebuild_stage;
    float m_parameter_rebuild_delay;

    Vector3D m_target;
    float m_yaw;
    float m_pitch;
    float m_distance;
    bool m_orbit_left;
    bool m_orbit_right;
    bool m_zoom_in;
    bool m_zoom_out;
    bool m_tilt_up;
    bool m_tilt_down;
    float m_scroll_zoom_target;
    ViewMode m_view;
  };
}
}

#endif
