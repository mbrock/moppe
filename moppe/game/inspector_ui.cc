#include <moppe/game/inspector_ui.hh>

#include <moppe/render/renderer.hh>

#include <algorithm>
#include <cmath>

namespace moppe {
  namespace game {
    namespace {
      void fill_rounded_rect (render::DrawList& dl,
                              const UiRect& bounds,
                              float radius) {
        radius = std::clamp (
          radius, 0.0f, std::min (bounds.width, bounds.height) * 0.5f);
        constexpr int corner_steps = 5;
        dl.begin (render::Prim::TriangleFan);
        dl.vertex (bounds.x + bounds.width * 0.5f,
                   bounds.y + bounds.height * 0.5f);
        for (int corner = 0; corner < 4; ++corner) {
          const float cx = corner == 0 || corner == 3
                             ? bounds.x + radius
                             : bounds.x + bounds.width - radius;
          const float cy =
            corner < 2 ? bounds.y + radius : bounds.y + bounds.height - radius;
          const float start = 3.14159265f + corner * 1.57079633f;
          for (int i = 0; i <= corner_steps; ++i) {
            const float angle = start + i * 1.57079633f / corner_steps;
            dl.vertex (cx + std::cos (angle) * radius,
                       cy + std::sin (angle) * radius);
          }
        }
        // Close on the first perimeter point.  Closing on the top edge instead
        // creates one giant overlapping triangle across the whole rectangle,
        // which is visible as a dark diagonal seam under alpha blending.
        dl.vertex (bounds.x, bounds.y + radius);
        dl.end ();
      }

      void draw_circle (render::DrawList& dl, float x, float y, float radius) {
        constexpr int steps = 20;
        dl.begin (render::Prim::TriangleFan);
        dl.vertex (x, y);
        for (int i = 0; i <= steps; ++i) {
          const float angle = i * 2.0f * 3.14159265f / steps;
          dl.vertex (x + std::cos (angle) * radius,
                     y + std::sin (angle) * radius);
        }
        dl.end ();
      }

      UiRect slider_rail_rect (const UiRect& bounds) {
        return {
          bounds.x + 38.0f, bounds.y + 34.0f, bounds.width - 50.0f, 1.0f
        };
      }
    }

    UiWindow::UiWindow (const UiRect& bounds)
        : m_bounds (bounds), m_drag_offset_x (0.0f), m_drag_offset_y (0.0f),
          m_dragging (false) {}

    const UiRect& UiWindow::bounds () const {
      return m_bounds;
    }

    UiRect UiWindow::local_bounds () const {
      return { 0.0f, 0.0f, m_bounds.width, m_bounds.height };
    }

    UiRect UiWindow::to_screen (const UiRect& local) const {
      return {
        m_bounds.x + local.x, m_bounds.y + local.y, local.width, local.height
      };
    }

    float UiWindow::local_x (float screen_x) const {
      return screen_x - m_bounds.x;
    }

    float UiWindow::local_y (float screen_y) const {
      return screen_y - m_bounds.y;
    }

    bool UiWindow::contains (float screen_x, float screen_y) const {
      return m_bounds.contains (screen_x, screen_y);
    }

    void UiWindow::set_position (float x, float y) {
      m_bounds.x = x;
      m_bounds.y = y;
    }

    void UiWindow::set_size (float width, float height) {
      m_bounds.width = std::max (0.0f, width);
      m_bounds.height = std::max (0.0f, height);
    }

    void UiWindow::constrain (float viewport_width,
                              float viewport_height,
                              float margin) {
      margin = std::max (0.0f, margin);
      const float maximum_x =
        std::max (margin, viewport_width - m_bounds.width - margin);
      const float maximum_y =
        std::max (margin, viewport_height - m_bounds.height - margin);
      m_bounds.x = std::clamp (m_bounds.x, margin, maximum_x);
      m_bounds.y = std::clamp (m_bounds.y, margin, maximum_y);
    }

    bool
    UiWindow::begin_drag (float screen_x, float screen_y, float title_height) {
      const UiRect title {
        m_bounds.x, m_bounds.y, m_bounds.width, title_height
      };
      if (!title.contains (screen_x, screen_y))
        return false;
      m_drag_offset_x = screen_x - m_bounds.x;
      m_drag_offset_y = screen_y - m_bounds.y;
      m_dragging = true;
      return true;
    }

