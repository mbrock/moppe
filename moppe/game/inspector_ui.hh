#ifndef MOPPE_GAME_INSPECTOR_UI_HH
#define MOPPE_GAME_INSPECTOR_UI_HH

#include <moppe/render/draw.hh>
#include <moppe/render/text.hh>

#include <memory>
#include <string>

namespace moppe {
namespace render { class Renderer; }

namespace game {
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

  private:
    std::unique_ptr<render::FontAtlas> m_body;
    std::unique_ptr<render::FontAtlas> m_title;
    std::unique_ptr<render::FontAtlas> m_key;
  };
}
}

#endif
