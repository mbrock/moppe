#ifndef MOPPE_GAME_HUD_HH
#define MOPPE_GAME_HUD_HH

#include <moppe/render/draw.hh>
#include <moppe/render/text.hh>

#include <memory>
#include <string>

namespace moppe {
  namespace render {
    class Renderer;
  }

  namespace game {
    // Game state consumed by the overlay.  Hud::draw clamps normalized
    // inputs before deriving geometry.
    struct HudState {
      // length(bike().velocity()) * 3.6f; ignored while on_foot.
      float speed_kmh;
      // bike().boost_charge(): 0..1.  The gauge's reserve dashes.
      float boost_ready01;
      // The remaining readings feed only the diagnostic overlay.
      float health01;
      float odometer_m;
      int lives;
      int stars;
      int score;
      float airtime_s;
      float spin_degrees;
      float landed_airtime_s;
      float landed_spin_degrees;
      int landed_points;
      bool landed_clean;
      float landed_age_s;
      bool on_foot;
      // Soaring turns the gauge into an airspeed instrument with a climb
      // readout; an airborne bike earns the deploy prompt.
      bool gliding;
      bool can_deploy_glider;
      bool can_drop_bike;
      // On foot within reach of the bike.
      bool can_mount;
      float vertical_speed_mps;
      // Real draw-callback interval: paces the fades and feeds the
      // diagnostic frame rate.
      float frame_time_s;
      // Heading in radians: zero is world +Z (north), positive turns east.
      float heading_radians;
      // On foot: the mushroom within reach, if any, and whether it may go
      // in the basket.
      const char* mushroom_in_reach = nullptr;
      bool mushroom_edible = true;
      // How many mushrooms the basket holds.
      int basket_total = 0;
      // The last mushroom reached for and how long ago; a refused one was
      // poisonous and stayed where it grew.
      const char* reached_name = nullptr;
      float reached_age_s = 100.0f;
      bool reached_refused = false;

      HudState ()
          : speed_kmh (0), boost_ready01 (1.0f), health01 (1.0f),
            odometer_m (0), lives (10), stars (0), score (0), airtime_s (0),
            spin_degrees (0), landed_airtime_s (0), landed_spin_degrees (0),
            landed_points (0), landed_clean (false), landed_age_s (10),
            on_foot (false), gliding (false), can_deploy_glider (false),
            can_drop_bike (false), can_mount (false), vertical_speed_mps (0),
            frame_time_s (1.0f / 60.0f), heading_radians (0.0f) {}
    };

    // The overlay keeps out of the way of the landscape.  In ordinary play
    // it is a single quiet speedometer -- a hairline arc with a riding
    // notch around a numeral -- and, only when they apply, short prompts
    // that fade in and out.  Everything is set in one TrueType face through
    // Slug, so text and the gauge's arcs are exact at any display scale.
    // The old instrument chrome (score, hearts, trick callouts, compass,
    // frame telemetry, trail map) survives as a diagnostic overlay behind
    // set_diagnostics (the H key, or MOPPE_HUD=debug).
    class Hud {
    public:
      Hud ();

      // Loads the face and the gauge shapes; call once after the renderer
      // is up.  A missing font leaves the overlay blank rather than
      // stopping the game.
      void load (render::Renderer& renderer);

      void draw (render::DrawList& dl,
                 render::TextList& text,
                 const HudState& state,
                 int width_pts,
                 int height_pts);

      // "Sorry.  You are in great pain."  Covers the frame in black.
      void draw_game_over (render::DrawList& dl,
                           render::TextList& text,
                           int width_pts,
                           int height_pts);

      // A centred key-and-action prompt at the bottom of the frame, as
      // used in play; the cinematic reuses it for its ride prompt.
      void draw_prompt (render::TextList& text,
                        const std::string& key,
                        const std::string& action,
                        float alpha,
                        int width_pts,
                        int height_pts) const;

      // The opening's prompt to skip ahead and begin; nothing on a
      // touch screen, which has no key for it.
      void draw_ride_prompt (render::TextList& text,
                             float alpha,
                             int width_pts,
                             int height_pts) const;

      // One line of the opening's titles: the game's name large at the
      // centre of the frame, or a small credit in its lower third, unless
      // `y` (a fraction of the frame's height) places it.
      void draw_title_card (render::TextList& text,
                            bool title,
                            const std::string& line,
                            float alpha,
                            float y,
                            int width_pts,
                            int height_pts) const;

      // Text with a soft shade beneath it, so light lettering stays
      // legible over snow and sky alike.
      void
      draw_shaded (render::TextList& text,
                   float x,
                   float y,
                   const std::string& line,
                   const render::TextStyle& style,
                   render::TextAlign align = render::TextAlign::Left) const;

      bool diagnostics () const {
        return m_diagnostics;
      }

      void set_diagnostics (bool on) {
        m_diagnostics = on;
      }

      // The HUD face, for other overlays (loading screen); null until
      // load() succeeds.
      const render::Font* font () const {
        return m_font.get ();
      }

    private:
      struct Shape;

      void draw_gauge (render::TextList& text,
                       const HudState& state,
                       float dt,
                       int width_pts,
                       int height_pts);
      void draw_basket (render::TextList& text,
                        const HudState& state,
                        float dt,
                        int width_pts,
                        int height_pts);
      void draw_prompts (render::TextList& text,
                         const HudState& state,
                         float dt,
                         int width_pts,
                         int height_pts);
      void draw_diagnostics (render::DrawList& dl,
                             render::TextList& text,
                             const HudState& state,
                             int width_pts,
                             int height_pts);
      void place_shape (render::TextList& text,
                        const render::SlugGlyph& shape,
                        float cx,
                        float cy,
                        float scale,
                        float angle_radians,
                        float red,
                        float green,
                        float blue,
                        float alpha,
                        float softness = 1.0f) const;

      std::unique_ptr<render::Font> m_font;
      render::GlyphSetPtr m_shapes;
      std::unique_ptr<Shape> m_shape;

      bool m_diagnostics;
      float m_fps;
      // Smoothed visibility of each element; a prompt keeps its last words
      // while it fades out.
      float m_gauge_alpha;
      float m_reserve_alpha;
      float m_reserve_hold;
      float m_prompt_alpha;
      float m_basket_alpha;
      std::string m_prompt_key;
      std::string m_prompt_action;
    };
  }
}

#endif