    void UiWindow::drag_to (float screen_x,
                            float screen_y,
                            float viewport_width,
                            float viewport_height) {
      if (!m_dragging)
        return;
      set_position (screen_x - m_drag_offset_x, screen_y - m_drag_offset_y);
      constrain (viewport_width, viewport_height);
    }

    void UiWindow::end_drag () {
      m_dragging = false;
    }

    bool UiWindow::dragging () const {
      return m_dragging;
    }

    void InspectorUi::load (render::Renderer& renderer) {
      const float scale = renderer.scale_factor ();
      m_body.reset (
        new render::FontAtlas (renderer, "Helvetica", 12.0f, scale));
      m_title.reset (
        new render::FontAtlas (renderer, "Helvetica", 15.0f, scale));
      m_key.reset (new render::FontAtlas (renderer, "Menlo", 11.0f, scale));
    }

    void InspectorUi::begin (render::DrawList& dl) const {
      render::DrawState state;
      state.blend = true;
      state.depth_test = false;
      state.depth_write = false;
      state.cull = false;
      dl.state (state);
      dl.lit (false);
      dl.fogged (false);
    }

    void InspectorUi::end (render::DrawList& dl) const {
      dl.state (render::DrawState ());
      dl.lit (true);
      dl.fogged (true);
      dl.color (1, 1, 1, 1);
    }

    void InspectorUi::begin_window (render::DrawList& dl,
                                    const UiWindow& window,
                                    const std::string& title) const {
      dl.push ();
      dl.translate (window.bounds ().x, window.bounds ().y, 0.0f);
      const UiRect outer = window.local_bounds ();
      surface (dl, outer);

      const UiRect title_bar { 4.0f, 4.0f, outer.width - 8.0f, 28.0f };
      dl.color (0.055f, 0.32f, 0.34f, 0.86f);
      fill_rounded_rect (dl, title_bar, 8.0f);
      dl.color (0.14f, 0.48f, 0.49f, 0.82f);
      dl.line (title_bar.x + 3.0f,
               title_bar.y + 4.0f,
               title_bar.x + title_bar.width - 3.0f,
               title_bar.y + 4.0f,
               1.0f);
      dl.line (title_bar.x + 3.0f,
               title_bar.y + 8.0f,
               title_bar.x + title_bar.width - 3.0f,
               title_bar.y + 8.0f,
               1.0f);
      if (m_title) {
        dl.color (0.94f, 1.0f, 0.84f, 0.99f);
        m_title->draw (dl, 12.0f, 24.0f, title);
      }
    }

    void InspectorUi::end_window (render::DrawList& dl) const {
      dl.pop ();
    }

    void InspectorUi::key_hint (render::DrawList& dl,
                                float x,
                                float y,
                                const std::string& key,
                                const std::string& description) const {
      if (!m_body || !m_key)
        return;

      const float width = m_key->measure (key) + 10.0f;
      dl.color (0.12f, 0.23f, 0.30f, 0.96f);
      dl.begin (render::Prim::Quads);
      dl.vertex (x, y - 12);
      dl.vertex (x + width, y - 12);
      dl.vertex (x + width, y + 4);
      dl.vertex (x, y + 4);
      dl.end ();

      dl.color (0.78f, 0.94f, 1.0f, 0.98f);
      m_key->draw (dl, x + 5, y, key);
      dl.color (0.74f, 0.79f, 0.83f, 0.96f);
      m_body->draw (dl, x + width + 8, y, description);
    }

