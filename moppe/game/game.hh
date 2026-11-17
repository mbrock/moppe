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

#include <moppe/game/blob_shadow.hh>
#include <moppe/game/boat_render.hh>
#include <moppe/game/boulders.hh>
#include <moppe/game/bridge_render.hh>
#include <moppe/game/capture_scripts.hh>
#include <moppe/game/companionship.hh>
#include <moppe/game/demo_pilot.hh>
#include <moppe/game/fingerposts.hh>
#include <moppe/game/forest.hh>
#include <moppe/game/frame_capture.hh>
#include <moppe/game/heads_up.hh>
#include <moppe/game/input_frame_adapter.hh>
#include <moppe/game/moppe_game.hh>
#include <moppe/game/opening_director.hh>
#include <moppe/game/scan.hh>
#include <moppe/game/simulation_clock.hh>
#include <moppe/game/sky_clock.hh>
#include <moppe/game/surroundings.hh>
#include <moppe/game/terrain.hh>
#include <moppe/game/waterfall_surface.hh>
#include <moppe/game/world_loading.hh>
#include <moppe/game/world_places.hh>

#include <atomic>

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

      void pointer_move (points_t, points_t, points_t dx, points_t dy) override;

      // The trodden trail tread is loose dirt; bare eroded faces are soil;
      // turf holds most of its dust and throws up clods of itself, and
      // fallen leaves lie wherever the turned groves have dropped them.
      GroundCover ground_cover (const position_t& at) const;

      // The trailhead is marked by a cairn at the path's edge, built once
      // per world.
      void build_home_base_marker ();

      void draw_home_base_marker (render::DrawList& dl) const;

      // The scan (scan.hh): a ring out from the walker, and what it finds
      // labelled where it stands. `ago` starts it that long back.
      void start_scan (duration_t ago);

      void draw_scan (render::Renderer& r, const FrameView& frame);

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
                 moppe::position (m_spawn_position) };
      }

      // MOPPE_DEMO=forest: park the bike in the forest's densest conifer
      // stand.
      void move_spawn_to_forest ();

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

      // -- camp and the HUD ----------------------------------------------

      // What the HUD reads of the world and of play (heads_up.hh).
      HudWorld hud_world () const;

      void draw_camp (render::DrawList& dl);

      // Whether the figure is drawn lying in the hammock rather than
      // standing beside it.
      bool lying_down () const;

      void draw_sleeper (render::DrawList& dl,
                         bool through_own_eyes,
                         seconds_t time);

      // What the walk scripts stand in and move (capture_scripts.hh).
      ScriptStage script_stage ();

      // The finished world arrived from the generation thread.  Everything
      // left runs in one go: build the retained presentations, place the
      // player, upload the terrain, and start playing.  The loading frame
      // announcing this work has already been submitted, so the screen
      // stays honest while it runs.
      void finish_loading (render::Renderer& r,
                           std::unique_ptr<GeneratedWorld> completed);

      void upload_world_terrain (render::Renderer& r);

      // -- company -----------------------------------------------------

      std::uint32_t this_world () const;

      // Both players begin beside a bike and a jeep parked on the same
      // spots. Whoever came later moves theirs a few steps aside, and
      // themselves with them if they have not yet walked off.
      void make_room (const CompanionSnapshot& theirs);

      // Once a frame: hears the other player (companionship.hh), making
      // room when first met, and tells them how this one looks.
      void keep_company (seconds_t interval);

      // -- simulation --------------------------------------------------

      void tick (seconds_t interval) override;

      void tick_simulation (seconds_t interval);

      // -- rendering ---------------------------------------------------

      static render::FrameParams frame_params_for (const FrameView& frame);

      // The vehicles lend their meshes to the sun's shadow before it is
      // drawn, so that they shade the ground under them and their own
      // insides; riders, skinned into a draw list, cast none.
      terrain::ElevationField animal_ground () const;
      plan_extent_t places_period () const;
      void cast_actor_shadows (render::Renderer& r, const FrameView& frame);

      void draw_world_layers (render::Renderer& r, const FrameView& frame);

      void draw_actor_layers (render::Renderer& r, const FrameView& frame);

      void draw_water_surfaces (render::Renderer& r, const FrameView& frame);

      // The boats' wakes lie on the water, so they are drawn over it.
      void draw_boat_wakes (render::Renderer& r, const FrameView& frame);

      void draw_effect_layers (render::Renderer& r, const FrameView& frame);

      void render (render::Renderer& r) override;

      void render_loading (render::Renderer& r);
      void render_game_over (render::Renderer& r);

      // -- input -------------------------------------------------------

      // A launch recording itself runs its remote control by the frames of
      // play it has drawn, starting with the first.
      std::optional<platform::host_time_t> script_seconds () const override;

      void controls (const platform::ControlState& state) override;

      void key (platform::Key k, bool down) override;

    private:
      FrameViewInput frame_view_input (float aspect) const;

      void update_frame_flare ();

      position_t subject_position () const {
        return session ().subject_position ();
      }

      direction_t subject_heading () const {
        return session ().subject_heading ();
      }

      void leave_cinematic ();

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
      // The day and its weather.
      SkyClock m_sky;
      Vec3 m_spawn_position;
      // Where the machines were parked, and the boat moored.
      Parking m_parking;
      Vec3 m_home_base_position;
      render::DrawList m_home_base_marker;
      // Each road place's name (place_names.hh), and the fingerposts.
      std::vector<std::string> m_place_names;
      RoadSigns m_signs;
      Scanner m_scan;
      bool m_skip_cinematic_requested = false;
      // The opening (opening_director.hh).
      OpeningDirector m_opening;
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
      BoatWakes m_wakes;
      BlobShadow m_blob;
      mov::TrunkField m_trunk_field;
      // The automated captures' walk scripts and cameras, and the demo
      // autopilot.
      WalkScripts m_walk;
      CaptureCameras m_capture_camera;
      DemoPilot m_demo;
      // The HUD, the map sheet, and the quit question.
      HeadsUp m_heads_up;

      render::Renderer* m_renderer;
      FrameCapture m_capture;
      // Whether L has handed the mouse back to the desktop during play.
      bool m_pointer_free = false;
      std::atomic<bool> m_ready;

      render::DrawList m_world_dl;
      // Where the shadow casters' riders go, to be thrown away.
      render::DrawList m_caster_dl;
    };
  }
}

#endif
