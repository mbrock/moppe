#include <moppe/game/inspector_ui.hh>

#include <moppe/gfx/tga.hh>
#include <moppe/platform/platform.hh>
#include <moppe/render/renderer.hh>

#include <algorithm>
#include <cmath>

namespace moppe {
  namespace game {
    namespace {
      void fill_rect (render::DrawList& dl, const UiRect& bounds) {
        dl.begin (render::Prim::Quads);
        dl.vertex (bounds.x, bounds.y);
        dl.vertex (bounds.x + bounds.width, bounds.y);
        dl.vertex (bounds.x + bounds.width, bounds.y + bounds.height);
        dl.vertex (bounds.x, bounds.y + bounds.height);
        dl.end ();
      }

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

      void bevel (render::DrawList& dl, const UiRect& bounds, bool pressed) {
        if (pressed)
          dl.color (0.035f, 0.055f, 0.06f, 1.0f);
        else
          dl.color (0.42f, 0.55f, 0.55f, 0.95f);
        dl.line (bounds.x, bounds.y, bounds.x + bounds.width, bounds.y, 1.0f);
        dl.line (bounds.x, bounds.y, bounds.x, bounds.y + bounds.height, 1.0f);
        if (pressed)
          dl.color (0.42f, 0.55f, 0.55f, 0.95f);
        else
          dl.color (0.025f, 0.04f, 0.045f, 1.0f);
        dl.line (bounds.x,
                 bounds.y + bounds.height,
                 bounds.x + bounds.width,
                 bounds.y + bounds.height,
                 1.0f);
        dl.line (bounds.x + bounds.width,
                 bounds.y,
                 bounds.x + bounds.width,
                 bounds.y + bounds.height,
                 1.0f);
      }
    }

    UiRect parameter_control_rect (const UiRect& bounds) {
      return {
        bounds.x + bounds.width - 112, bounds.y + 3, 108, bounds.height - 6
      };
    }

    UiRect counter_minus_rect (const UiRect& bounds) {
      const UiRect control = parameter_control_rect (bounds);
      return { control.x, control.y + 2, 28, control.height - 4 };
    }

    UiRect counter_plus_rect (const UiRect& bounds) {
      const UiRect control = parameter_control_rect (bounds);
      return {
        control.x + control.width - 28, control.y + 2, 28, control.height - 4
      };
    }

