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

      void key_hint (render::DrawList& dl,
                     float x,
                     float y,
                     const std::string& key,
                     const std::string& description) const;
      void slider (render::DrawList& dl,
                   const UiRect& bounds,
                   const std::string& title,
                   const std::string& low,
                   const std::string& high,
                   float normalized,
                   bool active) const;

    private:
      std::unique_ptr<render::FontAtlas> m_body;
      std::unique_ptr<render::FontAtlas> m_title;
      std::unique_ptr<render::FontAtlas> m_key;

      void surface (render::DrawList& dl, const UiRect& bounds) const;
    };
  }
}

#endif
