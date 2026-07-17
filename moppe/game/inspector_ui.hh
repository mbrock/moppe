#ifndef MOPPE_GAME_INSPECTOR_UI_HH
#define MOPPE_GAME_INSPECTOR_UI_HH

#include <moppe/render/draw.hh>
#include <moppe/render/text.hh>

#include <memory>
#include <string>

namespace moppe {
  namespace render {
    class Renderer;
  }

  namespace game {
    struct UiRect {
      float x;
      float y;
      float width;
      float height;

      bool contains (float px, float py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
      }
    };

    // Persistent placement and pointer state for a movable tool window.
    // Widgets and callers use local coordinates; only this object knows where
    // the window currently sits on screen.
    class UiWindow {
    public:
      explicit UiWindow (const UiRect& bounds = {});

      const UiRect& bounds () const;
      UiRect local_bounds () const;
      UiRect to_screen (const UiRect& local) const;
      float local_x (float screen_x) const;
      float local_y (float screen_y) const;
      bool contains (float screen_x, float screen_y) const;

      void set_position (float x, float y);
      void set_size (float width, float height);
      void constrain (float viewport_width,
                      float viewport_height,
                      float margin = 8.0f);

      bool
      begin_drag (float screen_x, float screen_y, float title_height = 34.0f);
      void drag_to (float screen_x,
                    float screen_y,
                    float viewport_width,
                    float viewport_height);
      void end_drag ();
      bool dragging () const;

    private:
      UiRect m_bounds;
      float m_drag_offset_x;
      float m_drag_offset_y;
      bool m_dragging;
    };

    enum class UiFlowDirection { Row, Column };

    // A deliberately small retained-layout primitive for immediate-mode
    // tools.  Callers describe rows, columns and grids instead of scattering
    // fixed coordinates through their drawing and hit-testing code.
    class UiFlow {
    public:
      UiFlow (const UiRect& bounds,
              UiFlowDirection direction,
              float gap = 0.0f);

      UiRect take (float extent);
      UiRect rest () const;

    private:
      UiRect m_remaining;
      UiFlowDirection m_direction;
      float m_gap;
    };

    UiRect ui_inset (const UiRect& bounds, float amount);
    UiRect ui_grid_cell (const UiRect& bounds,
                         int columns,
                         int index,
                         float row_height,
                         float gap);

    UiRect parameter_control_rect (const UiRect& bounds);
    UiRect counter_minus_rect (const UiRect& bounds);
    UiRect counter_plus_rect (const UiRect& bounds);
    UiRect friendly_slider_rail_rect (const UiRect& bounds);

    // Small immediate-mode inspector skin built on the renderer's existing
    // DrawList and FontAtlas.  It intentionally owns no widget state: tools
    // keep their values and call these drawing helpers every frame.
    class InspectorUi {
    public:
      void load (render::Renderer& renderer);

      void begin (render::DrawList& dl) const;
      void end (render::DrawList& dl) const;

      // Pushes a local coordinate system and draws the shared translucent
      // frame. Every begin_window() must be paired with end_window().
      void begin_window (render::DrawList& dl,
                         const UiWindow& window,
                         const std::string& title) const;
      void end_window (render::DrawList& dl) const;

      void panel (render::DrawList& dl,
                  float x,
                  float y,
                  float width,
                  float height,
                  const std::string& title) const;
      void label (render::DrawList& dl,
                  float x,
                  float y,
                  const std::string& text,
                  bool bright = false) const;
      void key_hint (render::DrawList& dl,
                     float x,
                     float y,
                     const std::string& key,
                     const std::string& description) const;
      void section_header (render::DrawList& dl,
                           const UiRect& bounds,
                           const std::string& title) const;
      void button (render::DrawList& dl,
                   const UiRect& bounds,
                   const std::string& text,
                   bool hot,
                   bool pressed,
                   bool selected = false) const;
      void pipeline_row (render::DrawList& dl,
                         const UiRect& bounds,
                         const std::string& index,
                         const std::string& name,
                         const std::string& detail,
                         bool hot,
                         bool pressed,
                         bool selected) const;
      void knob (render::DrawList& dl,
                 const UiRect& bounds,
                 const std::string& label,
                 const std::string& value,
                 float normalized,
                 bool hot,
                 bool active) const;
      void counter (render::DrawList& dl,
                    const UiRect& bounds,
                    const std::string& label,
                    const std::string& value,
                    bool minus_hot,
                    bool plus_hot,
                    bool pressed) const;

      // Shared translucent instrument skin.  Terrain Lab uses the same
      // vocabulary and drawing primitives at every disclosure level.
      void surface (render::DrawList& dl, const UiRect& bounds) const;
      void heading (render::DrawList& dl,
                    float x,
                    float y,
                    const std::string& text) const;
      void paragraph (render::DrawList& dl,
                      float x,
                      float y,
                      const std::string& text,
                      bool bright = false) const;
      void caption (render::DrawList& dl,
                    float x,
                    float y,
                    const std::string& text) const;
      void friendly_section (render::DrawList& dl,
                             const UiRect& bounds,
                             const std::string& title) const;
      void friendly_button (render::DrawList& dl,
                            const UiRect& bounds,
                            const std::string& title,
                            const std::string& detail,
                            bool hot,
                            bool pressed,
                            bool selected = false,
                            bool featured = false,
                            int icon = -1) const;
      void friendly_slider (render::DrawList& dl,
                            const UiRect& bounds,
                            const std::string& title,
                            const std::string& low,
                            const std::string& high,
                            float normalized,
                            bool hot,
                            bool active) const;
      void friendly_tool_cursor (render::DrawList& dl,
                                 float x,
                                 float y,
                                 int icon) const;

    private:
      std::unique_ptr<render::FontAtlas> m_body;
      std::unique_ptr<render::FontAtlas> m_title;
      std::unique_ptr<render::FontAtlas> m_key;
      std::unique_ptr<render::FontAtlas> m_display;
      std::unique_ptr<render::FontAtlas> m_friendly_body;
      std::unique_ptr<render::FontAtlas> m_friendly_label;
      render::TexturePtr m_friendly_icons;

      void friendly_icon (render::DrawList& dl,
                          const UiRect& bounds,
                          int icon,
                          float alpha = 1.0f,
                          float red = 0.82f,
                          float green = 0.94f,
                          float blue = 1.0f) const;
    };
  }
}

#endif
