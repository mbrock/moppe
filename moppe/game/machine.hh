#ifndef MOPPE_GAME_MACHINE_HH
#define MOPPE_GAME_MACHINE_HH

#include <moppe/gfx/math.hh>

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace moppe::game {
  // The machines a player has, one of each (game/fleet.hh). Their order
  // is only a name's: the fleet steps them in its own.
  enum class Machine : std::uint8_t {
    bike,
    jeep,
    tractor,
    caddy,
    helicopter,
    mountain_bike,
    boat,
    long_john,
    jet_ski
  };
  inline constexpr int machine_count = 9;

  // One of a machine's spheres, in its frame: x to the left, y up, z
  // forward, from the point its position() names.
  struct Bumper {
    displacement_t centre;
    meters_t radius {};
  };

  // What a machine goes on.
  enum class Medium : std::uint8_t { land, water, air };

  // How the view rides along with a machine's driver: the eyes over the
  // seat, up and ahead in the machine's frame; the point on it a
  // following camera swings about, so high over its position; and how
  // far behind, at the least, and how high that camera stands.
  struct SeatView {
    displacement_t eye;
    meters_t pivot_height {};
    meters_t distance {};
    meters_t nearest {};
    degrees_t elevation = 18.0f * u::deg;
  };

  // Everything the game knows of a kind of machine that is not its
  // physics (the mov classes) or its drawing: what it is called and
  // what its controls do, where one gets in and out, how the view
  // follows it, and its shape for the bumpers, the rope, and the sling.
  struct MachineKind {
    // "the tractor", for the prompts.
    const char* name = "";
    // The mount key's prompt beside it, with a key and without one.
    const char* take = "";
    const char* at_hand = "";
    // The mount key's, and the thumb's, from its seat.
    const char* leave = "";
    const char* thumb = nullptr;
    Medium medium = Medium::land;
    // Ridden by pedalling, with no jets; whose rear brake what runs on
    // foot stamps on; and whose front wheel leaning back lifts.
    bool pedalled = false;
    bool skids = false;
    bool wheelies = false;
    // Where it is reached for, in its frame, and from how far.
    displacement_t reach_from;
    meters_t reach {};
    // Where its driver stands on getting out, in its frame; none for
    // the water's machines, which set them down over whichever side has
    // the shallowest water.
    std::optional<displacement_t> step_out;
    SeatView view;
    // The other player's seat, where they ride along with its driver
    // (companion.hh): the middle of its cushion, in its frame. None
    // for the bikes and the tractor, which seat one.
    std::optional<displacement_t> passenger_seat;
    // How wide a path through the undergrowth it parts, and how wide
    // the soft shadow under it is (none on the water).
    meters_t undergrowth_parting {};
    meters_t shadow_radius {};
    // How high over it its waymark stands, and the waymark over the
    // other player driving it (heads_up.cc).
    meters_t mark_height {};
    meters_t driver_mark {};
    std::span<const Bumper> bumpers;
    // Where its weight is centred.
    displacement_t centre;
    // Where a rope is tied at its front or back end.
    displacement_t front_tie, back_tie;
    // The ring over it that the helicopter's line hooks on
    // (game/sling.hh), and where its bridle's legs are made fast (a
    // bike's two, at its ends, twice over).
    displacement_t ring;
    std::array<displacement_t, 4> legs {};
    // The bumpers of what it tows (the tractor's trailer), in the towed
    // body's frame.
    std::span<const Bumper> towed_bumpers;
  };
  const MachineKind& machine_kind (Machine machine);
}

#endif
