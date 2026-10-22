#ifndef MOPPE_GAME_MAP_SHEET_HH
#define MOPPE_GAME_MAP_SHEET_HH

#include <moppe/color.hh>
#include <moppe/game/relief_map.hh>
#include <moppe/gfx/math.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/text.hh>

#include <span>
#include <string>

namespace moppe::render {
  class Renderer;
}

namespace moppe::game {
  // The map unfolded over the view: the painted sheet (relief_map.hh) on
  // paper with a neatline, kilometre figures, a scale, and what moves on it
  // -- the walker, the vehicles, the camp. The first press shows the whole
  // world, the next a stretch round the walker, the third folds it away.
  class MapSheet {
  public:
    enum class View { Closed, Whole, Near };

    // Something to find on the sheet. The first is the walker, whom the
    // near view keeps in the middle.
    struct Mark {
      enum class Kind { Walker, Companion, Bike, Jeep, Camp, Trailhead };
      Kind kind = Kind::Walker;
      Vec3 position;
      // The way it faces, for those drawn as arrows.
      Vec3 heading { 0.0f, 0.0f, 1.0f };
      DisplayColor colour { 0.1f, 0.1f, 0.1f };
      const char* name = "";
    };

    View view () const noexcept {
      return m_view;
    }

    bool open () const noexcept {
      return m_view != View::Closed;
    }

    void show (View view) noexcept {
      m_view = view;
    }

    void turn () noexcept {
      m_view = m_view == View::Closed  ? View::Whole
               : m_view == View::Whole ? View::Near
                                       : View::Closed;
    }

    bool painted () const noexcept {
      return static_cast<bool> (m_texture);
    }

    // The uploaded picture and the ground it covers, for the chart in the
    // HUD's corner (hud.hh).
    const render::Texture* picture () const noexcept {
      return m_texture.get ();
    }

    float period_x_m () const noexcept {
      return m_sheet.period_x_m;
    }

    float period_z_m () const noexcept {
      return m_sheet.period_z_m;
    }

    // Takes a painted sheet and uploads its picture.
    void set (render::Renderer& renderer, ReliefMap sheet);

    // The world changed: the sheet is of another country.
    void forget ();

    void draw (render::DrawList& shapes,
               render::TextList& text,
               const render::Font* font,
               std::span<const Mark> marks,
               const std::string& title,
               int width_pts,
               int height_pts) const;

  private:
    View m_view = View::Closed;
    ReliefMap m_sheet;
    render::TexturePtr m_texture;
  };
}

#endif
