// The heads-up display over play: the HUD (hud.hh) and what it remembers
// from frame to frame, the map sheet (M), the quit question, and the
// readings of the world each frame's HUD is told.
#ifndef MOPPE_GAME_HEADS_UP_HH
#define MOPPE_GAME_HEADS_UP_HH

#include <moppe/game/bus.hh>
#include <moppe/game/frame_view.hh>
#include <moppe/game/hud.hh>
#include <moppe/game/map_sheet.hh>
#include <moppe/game/surroundings.hh>
#include <moppe/game/world_ground.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/text.hh>

#include <string>
#include <vector>

namespace moppe::game {
  struct Companionship;
  class GameSession;
  class GeneratedWorld;
  class OpeningDirector;
  struct SkyReading;

  // What the HUD reads of the world and of play.
  struct HudWorld {
    const GameSession& session;
    const GeneratedWorld& world;
    WorldGround ground;
    Surroundings around;
    const Companionship& company;
    const MushroomPatch& mushrooms;
    const PlacesPlan& places;
    // The bus round the loop and its stops (bus.hh).
    const BusLine& bus;
    // Each road place's name (place_names.hh).
    const std::vector<std::string>& place_names;
    position_t home_base;
    const SkyReading& sky;
  };

  class HeadsUp {
  public:
    // The HUD over the frame, kept inside the safe area (notch, home
    // indicator): the opening's titles while it plays, else play's HUD.
    // Even a clean inspection capture needs it: it is also the final
    // post-chain composite into the drawable.
    void draw (render::Renderer& r,
               const HudWorld& w,
               const OpeningDirector& opening,
               const FrameView& frame);

    // Paints the map sheet of this world and uploads it: for the map
    // over the view (M) and the chart in the HUD's corner.
    void paint_map (render::Renderer& r, const GeneratedWorld& world);

    Hud hud;
    MapSheet map;
    // Esc in play has asked whether to quit (Hud::draw_quit_question).
    bool quit_asked = false;
    // The lists the HUD is drawn into, the loading and game-over screens
    // too.
    render::DrawList dl;
    render::TextList text;

  private:
    static HudState hud_state_for (const FrameHud& reading);

    // The basket's tally, and the last thing reached for.
    void add_basket_readings (HudState& state, const HudWorld& w);

    // The meal just eaten, and what Space and the mount key do here.
    void add_camp_readings (HudState& state, const HudWorld& w);

    // The creel's tally and the last fish.
    void add_fishing_readings (HudState& state, const HudWorld& w);

    // What can be done here (affordances.hh): the prompt for what most
    // wants doing, and what the fire and hammock keys would do besides.
    void add_prompt (HudState& state, const HudWorld& w);

    // One waymark: where `at` falls in the frame, or which way it lies.
    // Nothing is marked nearer than `near`, and it fades in to `far`.
    static void add_waymark (HudState& state,
                             const FrameView& frame,
                             points_t width,
                             points_t height,
                             const char* name,
                             unsigned who,
                             const position_t& at,
                             meters_t near,
                             meters_t far,
                             const DisplayColor& tint,
                             meters_t vanish = 0.0f * u::m);

    // How near a named place must be for a waymark to point to it.
    static constexpr meters_t place_waymark = 1200.0f * u::m;

    // The way to the other player and to whatever of one's own is
    // parked out of reach, and to the nearest named place.
    void add_waymarks (HudState& state,
                       const HudWorld& w,
                       const FrameView& frame,
                       points_t width,
                       points_t height) const;

    // The board over the bus stop nearest the subject, while it is near:
    // the stop's name and when the bus comes, over the sign, in the HUD
    // so that it is drawn after the scene's temporal resolve, which
    // would lay the last string's history over a new one.
    void add_bus_board (const HudWorld& w,
                        const FrameView& frame,
                        points_t width,
                        points_t height);

    // What there is to find on the map, the walker first.
    static std::vector<MapSheet::Mark> map_marks (const HudWorld& w);

    // The chart in the HUD's corner: the map sheet round the subject,
    // turned to the way the camera looks, with the marks on it.
    void add_chart (HudState& state,
                    const FrameView& frame,
                    const HudWorld& w) const;

    void draw_trail_map (const HudWorld& w,
                         render::DrawList& dl,
                         points_t width,
                         points_t height,
                         const position_t& subject,
                         direction_t heading) const;

    // The map over the view (M): the sheet is painted with the world (or
    // the first time it is unfolded), and what moves is marked on it each
    // frame. It takes the place of the rest of the HUD while it is open.
    void draw_map (render::Renderer& r,
                   const HudWorld& w,
                   int width_pts,
                   int height_pts);

    // What the prompts remember from frame to frame.
    HudPrompts m_prompts;
  };
}

#endif
