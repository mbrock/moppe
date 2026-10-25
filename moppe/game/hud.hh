#ifndef MOPPE_GAME_HUD_HH
#define MOPPE_GAME_HUD_HH

#include <moppe/platform/input.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/text.hh>

#include <array>
#include <memory>
#include <string>
#include <vector>

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
      // On foot beside this player's jeep, or the other player's with a
      // seat free.
      bool can_drive = false;
      bool can_ride_along = false;
      // On foot beside the tractor, or the Caddy.
      bool can_drive_tractor = false;
      bool can_drive_caddy = false;
      // On foot beside the mountainbike.
      bool can_ride_mountain_bike = false;
      // On foot beside the boat.
      bool can_take_boat = false;
      // On foot beside a stuck machine ("the tractor"), which the mount
      // key would haul out; the machine being hauled; and whether the one
      // being driven is stuck.
      const char* haul = nullptr;
      const char* hauling = nullptr;
      bool stuck = false;
      // On foot beside the helicopter.
      bool can_fly = false;
      // Flying it: whether it is low and slow enough to step out.
      bool flying = false;
      bool can_step_out = false;
      // Riding a bike, whose rear brake Shift stamps on.
      bool skids = false;
      // Flying it with the Caddy on its line, low enough to set it down:
      // the mount key lets it go.
      bool can_release_caddy = false;
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
      // On foot: what there is to do about the camp here (game/camp.hh).
      enum class Camp { none, hang, lie_down, get_up, light_fire };
      Camp camp = Camp::none;
      // On foot by a fire: what there is to do about supper.
      enum class Cook { none, fry, frying, eat, eat_burnt };
      Cook cook = Cook::none;
      // Whether what goes in the pan, or is in it, is a fish.
      bool cook_fish = false;
      // On foot at the water, or at the helm of a drifting boat
      // (game/fishing.hh): what there is to do
      // about the line, how many fish are in the creel, and the last one
      // landed or lost, said for a moment afterwards.
      enum class Angle { none, cast, wait, strike, playing };
      Angle angle = Angle::none;
      int fish_total = 0;
      const char* catch_note = nullptr;
      float catch_age_s = 100.0f;
      // What a keyboard needs no prompt for, said for a host that offers
      // every action as a button (platform::offer_actions): what Space
      // does here, what F does from the saddle or the seat, and whether
      // the hammock beside the walker can come down and a fire be lit or
      // put out.
      const char* thumb = nullptr;
      const char* dismount = nullptr;
      bool can_take_down = false;
      bool can_light_fire = false;
      bool can_douse_fire = false;
      // What was last eaten, said for a moment afterwards.
      const char* meal = nullptr;
      float meal_age_s = 100.0f;
      // Lying in the hammock: the hour of the day, 0..24.
      bool resting = false;
      float clock_hours = 0.0f;
      // Something worth finding that is out of the way -- the other
      // player, the bike, the jeep -- as a small arrow: over it when it
      // is in view, and otherwise at the frame's edge pointing the way.
      struct Waymark {
        const char* name = "";
        // Where the arrow goes, in points, and which way it points:
        // radians counterclockwise from screen right.
        float x = 0.0f, y = 0.0f;
        float angle = 0.0f;
        bool in_view = false;
        float metres = 0.0f;
        float alpha = 0.0f;
        float red = 1.0f, green = 1.0f, blue = 1.0f;
      };
      std::array<Waymark, 4> waymarks {};
      int waymark_count = 0;
      // The chart in the corner: the map sheet's picture (map_sheet.hh)
      // turned so that the way the camera looks is up, round the
      // subject, in a compass bezel whose letters keep to the world's
      // quarters. No picture, no chart.
      struct Chart {
        const render::Texture* picture = nullptr;
        // The picture's period on the ground, and the world's +X and +Z
        // run across and down it (north is -Z, at the top).
        float period_x_m = 0.0f, period_z_m = 0.0f;
        // Where the subject stands, and the way ahead on the ground (a
        // unit vector in X and Z).
        float x_m = 0.0f, z_m = 0.0f;
        float ahead_x = 0.0f, ahead_z = -1.0f;
        // The way the subject faces, which the mark at the middle points.
        float facing_x = 0.0f, facing_z = -1.0f;
        // 0 in the dark of night, 1 by day: the paper dims with the light.
        float daylight = 1.0f;
        // What else is on it.
        struct Pin {
          enum class Kind { Companion, Bike, Machine, Camp, Trailhead };
          Kind kind = Kind::Machine;
          float x_m = 0.0f, z_m = 0.0f;
          float red = 0.1f, green = 0.1f, blue = 0.1f;
        };
        std::array<Pin, 10> pins {};
        int pin_count = 0;
      };
      Chart chart {};
      // A word about the other player, when there is one, and how long
      // ago it was said.
      const char* notice = nullptr;
      float notice_age_s = 100.0f;

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

      // Asks whether to quit (Esc in play): the frame veiled, the question
      // in the middle, and the keys that answer it.
      void draw_quit_question (render::DrawList& dl,
                               render::TextList& text,
                               int width_pts,
                               int height_pts) const;

      // Notes that `key` (by its name; none offers nothing) does `action`
      // just now.
      void offer (const std::string& key, const std::string& action);

      // A centred key-and-action prompt at the bottom of the frame, as
      // used in play; the cinematic reuses it for its ride prompt.
      void draw_prompt (render::TextList& text,
                        const std::string& key,
                        const std::string& action,
                        float alpha,
                        int width_pts,
                        int height_pts) const;

      // Hands the host this frame's offers -- the prompts drawn since the
      // last call and what else there was to do -- and learns whether it
      // shows them itself. Call once a frame, after drawing.
      void publish_offers ();

      // The opening's prompt to skip ahead and begin; nothing on a
      // touch screen, which has no key for it.
      void draw_ride_prompt (render::TextList& text,
                             float alpha,
                             int width_pts,
                             int height_pts);

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
      void draw_clock (render::TextList& text,
                       const HudState& state,
                       float dt,
                       int width_pts,
                       int height_pts);
      void draw_waymarks (render::TextList& text, const HudState& state);
      void draw_chart (render::DrawList& dl,
                       render::TextList& text,
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
      // A keycap with its action beside it, from x along the baseline, in
      // `words` (a prompt's, by default), and how wide that runs.
      render::TextStyle prompt_words (float alpha) const;
      float keyed_width (const std::string& key,
                         const std::string& action,
                         const render::TextStyle& words) const;
      void draw_keyed (render::TextList& text,
                       float x,
                       float baseline,
                       const std::string& key,
                       const std::string& action,
                       const render::TextStyle& words,
                       float alpha) const;
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
      // MOPPE_HUD=none: nothing over the picture, for films.
      bool m_hidden = false;
      float m_fps;
      // Smoothed visibility of each element; a prompt keeps its last words
      // while it fades out.
      float m_gauge_alpha;
      float m_reserve_alpha;
      float m_reserve_hold;
      float m_prompt_alpha;
      float m_basket_alpha;
      float m_clock_alpha = 0.0f;
      // The chart's visibility and the radius of ground it shows, which
      // widens with speed; MOPPE_CHART=none leaves it out.
      float m_chart_alpha = 0.0f;
      float m_chart_reach_m = 0.0f;
      bool m_chart_hidden = false;
      std::string m_prompt_key;
      std::string m_prompt_action;
      // What this frame offers the player, and whether the host shows it.
      std::vector<platform::Offer> m_offers;
      bool m_host_offers = false;
    };
  }
}

#endif
