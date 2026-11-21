#ifndef MOPPE_GAME_HUD_HH
#define MOPPE_GAME_HUD_HH

#include <moppe/color.hh>
#include <moppe/game/affordances.hh>
#include <moppe/game/doing.hh>
#include <moppe/game/game_clock.hh>
#include <moppe/game/place_kind.hh>
#include <moppe/game/rescue.hh>
#include <moppe/gfx/math.hh>
#include <moppe/platform/input.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/text.hh>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace moppe {
  namespace render {
    class Renderer;
  }

  namespace game {
    // Game state consumed by the overlay.  Hud::draw clamps normalized
    // inputs before deriving geometry.
    // What the HUD's readings remember from frame to frame: whose Space
    // was last prompted (HudState::thumb_hint), since when, and whether
    // the bike's and the boat's boosts and the wheelie have been tried;
    // and the last meal's and catch's notes, which the state points into.
    struct HudPrompts {
      std::optional<Doing> thumb_mode;
      play_time_t thumb_from = play_time (0.0);
      bool boost_tried[2] = { false, false };
      bool wheelie_tried = false;
      std::string meal_note;
      std::string catch_note;
      // The words of the prompt (HudState::prompt_keyed).
      std::string keyed;
      std::string unkeyed;
    };

    struct HudState {
      // length(bike().velocity()) * 3.6f, in km/h; ignored while on_foot.
      kilometres_per_hour_t speed = 0.0f * (u::km / u::h);
      // bike().boost_charge(): 0..1.  The gauge's reserve dashes.
      proportion_t boost_ready = 1.0f * proportion[one];
      // The remaining readings feed only the diagnostic overlay.
      proportion_t health = 1.0f * proportion[one];
      meters_t odometer = 0.0f * u::m;
      int lives = 10;
      int stars = 0;
      int score = 0;
      seconds_t airtime = 0.0f * u::s;
      degrees_t spin = 0.0f * u::deg;
      seconds_t landed_airtime = 0.0f * u::s;
      degrees_t landed_spin = 0.0f * u::deg;
      int landed_points = 0;
      bool landed_clean = false;
      seconds_t landed_age = 10.0f * u::s;
      // The wheelie under way on the back wheel (0 when there is none),
      // the last one's length and points, the longest, and the time
      // since the last one ended.
      meters_t wheelie = 0.0f * u::m;
      meters_t last_wheelie = 0.0f * u::m;
      int last_wheelie_points = 0;
      meters_t best_wheelie = 0.0f * u::m;
      seconds_t wheelie_age = 10.0f * u::s;
      // Riding the motocross, which wheelies; and whether to prompt for
      // it, as the jets are prompted (thumb_hint).
      bool wheelies = false;
      bool wheelie_hint = false;
      bool on_foot = false;
      // Soaring turns the gauge into an airspeed instrument with a climb
      // readout.
      bool gliding = false;
      // What most wants doing here (game/affordances.hh): the button that
      // does it, if it shows one, and the words with a key for it
      // ("Drive the tractor") and without ("Tractor within reach").
      std::optional<Button> prompt_key;
      const char* prompt_keyed = nullptr;
      const char* prompt_unkeyed = nullptr;
      // Flying it: whether it is low and slow enough to step out.
      bool flying = false;
      bool swimming = false;
      bool submerged = false;
      bool can_step_out = false;
      // Riding a bike, whose rear brake Shift stamps on.
      bool skids = false;
      speed_t vertical_speed = 0.0f * (u::m / u::s);
      // Real draw-callback interval: paces the fades and feeds the
      // diagnostic frame rate.
      seconds_t frame_time = (1.0f / 60.0f) * u::s;
      // Zero is world +Z (north), positive turns east.
      heading_t heading = 0.0f * moppe::heading[si::radian];
      // How many mushrooms the basket holds, and eggs among them.
      int basket_total = 0;
      int basket_eggs = 0;
      // The last mushroom reached for and how long ago; a refused one was
      // poisonous and stayed where it grew.
      const char* reached_name = nullptr;
      seconds_t reached_age = 100.0f * u::s;
      bool reached_refused = false;
      // How many fish are in the creel, and the last one landed or
      // lost, said for a moment afterwards.
      int fish_total = 0;
      const char* catch_note = nullptr;
      seconds_t catch_age = 100.0f * u::s;
      // What a keyboard needs no prompt for, said for a host that offers
      // every action as a button (platform::offer_actions): what Space
      // does here, what F does from the saddle or the seat, and whether
      // the hammock beside the walker can come down and a fire be lit or
      // put out.
      const char* thumb = nullptr;
      const char* dismount = nullptr;
      // Whether the keyboard's prompt should say what Space does when it
      // has nothing else to say: riding the bike or the boat before its
      // boost has been tried.
      bool thumb_hint = false;
      bool can_take_down = false;
      bool can_light_fire = false;
      bool can_douse_fire = false;
      // What was last eaten, said for a moment afterwards.
      const char* meal = nullptr;
      seconds_t meal_age = 100.0f * u::s;
      // Lying in the hammock: the hour of the day, 0..24.
      bool resting = false;
      hours_t clock = 0.0f * u::h;
      // Something worth finding that is out of the way -- the other
      // player, the nearest named place, or a parked machine -- over it
      // when it is in view, and otherwise at the frame's edge pointing
      // the way. Marks whose words would meet on the screen are drawn as
      // one (group_waymarks).
      struct Waymark {
        // Who it is: one bit each, so that a group is a set of them that
        // stays the same from frame to frame.
        enum Who : unsigned {
          Bike = 1u << static_cast<unsigned> (Machine::bike),
          Jeep = 1u << static_cast<unsigned> (Machine::jeep),
          Tractor = 1u << static_cast<unsigned> (Machine::tractor),
          Caddy = 1u << static_cast<unsigned> (Machine::caddy),
          Helicopter = 1u << static_cast<unsigned> (Machine::helicopter),
          MountainBike = 1u << static_cast<unsigned> (Machine::mountain_bike),
          Boat = 1u << static_cast<unsigned> (Machine::boat),
          LongJohn = 1u << static_cast<unsigned> (Machine::long_john),
          JetSki = 1u << static_cast<unsigned> (Machine::jet_ski),
          Snowmobile = 1u << static_cast<unsigned> (Machine::snowmobile),
          Vehicles = (1u << machine_count) - 1,
          Companion = 1u << machine_count,
          Place = 1u << (machine_count + 1),
        };
        // The bit a machine's mark is.
        static constexpr unsigned of (Machine machine) noexcept {
          return 1u << static_cast<unsigned> (machine);
        }
        unsigned who = 0;
        // The machine's silhouette, if it is one.
        unsigned vehicles () const noexcept {
          return who & Vehicles;
        }
        const char* name = "";
        position_t position = moppe::position (Vec3 ());
        // Where the arrow goes, and which way it points: counterclockwise
        // from screen right.
        points_t x = 0.0f * u::pt, y = 0.0f * u::pt;
        radians_t angle = 0.0f * u::rad;
        bool in_view = false;
        // How far it is, and how far faded in.
        meters_t metres = 0.0f * u::m;
        opacity_t alpha = 0.0f * opacity[one];
        DisplayColor colour { 1.0f, 1.0f, 1.0f };
      };
      std::array<Waymark, 12> waymarks {};
      int waymark_count = 0;
      // The chart in the corner: the map sheet's picture (map_sheet.hh)
      // turned so that the way the camera looks is up, round the
      // subject, in a compass bezel whose letters keep to the world's
      // quarters. No picture, no chart.
      struct Chart {
        const render::Texture* picture = nullptr;
        // The picture's period on the ground, and the world's +X and +Z
        // run across and down it (north is -Z, at the top).
        plan_extent_t period = Vec2 (0.0f, 0.0f) * spatial_extent[si::metre];
        // Where the subject stands, and the way ahead on the ground (the
        // HUD makes it a unit vector).
        plan_position_t subject = plan_position (0.0f, 0.0f);
        plan_direction_t ahead = plan_unit_direction (Vec2 (0.0f, -1.0f));
        // The way the subject faces, which the mark at the middle points.
        plan_direction_t facing = plan_unit_direction (Vec2 (0.0f, -1.0f));
        // 0 in the dark of night, 1 by day: the paper dims with the light.
        proportion_t daylight = 1.0f * proportion[one];
        // What else is on it.
        struct Pin {
          enum class Kind {
            Companion,
            Bike,
            Machine,
            Camp,
            // The road network's places (game/place_kind.hh): named
            // ones are shown only while on the chart, not on its rim.
            Place,
            // A bus and a bus stop (bus.hh), only while on the chart.
            Bus,
            BusStop,
          };
          Kind kind = Kind::Machine;
          terrain::RoadPlace::Kind place = terrain::RoadPlace::Kind::Trailhead;
          plan_position_t position = plan_position (0.0f, 0.0f);
          DisplayColor colour { 0.1f, 0.1f, 0.1f };
        };
        std::array<Pin, 64> pins {};
        int pin_count = 0;
      };
      Chart chart {};
      // A word about the other player, when there is one, and how long
      // ago it was said.
      const char* notice = nullptr;
      seconds_t notice_age = 100.0f * u::s;
    };

    // A box on the screen: left, top, right, bottom.
    using HudBox = std::array<points_t, 4>;

    // Which waymarks share one marker. Marks start alone and any two
    // groups of the same kind (in view, or at the edge) whose boxes --
    // what `box` says a group's marker covers -- come within a few points
    // of each other become one, until no two meet; so the markers drawn
    // never overlap, and a group opens out again once its members draw
    // apart on the screen. Groups that were together (`before`, as sets
    // of `who`) keep together until a wider gap opens, so that a group
    // does not flicker at the threshold. Faded-out marks join nothing.
    // Each group is a set of indices into `marks`, as bits.
    std::vector<unsigned>
    group_waymarks (std::span<const HudState::Waymark> marks,
                    const std::function<HudBox (unsigned)>& box,
                    std::span<const unsigned> before);

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
      // Out of line, where Shape is complete.
      ~Hud ();

      // Loads the face and the gauge shapes; call once after the renderer
      // is up.  A missing font leaves the overlay blank rather than
      // stopping the game.
      void load (render::Renderer& renderer);

      void draw (render::DrawList& dl,
                 render::TextList& text,
                 const HudState& state,
                 points_t width,
                 points_t height);

      // "Sorry.  You are in great pain."  Covers the frame in black.
      void draw_game_over (render::DrawList& dl,
                           render::TextList& text,
                           points_t width,
                           points_t height);

      // Asks whether to quit (Esc in play): the frame veiled, the question
      // in the middle, and the keys that answer it.
      void draw_quit_question (render::DrawList& dl,
                               render::TextList& text,
                               points_t width,
                               points_t height) const;

      // The cheat menu (` in play): the frame veiled, and every machine
      // listed by name, `chosen` marked, with the keys that work it.
      void draw_cheat_menu (render::DrawList& dl,
                            render::TextList& text,
                            points_t width,
                            points_t height,
                            Machine chosen) const;

      // Notes that `key` (by its name; none offers nothing) does `action`
      // just now.
      void offer (const std::string& key, const std::string& action);

      // A centred key-and-action prompt at the bottom of the frame, as
      // used in play; the cinematic reuses it for its ride prompt.
      void draw_prompt (render::TextList& text,
                        const std::string& key,
                        const std::string& action,
                        opacity_t alpha,
                        points_t width,
                        points_t height) const;

      // Hands the host this frame's offers -- the prompts drawn since the
      // last call and what else there was to do -- and learns whether it
      // shows them itself. Call once a frame, after drawing.
      void publish_offers ();

      // The opening's prompt to skip ahead and begin; nothing on a
      // touch screen, which has no key for it.
      void draw_ride_prompt (render::TextList& text,
                             opacity_t alpha,
                             points_t width,
                             points_t height);

      // One line of the opening's titles: the game's name large at the
      // centre of the frame, or a small credit in its lower third, unless
      // `y` (a fraction of the frame's height) places it.
      void draw_title_card (render::TextList& text,
                            bool title,
                            const std::string& line,
                            opacity_t alpha,
                            proportion_t y,
                            points_t width,
                            points_t height) const;

      // Text with a soft shade beneath it, so light lettering stays
      // legible over snow and sky alike.
      void
      draw_shaded (render::TextList& text,
                   points_t x,
                   points_t y,
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
                       seconds_t dt,
                       points_t width,
                       points_t height);
      void draw_basket (render::TextList& text,
                        const HudState& state,
                        seconds_t dt,
                        points_t width,
                        points_t height);
      void draw_clock (render::TextList& text,
                       const HudState& state,
                       seconds_t dt,
                       points_t width,
                       points_t height);
      void draw_waymarks (render::TextList& text,
                          const HudState& state,
                          seconds_t dt,
                          points_t width,
                          points_t height);
      void draw_chart (render::DrawList& dl,
                       render::TextList& text,
                       const HudState& state,
                       seconds_t dt,
                       points_t width,
                       points_t height);
      void draw_prompts (render::TextList& text,
                         const HudState& state,
                         seconds_t dt,
                         points_t width,
                         points_t height);
      void draw_diagnostics (render::DrawList& dl,
                             render::TextList& text,
                             const HudState& state,
                             points_t width,
                             points_t height);
      // A keycap with its action beside it, from x along the baseline, in
      // `words` (a prompt's, by default), and how wide that runs.
      render::TextStyle prompt_words (opacity_t alpha) const;
      points_t keyed_width (const std::string& key,
                            const std::string& action,
                            const render::TextStyle& words) const;
      void draw_keyed (render::TextList& text,
                       points_t x,
                       points_t baseline,
                       const std::string& key,
                       const std::string& action,
                       const render::TextStyle& words,
                       opacity_t alpha) const;
      void place_shape (render::TextList& text,
                        const render::SlugGlyph& shape,
                        points_t cx,
                        points_t cy,
                        points_t scale,
                        radians_t angle,
                        DisplayColor colour,
                        opacity_t alpha,
                        points_t softness = 1.0f * u::pt) const;

      std::unique_ptr<render::Font> m_font;
      render::GlyphSetPtr m_shapes;
      std::unique_ptr<Shape> m_shape;

      bool m_diagnostics;
      // MOPPE_HUD=none: nothing over the picture, for films.
      bool m_hidden = false;
      frequency_t m_fps = 0.0f * u::Hz;
      // Smoothed visibility of each element; a prompt keeps its last words
      // while it fades out. The reserve lingers for m_reserve_hold.
      opacity_t m_gauge_alpha = 0.0f * opacity[one];
      opacity_t m_reserve_alpha = 0.0f * opacity[one];
      seconds_t m_reserve_hold = 0.0f * u::s;
      opacity_t m_prompt_alpha = 0.0f * opacity[one];
      opacity_t m_basket_alpha = 0.0f * opacity[one];
      opacity_t m_clock_alpha = 0.0f * opacity[one];
      // The chart's visibility and the radius of ground it shows, which
      // widens with speed;
      opacity_t m_chart_alpha = 0.0f * opacity[one];
      meters_t m_chart_reach = 0.0f * u::m;
      // The waymarks' markers, each a set of HudState::Waymark::who and
      // how far it has faded in: those of this frame come up and those
      // no longer drawn fade away, so that a group opens out or closes
      // up by crossing over rather than in a jump.
      struct Marker {
        unsigned who;
        opacity_t alpha;
        bool live; // grouped so in the last frame
      };
      std::vector<Marker> m_markers;
      std::string m_prompt_key;
      std::string m_prompt_action;
      // What this frame offers the player, and whether the host shows it.
      std::vector<platform::Offer> m_offers;
      bool m_host_offers = false;
    };
  }
}

#endif
