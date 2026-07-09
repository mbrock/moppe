#ifndef MOPPE_GAME_HUD_HH
#define MOPPE_GAME_HUD_HH

#include <moppe/render/draw.hh>
#include <moppe/render/text.hh>

#include <memory>

namespace moppe {
namespace render { class Renderer; }

namespace game {
  // Exactly what draw_hud read from the old App object, with the
  // GL-build source of each value noted.  The dial angles, blink
  // phases, and lamp thresholds are all derived from these inside
  // Hud::draw, same formulas as the GL code.
  struct HudState {
    // active_vehicle().velocity().length() * 3.6f; ignored (treated
    // as 0) while on_foot, as at the old call site.
    float speed_kmh;
    // m_fuel: 0..100.  Fuel needle sweep and the amber low-fuel
    // lamp (below 20) read this directly.
    float fuel;
    // active_vehicle().rocket_charge(): 0..1.  Drives the boost
    // dial and the green boost-ready lamp (at exactly 1.0); ignored
    // (treated as 1.0) while on_foot.
    float boost_ready01;
    // m_health / 100: 0..1.  Health bar fill/color and the red
    // damage lamp (below 0.35).
    float health01;
    // m_odometer: meters ridden; displayed as km with one decimal.
    float odometer_m;
    // m_lives: 0..10 filled hearts.
    int lives;
    // m_star_field.collected(): the "x N" star counter.
    int stars;
    // m_total_time: drives the warning-lamp blink phases.
    float time;
    // m_mode == M_FOOT: zeroes the speed, parks the boost dial and
    // shows the "ON FOOT" tag.
    bool on_foot;
    // m_cam_mode == CAM_HELMET: centers the cluster on an opaque
    // backing instead of tucking it translucently into the corner.
    bool helmet_view;

    HudState ()
      : speed_kmh (0), fuel (100.0f), boost_ready01 (1.0f),
	health01 (1.0f), odometer_m (0), lives (10), stars (0),
	time (0), on_foot (false), helmet_view (false)
    { }
  };

  // The instrument cluster: dashboard panel, speedometer with
  // redline ring / gradient speed arc / graduations / needle /
  // odometer, fuel and boost mini dials, three blinking warning
  // lamps, and the top-left readouts (star counter, health bar,
  // hearts).  A straight port of draw_hud/draw_game_over from the
  // GL build; records into the caller's HUD DrawList in point
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
    std::unique_ptr<render::FontAtlas> m_times24;  // star counter
    std::unique_ptr<render::FontAtlas> m_mono;     // odometer 8x13
  };
}
}

#endif
