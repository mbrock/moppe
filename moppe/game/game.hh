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
#include <moppe/game/chase_camera.hh>
#include <moppe/game/cinematic_flight.hh>
#include <moppe/game/companion_link.hh>
#include <moppe/game/daylight.hh>
#include <moppe/game/dust.hh>
#include <moppe/game/farm_model.hh>
#include <moppe/game/figure.hh>
#include <moppe/game/fingerposts.hh>
#include <moppe/game/fishing.hh>
#include <moppe/game/forest.hh>
#include <moppe/game/frame_view.hh>
#include <moppe/game/game_session.hh>
#include <moppe/game/generated_world.hh>
#include <moppe/game/glider_render.hh>
#include <moppe/game/graphics_benchmark.hh>
#include <moppe/game/graphics_settings.hh>
#include <moppe/game/helicopter_render.hh>
#include <moppe/game/hens.hh>
#include <moppe/game/hud.hh>
#include <moppe/game/input_frame_adapter.hh>
#include <moppe/game/jeep_render.hh>
#include <moppe/game/jet_ski_render.hh>
#include <moppe/game/landscape_gazetteer.hh>
#include <moppe/game/landscape_summary.hh>
#include <moppe/game/launch_options.hh>
#include <moppe/game/long_john_render.hh>
#include <moppe/game/map_sheet.hh>
#include <moppe/game/moppe_game.hh>
#include <moppe/game/mountain_bike_render.hh>
#include <moppe/game/mushrooms.hh>
#include <moppe/game/opening.hh>
#include <moppe/game/place_names.hh>
#include <moppe/game/places.hh>
#include <moppe/game/scan.hh>
#include <moppe/game/seed_memory.hh>
#include <moppe/game/simulation_clock.hh>
#include <moppe/game/stars.hh>
#include <moppe/game/surface_presentation.hh>
#include <moppe/game/terrain.hh>
#include <moppe/game/tractor_render.hh>
#include <moppe/game/vehicle_render.hh>
#include <moppe/game/video.hh>
#include <moppe/game/walker_render.hh>
#include <moppe/game/water_capture.hh>
#include <moppe/game/water_presentation.hh>
#include <moppe/game/waterfall_surface.hh>
#include <moppe/game/weather.hh>
#include <moppe/game/world.hh>
#include <moppe/game/world_loading.hh>
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

      // The spectator starts in the densest conifer stand the forest holds:
      // the plan's sites are counted on a 40-metre lattice and the camera
      // stands at the centre of the fullest cell.
      void place_spectator ();

      // WASD walks the eye through the air, Space and Tab rise and sink, and
      // the mouse (captured, no button needed) or the arrow keys turn the
      // head.
      void spectator_camera (seconds_t interval);

      void pointer_move (float, float, float dx, float dy) override;

      // Temporal-stability inspection: MOPPE_ORBIT="radius,height,seconds"
      // circles the camera at a steady pace around the tree nearest the
      // rider, so frame-to-frame discontinuities in the forest stand out
      // against otherwise smooth motion.
      void orbit_camera ();

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

      // -- places ----------------------------------------------------

      PlaceGround place_ground () const;

      // Where the places' things stand is planned before the trees and
      // rocks, which keep out of their clearings.
      void plan_world_places ();

      // Once the trees stand: the camps among them, and the built things
      // with their colliders.
      void build_world_places ();

      // -- animals -------------------------------------------------

      // The barns of the farms still lived in, in the plan's order.
      std::vector<const Building*> kept_barns () const;

      std::uint32_t egg_seed () const;

      // Each barn's stock: two horses and two cows tied in their stalls
      // with their heads to the manger, and the pigs loose in their pen.
      std::vector<Stall> barn_stalls () const;

      // What the animals read of the world: the ground and its water,
      // the grazing and the plants, and the trunks and walls about.
      AnimalGround animal_ground () const;

      // The stock goes out round the places still lived in, and the wild
      // ones are where their country is.
      void let_out_animals ();

      // Everyone about, as the animals mind them: the walker, whatever
      // the player rides or drives, and the other player.
      void tend_animals (seconds_t interval);

      // The beacon a walker stands at, if any.
      std::optional<std::size_t> beacon_at_hand () const;

      // Each beacon burns as fiercely as the fire either player keeps on
      // it; the other's catch and die down here as they are heard of.
      void tend_beacons ();

      // Whether the other player keeps beacon `i` burning.
      bool their_beacon (std::size_t i) const;

      // The boat lies at the water's edge of the sea, the nearest open
      // shore of it to the spawn (or, with no sea in reach or with
      // `MOPPE_BOAT=lake`, of a lake big enough to take it out on): afloat
      // a few metres out with its bow drawn up toward the land, as left by
      // someone gone ashore. `outward` turns it to face the open water.
      void park_boat (bool outward = false);

      // The jet ski lies afloat by the boat: at the landing, alongside the
      // jetty's other side; on an open shore or a river, a few metres
      // along from the boat, wherever the water there will float it. Its
      // bow is to the land as the boat's is, or with `outward` to the
      // open water.
      void park_jet_ski (bool outward = false);

      // How deep the still water stands over the ground at `at`.
      float boat_depth_at (const Vec3& at) const;

      // MOPPE_BOAT=river: the boat lies on the greatest river, as far up
      // from the sea as the water stays deep enough to drive it the whole
      // way down, facing downstream, and the autopilot follows the
      // river's course out to sea.
      bool moor_on_river ();

      // The boat autopilot (MOPPE_DEMO=boat): down the river's course
      // where it has one, and otherwise a lazy weave over open water;
      // either way it steers clear of shallows it sees ahead.
      InputFrame boat_autopilot (float t);

      // The jet ski's (MOPPE_DEMO=jet_ski): the same weave, but it makes
      // for the widest water it can see, looks as far ahead as it covers
      // in a few seconds, and shuts the throttle to come round when the
      // shallows are near.
      InputFrame jet_ski_autopilot (float t);

      // What the water autopilot steers: how fast it is going, m/s, for
      // how far ahead it must mind the shallows; whether it follows the
      // river's course where there is one; how far it looks for open
      // water, metres; and how much of that it cares to have ahead of
      // it before the weave matters more.
      struct WaterPilot {
        float speed = 0.0f;
        bool follow_route = false;
        float sight = 90.0f;
        float room = 60.0f;
      };

      // Steers something afloat at `at`, heading `heading`, along the
      // river's course if there is one to follow, and otherwise over
      // open water.
      InputFrame water_autopilot (float t,
                                  const Vec3& at,
                                  Vec3 heading,
                                  const WaterPilot& craft);

      bool moor_boat (bool sea_only, bool outward);

      // MOPPE_WALK=haul: the tractor is stuck where it stands and the
      // walker stands before it, facing away, to haul it out on a rope.
      // MOPPE_WALK=bump: the jeep stands back from the tractor's flank,
      // aimed at it, with the walker at its door.
      void stage_rescue ();

      // Whether a machine `room` metres round could stand at `at`: on dry
      // ground well above any water, no steeper across that than `relief`
      // metres, and with no trunk or boulder within reach.
      bool open_ground (const Vec3& at, float room, float relief) const;

      // The open ground nearest `want`, looked for out to `search` metres
      // round it; `want` itself when there is none.
      Vec3 open_ground_near (const Vec3& want,
                             float room,
                             float relief,
                             float search = 30.0f) const;

      // The trailhead's frame: the bike's heading on the level, and its
      // left.
      std::pair<Vec3, Vec3> bike_frame () const;

      // Where a point unwrapped across the world's seam stands in the lap
      // nearest the spawn.
      Vec3 near_spawn (Vec3 at) const;

      // Whether a machine `room` metres round at `at` keeps clear of the
      // machines already parked there.
      bool clear_of_machines (const Vec3& at, float room) const;

      float from_spawn (const Vec3& at) const;

      void report_parked (const char* name, const Vec3& at, const char* where);

      // The jeep is pulled off onto the verge of the nearest cart road, a
      // few hundred metres out from the trailhead, nose along the road as
      // if left there for the walk in; beside the bike when no road comes
      // near enough.
      void park_jeep ();

      // The tractor stands out on the open ground ahead on the bike's
      // other side, turned to show its flank, as if left there at the end
      // of a day's work; the ground found is room for it and the trailer
      // behind it, some six and a half metres from nose to tailgate.
      void park_tractor ();

      // The Caddy is parked by the sea, on the nearest level, open ground
      // above the shore the boat was left at, nose to the water as if its
      // driver had stood looking out; with no boat, at the trailhead.
      void park_caddy ();

      // The mountainbike is left at the top of the trail's nearest climb,
      // the highest point of it within a kilometre or so, off the tread and
      // facing back down the way it came up, ready for the descent; a few
      // steps off the motocross when the trail does not climb.
      void park_mountain_bike ();

      // The Long John, a cargo bike for errands, stands outside the nearest
      // farmhouse still lived in, or else at the nearest hamlet the roads
      // join -- a lake's outlet or the landing -- pointed home
      // toward the trailhead; back down the trail behind the motocross when
      // none is near.
      void park_long_john ();

      // The helicopter stands on a pad of its own a few hundred metres
      // from the trailhead: the levellest open ground round there, raised
      // above its surroundings where it can be, as a knoll or a shoulder
      // makes a landing site, with no tree within reach of its rotor;
      // clear of `avoid` (the other player's) when given one.
      void park_helicopter (std::optional<Vec3> avoid = std::nullopt);

      // MOPPE_DEMO=forest: park the bike at the world's forest-floor site.
      // MOPPE_START=fingerpost[:N]: before the fingerpost nearest the
      // trailhead (or the Nth further), facing its first arm's lettered
      // side, to read it.
      void move_spawn_to_fingerpost (std::size_t rank);

      void move_spawn_to_forest ();

      // The tree laboratory (MOPPE_TREE_LAB, best with --uplift-years 0 for
      // a rolling plain): the whole forest is replaced by a few specimens
      // standing in the open ahead of the spawn -- a spruce, a birch, and a
      // taller spruce -- so their behaviour can be studied with nothing else
      // in view.
      ForestPlan tree_lab_plan () const;

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

      // A walker reaches for the mushroom at hand: an edible one goes in
      // the basket, a poisonous one is left where it grows.
      void reach_for_mushroom (const InputFrame& input);

      // The nest box with an egg in it the walker can reach into.
      std::optional<std::size_t> egg_at_hand () const;

      // Trunks and the larger boulders stop the rider alike, so both feed
      // the one streamed collision field.
      void settle_obstacles ();

      // -- camp --------------------------------------------------------

      GroundHeight ground_reader () const;

      // The pair of trees the walker stands between, if a hammock would
      // hang there; looked for again only once they have moved.
      void seek_hammock_site ();

      // Within a step of the hung hammock, beside its length.
      bool beside_hammock () const;

      // A fire is laid a step ahead of the walker, on dry and fairly
      // level ground.
      std::optional<Vec3> hearth_ahead () const;

      // The fire a walker could cook on: the nearest one burning well
      // within a couple of steps, their own or the other player's.
      std::optional<Vec3> fire_at_hand () const;

      bool beside_pan () const;

      // What there is to do about supper where the walker stands.
      HudState::Cook supper_at_hand () const;

      // By a fire, the pick key sets a pan of mushrooms from the basket
      // on its stones; once they are fried it eats them, which mends the
      // walker -- less so when they were left to burn.
      void cook_supper (InputFrame& input);

      // -- fishing -------------------------------------------------------

      struct FishingWater {
        Vec3 aim;
        Waters waters = Waters::still;
        bool upland = false;
      };

      // Where the angler fishes from and the way they would cast: on foot,
      // the walker's feet and heading; at the boat's helm, the seat and
      // the way the view looks.
      struct Angler {
        Vec3 at;
        Vec3 ahead;
      };
      std::optional<Angler> angler () const;

      // The river's current at `at`, level.
      Vec3 current_at (const Vec3& at) const;

      // The boat only floats, its engine at rest and hardly moving
      // through the water (a river may carry it): a line can go out.
      bool boat_drifting () const;

      // The open water the angler could cast into from where they are: as
      // far out ahead as a cast reaches where it is deep enough to float a
      // bait. From the helm, only over the bow or the sides, not back over
      // the long-tail.
      std::optional<FishingWater> water_ahead () const;

      // Whether the pick key has nothing nearer to do than cast.
      bool nothing_else_at_hand () const;

      // A splash where the line meets the water.
      void splash (const Vec3& at, int drops, float vigour);

      // On foot by open water, or at the helm of a boat that only floats,
      // the pick key casts a line, and while it is out the key is the
      // line's: it strikes when something takes the float under, and
      // otherwise winds the line in. Walking off, or getting the boat
      // under way, winds it in too; while a fish is fought in and held up,
      // the walker stands and the boat's engine rests.
      void go_fishing (InputFrame& input, seconds_t interval);

      // What there is to do about the line here, and the last fish.
      void add_fishing_readings (HudState& state) const;

      // Lying in the hammock the hours run forty times as fast: one goes
      // by every three seconds of an ordinary day.
      static constexpr float RESTING_HASTE = 40.0f;

      // Hanging and taking down the hammock, lighting and dousing the fire,
      // lying down and getting up; and, while lying there, rocking it and
      // looking about, which is all that a sleeper's controls do.
      void tend_camp (InputFrame& input, seconds_t interval);

      // Sparks fly up from a burning fire and its smoke drifts off; a
      // doused one smoulders a while.
      void feed_fire ();

      // Someone in the hammock sees through eyes that lie back in it; any
      // other camera stands off and can be walked round with the look.
      // Both ease from and to where the walker stood.
      void rest_camera ();

      // What there is to do about the camp where the walker stands.
      void add_camp_readings (HudState& state) const;

      // The places' stones, timber, and buildings; each beacon's laid
      // wood; and the old fire rings, cold until a camp's fire is lit in
      // one.
      void draw_places (render::Renderer& r, const FrameView& frame);

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

      // Whether frames are drawn to keep up with the display. A capture, a
      // recording, or a benchmark draws each frame it is given however
      // long that takes, and its picture must not follow the machine's
      // speed: those hold their resolution.
      bool keeping_pace () const;

      void plan_gazetteer_capture ();

      // The finished world arrived from the generation thread.  Everything
      // left runs in one go: build the retained presentations, place the
      // player, upload the terrain, and start playing.  The loading frame
      // announcing this work has already been submitted, so the screen
      // stays honest while it runs.
      void finish_loading (render::Renderer& r,
                           std::unique_ptr<GeneratedWorld> completed);

      void upload_world_terrain (render::Renderer& r);

      void cast_world_shadows (render::Renderer& r);

      void update_world_atmosphere (float total_time);

      // The weather follows the clock from spell to spell, unless
      // MOPPE_WEATHER holds one kind. Composed views, whose day does not
      // turn, keep the drifting sky they were made under.
      Weather read_weather ();

      // The day: MOPPE_DAY is how many minutes of play one takes (48
      // unless told; 0 holds the sun still), and MOPPE_CLOCK the hour it
      // starts at. Composed views -- the gazetteer, benchmarks, water
      // captures, and any launch that names a sun height without asking
      // for a day -- keep the fixed sun they were made under.
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

      // The clock runs with play, and faster for someone lying down to
      // watch it.
      void pass_time (seconds_t interval);

      // -- company -----------------------------------------------------

      // Opens the link to another player (game/companion_link.hh). A
      // launch that cannot have one plays on alone.
      void seek_company (const std::string& address);

      std::uint32_t this_world () const;

      void forget_companion ();

      void notice (const char* words);

      // How this player looks now, for the other to draw.
      CompanionSnapshot my_snapshot () const;

      // The companion's picks leave the ground here too, and fill the
      // basket they are seen carrying.
      void hear_of_picks (const CompanionSnapshot& theirs);

      // Both players begin beside a bike and a jeep parked on the same
      // spots. Whoever came later moves theirs a few steps aside, and
      // themselves with them if they have not yet walked off.
      void make_room (const CompanionSnapshot& theirs);

      Outfit their_outfit () const;

      // The passenger's seat of the other player's jeep, in the world.
      static Vec3 companion_seat (const CompanionSnapshot& theirs);

      // The fore seat of the other player's boat, in the world.
      static Vec3 companion_boat_seat (const CompanionSnapshot& theirs);

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

      // A burning beacon near the eye lights what stands round it.
      void light_beacons (FrameView& frame) const;

      // A barn lived in keeps its lantern lit in the dark hours; by day
      // the light from its open doors fills it. Near the eye, it is the
      // lamp, unless a fire is nearer.
      void light_barn (FrameView& frame) const;

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
      // A launch told to record itself: MOPPE_VIDEO names the file, and
      // beside it MOPPE_VIDEO_SECONDS how long the video runs before the
      // game quits (otherwise until it is quit), MOPPE_VIDEO_START the
      // seconds of play to let pass first, MOPPE_VIDEO_FPS 30 or 60,
      // MOPPE_VIDEO_SPEED a whole number of times faster than life, and
      // MOPPE_VIDEO_HEIGHT a height to scale to. The world then advances a
      // sixtieth of a second per rendered frame, so the video is even
      // however long its frames took (tools/record).
      struct VideoPlan {
        std::string path;
        int fps = 30;
        float seconds = 0.0f;
        float start = 0.0f;
        int speed = 1;
        int height = 0;
      };

      static std::optional<VideoPlan> plan_video ();

      // Hands this frame to the video being recorded, if it is one of the
      // frames the video takes.
      void record_frame (render::Renderer& r);

      std::string next_clip_path ();

      // The screenshot key and the video key drop their files into one
      // per-run timestamped directory.
      const std::string& snapshot_directory ();

      // The in-game screenshot key's frames number on through a run, so a
      // walk through the world becomes a reviewable series.
      std::string next_snapshot_path ();

      // Beside each screenshot, the camera that took it as a `shot` line for
      // data/opening.txt: printed to the log (on the Xbox, LocalState's
      // log.txt) and gathered in the run directory's shots.txt.
      void record_shot_pose (const FrameView& frame, const std::string& path);

      static int glide_frame_limit ();

      static float glide_speed_mps ();

      static float glide_vertical_speed_mps ();

      const GazetteerShot* current_gazetteer_shot () const noexcept;

      FrameViewInput frame_view_input (float aspect) const;

      void update_frame_flare ();

      void prepare_benchmark_epoch ();

      void finish_benchmark_frame (GraphicsBenchmarkReplay::Boundary boundary);

      void update_benchmark_title (int epoch, uint32_t partition_mask) const;

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
      // The shore the boat was left at, when it was.
      std::optional<Vec3> m_mooring;
      // The way out to open water from there, and where the boat lay
      // and how it faced.
      std::optional<Vec3> m_mooring_out;
      std::optional<std::pair<Vec3, Vec3>> m_boat_berth;
      // How far aside the later of two players parks the machines that
      // stand at places in the world, so that theirs and ours do not
      // stand in one another.
      Vec3 m_park_aside {};
      // MOPPE_BOAT=river: the river's course down to the sea, which the
      // boat autopilot follows, and the next point of it ahead.
      std::vector<Vec3> m_boat_route;
      std::size_t m_boat_route_at = 0;
      // The boat's film camera: how long it has run, the subject and its
      // heading as eased, the fly-by's stand, and its lens (degrees).
      float m_film_time = 0.0f;
      Vec3 m_film_at;
      Vec3 m_film_heading;
      Vec3 m_film_stand;
      std::optional<float> m_film_fov;
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
      // Whose Space was last prompted (HudState::thumb_hint), since when,
      // and whether the bike's and the boat's boosts have been tried.
      mutable int m_thumb_mode = -1;
      mutable double m_thumb_from = 0.0;
      mutable bool m_boost_tried[2] = { false, false };
      mutable bool m_wheelie_tried = false;
      // MOPPE_DEMO=wheelie's last reading of the bike's pitch.
      float m_demo_pitch = 0.0f;
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
      // What stands at the ends of the roads (game/places.hh), and how
      // each sanctuary's beacon burns this frame.
      PlacesPlan m_places;
      Animals m_animals;
      // The barns' poultry, and their nest boxes (the eggs in them are
      // the logic's m_nests).
      Hens m_hens;
      std::vector<Nest> m_nest_sites;
      // MOPPE_WALK=animals: the animal the walk began by, and the capture
      // camera's side of it and its eased aim.
      std::optional<std::size_t> m_watched;
      Vec3 m_watch_side { 1, 0, 0 };
      std::optional<Vec3> m_watch_target;
      // Where the subject stood a step ago, for how fast it goes.
      std::optional<Vec3> m_last_subject;
      PlacesLandscape m_places_landscape;
      std::vector<float> m_beacon_burn;
      // The other player's beacons as last heard (presentation only).
      std::vector<Beacon> m_their_beacons;
      MushroomPatch m_mushrooms;
      // The poisonous mushrooms the foraging script has left be.
      std::vector<std::uint32_t> m_forage_refused;
      // Scratch for the basket's contents as drawn.
      std::vector<MushroomSite> m_basket_contents;
      // The other player, when there is one (game/companion.hh): the wire
      // to them, what they have lately sent, and how they look this frame.
      std::unique_ptr<CompanionLink> m_link;
      CompanionTrack m_track;
      std::vector<CompanionSnapshot> m_arrived;
      std::optional<CompanionSnapshot> m_companion;
      // The other player's machines as ours bump into them, and where
      // each stood a frame ago, to tell how they move.
      std::vector<MachineBody> m_strangers;
      std::optional<std::array<Vec3, machine_count>> m_strangers_were;
      // Their picks in the order they made them, as far as heard, and
      // the same as mushrooms for the basket they carry.
      std::vector<std::uint32_t> m_companion_picks;
      std::vector<MushroomSite> m_companion_basket;
      // Scratch for a pan's contents as drawn, and the last meal's note.
      std::vector<MushroomSite> m_pan_contents;
      mutable std::string m_meal_note;
      mutable std::string m_catch_note;
      // The boats' wakes, ours and the other player's, and their list.
      BoatWake m_boat_wake;
      BoatWake m_their_wake;
      BoatWake m_jet_ski_wake;
      BoatWake m_their_jet_ski_wake;
      render::DrawList m_wake_dl;
      // MOPPE_WALK=fish: when the line last came in.
      float m_fish_script_rest = 1.0f;
      // MOPPE_WALK=boat-fish: when it got under way again.
      float m_boat_fish_off = -1.0f;
      // Their hour of the day; negative with no one to keep time with.
      double m_companion_hours = -1.0;
      bool m_companion_met = false;
      bool m_rode_along = false;
      // What this player wears; the other wears the other.
      Outfit m_outfit = Outfit::blue_jacket;
      bool m_companion_elsewhere = false;
      std::uint32_t m_snapshots_sent = 0;
      // The clock this player's snapshots are stamped by, the link's
      // clock when it was last read, and how far the simulation has
      // stepped since.
      double m_pose_clock = 0.0;
      double m_pose_heard = -1.0;
      double m_stepped = 0.0;
      const char* m_notice = nullptr;
      double m_notice_at = 0.0;
      OpeningArrival m_opening_arrival;
      BlobShadow m_blob;
      mov::TrunkField m_trunk_field;
      // The trees the walker stands between that would take the hammock,
      // and where they last stood when that was worked out.
      std::optional<HammockSite> m_hammock_at_hand;
      Vec3 m_hammock_sought_from;
      bool m_hammock_sought = false;
      float m_walk_script_time = 0.0f;
      // MOPPE_WALK=supper: when the foraging was done.
      float m_supper_since = -1.0f;
      // MOPPE_WALK=barn|byre|eggs: the barn's frame, and how far through
      // the way out of it the egg gatherer has come.
      std::optional<Mat4> m_script_barn;
      int m_egg_script_leg = 0;
      // MOPPE_WALK=camp: the trees it camps between, the side it walks in
      // from, and how far through its steps it is.
      struct CampScript {
        std::optional<HammockSite> site;
        float side = 1.0f;
        int step = 0;
        float since = 0.0f;
      };
      CampScript m_camp_script;
      Hud m_hud;
      render::TextList m_hud_text;

      render::Renderer* m_renderer;
      bool m_automated_regeneration_done = false;
      std::string m_screenshot_path;
      bool m_snapshot_requested = false;
      std::string m_snapshot_directory;
      int m_snapshot_count = 0;
      VideoRecorder m_video;
      std::optional<VideoPlan> m_video_plan = plan_video ();
      int m_video_render_frame = 0;
      bool m_video_saw_flight = false;
      int m_clip_count = 0;
      bool m_video_done = false;
      std::optional<WaterShot> m_water_shot;
      std::optional<WaterInspection> m_water_inspection;
      std::optional<mov::Trunk> m_orbit_tree;
      // Whether L has handed the mouse back to the desktop during play.
      bool m_pointer_free = false;
      MapSheet m_map;
      // Esc in play has asked whether to quit (Hud::draw_quit_question).
      bool m_quit_asked = false;
      bool m_map_asked = false;
      // A free camera with no rider, for looking at the world as it is.
      struct Spectator {
        Vec3 eye {};
        float yaw = 0.0f;
        float pitch = 0.0f;
        std::set<platform::Key> held;
        bool pointer_free = false;
      };
      std::optional<Spectator> m_spectator =
        moppe::environment ("MOPPE_SPECTATOR")
          ? std::optional<Spectator> (Spectator {})
          : std::nullopt;
      std::optional<GazetteerCaptureConfig> m_gazetteer;
      LandscapeGazetteer m_gazetteer_plan;
      std::size_t m_gazetteer_shot = 0;
      int m_gazetteer_settle_frame = 0;
      int m_glide_frame = 0;
      int m_screenshot_frames;
      int m_cinematic_capture_frame = 0;
      int m_cinematic_capture_render_frame = 0;
      int m_ride_capture_frame = 0;
      int m_ride_capture_render_frame = 0;
      std::atomic<bool> m_ready;
      std::optional<GraphicsBenchmarkConfig> m_benchmark;
      GraphicsSettings m_benchmark_baseline;
      std::optional<GraphicsBenchmarkReplay> m_benchmark_replay;
      std::optional<GameState> m_benchmark_checkpoint;
      std::optional<GraphicsBenchmarkReplay::Frame> m_benchmark_render_frame;
      uint32_t m_benchmark_mask = 0;
      bool m_benchmark_epoch_pending = false;
      bool m_benchmark_submitted = false;
      bool m_benchmark_results_written = false;

      render::DrawList m_world_dl;
      // Where the shadow casters' riders go, to be thrown away.
      render::DrawList m_caster_dl;
      render::DrawList m_hud_dl;
    };
  }
}

#endif
