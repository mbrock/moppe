// What the automated captures' scripts and cameras keep from step to
// step (MOPPE_WALK, MOPPE_DEMO, MOPPE_ANIMAL_CAMERA, MOPPE_RIDE_CAMERA):
// how far each walk script is through its steps, and what the capture
// cameras ease. MoppeGame's game_scripts.cc and game_cameras.cc work them;
// play reads none of it.
#ifndef MOPPE_GAME_CAPTURE_SCRIPTS_HH
#define MOPPE_GAME_CAPTURE_SCRIPTS_HH

#include <moppe/game/camp.hh>
#include <moppe/gfx/mat4.hh>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace moppe::game {
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
    // MOPPE_DEMO=wheelie's last reading of the bike's pitch.
    float demo_pitch = 0.0f;
  };

  struct CaptureCameraState {
    // MOPPE_WALK=animals: the animal the walk began by, and the capture
    // camera's side of it and its eased aim.
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
}

#endif
