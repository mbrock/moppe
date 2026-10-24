#ifndef MOPPE_GAME_RESCUE_HH
#define MOPPE_GAME_RESCUE_HH

#include <moppe/gfx/math.hh>

#include <cstdint>
#include <span>
#include <vector>

namespace moppe::game {
  // Help for a machine in trouble, and the knocks machines give each
  // other.
  //
  // Each machine stands in a Box3D world of its own, so two never meet
  // there. They meet here instead, each as a few spheres (its bumpers),
  // and a meeting is a pair of equal and opposite blows that send them
  // apart a little livelier than they came together, as bumper cars do.
  // The other player's machines are met the same way; each machine takes
  // only its own share of the blow, so the two games, each pushing only
  // what it simulates, agree.
  //
  // A machine is stuck when its driver has held the throttle open for a
  // few seconds and it has not got three metres from where it stood.
  // Driven and stuck, it heaves itself the way the driver wants to go, a
  // hop every second and a half and a bigger one each time. Left stuck,
  // the mount key beside it takes up a rope tied to its nearer end
  // instead of getting in, and walking away hauls it out after the
  // walker (walking at it pushes it); the key again lets go. Once it has
  // been moved three metres, it is no longer stuck.
  enum class Machine : std::uint8_t {
    bike,
    jeep,
    tractor,
    caddy,
    helicopter,
    mountain_bike
  };
  inline constexpr int machine_count = 6;

  // "the tractor", for the prompts.
  const char* machine_name (Machine machine);

  // One of a machine's spheres, in its frame: x to the left, y up, z
  // forward, from the point its position() names.
  struct Bumper {
    Vec3 centre;
    float radius = 0.0f;
  };
  std::span<const Bumper> bumpers (Machine machine);

  // Where its weight is centred, in the same frame.
  Vec3 machine_centre (Machine machine);

  // Where a rope is tied at its front or back end, in its frame.
  Vec3 tie_point (Machine machine, bool front);

  // A machine as the bumpers see it.
  struct MachineBody {
    Machine machine = Machine::bike;
    Vec3 position;
    Vec3 forward { 0, 0, 1 };
    Vec3 up { 0, 1, 0 };
    Vec3 velocity;
    float mass = 1.0f;
    // This game's to push; the other player's machines are theirs.
    bool ours = true;

    Vec3 point (const Vec3& local) const;
  };

  // Two machines meeting: `a` takes `impulse` at `at`, and `b` the
  // opposite. `speed` is how fast they closed.
  struct Bump {
    int a = 0, b = 0;
    Vec3 at;
    Vec3 impulse;
    float speed = 0.0f;
  };

  // The blows between every two of `bodies` that touch, at least one of
  // them ours.
  std::vector<Bump> find_bumps (std::span<const MachineBody> bodies);

  // How a machine is getting on: where it was last seen to stand, how
  // long the throttle has been open since without its getting away, and
  // the help it has had.
  struct Trouble {
    Vec3 anchor;
    float strain = 0.0f;
    bool stuck = false;
    float heave_wait = 0.0f;
    int heaves = 0;
  };

  // Moves a machine's trouble on by `dt`, `throttle` being its driver's
  // (zero when no one drives it). True when it should heave just now.
  bool
  strain (Trouble& trouble, const Vec3& position, float throttle, float dt);

  // The change of velocity a heave gives: along the level way it faces,
  // signed by the throttle, and up.
  Vec3 heave (const Trouble& trouble, const Vec3& forward, float throttle);

  // A rope from the walker's hand to a machine.
  struct Haul {
    bool active = false;
    Machine machine = Machine::bike;
    // Where it is tied, in the machine's frame, and how long it is,
    // measured level from there to the walker's feet.
    Vec3 tie;
    float rope = 1.4f;
  };

  // How much further the walker can lean out on the rope, or press in.
  inline constexpr float rope_give = 0.35f;

  // What the rope does to the machine over a step, forces in newtons:
  // `pull` at the tie, after the walker when they are further off than
  // its length and away from them, holding it off, when they are nearer;
  // and while it pulls, `lift` at the machine's centre, taking some of
  // its weight off the ground without tipping it.
  struct HaulForce {
    Vec3 pull;
    Vec3 lift;
  };
  HaulForce haul_force (const Haul& haul,
                        const MachineBody& body,
                        const Vec3& feet,
                        const Vec3& walker_velocity);
}

#endif
