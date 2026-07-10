#include <moppe/game/inspector_ui.hh>

#include <moppe/render/renderer.hh>

namespace moppe {
namespace game {
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
    const float corner = 9.0f;
    dl.color (0.025f, 0.035f, 0.055f, 0.88f);
    dl.begin (render::Prim::Polygon);
    dl.vertex (x + corner, y);
    dl.vertex (x + width - corner, y);
    dl.vertex (x + width, y + corner);
    dl.vertex (x + width, y + height - corner);
    dl.vertex (x + width - corner, y + height);
    dl.vertex (x + corner, y + height);
    dl.vertex (x, y + height - corner);
    dl.vertex (x, y + corner);
    dl.end ();

    dl.color (0.30f, 0.58f, 0.72f, 0.72f);
    dl.line (x + corner, y, x + width - corner, y, 1.5f);
    dl.line (x, y + 34, x + width, y + 34, 1.0f);

    if (m_title) {
      dl.color (0.82f, 0.94f, 1.0f, 0.98f);
      m_title->draw (dl, x + 13, y + 23, title);
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
}
}
