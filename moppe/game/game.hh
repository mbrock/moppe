// The game: the port of the original MoppeGLUT application class onto
// the platform/render abstractions.  World generation runs on a
// background thread behind a loading screen; the frame follows the
// exact pass order of the GL build's render_scene().  The command line
// that configures a launch is resolved before this file is reached; see
// launch_options.hh and main.cc.
//
// This header is private to the game's own files: hosts reach the game
// through make_moppe_game (moppe_game.hh). The member functions are
// defined across game.cc and the game_*.cc files beside it, by topic.

#ifndef MOPPE_GAME_GAME_HH
#define MOPPE_GAME_GAME_HH

#include <moppe/environment.hh>
#include <moppe/platform/platform.hh>
#include <moppe/profile.hh>
#include <moppe/render/renderer.hh>
#include <moppe/render/text.hh>

#include <moppe/game/animal_render.hh>
#include <moppe/game/animals.hh>
#include <moppe/game/blob_shadow.hh>
#include <moppe/game/boat_render.hh>
#include <moppe/game/boulders.hh>
#include <moppe/game/bridge_render.hh>
#include <moppe/game/caddy_render.hh>
#include <moppe/game/cairn.hh>
#include <moppe/game/camp.hh>
#include <moppe/game/capture_scripts.hh>
#include <moppe/game/chase_camera.hh>
#include <moppe/game/cinematic_flight.hh>
#include <moppe/game/companionship.hh>
#include <moppe/game/daylight.hh>
#include <moppe/game/dust.hh>
#include <moppe/game/farm_model.hh>
#include <moppe/game/figure.hh>
#include <moppe/game/fingerposts.hh>
#include <moppe/game/fishing.hh>
#include <moppe/game/forest.hh>
#include <moppe/game/frame_capture.hh>
#include <moppe/game/frame_view.hh>
#include <moppe/game/game_session.hh>
#include <moppe/game/generated_world.hh>
#include <moppe/game/glider_render.hh>
#include <moppe/game/graphics_settings.hh>
#include <moppe/game/helicopter_render.hh>
#include <moppe/game/hens.hh>
#include <moppe/game/hud.hh>
#include <moppe/game/input_frame_adapter.hh>
#include <moppe/game/jeep_render.hh>
#include <moppe/game/jet_ski_render.hh>
#include <moppe/game/launch_options.hh>
#include <moppe/game/long_john_render.hh>
#include <moppe/game/map_sheet.hh>
#include <moppe/game/moppe_game.hh>
#include <moppe/game/mountain_bike_render.hh>
#include <moppe/game/mushrooms.hh>
#include <moppe/game/opening.hh>
#include <moppe/game/parking.hh>
#include <moppe/game/place_names.hh>
#include <moppe/game/places.hh>
#include <moppe/game/scan.hh>
#include <moppe/game/seed_memory.hh>
#include <moppe/game/simulation_clock.hh>
#include <moppe/game/stars.hh>
#include <moppe/game/surface_presentation.hh>
#include <moppe/game/surroundings.hh>
#include <moppe/game/terrain.hh>
#include <moppe/game/tractor_render.hh>
#include <moppe/game/vehicle_render.hh>
#include <moppe/game/walker_render.hh>
#include <moppe/game/water_presentation.hh>
#include <moppe/game/waterfall_surface.hh>
#include <moppe/game/weather.hh>
#include <moppe/game/world.hh>
#include <moppe/game/world_loading.hh>
#include <moppe/game/world_places.hh>
#include <moppe/map/surface.hh>
#include <moppe/mov/box3d_vec.hh>
#include <moppe/mov/glider.hh>
#include <moppe/mov/vehicle.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/fractional_drainage.hh>
#include <moppe/terrain/moisture.hh>
#include <moppe/terrain/readings.hh>
#include <moppe/terrain/river.hh>
#include <moppe/terrain/trail.hh>
#include <moppe/terrain/watercourse.hh>
#include <moppe/terrain/waterline.hh>
#include <moppe/terrain/world_recipe.hh>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace moppe {
  namespace game {
    class MoppeGame : public platform::Game {
    public:
      MoppeGame (const LaunchOptions& options, terrain::WorldRecipe recipe);

      // -- lifecycle ---------------------------------------------------

      void setup (render::Renderer& r, int, int) override;

      GameSession& session () noexcept {
        return *m_session;
      }

      const GameSession& session () const noexcept {
        return *m_session;
      }

      GameLogicState& logic () noexcept {
        return session ().logic ();
      }

      const GameLogicState& logic () const noexcept {
        return session ().logic ();
      }

      const GeneratedWorld& generated_world () const noexcept {
        return *m_generated_world;
      }

      GeneratedWorld& generated_world () noexcept {
        return *m_generated_world;
      }

      // The recipe and its bound parameters are known before the world is
      // generated; the loading screen runs on them alone.
      const terrain::WorldRecipe& recipe () const noexcept {
        return m_recipe;
      }

      const WorldParams& world () const noexcept {
        return m_params;
      }

      const map::SurfaceGeometry& surface () const noexcept {
        return generated_world ().surface ();
      }

      void pointer_move (float, float, float dx, float dy) override;

      // The trodden trail tread is loose dirt; bare eroded faces are soil;
      // turf holds most of its dust and throws up clods of itself, and
      // fallen leaves lie wherever the turned groves have dropped them.
      GroundCover ground_cover (const Vec3& at) const;

      const map::SurfaceReadings& surface_readings () const noexcept {
        return generated_world ().readings ();
      }

      template <typename Artifact>
      const Artifact& hydrology_artifact () const noexcept {
        return std::get<Artifact> (generated_world ().hydrology ());
      }

      const terrain::FloodField& standing_water () const noexcept {
        return hydrology_artifact<terrain::FloodField> ();
      }

      const terrain::LakeCensus& lake_census () const noexcept {
        return hydrology_artifact<terrain::LakeCensus> ();
      }

      const terrain::DrainageGraph& drainage () const noexcept {
        return hydrology_artifact<terrain::DrainageGraph> ();
      }

      const terrain::RiverNetwork& rivers () const noexcept {
        return hydrology_artifact<terrain::RiverNetwork> ();
      }

      const terrain::TrailNetwork& trail_network () const noexcept {
        return generated_world ().trails ();
      }

      Vec3 trail_cell_position (terrain::CellIndex cell) const;

      Vec3 trail_alignment_position (
        const terrain::TrailAlignmentPoint& point) const;

      Vec3 trail_direction_from_home () const;

      // The trailhead is marked by a cairn at the path's edge, built once
      // per world.
      void build_home_base_marker ();

      void draw_home_base_marker (render::DrawList& dl) const;

      // The fingerposts at the network's junctions, planned with the
      // world and made the first time they are drawn, when the renderer
      // and the HUD's face are up.
      void draw_fingerposts (render::Renderer& r);

      // The scan (scan.hh): a ring out from the walker, and what it finds
      // labelled where it stands. `ago` starts it that many seconds back.
      void start_scan (double ago);

      void draw_scan (render::Renderer& r, const FrameView& frame);

      void draw_trail_map (render::DrawList& dl,
                           int width_pts,
                           int height_pts,
                           const Vec3& subject,
                           Vec3 heading) const;

      void activate_completed_world (std::unique_ptr<GeneratedWorld> completed);

      void prepare_world_water ();

      void prepare_world_surface ();

      void place_stars_and_player ();

      // What the session's step reaches for beside the ground
      // (surroundings.hh); what the animals read of the world is left
      // out unless the step is to tend them.
      Surroundings surroundings (const AnimalGround* animals = nullptr) const;

      // The active world as what stands on it reads it, and what parking
      // reads and moves.
      WorldGround world_ground () const {
        return { world (), generated_world (), m_trunk_field };
      }

      ParkingLot parking_lot () {
        return { session (),
                 world_ground (),
                 m_places.layout,
                 m_trunk_field,
                 m_spawn_position };
      }

      // The boat autopilot (MOPPE_DEMO=boat): down the river's course
      // where it has one, and otherwise a lazy weave over open water;
      // either way it steers clear of shallows it sees ahead.
      InputFrame boat_autopilot (float t);

      // The jet ski's (MOPPE_DEMO=jet_ski): the same weave, but it makes
      // for the widest water it can see, looks as far ahead as it covers
      // in a few seconds, and shuts the throttle to come round when the
      // shallows are near.
      InputFrame jet_ski_autopilot (float t);

      // MOPPE_DEMO=forest: park the bike in the forest's densest conifer
      // stand.
      void move_spawn_to_forest ();

      // Whether something `reach` metres round stands on a cart road's
      // tread (or, with `decks`, under a bridge's deck). The forest and
      // the rocks know the roads only through the coarse trail field,
      // which a burn's snags and a floodplain's trees under a bridge slip
      // through: they stood in the road, and stopped whatever rode it.
      bool on_road (const Vec3& at, float reach, bool decks) const;

      void grow_global_forest ();

      // Loose rock is planned from the finished surface each time a world
      // activates: it is cheap beside the forest and needs no cache.
      void scatter_boulders ();

      // The autumn's mushrooms come up around the trees each time a world
      // activates; like the boulders they are cheap and need no cache.
      void grow_mushrooms ();

      // Trunks, the larger boulders, and (once built) the places' walls
      // stop the rider alike, so all feed the one streamed collision
      // field.
      void settle_obstacles (bool with_places);

      // -- camp --------------------------------------------------------

      GroundHeight ground_reader () const;

      // -- fishing -------------------------------------------------------

      // What there is to do about the line here, and the last fish.
      void add_fishing_readings (HudState& state) const;

      // Someone in the hammock sees through eyes that lie back in it; any
      // other camera stands off and can be walked round with the look.
      // Both ease from and to where the walker stood.
      void rest_camera ();

      // What there is to do about the camp where the walker stands.
      void add_camp_readings (HudState& state) const;

      void draw_camp (render::DrawList& dl);

      // Whether the figure is drawn lying in the hammock rather than
      // standing beside it.
      bool lying_down () const;

      void
      draw_sleeper (render::DrawList& dl, bool through_own_eyes, float time);

      // MOPPE_WALK=fish: the walker starts on the bank of the open water
      // nearest the spawn, facing out over it.
      void stage_fish_script ();

      // MOPPE_WALK=camp: the walker starts a few steps from the fit pair of
      // trees nearest the spawn, on whichever side is clearer to walk in
      // from.
      // MOPPE_WALK=landing|sanctuary|farm: on foot at the landing's
      // jetty, a sanctuary's gate, or a farm, facing it.
      void stage_place_script ();

      void stage_camp_script ();

      // The script itself: walk in between the trees and hang the hammock,
      // step out and light a fire, come back and lie down, and after a
      // while see the sky through the sleeper's eyes.
      InputFrame camp_script (seconds_t interval);

      // The opening is the authored shot list (data/opening.txt) when it
      // was composed in this world, and otherwise one generated still of
      // the trailhead; either ends in the player's eyes.  MOPPE_OPENING
      // names another shot list, `generated` for the still, `flight` for
      // the old drone flight through the planned landmarks, which the
      // terrain survey still samples, `river` for a flight down the
      // greatest watercourse from its divide to the sea (at a watching
      // pace, or in MOPPE_FLYOVER_SECONDS), or `none` to begin in play.
      void plan_opening_journey ();

      float ground_height (const Vec3& at) const;

      // Reads the authored shots, keeping them only if they were composed
      // in this world and every camera stands above its ground.
      std::optional<OpeningReel> authored_opening (const char* choice) const;

      // Any world's opening: one long still looking back at the trailhead
      // from up the trail, with the titles over it.
      std::vector<OpeningShot> generated_opening () const;

      void plan_opening_reel (const char* choice);

      void start_opening ();

      // Each shot may set the world's clock at its cut, choosing its sky.
      void follow_opening_clock ();

      bool opening_active () const noexcept {
        return m_cinematic.active () || m_opening.active ();
      }

      // Whether frames are drawn to keep up with the display. A capture or
      // a recording draws each frame it is given however
      // long that takes, and its picture must not follow the machine's
      // speed: those hold their resolution.
      bool keeping_pace () const;

      // The finished world arrived from the generation thread.  Everything
      // left runs in one go: build the retained presentations, place the
      // player, upload the terrain, and start playing.  The loading frame
      // announcing this work has already been submitted, so the screen
      // stays honest while it runs.
      void finish_loading (render::Renderer& r,
                           std::unique_ptr<GeneratedWorld> completed);

      void upload_world_terrain (render::Renderer& r);

      void update_world_atmosphere (float total_time);

      // The weather follows the clock from spell to spell, unless
      // MOPPE_WEATHER holds one kind. Composed views, whose day does not
      // turn, keep the drifting sky they were made under.
      Weather read_weather ();

      // The day: MOPPE_DAY is how many minutes of play one takes (48
      // unless told; 0 holds the sun still), and MOPPE_CLOCK the hour it
      // starts at. A launch that names a sun height without asking for a
      // day keeps the fixed sun it was made under.
      struct Day {
        // Hours of the clock per second of play.
        double pace = 0.0;
        // Whether the sky follows the clock, or holds the authored sun.
        bool turning = false;
        std::optional<float> first_hour;
      };

      Day plan_day () const;

      void begin_day ();

      void read_sky ();

      // -- company -----------------------------------------------------

      // Opens the link to another player (game/companion_link.hh). A
      // launch that cannot have one plays on alone.
      void seek_company (const std::string& address);

      std::uint32_t this_world () const;

      // How this player looks now, for the other to draw.
      CompanionSnapshot my_snapshot () const;

      // The companion's picks leave the ground here too, and fill the
      // basket they are seen carrying.
      void hear_of_picks (const CompanionSnapshot& theirs);

      // Both players begin beside a bike and a jeep parked on the same
      // spots. Whoever came later moves theirs a few steps aside, and
      // themselves with them if they have not yet walked off.
      void make_room (const CompanionSnapshot& theirs);

      // On foot beside the other player's jeep, or their boat while they
      // are at its helm, and nearer it than anything of one's own to
      // ride: which of them, if either.
      enum class Seat { none, jeep, boat };
      Seat seat_within_reach () const;
      bool can_ride_along () const;

      // Taking the seat beside the other player, or ahead of them in
      // their boat, keeping it as it moves, and leaving it: by the mount
      // key, or when they have gone. Their weight sits in our boat while
      // they ride in it.
      void ride_along (InputFrame& input);

      // The other player's machines, for ours to bump into: where they
      // stand, and how they move from frame to frame (the jeep's motion
      // is told).
      void meet_strangers (seconds_t interval);

      // Once a frame: hears what the other player has sent, works out how
      // they look just now, and tells them how this one does.
      void keep_company (seconds_t interval);

      // The other player as the world's second figure: their bike where
      // they left or ride it, themselves on foot, in the hammock, or
      // under the wing, and their camp.
      void
      draw_companion (render::Renderer& r, render::DrawList& dl, float time);

      // The renderer has one lamp. With two fires burning it is the one
      // nearer the eye.
      void light_companion_fire (FrameView& frame) const;

      // One waymark: where `at` falls in the frame, or which way it lies.
      // Nothing is marked nearer than `near`, and it fades in to `far`.
      static void add_waymark (HudState& state,
                               const FrameView& frame,
                               int width,
                               int height,
                               const char* name,
                               unsigned who,
                               const Vec3& at,
                               float near,
                               float far,
                               const DisplayColor& tint,
                               float vanish = 0.0f);

      // How near a named place must be for a waymark to point to it.
      static constexpr float place_waymark_m = 1200.0f;

      // The way to the other player and to whatever of one's own is
      // parked out of reach, and to the nearest named place.
      void add_waymarks (HudState& state,
                         const FrameView& frame,
                         int width,
                         int height) const;

      void add_company_readings (HudState& state) const;

      // -- simulation --------------------------------------------------

      void tick (seconds_t interval) override;

      // A deterministic on-foot script for automated captures (MOPPE_WALK):
      // "walk", "run", and "jump" hold one gait, "tour" stands, walks,
      // runs, jumps twice from the run, and comes to rest, turning gently
      // throughout so a following camera sees the figure from changing
      // sides, and "forage" starts beside the mushrooms nearest the spawn
      // and picks its way from one to the next.
      InputFrame scripted_walk (std::string_view script, seconds_t interval);

      // MOPPE_WALK_CAMERA=side: a camera locked beside the walking figure,
      // so the gait can be judged against the ground passing beneath it.
      void walk_side_camera ();

      // MOPPE_ANIMAL_CAMERA=<metres>, with MOPPE_WALK=animals[:SPECIES]:
      // a camera that far from the animal the walk began by, square to the
      // walker's way in so both show, following it as it goes.
      void animal_camera (seconds_t interval);

      // MOPPE_RIDE_CAMERA=side|front: a camera locked beside or ahead of the
      // ridden bike or mountainbike, the glider, the driven jeep or boat,
      // or whatever hangs under the helicopter, to judge the rider and the
      // machine.
      void ride_capture_camera (seconds_t interval);

      // Cameras for filming the boat (MOPPE_RIDE_CAMERA): `drone` follows
      // high behind, `aerial` far off with a long lens, `orbit` circles it,
      // `top` looks straight down on it, `fpv` races close behind and low,
      // `hull` is mounted low on its side looking back at the helmsman,
      // `stern` over the long-tail looking ahead, `helm` is the
      // helmsman's eyes, and `flyby` stands on the water ahead of it and
      // watches it pass on a long lens. MOPPE_RIDE_CAMERA_ANGLE
      // swings the following ones round, degrees to the right of behind.
      bool boat_film_camera (std::string_view view, seconds_t interval);

      void tick_simulation (seconds_t interval);

      // -- rendering ---------------------------------------------------

      static render::FrameParams frame_params_for (const FrameView& frame);

      static HudState hud_state_for (const FrameHud& reading);

      // The mushroom at hand and the basket, for the prompts and the tally.
      void add_basket_readings (HudState& state) const;

      // The vehicles lend their meshes to the sun's shadow before it is
      // drawn, so that they shade the ground under them and their own
      // insides; riders, skinned into a draw list, cast none.
      void cast_actor_shadows (render::Renderer& r, const FrameView& frame);

      void draw_world_layers (render::Renderer& r, const FrameView& frame);

      void draw_actor_layers (render::Renderer& r, const FrameView& frame);

      void draw_water_surfaces (render::Renderer& r, const FrameView& frame);

      // The boats' wakes lie on the water, so they are drawn over it.
      void draw_boat_wakes (render::Renderer& r, const FrameView& frame);

      void draw_effect_layers (render::Renderer& r, const FrameView& frame);

      void draw_overlays (render::Renderer& r, const FrameView& frame);

      // Paints the map sheet of this world and uploads it: for the map
      // over the view (M) and the chart in the HUD's corner.
      void paint_map (render::Renderer& r);

      // What there is to find on the map, the walker first.
      std::vector<MapSheet::Mark> map_marks () const;

      // The chart in the HUD's corner: the map sheet round the subject,
      // turned to the way the camera looks, with the marks on it.
      void add_chart (HudState& state, const FrameView& frame) const;

      // The map over the view (M): the sheet is painted with the world
      // (or the first time it is unfolded), and what moves is marked on
      // it each frame. It takes the place of the rest of the HUD while it
      // is open.
      void draw_map (render::Renderer& r, int width_pts, int height_pts);

      // The opening's fades are a black veil over the frame, and its
      // captions are set over that.
      void draw_opening_titles (int width_pts, int height_pts);

      void render (render::Renderer& r) override;

      void render_loading (render::Renderer& r);
      void render_game_over (render::Renderer& r);

      // -- input -------------------------------------------------------

      // A launch recording itself runs its remote control by the frames of
      // play it has drawn, starting with the first.
      std::optional<double> script_seconds () const override;

      void controls (const platform::ControlState& state) override;

      void key (platform::Key k, bool down) override;

    private:
      FrameViewInput frame_view_input (float aspect) const;

      void update_frame_flare ();

      Vec3 subject_position () const {
        return session ().subject_position ();
      }

      Vec3 subject_heading () const {
        return session ().subject_heading ();
      }

      void leave_cinematic ();

      // The opening's last shot lands where play takes up the view: at
      // the following camera's place behind the player, or in their eyes.
      OpeningShot arrival ();

      void regenerate_world ();

      void revive ();

      WorldParams m_params;
      terrain::WorldRecipe m_recipe;
      // Absent until the loading worker finishes the first world. The active
      // owner changes only in activate_completed_world(); all gameplay reads
      // go through the accessors above, so no stale reference aliases survive
      // a handoff.
      std::unique_ptr<GeneratedWorld> m_generated_world;
      // Declared after its world so session-held terrain and surface borrows
      // release first during normal teardown.
      std::unique_ptr<GameSession> m_session;
      WorldLoading m_loading;
      GraphicsSettings m_graphics;
      Almanac m_almanac;
      Day m_day;
      std::optional<SkyKind> m_held_sky;
      std::optional<WeatherTimeline> m_weather_timeline;
      std::optional<SkyKind> m_logged_sky;
      SkyReading m_sky;
      Vec3 m_spawn_position;
      // Where the machines were parked, and the boat moored.
      Parking m_parking;
      Vec3 m_home_base_position;
      render::DrawList m_home_base_marker;
      // Each road place's name (place_names.hh), and the fingerposts.
      std::vector<std::string> m_place_names;
      std::vector<Fingerpost> m_fingerposts;
      bool m_fingerposts_made = false;
      render::MeshPtr m_fingerpost_mesh;
      render::WorldTextPtr m_fingerpost_lettering;
      Scan m_scan;
      ScanMarks m_scan_marks;
      render::DrawList m_scan_dl;
      render::TextList m_scan_text;
      std::unique_ptr<render::Font> m_sign_font;
      bool m_skip_cinematic_requested = false;
      CinematicFlightPlan m_cinematic_plan;
      CinematicFlight m_cinematic;
      std::vector<OpeningShot> m_opening_shots;
      OpeningPlayer m_opening;
      std::size_t m_opening_shot_seen = SIZE_MAX;
      std::size_t m_opening_rendered_shot = SIZE_MAX;
      InputFrameAdapter m_live_input;
      SimulationClock m_simulation_clock;
      WaterfallSurface m_waterfall_surface;
      BridgeSurface m_bridges;
      Terrain m_terrain;
      ForestLandscape m_forest;
      BoulderLandscape m_boulders;
      // What stands at the ends of the roads, and what lives there.
      WorldPlaces m_places;
      MushroomPatch m_mushrooms;
      // Scratch for the basket's contents as drawn.
      std::vector<MushroomSite> m_basket_contents;
      // The other player, when there is one.
      Companionship m_company;
      // Scratch for a pan's contents as drawn, and the last meal's note.
      std::vector<MushroomSite> m_pan_contents;
      // The boats' wakes, ours and the other player's, and their list.
      BoatWake m_boat_wake;
      BoatWake m_their_wake;
      BoatWake m_jet_ski_wake;
      BoatWake m_their_jet_ski_wake;
      render::DrawList m_wake_dl;
      OpeningArrival m_opening_arrival;
      BlobShadow m_blob;
      mov::TrunkField m_trunk_field;
      // The automated captures' walk scripts and cameras.
      WalkScriptState m_walk;
      CaptureCameraState m_capture_camera;
      // What the HUD's prompts remember from frame to frame.
      mutable HudPrompts m_prompts;
      Hud m_hud;
      render::TextList m_hud_text;

      render::Renderer* m_renderer;
      FrameCapture m_capture;
      // Whether L has handed the mouse back to the desktop during play.
      bool m_pointer_free = false;
      MapSheet m_map;
      // Esc in play has asked whether to quit (Hud::draw_quit_question).
      bool m_quit_asked = false;
      std::atomic<bool> m_ready;

      render::DrawList m_world_dl;
      // Where the shadow casters' riders go, to be thrown away.
      render::DrawList m_caster_dl;
      render::DrawList m_hud_dl;
    };
  }
}

#endif
