#ifndef MOPPE_GAME_HUD_HH
#define MOPPE_GAME_HUD_HH

#include <moppe/render/draw.hh>
#include <moppe/render/text.hh>

#include <memory>

namespace moppe {
namespace render { class Renderer; }

namespace game {
  // Game state consumed by the compact overlay.  Hud::draw clamps
  // normalized inputs before deriving dial geometry.
  struct HudState {
    // active_vehicle().velocity().length() * 3.6f; ignored (treated
    // as 0) while on_foot, as at the old call site.
    float speed_kmh;
    // m_fuel: 0..100.  Drives the fuel needle sweep.
    float fuel;
    // active_vehicle().boost_charge(): 0..1.  Drives the boost dial's
    // blue reserve arc; ignored (treated as 1.0) on foot.
    float boost_ready01;
    // m_health / 100: 0..1.  Drives the health bar fill and color.
    float health01;
    // m_odometer: meters ridden; displayed as km with one decimal.
    float odometer_m;
    // m_lives: 0..10 filled hearts.
    int lives;
    // m_star_field.collected(): the "x N" star counter.
    int stars;
    // m_mode == M_FOOT: zeroes the speed, parks the boost dial and
    // shows the "ON FOOT" tag.
    bool on_foot;

    HudState ()
      : speed_kmh (0), fuel (100.0f), boost_ready01 (1.0f),
	health01 (1.0f), odometer_m (0), lives (10), stars (0),
	on_foot (false)
    { }
  };

  // Compact instrument cluster: a digital speed arc with overlapping
  // fuel and boost mini dials, plus top-left star, health, and life
  // readouts.  Records into the caller's HUD DrawList in point
  // coordinates, y-down, origin top-left.
  class Hud {
  public:
    // Builds the font atlases; call once after the renderer is up.
    void load (render::Renderer& renderer);

    void draw (render::DrawList& dl, const HudState& state,
	       int width_pts, int height_pts);

    // "Sorry.  You are in great pain."  Covers the frame in black
    // (the GL build cleared to black instead of rendering a scene).
    void draw_game_over (render::DrawList& dl,
			 int width_pts, int height_pts);

  private:
    std::unique_ptr<render::FontAtlas> m_helv10;   // dial labels
    std::unique_ptr<render::FontAtlas> m_helv12;   // ON FOOT etc.
    std::unique_ptr<render::FontAtlas> m_display24; // digital speed
    std::unique_ptr<render::FontAtlas> m_times24;  // game over
  };
}
}

#endif
