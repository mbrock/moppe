#ifndef MOPPE_GAME_HUD_HH
#define MOPPE_GAME_HUD_HH

#include <moppe/render/draw.hh>
#include <moppe/render/text.hh>

#include <memory>

namespace moppe {
  namespace render {
    class Renderer;
  }

  namespace game {
    // Game state consumed by the compact overlay.  Hud::draw clamps
    // normalized inputs before deriving dial geometry.
    struct HudState {
      // length(active_vehicle().velocity()) * 3.6f; ignored (treated
      // as 0) while on_foot, as at the old call site.
      float speed_kmh;
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
      // Accumulated stunt score and the current/resulting long jump.
      int score;
      float airtime_s;
      float landed_airtime_s;
      int landed_points;
      float landed_age_s;
      // m_mode == M_FOOT: zeroes the speed, parks the boost dial and
      // shows the "ON FOOT" tag.
      bool on_foot;
      // Soaring reuses the speed cluster as an airspeed instrument; the boost
      // dial becomes a variometer and an airborne bike gets a deploy prompt.
      bool gliding;
      bool can_deploy_glider;
      bool can_drop_bike;
      float vertical_speed_mps;
      // Real draw-callback interval, used by the ECU telemetry trace.
      float frame_time_s;
      // Heading in radians: zero is world +Z (north), positive turns east.
      float heading_radians;

      HudState ()
          : speed_kmh (0), boost_ready01 (1.0f), health01 (1.0f),
            odometer_m (0), lives (10), stars (0), score (0), airtime_s (0),
            landed_airtime_s (0), landed_points (0), landed_age_s (10),
            on_foot (false), gliding (false), can_deploy_glider (false),
            can_drop_bike (false), vertical_speed_mps (0),
            frame_time_s (1.0f / 60.0f), heading_radians (0.0f) {}
    };

    // Compact instrument cluster: a digital speed arc with overlapping
    // boost mini dial, plus top-left star, health, and life
    // readouts.  Records into the caller's HUD DrawList in point
    // coordinates, y-down, origin top-left.
    class Hud {
    public:
      Hud ();

      // Builds the font atlases; call once after the renderer is up.
      void load (render::Renderer& renderer);

      void draw (render::DrawList& dl,
                 const HudState& state,
                 int width_pts,
                 int height_pts);

      // "Sorry.  You are in great pain."  Covers the frame in black
      // (the GL build cleared to black instead of rendering a scene).
      void draw_game_over (render::DrawList& dl, int width_pts, int height_pts);

    private:
      void rebuild_static (int width_pts, int height_pts);

      std::unique_ptr<render::FontAtlas> m_helv10;    // dial labels
      std::unique_ptr<render::FontAtlas> m_helv12;    // ON FOOT etc.
      std::unique_ptr<render::FontAtlas> m_display24; // digital speed
      std::unique_ptr<render::FontAtlas> m_times24;   // game over
      float m_fps_history[48];
      float m_fps;
      int m_fps_cursor;
      int m_fps_count;

      // Everything that only depends on the layout (dial faces, ticks,
      // panels, fixed labels) is tessellated once into this list and
      // spliced into the frame each draw; only needles, arcs, bars, and
      // live text re-record.
      render::DrawList m_static;
      int m_static_width;
      int m_static_height;
    };
  }
}

#endif