    UiRect friendly_slider_rail_rect (const UiRect& bounds) {
      return { bounds.x + 38.0f, bounds.y + 34.0f, bounds.width - 50.0f, 1.0f };
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

    UiFlow::UiFlow (const UiRect& bounds, UiFlowDirection direction, float gap)
        : m_remaining (bounds), m_direction (direction), m_gap (gap) {}

    UiRect UiFlow::take (float extent) {
      if (m_direction == UiFlowDirection::Row) {
        extent = std::clamp (extent, 0.0f, m_remaining.width);
        const UiRect result {
          m_remaining.x, m_remaining.y, extent, m_remaining.height
        };
        const float consumed = std::min (m_remaining.width, extent + m_gap);
        m_remaining.x += consumed;
        m_remaining.width -= consumed;
        return result;
      }
      extent = std::clamp (extent, 0.0f, m_remaining.height);
      const UiRect result {
        m_remaining.x, m_remaining.y, m_remaining.width, extent
      };
      const float consumed = std::min (m_remaining.height, extent + m_gap);
      m_remaining.y += consumed;
      m_remaining.height -= consumed;
      return result;
    }

    UiRect UiFlow::rest () const {
      return m_remaining;
    }

    UiRect ui_inset (const UiRect& bounds, float amount) {
      const float x = std::clamp (amount, 0.0f, bounds.width * 0.5f);
      const float y = std::clamp (amount, 0.0f, bounds.height * 0.5f);
      return { bounds.x + x,
               bounds.y + y,
               std::max (0.0f, bounds.width - x * 2.0f),
               std::max (0.0f, bounds.height - y * 2.0f) };
    }

    UiRect ui_grid_cell (const UiRect& bounds,
                         int columns,
                         int index,
                         float row_height,
                         float gap) {
      columns = std::max (1, columns);
      index = std::max (0, index);
      const int column = index % columns;
      const int row = index / columns;
      const float width =
        std::max (0.0f, (bounds.width - gap * (columns - 1)) / columns);
      return { bounds.x + column * (width + gap),
               bounds.y + row * (row_height + gap),
               width,
               row_height };
    }

    void InspectorUi::load (render::Renderer& renderer) {
      const float scale = renderer.scale_factor ();
      m_body.reset (
        new render::FontAtlas (renderer, "Helvetica", 12.0f, scale));
      m_title.reset (
        new render::FontAtlas (renderer, "Helvetica", 15.0f, scale));
      m_key.reset (new render::FontAtlas (renderer, "Menlo", 11.0f, scale));
      m_display.reset (
        new render::FontAtlas (renderer, "AvenirNext-DemiBold", 29.0f, scale));
      m_friendly_body.reset (
        new render::FontAtlas (renderer, "AvenirNext-DemiBold", 14.0f, scale));
      m_friendly_label.reset (
        new render::FontAtlas (renderer, "AvenirNext-Medium", 11.0f, scale));

      tga_image::TGAImg icons;
      const std::string icon_path =
        platform::asset_path ("textures/ui/terrain-lab-icons.tga");
      if (icons.Load (const_cast<char*> (icon_path.c_str ())) == IMG_OK) {
        render::TextureDesc desc;
        desc.width = icons.GetWidth ();
        desc.height = icons.GetHeight ();
        desc.format = render::TextureFormat::RGBA8;
        desc.filter = render::TextureFilter::Linear;
        desc.wrap = render::TextureWrap::Clamp;
        m_friendly_icons = renderer.create_texture (desc, icons.GetImg ());
      }
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

    void InspectorUi::panel (render::DrawList& dl,
                             float x,
                             float y,
                             float width,
                             float height,
                             const std::string& title) const {
      const UiRect outer { x, y, width, height };
      UiWindow window (outer);
      begin_window (dl, window, title);
      end_window (dl);
    }

    void InspectorUi::label (render::DrawList& dl,
                             float x,
                             float y,
                             const std::string& text,
                             bool bright) const {
      if (!m_body)
        return;
      if (bright)
        dl.color (0.88f, 0.95f, 0.98f, 0.98f);
      else
        dl.color (0.65f, 0.72f, 0.78f, 0.96f);
      m_body->draw (dl, x, y, text);
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

    void InspectorUi::section_header (render::DrawList& dl,
                                      const UiRect& bounds,
                                      const std::string& title) const {
      dl.color (0.055f, 0.10f, 0.105f, 0.98f);
      fill_rect (dl, bounds);
      dl.color (0.27f, 0.43f, 0.42f, 0.9f);
      dl.line (bounds.x,
               bounds.y + bounds.height,
               bounds.x + bounds.width,
               bounds.y + bounds.height,
               1.0f);
      if (m_key) {
        dl.color (0.72f, 0.90f, 0.74f, 0.98f);
        m_key->draw (dl, bounds.x + 7, bounds.y + bounds.height - 7, title);
      }
    }

    void InspectorUi::button (render::DrawList& dl,
                              const UiRect& bounds,
                              const std::string& text,
                              bool hot,
                              bool pressed,
                              bool selected) const {
      const bool pushed = hot && pressed;
      if (selected)
        dl.color (0.17f, 0.46f, 0.40f, 0.98f);
      else if (hot)
        dl.color (0.25f, 0.39f, 0.37f, 0.98f);
      else
        dl.color (0.18f, 0.28f, 0.28f, 0.98f);
      fill_rounded_rect (dl, bounds, 5.0f);
      if (pushed) {
        dl.color (0.02f, 0.08f, 0.08f, 0.34f);
        fill_rounded_rect (dl, ui_inset (bounds, 2.0f), 4.0f);
      }
      if (!m_body)
        return;
      dl.color (selected ? 0.98f : 0.86f,
                selected ? 1.0f : 0.92f,
                selected ? 0.80f : 0.91f,
                0.99f);
      const float width = m_body->measure (text);
      m_body->draw (dl,
                    bounds.x + (bounds.width - width) * 0.5f,
                    bounds.y + bounds.height * 0.5f + 4,
                    text);
    }

    void InspectorUi::pipeline_row (render::DrawList& dl,
                                    const UiRect& bounds,
                                    const std::string& index,
                                    const std::string& name,
                                    const std::string& detail,
                                    bool hot,
                                    bool pressed,
                                    bool selected) const {
      const bool pushed = hot && pressed;
      if (selected)
        dl.color (0.12f, 0.34f, 0.30f, 0.99f);
      else if (hot)
        dl.color (0.18f, 0.29f, 0.285f, 0.99f);
      else
        dl.color (0.12f, 0.205f, 0.205f, 0.99f);
      fill_rounded_rect (dl, bounds, 5.0f);
      if (pushed) {
        dl.color (0.02f, 0.08f, 0.08f, 0.32f);
        fill_rounded_rect (dl, ui_inset (bounds, 2.0f), 4.0f);
      }

      const UiRect badge { bounds.x + 5, bounds.y + 5, 25, bounds.height - 10 };
      dl.color (selected ? 0.48f : 0.27f,
                selected ? 0.60f : 0.38f,
                selected ? 0.27f : 0.35f,
                0.98f);
      fill_rounded_rect (dl, badge, 4.0f);
      if (m_key) {
        dl.color (0.95f, 0.98f, 0.78f, 0.99f);
        const float index_width = m_key->measure (index);
        m_key->draw (dl,
                     badge.x + (badge.width - index_width) * 0.5f,
                     badge.y + badge.height * 0.5f + 4,
                     index);
      }
      if (m_key) {
        dl.color (0.90f, 0.97f, 0.88f, 0.99f);
        m_key->draw (dl, bounds.x + 37, bounds.y + 15, name);
      }
      if (m_body) {
        dl.color (0.57f, 0.70f, 0.68f, 0.98f);
        m_body->draw (dl, bounds.x + 37, bounds.y + 30, detail);
      }
    }

    void InspectorUi::knob (render::DrawList& dl,
                            const UiRect& bounds,
                            const std::string& label_text,
                            const std::string& value,
                            float normalized,
                            bool hot,
                            bool active) const {
      normalized = std::clamp (normalized, 0.0f, 1.0f);
      dl.color (
        hot ? 0.12f : 0.10f, hot ? 0.22f : 0.18f, hot ? 0.21f : 0.18f, 0.98f);
      fill_rect (dl, bounds);
      dl.color (0.20f, 0.31f, 0.30f, 0.9f);
      dl.line (bounds.x,
               bounds.y + bounds.height,
               bounds.x + bounds.width,
               bounds.y + bounds.height,
               1.0f);
      if (m_body) {
        dl.color (0.72f, 0.82f, 0.79f, 0.99f);
        m_body->draw (
          dl, bounds.x + 7, bounds.y + bounds.height * 0.5f + 4, label_text);
      }

      const UiRect control = parameter_control_rect (bounds);
      const UiRect value_box {
        control.x, control.y + 2, control.width - 39, control.height - 4
      };
      dl.color (0.045f, 0.08f, 0.08f, 0.98f);
      fill_rect (dl, value_box);
      bevel (dl, value_box, active);
      if (m_key) {
        dl.color (0.93f, 0.97f, 0.74f, 0.99f);
        const float value_width = m_key->measure (value);
        m_key->draw (dl,
                     value_box.x + (value_box.width - value_width) * 0.5f,
                     value_box.y + value_box.height * 0.5f + 4,
                     value);
      }

      const float cx = control.x + control.width - 18.0f;
      const float cy = bounds.y + bounds.height * 0.5f;
      const float radius = 13.0f;
      dl.color (active ? 0.36f
                : hot  ? 0.29f
                       : 0.20f,
                active ? 0.56f
                : hot  ? 0.43f
                       : 0.31f,
                active ? 0.48f
                : hot  ? 0.39f
                       : 0.32f,
                1.0f);
      dl.begin (render::Prim::TriangleFan);
      dl.vertex (cx, cy);
      for (int i = 0; i <= 24; ++i) {
        const float angle = i * 2.0f * 3.14159265f / 24.0f;
        dl.vertex (cx + std::cos (angle) * radius,
                   cy + std::sin (angle) * radius);
      }
      dl.end ();

      const float angle = (0.75f + normalized * 1.5f) * 3.14159265f;
      dl.color (0.24f, 0.38f, 0.35f, 0.9f);
      for (int i = 0; i < 9; ++i) {
        const float tick = (0.75f + i * 1.5f / 8.0f) * 3.14159265f;
        dl.line (cx + std::cos (tick) * 15.0f,
                 cy + std::sin (tick) * 15.0f,
                 cx + std::cos (tick) * 17.0f,
                 cy + std::sin (tick) * 17.0f,
                 1.0f);
      }
      dl.color (0.96f, 1.0f, 0.72f, 0.99f);
      dl.line (cx,
               cy,
               cx + std::cos (angle) * 9.0f,
               cy + std::sin (angle) * 9.0f,
               2.0f);
    }

    void InspectorUi::counter (render::DrawList& dl,
                               const UiRect& bounds,
                               const std::string& label_text,
                               const std::string& value,
                               bool minus_hot,
                               bool plus_hot,
                               bool pressed) const {
      dl.color (0.095f, 0.16f, 0.19f, 0.98f);
      fill_rect (dl, bounds);
      dl.color (0.18f, 0.29f, 0.34f, 0.9f);
      dl.line (bounds.x,
               bounds.y + bounds.height,
               bounds.x + bounds.width,
               bounds.y + bounds.height,
               1.0f);
      if (m_body) {
        dl.color (0.68f, 0.80f, 0.86f, 0.99f);
        m_body->draw (
          dl, bounds.x + 7, bounds.y + bounds.height * 0.5f + 4, label_text);
      }

      const UiRect minus = counter_minus_rect (bounds);
      const UiRect plus = counter_plus_rect (bounds);
      const UiRect value_box { minus.x + minus.width + 3,
                               minus.y,
                               plus.x - minus.x - minus.width - 6,
                               minus.height };
      button (dl, minus, "-", minus_hot, pressed, false);
      button (dl, plus, "+", plus_hot, pressed, false);
      dl.color (0.025f, 0.055f, 0.07f, 0.99f);
      fill_rect (dl, value_box);
      bevel (dl, value_box, true);
      if (m_key) {
        dl.color (0.72f, 0.94f, 1.0f, 0.99f);
        const float value_width = m_key->measure (value);
        m_key->draw (dl,
                     value_box.x + (value_box.width - value_width) * 0.5f,
                     value_box.y + value_box.height * 0.5f + 4,
                     value);
      }
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

    void InspectorUi::heading (render::DrawList& dl,
                               float x,
                               float y,
                               const std::string& text) const {
      if (!m_display)
        return;
      dl.color (0.95f, 0.94f, 0.77f, 1.0f);
      m_display->draw (dl, x, y, text);
    }

    void InspectorUi::paragraph (render::DrawList& dl,
                                 float x,
                                 float y,
                                 const std::string& text,
                                 bool bright) const {
      if (!m_friendly_body)
        return;
      if (bright)
        dl.color (0.91f, 0.95f, 0.89f, 0.99f);
      else
        dl.color (0.67f, 0.76f, 0.72f, 0.98f);
      m_friendly_body->draw (dl, x, y, text);
    }

    void InspectorUi::caption (render::DrawList& dl,
                               float x,
                               float y,
                               const std::string& text) const {
      if (!m_friendly_label)
        return;
      dl.color (0.58f, 0.73f, 0.81f, 0.98f);
      m_friendly_label->draw (dl, x, y, text);
    }

    void InspectorUi::friendly_section (render::DrawList& dl,
                                        const UiRect& bounds,
                                        const std::string& title) const {
      if (!m_friendly_label)
        return;
      dl.color (0.55f, 0.75f, 0.86f, 0.98f);
      m_friendly_label->draw (
        dl, bounds.x + 13.0f, bounds.y + bounds.height - 5.0f, title);
      const float text_width = m_friendly_label->measure (title);
      const float y = bounds.y + bounds.height - 9.0f;
      dl.color (0.24f, 0.52f, 0.65f, 0.58f);
      dl.line (bounds.x + 22.0f + text_width,
               y,
               bounds.x + bounds.width - 9.0f,
               y,
               1.0f);
      dl.line (bounds.x,
               bounds.y + bounds.height - 3.0f,
               bounds.x,
               bounds.y + bounds.height + 4.0f,
               1.0f);
      dl.line (bounds.x + bounds.width,
               bounds.y + bounds.height - 3.0f,
               bounds.x + bounds.width,
               bounds.y + bounds.height + 4.0f,
               1.0f);
    }

    void InspectorUi::friendly_button (render::DrawList& dl,
                                       const UiRect& bounds,
                                       const std::string& title,
                                       const std::string& detail,
                                       bool hot,
                                       bool pressed,
                                       bool selected,
                                       bool featured,
                                       int icon) const {
      const bool pushed = hot && pressed;
      const UiRect button_shadow {
        bounds.x + 1, bounds.y + 3, bounds.width, bounds.height
      };
      dl.color (0.0f, 0.012f, 0.014f, 0.55f);
      fill_rounded_rect (dl, button_shadow, 8.0f);
      if (selected) {
        const UiRect glow {
          bounds.x - 2, bounds.y - 2, bounds.width + 4, bounds.height + 4
        };
        dl.color (0.20f, 0.92f, 0.82f, 0.16f);
        fill_rounded_rect (dl, glow, 10.0f);
      }
      dl.color (selected   ? 0.48f
                : featured ? 0.69f
                           : 0.20f,
                selected   ? 0.96f
                : featured ? 0.53f
                           : 0.44f,
                selected   ? 0.86f
                : featured ? 0.26f
                           : 0.40f,
                0.96f);
      fill_rounded_rect (dl, bounds, 8.0f);
      if (selected)
        dl.color (0.075f, 0.30f, 0.29f, 0.99f);
      else if (pushed)
        dl.color (0.065f, 0.16f, 0.16f, 0.99f);
      else if (hot)
        dl.color (0.10f, 0.28f, 0.27f, 0.99f);
      else if (featured)
        dl.color (0.19f, 0.20f, 0.13f, 0.99f);
      else
        dl.color (0.055f, 0.14f, 0.14f, 0.99f);
      const UiRect inner {
        bounds.x + 1, bounds.y + 1, bounds.width - 2, bounds.height - 2
      };
      fill_rounded_rect (dl, inner, 7.0f);
      dl.color (selected   ? 0.55f
                : featured ? 0.76f
                           : 0.34f,
                selected   ? 1.0f
                : featured ? 0.64f
                           : 0.58f,
                selected   ? 0.90f
                : featured ? 0.34f
                           : 0.52f,
                0.36f);
      dl.line (bounds.x + 8,
               bounds.y + 2,
               bounds.x + bounds.width - 8,
               bounds.y + 2,
               1.0f);
      float text_x = bounds.x + 12.0f;
      if (icon >= 0) {
        const float size = std::min (40.0f, bounds.height - 10.0f);
        const UiRect icon_bounds {
          bounds.x + 6.0f, bounds.y + (bounds.height - size) * 0.5f, size, size
        };
        friendly_icon (dl, icon_bounds, icon);
        text_x = icon_bounds.x + icon_bounds.width + 7.0f;
      }
      if (m_friendly_body) {
        dl.color (0.94f, 0.96f, 0.84f, 1.0f);
        const float title_y = detail.empty ()
                                ? bounds.y + bounds.height * 0.5f + 6.0f
                                : bounds.y + 25.0f;
        m_friendly_body->draw (dl, text_x, title_y, title);
      }
      if (!detail.empty () && m_friendly_label) {
        dl.color (0.60f, 0.72f, 0.67f, 0.98f);
        m_friendly_label->draw (
          dl, bounds.x + 12, bounds.y + bounds.height - 10, detail);
      }
      if (selected) {
        dl.color (0.78f, 1.0f, 0.72f, 0.98f);
        draw_circle (
          dl, bounds.x + bounds.width - 13.0f, bounds.y + 13.0f, 3.5f);
      }
    }

    void InspectorUi::friendly_slider (render::DrawList& dl,
                                       const UiRect& bounds,
                                       const std::string& title,
                                       const std::string& low,
                                       const std::string& high,
                                       float normalized,
                                       bool hot,
                                       bool active) const {
      normalized = std::clamp (normalized, 0.0f, 1.0f);
      float accent_r = 0.36f;
      float accent_g = 0.82f;
      float accent_b = 0.68f;
      if (title == "AGE") {
        accent_r = 0.94f;
        accent_g = 0.75f;
        accent_b = 0.35f;
      } else if (title == "RAINFALL") {
        accent_r = 0.28f;
        accent_g = 0.78f;
        accent_b = 0.94f;
      } else if (title == "MOUNTAINS") {
        accent_r = 0.72f;
        accent_g = 0.86f;
        accent_b = 0.58f;
      }
      const int icon = title == "MOUNTAINS"     ? 7
                       : title == "WILD RIDGES" ? 8
                       : title == "AGE"         ? 9
                                                : 10;
      friendly_icon (
        dl, { bounds.x, bounds.y - 1.0f, 25.0f, 25.0f }, icon, 0.88f);
      if (m_friendly_body) {
        dl.color (0.89f, 0.93f, 0.82f, 1.0f);
        m_friendly_body->draw (dl, bounds.x + 31, bounds.y + 17, title);
      }
      if (m_friendly_label) {
        dl.color (0.52f, 0.64f, 0.59f, 0.98f);
        m_friendly_label->draw (dl, bounds.x, bounds.y + bounds.height, low);
        const float high_width = m_friendly_label->measure (high);
        m_friendly_label->draw (dl,
                                bounds.x + bounds.width - high_width,
                                bounds.y + bounds.height,
                                high);
      }
      const UiRect rail = friendly_slider_rail_rect (bounds);
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

    void InspectorUi::friendly_icon (render::DrawList& dl,
                                     const UiRect& bounds,
                                     int icon,
                                     float alpha,
                                     float red,
                                     float green,
                                     float blue) const {
      if (!m_friendly_icons || icon < 0 || icon >= 15)
        return;
      constexpr float cells = 4.0f;
      const int column = icon % 4;
      const int row = icon / 4;
      const float u0 = column / cells;
      const float v0 = row / cells;
      const float u1 = (column + 1) / cells;
      const float v1 = (row + 1) / cells;
      dl.set_texture (m_friendly_icons.get ());
      dl.color (red, green, blue, alpha);
      dl.begin (render::Prim::Quads);
      dl.uv (u0, v0);
      dl.vertex (bounds.x, bounds.y);
      dl.uv (u1, v0);
      dl.vertex (bounds.x + bounds.width, bounds.y);
      dl.uv (u1, v1);
      dl.vertex (bounds.x + bounds.width, bounds.y + bounds.height);
      dl.uv (u0, v1);
      dl.vertex (bounds.x, bounds.y + bounds.height);
      dl.end ();
      dl.set_texture (nullptr);
      dl.uv (0.0f, 0.0f);
    }

    void InspectorUi::friendly_tool_cursor (render::DrawList& dl,
                                            float x,
                                            float y,
                                            int icon) const {
      dl.color (0.20f, 0.92f, 0.82f, 0.18f);
      draw_circle (dl, x + 21.0f, y + 21.0f, 18.0f);
      friendly_icon (dl, { x + 7.0f, y + 7.0f, 28.0f, 28.0f }, icon);
    }
  }
}
