#ifndef MOPPE_GAME_TERRAIN_LAB_HH
#define MOPPE_GAME_TERRAIN_LAB_HH

#include <moppe/game/graphics_settings.hh>
#include <moppe/game/inspector_ui.hh>
#include <moppe/game/river_surface.hh>
#include <moppe/game/terrain.hh>
#include <moppe/game/world.hh>
#include <moppe/gfx/mat4.hh>
#include <moppe/map/generate.hh>
#include <moppe/map/terrain_evaluator.hh>
#include <moppe/platform/platform.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/fractional_drainage.hh>
#include <moppe/terrain/transform.hh>

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace moppe {
  namespace game {
    // Interactive, renderer-backed workbench for heightmap generators and
    // transforms.  It temporarily borrows the game's map but snapshots and
    // restores it so experiments cannot alter the playable world.
    class TerrainLab {
    public:
      enum class ViewMode { Tile, Cover, Torus };

      TerrainLab ();

      void load (render::Renderer& renderer);
      void enter (render::Renderer& renderer,
                  map::RandomHeightMap& map,
                  Terrain& terrain,
                  const WorldParams& world,
                  const GraphicsSettings& graphics,
                  const terrain::TerrainProgram& program,
                  std::span<const float> trail_influence,
                  std::span<const float> home_base_influence,
                  const std::vector<std::vector<float>>& history,
                  const Vec3& sun_dir);
      void leave ();

      bool active () const {
        return m_active;
      }
      bool cover_view () const {
        return m_view == ViewMode::Cover;
      }
      bool torus_view () const {
        return m_view == ViewMode::Torus;
      }
      // True until a pipeline edit rebuilds the map: the terrain on
      // screen is still the game's own, so the game's water sheets and
      // full-quality setup remain valid.
      bool map_pristine () const {
        return m_map_pristine;
      }
      // The haze the current lab view renders with; the orbit cameras
      // sit kilometres out, so full gameplay fog would swallow the map.
      attenuation_t scene_fog (attenuation_t world_fog) const;
      void tick (float dt);
      void key (platform::Key key, bool down);
      void pointer_move (float x, float y, float dx, float dy);
      void pointer_button (platform::PointerButton button,
                           bool down,
                           float x,
                           float y);
      void pointer_scroll (float x, float y, float delta);

      Vec3 position () const;
      Vec3 forward () const;
      Mat4 view_matrix () const;
      void render_rivers (render::Renderer& renderer, const Vec3& camera) const;

      void draw (render::DrawList& dl, int width_pts, int height_pts);

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
        Trace,
        StandingWater,
        PermanentWater,
        Waterfalls,
        Eroded,
        Deposited
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
      terrain::ParameterDomain selected_property_domain (int row) const;
      bool selected_property_drag_enabled (int row) const;
      bool set_selected_property_normalized (int row, float value);
      bool adjust_selected_continuous (int row, int direction);
      bool adjust_selected_natural (int row, int direction);
      void queue_parameter_rebuild ();
      void run_pending_parameter_rebuild ();
      void handle_build_click (float x, float y);
      void handle_readings_click (float x, float y);
      void handle_friendly_click (float x, float y);
      void apply_friendly_preset (int preset);
      void
      draw_friendly (render::DrawList& dl, int width_pts, int height_pts) const;
      void draw_compass (render::DrawList& dl, int width_pts) const;
      void draw_build (render::DrawList& dl) const;
      void ensure_selected_stage_visible ();
      void refresh (bool inspection_fog = true);
      void restore_game_map ();
      void fit_view ();
      void cycle_view ();
      void set_overlay (OverlayMode mode);
      void update_overlay ();
      void invalidate_analysis ();
      const terrain::DrainageGraph& drainage ();
      const terrain::FloodField& standing_water ();
      void inspect_drainage (float x, float y);
      std::optional<Vec3> terrain_point_at_screen (float x, float y) const;
      void show_history_snapshot (std::size_t index);
      std::string history_snapshot_name (std::size_t index) const;

      InspectorUi m_ui;
      UiWindow m_observe_window;
      UiWindow m_build_window;
      UiWindow m_readings_window;
      render::Renderer* m_renderer;
      map::RandomHeightMap* m_map;
      std::unique_ptr<terrain::FieldEvaluator> m_source_evaluator;
      std::unique_ptr<map::TerrainEvaluator> m_evaluator;
      Terrain* m_terrain;
      const WorldParams* m_world;
      const GraphicsSettings* m_graphics;
      Vec3 m_sun_dir;
      std::vector<float> m_saved_heights;
      std::vector<float> m_saved_trail_influence;
      std::vector<float> m_saved_home_base_influence;
      const std::vector<std::vector<float>>* m_history;
      std::size_t m_history_index;
      float m_history_age;
      bool m_history_playing;

      bool m_active;
      bool m_map_pristine;
      terrain::TerrainProgram m_program;
      std::vector<map::TerrainCheckpoint> m_checkpoints;
      std::vector<terrain::TerrainTransformReport> m_reports;
      std::optional<terrain::DrainageGraph> m_drainage;
      std::optional<terrain::FractionalDrainage> m_channel_drainage;
      std::optional<terrain::WaterNetwork> m_water_network;
      std::optional<terrain::RiverNetwork> m_rivers;
      RiverSurface m_river_surface;
      std::optional<terrain::FloodField> m_flood;
      std::optional<terrain::LakeCensus> m_lakes;
      OverlayMode m_overlay;
      std::string m_overlay_status;
      std::string m_analysis_status;
      std::string m_flood_status;
      std::string m_census_status;
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
      bool m_build_ui;
      int m_friendly_preset;
      int m_ui_width;
      int m_ui_height;
      int m_drag_property;
      float m_drag_start_y;
      float m_drag_start_normalized;
      bool m_parameter_rebuild_pending;
      int m_parameter_rebuild_stage;
      float m_parameter_rebuild_delay;

      Vec3 m_target;
      float m_yaw;
      float m_pitch;
      float m_distance;
      float m_fit_distance;
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