    void InspectorUi::surface (render::DrawList& dl,
                               const UiRect& bounds) const {
      const UiRect deep_shadow {
        bounds.x + 5, bounds.y + 8, bounds.width, bounds.height
      };
      dl.color (0.0f, 0.012f, 0.024f, 0.28f);
      fill_rounded_rect (dl, deep_shadow, 14.0f);
      const UiRect shadow {
        bounds.x + 2, bounds.y + 4, bounds.width, bounds.height
      };
      dl.color (0.005f, 0.02f, 0.035f, 0.64f);
      fill_rounded_rect (dl, shadow, 13.0f);
      dl.color (0.14f, 0.34f, 0.45f, 0.18f);
      fill_rounded_rect (dl, bounds, 13.0f);
      dl.color (0.17f, 0.42f, 0.54f, 0.22f);
      const UiRect inner {
        bounds.x + 1, bounds.y + 1, bounds.width - 2, bounds.height - 2
      };
      fill_rounded_rect (dl, inner, 12.0f);
      dl.color (0.025f, 0.095f, 0.14f, 0.75f);
      const UiRect face {
        bounds.x + 2, bounds.y + 2, bounds.width - 4, bounds.height - 4
      };
      fill_rounded_rect (dl, face, 11.0f);

      dl.color (0.43f, 0.76f, 0.91f, 0.44f);
      dl.line (bounds.x + 16,
               bounds.y + 2,
               bounds.x + bounds.width - 16,
               bounds.y + 2,
               1.0f);

      for (float x : { bounds.x + 11.0f, bounds.x + bounds.width - 11.0f }) {
        dl.color (0.005f, 0.025f, 0.045f, 0.95f);
        draw_circle (dl, x, bounds.y + 11.0f, 3.5f);
        dl.color (0.39f, 0.69f, 0.81f, 0.88f);
        draw_circle (dl, x, bounds.y + 11.0f, 1.7f);
      }

      // Barely-visible contour lines keep the surface from feeling like an
      // empty black rectangle without competing with the controls.
      dl.color (0.30f, 0.64f, 0.78f, 0.030f);
      for (int row = 0; row < 8; ++row) {
        const float base_y = bounds.y + 88.0f + row * 82.0f;
        if (base_y > bounds.y + bounds.height - 18.0f)
          break;
        float previous_x = bounds.x + 18.0f;
        float previous_y = base_y;
        for (int segment = 1; segment <= 12; ++segment) {
          const float x =
            bounds.x + 18.0f + segment * (bounds.width - 36.0f) / 12.0f;
          const float y =
            base_y + std::sin (segment * 0.86f + row * 0.7f) * 5.0f;
          dl.line (previous_x, previous_y, x, y, 1.0f);
          previous_x = x;
          previous_y = y;
        }
      }
    }

    void InspectorUi::slider (render::DrawList& dl,
                              const UiRect& bounds,
                              const std::string& title,
                              const std::string& low,
                              const std::string& high,
                              float normalized,
                              bool active) const {
      normalized = std::clamp (normalized, 0.0f, 1.0f);
      float accent_r = 0.36f;
      float accent_g = 0.82f;
      float accent_b = 0.68f;
      if (m_body) {
        dl.color (0.89f, 0.93f, 0.82f, 1.0f);
        m_body->draw (dl, bounds.x, bounds.y + 17, title);
      }
      if (m_key) {
        dl.color (0.52f, 0.64f, 0.59f, 0.98f);
        m_key->draw (dl, bounds.x, bounds.y + bounds.height, low);
        const float high_width = m_key->measure (high);
        m_key->draw (dl,
                     bounds.x + bounds.width - high_width,
                     bounds.y + bounds.height,
                     high);
      }
      const UiRect rail = slider_rail_rect (bounds);
      const float rail_x = rail.x;
      const float rail_width = rail.width;
      const float y = rail.y;
      dl.color (0.28f, 0.52f, 0.62f, 0.52f);
      for (int i = 0; i <= 20; ++i) {
        const float tick_x = rail_x + rail_width * i / 20.0f;
        const float tick = i % 5 == 0 ? 4.0f : 2.0f;
        dl.line (tick_x, y + 5.0f, tick_x, y + 5.0f + tick, 1.0f);
      }
      dl.color (0.12f, 0.28f, 0.35f, 0.95f);
      dl.line (rail_x, y, rail_x + rail_width, y, 4.0f);
      dl.color (accent_r, accent_g, accent_b, 0.95f);
      dl.line (rail_x, y, rail_x + rail_width * normalized, y, 2.0f);
      const float cx = rail_x + rail_width * normalized;
      dl.color (accent_r, accent_g, accent_b, active ? 0.24f : 0.12f);
      draw_circle (dl, cx, y, active ? 12.0f : 10.0f);
      dl.color (0.015f, 0.035f, 0.035f, 1.0f);
      draw_circle (dl, cx, y, 7.5f);
      dl.color (accent_r, accent_g, accent_b, 1.0f);
      draw_circle (dl, cx, y, 5.7f);
      dl.color (0.96f, 0.98f, 0.78f, 0.92f);
      draw_circle (dl, cx - 1.0f, y - 1.0f, 2.5f);
    }

  }
}
