#include <moppe/game/inspector_ui.hh>

#include <moppe/render/renderer.hh>

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

    void bevel (render::DrawList& dl, const UiRect& bounds,
		 bool pressed) {
      if (pressed)
	dl.color (0.035f, 0.055f, 0.06f, 1.0f);
      else
	dl.color (0.42f, 0.55f, 0.55f, 0.95f);
      dl.line (bounds.x, bounds.y,
	       bounds.x + bounds.width, bounds.y, 1.0f);
      dl.line (bounds.x, bounds.y,
	       bounds.x, bounds.y + bounds.height, 1.0f);
      if (pressed)
	dl.color (0.42f, 0.55f, 0.55f, 0.95f);
      else
	dl.color (0.025f, 0.04f, 0.045f, 1.0f);
      dl.line (bounds.x, bounds.y + bounds.height,
	       bounds.x + bounds.width, bounds.y + bounds.height, 1.0f);
      dl.line (bounds.x + bounds.width, bounds.y,
	       bounds.x + bounds.width, bounds.y + bounds.height, 1.0f);
    }
  }

  UiRect stepper_minus_rect (const UiRect& bounds) {
    return { bounds.x + bounds.width - 112, bounds.y + 5, 26,
	     bounds.height - 10 };
  }

  UiRect stepper_plus_rect (const UiRect& bounds) {
    return { bounds.x + bounds.width - 26, bounds.y + 5, 26,
	     bounds.height - 10 };
  }

  void
  InspectorUi::load (render::Renderer& renderer)
  {
    const float scale = renderer.scale_factor ();
    m_body.reset
      (new render::FontAtlas (renderer, "Helvetica", 12.0f, scale));
    m_title.reset
      (new render::FontAtlas (renderer, "Helvetica", 15.0f, scale));
    m_key.reset
      (new render::FontAtlas (renderer, "Menlo", 11.0f, scale));
  }

  void
  InspectorUi::begin (render::DrawList& dl) const
  {
    render::DrawState state;
    state.blend = true;
    state.depth_test = false;
    state.depth_write = false;
    state.cull = false;
    dl.state (state);
    dl.lit (false);
    dl.fogged (false);
  }

  void
  InspectorUi::end (render::DrawList& dl) const
  {
    dl.state (render::DrawState ());
    dl.lit (true);
    dl.fogged (true);
    dl.color (1, 1, 1, 1);
  }

  void
  InspectorUi::panel (render::DrawList& dl, float x, float y,
		      float width, float height,
		      const std::string& title) const
  {
    const UiRect outer { x, y, width, height };
    dl.color (0.11f, 0.17f, 0.17f, 0.96f);
    fill_rect (dl, outer);
    bevel (dl, outer, false);

    const UiRect title_bar { x + 4, y + 4, width - 8, 28 };
    dl.color (0.055f, 0.32f, 0.34f, 0.98f);
    fill_rect (dl, title_bar);
    dl.color (0.14f, 0.48f, 0.49f, 0.82f);
    dl.line (title_bar.x + 3, title_bar.y + 4,
	     title_bar.x + title_bar.width - 3, title_bar.y + 4, 1.0f);
    dl.line (title_bar.x + 3, title_bar.y + 8,
	     title_bar.x + title_bar.width - 3, title_bar.y + 8, 1.0f);
    bevel (dl, title_bar, false);

    if (m_title) {
      dl.color (0.94f, 1.0f, 0.84f, 0.99f);
      m_title->draw (dl, x + 12, y + 24, title);
    }
  }

  void
  InspectorUi::label (render::DrawList& dl, float x, float y,
		      const std::string& text, bool bright) const
  {
    if (!m_body)
      return;
    if (bright)
      dl.color (0.88f, 0.95f, 0.98f, 0.98f);
    else
      dl.color (0.65f, 0.72f, 0.78f, 0.96f);
    m_body->draw (dl, x, y, text);
  }

  void
  InspectorUi::key_hint (render::DrawList& dl, float x, float y,
			 const std::string& key,
			 const std::string& description) const
  {
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

  void
  InspectorUi::section_header (render::DrawList& dl,
			       const UiRect& bounds,
			       const std::string& title) const
  {
    dl.color (0.055f, 0.10f, 0.105f, 0.98f);
    fill_rect (dl, bounds);
    dl.color (0.27f, 0.43f, 0.42f, 0.9f);
    dl.line (bounds.x, bounds.y + bounds.height,
	     bounds.x + bounds.width, bounds.y + bounds.height, 1.0f);
    if (m_key) {
      dl.color (0.72f, 0.90f, 0.74f, 0.98f);
      m_key->draw (dl, bounds.x + 7, bounds.y + bounds.height - 7,
		   title);
    }
  }

  void
  InspectorUi::button (render::DrawList& dl, const UiRect& bounds,
		       const std::string& text, bool hot, bool pressed,
		       bool selected) const
  {
    const bool pushed = hot && pressed;
    if (selected)
      dl.color (0.17f, 0.46f, 0.40f, 0.98f);
    else if (hot)
      dl.color (0.25f, 0.39f, 0.37f, 0.98f);
    else
      dl.color (0.18f, 0.28f, 0.28f, 0.98f);
    fill_rect (dl, bounds);
    bevel (dl, bounds, pushed);
    if (!m_body)
      return;
    dl.color (selected ? 0.98f : 0.86f,
	      selected ? 1.0f : 0.92f,
	      selected ? 0.80f : 0.91f, 0.99f);
    const float width = m_body->measure (text);
    m_body->draw (dl, bounds.x + (bounds.width - width) * 0.5f,
		  bounds.y + bounds.height * 0.5f + 4, text);
  }

  void
  InspectorUi::pipeline_row
    (render::DrawList& dl, const UiRect& bounds,
     const std::string& index, const std::string& name,
     const std::string& detail, bool hot, bool pressed,
     bool selected) const
  {
    const bool pushed = hot && pressed;
    if (selected)
      dl.color (0.12f, 0.34f, 0.30f, 0.99f);
    else if (hot)
      dl.color (0.18f, 0.29f, 0.285f, 0.99f);
    else
      dl.color (0.12f, 0.205f, 0.205f, 0.99f);
    fill_rect (dl, bounds);
    bevel (dl, bounds, pushed);

    const UiRect badge
      { bounds.x + 5, bounds.y + 5, 25, bounds.height - 10 };
    dl.color (selected ? 0.48f : 0.27f,
	      selected ? 0.60f : 0.38f,
	      selected ? 0.27f : 0.35f, 0.98f);
    fill_rect (dl, badge);
    bevel (dl, badge, false);
    if (m_key) {
      dl.color (0.95f, 0.98f, 0.78f, 0.99f);
      const float index_width = m_key->measure (index);
      m_key->draw (dl, badge.x + (badge.width - index_width) * 0.5f,
		   badge.y + badge.height * 0.5f + 4, index);
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

  void
  InspectorUi::stepper
    (render::DrawList& dl, const UiRect& bounds,
     const std::string& label_text, const std::string& value,
     bool minus_hot, bool plus_hot, bool pressed) const
  {
    dl.color (0.10f, 0.18f, 0.18f, 0.98f);
    fill_rect (dl, bounds);
    dl.color (0.20f, 0.31f, 0.30f, 0.9f);
    dl.line (bounds.x, bounds.y + bounds.height,
	     bounds.x + bounds.width, bounds.y + bounds.height, 1.0f);
    if (m_body) {
      dl.color (0.72f, 0.82f, 0.79f, 0.99f);
      m_body->draw (dl, bounds.x + 7,
		    bounds.y + bounds.height * 0.5f + 4,
		    label_text);
    }

    const UiRect minus = stepper_minus_rect (bounds);
    const UiRect plus = stepper_plus_rect (bounds);
    const UiRect value_box
      { minus.x + minus.width + 2, minus.y,
	plus.x - minus.x - minus.width - 4, minus.height };
    button (dl, minus, "-", minus_hot, pressed, false);
    button (dl, plus, "+", plus_hot, pressed, false);
    dl.color (0.045f, 0.08f, 0.08f, 0.98f);
    fill_rect (dl, value_box);
    bevel (dl, value_box, true);
    if (m_key) {
      dl.color (0.93f, 0.97f, 0.74f, 0.99f);
      const float value_width = m_key->measure (value);
      m_key->draw (dl,
	value_box.x + (value_box.width - value_width) * 0.5f,
	value_box.y + value_box.height * 0.5f + 4, value);
    }
  }
}
}
