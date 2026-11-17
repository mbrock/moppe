#ifndef MOPPE_GAME_MAP_SHEET_HH
#define MOPPE_GAME_MAP_SHEET_HH

#include <moppe/color.hh>
#include <moppe/game/place_kind.hh>
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
  // -- the walker, the vehicles, the camp, the buses and their stops. The first
  // press shows the whole world, the next a stretch round the walker, the third
  // folds it away.
  class MapSheet {
  public:
    enum class View { Closed, Whole, Near };

    // Something to find on the sheet. The first is the walker, whom the
    // near view keeps in the middle.
    struct Mark {
      enum class Kind {
        Walker,
        Companion,
        Bike,
        Jeep,
        Camp,
        // The road network's places (roads.hh): the trailhead, and the
        // rest drawn with their names.
        Place,
        // A bus on the loop, and a stop of its (bus.hh), whose name is
        // set only in the near view.
        Bus,
        BusStop,
      };
      Kind kind = Kind::Walker;
      position_t position = moppe::position (Vec3 ());
      // The way it faces, for those drawn as arrows.
      direction_t heading = unit_direction (Vec3 (0.0f, 0.0f, 1.0f));
      DisplayColor colour { 0.1f, 0.1f, 0.1f };
      const char* name = "";
      // Which place, for a Place.
      terrain::RoadPlace::Kind place = terrain::RoadPlace::Kind::Trailhead;

      // A place on the map rather than a thing, named in the sheet's
      // own lettering.
      bool named () const {
        return kind == Kind::Place && place_kind (place).named;
      }
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

    plan_extent_t period () const noexcept {
      return m_sheet.period;
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
               points_t width,
               points_t height) const;

  private:
    View m_view = View::Closed;
    ReliefMap m_sheet;
    render::TexturePtr m_texture;
  };
}

#endif
