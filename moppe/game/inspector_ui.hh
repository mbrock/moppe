#ifndef MOPPE_GAME_INSPECTOR_UI_HH
#define MOPPE_GAME_INSPECTOR_UI_HH

#include <moppe/render/draw.hh>
#include <moppe/render/text.hh>

#include <memory>
#include <string>

namespace moppe {
namespace render { class Renderer; }

namespace game {
  struct UiRect {
    float x;
    float y;
    float width;
    float height;

    bool contains (float px, float py) const {
      return px >= x && px < x + width
	&& py >= y && py < y + height;
    }
  };

  UiRect parameter_control_rect (const UiRect& bounds);
  UiRect counter_minus_rect (const UiRect& bounds);
  UiRect counter_plus_rect (const UiRect& bounds);

  // Small immediate-mode inspector skin built on the renderer's existing
  // DrawList and FontAtlas.  It intentionally owns no widget state: tools
  // keep their values and call these drawing helpers every frame.
  class InspectorUi {
  public:
    void load (render::Renderer& renderer);

    void begin (render::DrawList& dl) const;
    void end (render::DrawList& dl) const;

    void panel (render::DrawList& dl, float x, float y,
		float width, float height, const std::string& title) const;
    void label (render::DrawList& dl, float x, float y,
		const std::string& text, bool bright = false) const;
    void key_hint (render::DrawList& dl, float x, float y,
		   const std::string& key,
		   const std::string& description) const;
    void section_header (render::DrawList& dl, const UiRect& bounds,
			 const std::string& title) const;
    void button (render::DrawList& dl, const UiRect& bounds,
		 const std::string& text, bool hot, bool pressed,
		 bool selected = false) const;
    void pipeline_row (render::DrawList& dl, const UiRect& bounds,
		       const std::string& index,
		       const std::string& name,
		       const std::string& detail,
		       bool hot, bool pressed, bool selected) const;
    void knob (render::DrawList& dl, const UiRect& bounds,
	       const std::string& label, const std::string& value,
	       float normalized, bool hot, bool active) const;
    void counter (render::DrawList& dl, const UiRect& bounds,
		  const std::string& label, const std::string& value,
		  bool minus_hot, bool plus_hot, bool pressed) const;

    // Spacious, friendly skin used by the public-facing Terrain Lab.  The
    // old inspector widgets above deliberately remain available for the
    // expert editor.
    void surface (render::DrawList& dl, const UiRect& bounds) const;
    void heading (render::DrawList& dl, float x, float y,
		  const std::string& text) const;
    void paragraph (render::DrawList& dl, float x, float y,
		    const std::string& text, bool bright = false) const;
    void caption (render::DrawList& dl, float x, float y,
		  const std::string& text) const;
    void friendly_button (render::DrawList& dl, const UiRect& bounds,
			  const std::string& title,
			  const std::string& detail,
			  bool hot, bool pressed,
			  bool selected = false,
			  bool featured = false,
			  int icon = -1) const;
    void friendly_slider (render::DrawList& dl, const UiRect& bounds,
			  const std::string& title,
			  const std::string& low,
			  const std::string& high,
			  float normalized, bool hot,
			  bool active) const;
    void friendly_tool_cursor (render::DrawList& dl, float x, float y,
			       int icon) const;

  private:
    std::unique_ptr<render::FontAtlas> m_body;
    std::unique_ptr<render::FontAtlas> m_title;
    std::unique_ptr<render::FontAtlas> m_key;
    std::unique_ptr<render::FontAtlas> m_display;
    std::unique_ptr<render::FontAtlas> m_friendly_body;
    std::unique_ptr<render::FontAtlas> m_friendly_label;
    render::TexturePtr m_friendly_icons;

    void friendly_icon (render::DrawList& dl, const UiRect& bounds,
			int icon, float alpha = 1.0f) const;
  };
}
}

#endif
