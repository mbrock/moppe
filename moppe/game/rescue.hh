#ifndef MOPPE_GAME_RESCUE_HH
#define MOPPE_GAME_RESCUE_HH

#include <moppe/game/machine.hh>
#include <moppe/gfx/math.hh>

#include <array>
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
  // been moved three metres, it is no longer stuck. The boat, run up on
  // the land, is dragged back to the water the same way, by the handle
  // on its bow, and the jet ski by the eye in its stem.
  // A machine as the bumpers see it.
  struct MachineBody {
    Machine machine = Machine::bike;
    position_t position {};
    direction_t forward = unit_direction (Vec3 (0, 0, 1));
    direction_t up = unit_direction (Vec3 (0, 1, 0));
    velocity_t velocity = moppe::velocity (Vec3 ());
    kilograms_t mass = 1.0f * u::kg;
    // This game's to push; the other player's machines are theirs.
    bool ours = true;
    // Whether it tows something, the frame of what it tows, for its
    // towed bumpers, and its weight.
    bool towing = false;
    position_t towed_position {};
    direction_t towed_forward = unit_direction (Vec3 (0, 0, 1));
    direction_t towed_up = unit_direction (Vec3 (0, 1, 0));
    kilograms_t towed_mass = 1.0f * u::kg;
    // Its own bumpers, in place of its kind's: what is not one of the
    // players' machines (a bus, bus.hh).
    std::span<const Bumper> bumpers;

    position_t point (const displacement_t& local) const;
  };

  // Two machines meeting: `a` takes `impulse` at `at`, and `b` the
  // opposite. `speed` is how fast they closed.
  struct Bump {
    int a = 0, b = 0;
    position_t at {};
    impulse_t impulse = moppe::impulse (Vec3 ());
    speed_t speed {};
  };

  // The blows between every two of `bodies` that touch, at least one of
  // them ours.
  std::vector<Bump> find_bumps (std::span<const MachineBody> bodies);

  // How a machine is getting on: where it was last seen to stand, how
  // long the throttle has been open since without its getting away, and
  // the help it has had.
  struct Trouble {
    position_t anchor {};
    seconds_t strain {};
    bool stuck = false;
    seconds_t heave_wait {};
    int heaves = 0;
  };

  // Moves a machine's trouble on by `dt`, `throttle` being its driver's
  // (zero when no one drives it). True when it should heave just now.
  bool strain (Trouble& trouble,
               position_t position,
               control_signal_t throttle,
               seconds_t dt);

  // The change of velocity a heave gives: along the level way it faces,
  // signed by the throttle, and up.
  velocity_t heave (const Trouble& trouble,
                    direction_t forward,
                    control_signal_t throttle);

  // A rope from the walker's hand to a machine.
  struct Haul {
    bool active = false;
    Machine machine = Machine::bike;
    // Where it is tied, in the machine's frame, and how long it is,
    // measured level from there to the walker's feet.
    displacement_t tie = displacement (Vec3 ());
    meters_t rope = 1.4f * u::m;
  };

  // How much further the walker can lean out on the rope, or press in.
  inline constexpr meters_t rope_give = 0.35f * u::m;

  // What the rope does to the machine over a step:
  // `pull` at the tie, after the walker when they are further off than
  // its length and away from them, holding it off, when they are nearer;
  // and while it pulls, `lift` at the machine's centre, taking some of
  // its weight off the ground without tipping it.
  struct HaulForce {
    force_t pull = force (Vec3 ());
    force_t lift = force (Vec3 ());
  };
  HaulForce haul_force (const Haul& haul,
                        const MachineBody& body,
                        position_t feet,
                        velocity_t walker_velocity);
}

#endif
