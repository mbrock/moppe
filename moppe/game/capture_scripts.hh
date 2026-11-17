// The automated captures' scripted walks (MOPPE_WALK, walk_scripts.cc):
// where each starts, what it keeps from step to step, and the input it
// drives the walker with; and the cameras that film them
// (capture_cameras.cc).
#ifndef MOPPE_GAME_CAPTURE_SCRIPTS_HH
#define MOPPE_GAME_CAPTURE_SCRIPTS_HH

#include <moppe/game/camp.hh>
#include <moppe/game/game_session.hh>
#include <moppe/game/input_frame.hh>
#include <moppe/game/surroundings.hh>
#include <moppe/game/world_ground.hh>
#include <moppe/gfx/mat4.hh>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace moppe::render {
  class Renderer;
}

namespace moppe::game {
  struct WorldPlaces;

  struct WalkScriptState {
    float time = 0.0f;
    // MOPPE_WALK=forage: the poisonous mushrooms it has left be.
    std::vector<std::uint32_t> forage_refused;
    // MOPPE_WALK=supper: when the foraging was done.
    float supper_since = -1.0f;
    // MOPPE_WALK=barn|byre|eggs: the barn's frame, and how far through
    // the way out of it the egg gatherer has come.
    std::optional<Mat4> barn;
    int egg_leg = 0;
    // MOPPE_WALK=fish: when the line last came in.
    float fish_rest = 1.0f;
    // MOPPE_WALK=boat-fish: when it got under way again.
    float boat_fish_off = -1.0f;
    // MOPPE_WALK=camp: the trees it camps between, the side it walks in
    // from, and how far through its steps it is.
    struct Camp {
      std::optional<HammockSite> site;
      float side = 1.0f;
      int step = 0;
      float since = 0.0f;
    };
    Camp camp;
  };

  // The cameras automated captures film with (MOPPE_WALK_CAMERA=side,
  // MOPPE_RIDE_CAMERA, MOPPE_ANIMAL_CAMERA), and the view from the
  // hammock, each taking over play's following camera where it applies.
  class CaptureCameras {
  public:
    // After the session's step: places play's camera where one of the
    // views applies.
    void override_camera (GameSession& session,
                          const WorldGround& ground,
                          seconds_t interval);

    // MOPPE_WALK=animals: the animal the walk began by, to film from the
    // side `side` of it (or the side last filmed from).
    void watch (std::size_t animal, std::optional<Vec3> side) {
      watched = animal;
      if (side)
        watch_side = *side;
    }

    // The lens a film camera asks for this step, degrees.
    std::optional<float> lens () const {
      return film_fov;
    }

  private:
    // Someone in the hammock sees through eyes that lie back in it; any
    // other camera stands off and can be walked round with the look.
    // Both ease from and to where the walker stood.
    void rest (GameSession& session, const WorldGround& ground);

    // MOPPE_WALK_CAMERA=side: a camera locked beside the walking figure,
    // so the gait can be judged against the ground passing beneath it.
    void walk_side (GameSession& session);

    // MOPPE_ANIMAL_CAMERA=<metres>, with MOPPE_WALK=animals[:SPECIES]: a
    // camera that far from the animal the walk began by, square to the
    // walker's way in so both show, following it as it goes.
    void follow_animal (GameSession& session,
                        const WorldGround& ground,
                        seconds_t interval);

    // MOPPE_RIDE_CAMERA=side|front: a camera locked beside or ahead of
    // the ridden bike or mountainbike, the glider, the driven jeep or
    // boat, or whatever hangs under the helicopter, to judge the rider
    // and the machine.
    void
    ride (GameSession& session, const WorldGround& ground, seconds_t interval);

    // Cameras for filming the boat (MOPPE_RIDE_CAMERA): `drone` follows
    // high behind, `aerial` far off with a long lens, `orbit` circles it,
    // `top` looks straight down on it, `fpv` races close behind and low,
    // `hull` is mounted low on its side looking back at the helmsman,
    // `stern` over the long-tail looking ahead, `helm` is the helmsman's
    // eyes, and `flyby` stands on the water ahead of it and watches it
    // pass on a long lens. MOPPE_RIDE_CAMERA_ANGLE swings the following
    // ones round, degrees to the right of behind.
    bool film_boat (GameSession& session,
                    const WorldGround& ground,
                    std::string_view view,
                    seconds_t interval);

    // The animal camera's subject, its side of it, and its eased aim.
    std::optional<std::size_t> watched;
    Vec3 watch_side { 1, 0, 0 };
    std::optional<Vec3> watch_target;
    // The boat's film camera: how long it has run, the subject and its
    // heading as eased, the fly-by's stand, and its lens (degrees).
    float film_time = 0.0f;
    Vec3 film_at;
    Vec3 film_heading;
    Vec3 film_stand;
    std::optional<float> film_fov;
  };

  // What a walk script stands in and moves.
  struct ScriptStage {
    GameSession& session;
    WorldGround ground;
    Surroundings around;
    const WorldPlaces& places;
    const MushroomPatch& mushrooms;
    Vec3 spawn;
    CaptureCameras& cameras;
    render::Renderer* renderer;
  };

  class WalkScripts {
  public:
    // Once the world is ready, places the walker where the launch's
    // script begins: MOPPE_WALK=camp a few steps from the fit pair of
    // trees nearest the spawn, on whichever side is clearer to walk in
    // from; fish on the bank of the open water nearest the spawn, facing
    // out over it; landing|sanctuary|farm and the like on foot at the
    // landing's jetty, a sanctuary's gate, or a farm, facing it.
    void stage (const ScriptStage& s);

    // A deterministic on-foot script: "walk", "run", and "jump" hold one
    // gait, "tour" stands, walks, runs, jumps twice from the run, and
    // comes to rest, turning gently throughout so a following camera
    // sees the figure from changing sides, and "forage" starts beside the
    // mushrooms nearest the spawn and picks its way from one to the next.
    InputFrame
    step (const ScriptStage& s, std::string_view script, seconds_t interval);

  private:
    void stage_fish (const ScriptStage& s);
    void stage_place (const ScriptStage& s);
    void stage_camp (const ScriptStage& s);

    // The camp script: walk in between the trees and hang the hammock,
    // step out and light a fire, come back and lie down, and after a
    // while see the sky through the sleeper's eyes.
    InputFrame camp (const ScriptStage& s, seconds_t interval);

    WalkScriptState m_state;
  };
}

#endif
