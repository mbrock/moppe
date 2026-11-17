#include <moppe/game/machine.hh>

namespace moppe::game {
  namespace {
    // Kept in a namespace of its own: the engine is a unity build.
    namespace machine_parts {
      // The bumpers: the Trail 90's wheels and rider, the jeep's tub, the
      // tractor's big rear wheels and its bonnet, the Caddy's long body,
      // the helicopter's cabin and tail boom, and the mountainbike's
      // wheels and rider.
      const Bumper bike_bumpers[] = {
        { Vec3 (0.0f, 0.05f, 0.45f), 0.36f * u::m },
        { Vec3 (0.0f, 0.1f, -0.5f), 0.38f * u::m },
        { Vec3 (0.0f, 0.85f, -0.15f), 0.32f * u::m },
      };
      const Bumper jeep_bumpers[] = {
        { Vec3 (0.0f, 0.5f, 1.15f), 0.85f * u::m },
        { Vec3 (0.0f, 0.5f, 0.0f), 0.9f * u::m },
        { Vec3 (0.0f, 0.5f, -1.15f), 0.85f * u::m },
      };
      const Bumper tractor_bumpers[] = {
        { Vec3 (0.0f, 0.8f, -0.8f), 1.0f * u::m },
        { Vec3 (0.0f, 0.75f, 0.85f), 0.75f * u::m },
      };
      // Its trailer's bed, from the tailgate to the headboard.
      const Bumper trailer_bumpers[] = {
        { Vec3 (0.0f, 0.85f, -0.6f), 0.8f * u::m },
        { Vec3 (0.0f, 0.85f, 0.15f), 0.8f * u::m },
        { Vec3 (0.0f, 0.85f, 0.9f), 0.8f * u::m },
      };
      const Bumper caddy_bumpers[] = {
        { Vec3 (0.0f, 0.45f, 1.3f), 0.82f * u::m },
        { Vec3 (0.0f, 0.45f, 0.0f), 0.85f * u::m },
        { Vec3 (0.0f, 0.5f, -1.4f), 0.82f * u::m },
      };
      const Bumper mountain_bike_bumpers[] = {
        { Vec3 (0.0f, 0.05f, 0.4f), 0.36f * u::m },
        { Vec3 (0.0f, 0.05f, -0.4f), 0.36f * u::m },
        { Vec3 (0.0f, 0.85f, -0.1f), 0.32f * u::m },
      };
      // The Long John's back wheel, its box, its front wheel, and the
      // rider over the back.
      const Bumper long_john_bumpers[] = {
        { Vec3 (0.0f, 0.0f, -0.95f), 0.36f * u::m },
        { Vec3 (0.0f, -0.05f, 0.3f), 0.4f * u::m },
        { Vec3 (0.0f, -0.05f, 0.9f), 0.3f * u::m },
        { Vec3 (0.0f, 0.8f, -0.6f), 0.3f * u::m },
      };
      // The boat's long narrow hull, keel to gunwale.
      const Bumper boat_bumpers[] = {
        { Vec3 (0.0f, 0.22f, -0.5f), 0.45f * u::m },
        { Vec3 (0.0f, 0.22f, 0.4f), 0.45f * u::m },
        { Vec3 (0.0f, 0.22f, 1.3f), 0.45f * u::m },
        { Vec3 (0.0f, 0.25f, 2.2f), 0.4f * u::m },
      };
      // The jet ski's hull, and its cowl and saddle over it.
      const Bumper jet_ski_bumpers[] = {
        { Vec3 (0.0f, 0.3f, -0.6f), 0.5f * u::m },
        { Vec3 (0.0f, 0.35f, 0.3f), 0.5f * u::m },
        { Vec3 (0.0f, 0.4f, 1.15f), 0.4f * u::m },
      };
      const Bumper helicopter_bumpers[] = {
        { Vec3 (0.0f, 1.3f, 0.3f), 1.25f * u::m },
        { Vec3 (0.0f, 1.7f, -2.4f), 0.5f * u::m },
        { Vec3 (0.0f, 1.75f, -4.2f), 0.5f * u::m },
      };
    }
  }

  using namespace machine_parts;

  const MachineKind& machine_kind (Machine machine) {
    // By Machine. The bumpers are laid over each machine's Box3D hulls;
    // the helicopter's rotor meets nothing, and nothing hangs from its
    // hook beneath it.
    static const MachineKind kinds[machine_count] = {
      { .name = "the bike",
        .take = "Ride",
        .at_hand = "Bike within reach",
        .leave = "Get off",
        .thumb = "Boost",
        .skids = true,
        .wheelies = true,
        .reach = 5.0f * u::m,
        .step_out = Vec3 (1.8f, 0.0f, 0.0f),
        // The Trail 90 is life size, its reference point between the
        // axles at their height; the seated rider's eyes are some 1.4 m
        // over it.
        .view = { .eye = Vec3 (0.0f, 1.4f, 0.05f),
                  .pivot_height = 0.8f * u::m,
                  .distance = 5.0f * u::m,
                  .nearest = 1.8f * u::m,
                  .elevation = 15.0f * u::deg },
        .undergrowth_parting = 1.15f * u::m,
        .shadow_radius = 2.2f * u::m,
        .mark_height = 1.6f * u::m,
        .bumpers = bike_bumpers,
        .centre = Vec3 (0.0f, 0.42f, 0.0f),
        // The front fender's stays, or the rack's rear rail.
        .front_tie = Vec3 (0.0f, 0.2f, 0.85f),
        .back_tie = Vec3 (0.0f, 0.36f, -1.0f),
        .ring = Vec3 (0.0f, 2.1f, 0.0f),
        // The fork's top clamp and the rack's rear corners.
        .legs = { Vec3 (0.0f, 0.53f, 0.35f),
                  Vec3 (0.0f, 0.53f, 0.35f),
                  Vec3 (0.13f, 0.36f, -0.97f),
                  Vec3 (-0.13f, 0.36f, -0.97f) } },
      { .name = "the jeep",
        .take = "Drive",
        .at_hand = "Jeep within reach",
        .leave = "Step out",
        .thumb = "Brake",
        .reach = 4.5f * u::m,
        // On the driver's side.
        .step_out = Vec3 (1.75f, 0.3f, -0.1f),
        .view = { .eye = Vec3 (0.0f, 0.78f, 0.08f),
                  .pivot_height = 0.9f * u::m,
                  .distance = 8.0f * u::m,
                  .nearest = 3.4f * u::m },
        .undergrowth_parting = 1.7f * u::m,
        .shadow_radius = 3.4f * u::m,
        .mark_height = 1.9f * u::m,
        .bumpers = jeep_bumpers,
        .centre = Vec3 (0.0f, 0.15f, 0.05f),
        .front_tie = Vec3 (0.0f, 0.35f, 1.95f),
        .back_tie = Vec3 (0.0f, 0.35f, -1.95f),
        .ring = Vec3 (0.0f, 2.8f, 0.05f),
        // The roll cage's corners.
        .legs = { Vec3 (0.72f, 0.95f, 0.55f),
                  Vec3 (-0.72f, 0.95f, 0.55f),
                  Vec3 (0.72f, 0.95f, -1.2f),
                  Vec3 (-0.72f, 0.95f, -1.2f) } },
      { .name = "the tractor",
        .take = "Drive the tractor",
        .at_hand = "Tractor within reach",
        .leave = "Climb down",
        .thumb = "Brake",
        .reach = 3.6f * u::m,
        // Down off the left side, ahead of the big wheel.
        .step_out = Vec3 (1.2f, 0.1f, 0.35f),
        // Back over the trailer.
        .view = { .eye = Vec3 (0.0f, 0.80f, 0.1f),
                  .pivot_height = 1.5f * u::m,
                  .distance = 8.5f * u::m,
                  .nearest = 3.0f * u::m },
        .undergrowth_parting = 1.8f * u::m,
        .shadow_radius = 2.8f * u::m,
        .mark_height = 2.4f * u::m,
        .driver_mark = 0.8f * u::m,
        .bumpers = tractor_bumpers,
        .centre = Vec3 (0.0f, 0.78f, -0.15f),
        // The front axle's beam, or the drawbar under the seat.
        .front_tie = Vec3 (0.0f, 0.45f, 1.55f),
        .back_tie = Vec3 (0.0f, 0.55f, -1.45f),
        .ring = Vec3 (0.0f, 3.4f, -0.15f),
        // The front axle's ends and the rear mudguards.
        .legs = { Vec3 (0.45f, 0.75f, 1.35f),
                  Vec3 (-0.45f, 0.75f, 1.35f),
                  Vec3 (0.75f, 1.25f, -0.75f),
                  Vec3 (-0.75f, 1.25f, -0.75f) },
        .towed_bumpers = trailer_bumpers },
      { .name = "the Caddy",
        .take = "Drive the Caddy",
        .at_hand = "Caddy within reach",
        .leave = "Step out",
        .thumb = "Brake",
        // From its cab.
        .reach_from = Vec3 (0.0f, 0.0f, 0.25f),
        .reach = 3.4f * u::m,
        // Out of the driver's door, on the left.
        .step_out = Vec3 (1.35f, 0.2f, 0.25f),
        .view = { .eye = Vec3 (0.0f, 0.70f, 0.06f),
                  .pivot_height = 1.0f * u::m,
                  .distance = 8.5f * u::m,
                  .nearest = 3.6f * u::m },
        .undergrowth_parting = 1.6f * u::m,
        .shadow_radius = 3.6f * u::m,
        .mark_height = 2.0f * u::m,
        .driver_mark = 1.0f * u::m,
        .bumpers = caddy_bumpers,
        .centre = Vec3 (0.0f, 0.24f, 0.30f),
        .front_tie = Vec3 (0.0f, 0.25f, 2.06f),
        .back_tie = Vec3 (0.0f, 0.25f, -2.22f),
        .ring = Vec3 (0.0f, 3.0f, 0.30f),
        // The bonnet's front corners and the bed's back ones.
        .legs = { Vec3 (0.72f, 0.62f, 1.70f),
                  Vec3 (-0.72f, 0.62f, 1.70f),
                  Vec3 (0.74f, 0.68f, -2.12f),
                  Vec3 (-0.74f, 0.68f, -2.12f) } },
      { .name = "the helicopter",
        .take = "Fly the helicopter",
        .at_hand = "Helicopter within reach",
        .leave = "Step out",
        .thumb = "Climb",
        .medium = Medium::air,
        // From its cabin, a little ahead of its mast.
        .reach_from = Vec3 (0.0f, 0.0f, 0.4f),
        .reach = 4.0f * u::m,
        // Out on the pilot's side, the right.
        .step_out = Vec3 (-2.0f, 0.3f, 0.4f),
        .view = { .eye = Vec3 (0.0f, 0.78f, 0.1f),
                  .pivot_height = 1.6f * u::m,
                  .distance = 12.0f * u::m,
                  .nearest = 5.0f * u::m,
                  .elevation = 21.0f * u::deg },
        // The downwash flattens the grass under it.
        .undergrowth_parting = 4.0f * u::m,
        .shadow_radius = 4.5f * u::m,
        .mark_height = 3.4f * u::m,
        .driver_mark = 1.0f * u::m,
        .bumpers = helicopter_bumpers,
        .centre = Vec3 (0.0f, 1.15f, 0.05f),
        .front_tie = Vec3 (0.0f, 0.3f, 1.6f),
        .back_tie = Vec3 (0.0f, 0.3f, -1.6f) },
      { .name = "the mountainbike",
        .take = "Ride the mountainbike",
        .at_hand = "Mountainbike within reach",
        .leave = "Get off",
        .thumb = "Hop",
        .pedalled = true,
        .skids = true,
        .reach = 2.4f * u::m,
        // Off on the left, as a cyclist gets off.
        .step_out = Vec3 (0.8f, 0.0f, 0.0f),
        // The bike is life size, its reference point between the axles
        // at their height; the rider's eyes are some 1.3 m over that.
        .view = { .eye = Vec3 (0.0f, 1.3f, 0.1f),
                  .pivot_height = 0.75f * u::m,
                  .distance = 4.6f * u::m,
                  .nearest = 1.6f * u::m,
                  .elevation = 15.0f * u::deg },
        .undergrowth_parting = 0.6f * u::m,
        .shadow_radius = 1.3f * u::m,
        .mark_height = 1.5f * u::m,
        .driver_mark = 1.4f * u::m,
        .bumpers = mountain_bike_bumpers,
        .centre = Vec3 (0.0f, 0.55f, 0.0f),
        // The head tube, or the seat post.
        .front_tie = Vec3 (0.0f, 0.45f, 0.35f),
        .back_tie = Vec3 (0.0f, 0.5f, -0.25f),
        .ring = Vec3 (0.0f, 2.0f, 0.05f),
        .legs = { Vec3 (0.0f, 0.75f, 0.4f),
                  Vec3 (0.0f, 0.75f, 0.4f),
                  Vec3 (0.0f, 0.8f, -0.25f),
                  Vec3 (0.0f, 0.8f, -0.25f) } },
      { .name = "the boat",
        .take = "Take the boat out",
        .at_hand = "Boat within reach",
        .leave = "Step ashore",
        .thumb = "Flat out",
        .medium = Medium::water,
        // From its middle: it may lie a step out in the water.
        .reach_from = Vec3 (0.0f, 0.0f, 0.6f),
        .reach = 4.0f * u::m,
        .view = { .eye = Vec3 (0.0f, 0.85f, 0.1f),
                  .pivot_height = 1.0f * u::m,
                  .distance = 6.5f * u::m,
                  .nearest = 3.0f * u::m,
                  .elevation = 16.0f * u::deg },
        .mark_height = 1.4f * u::m,
        .driver_mark = 1.2f * u::m,
        .bumpers = boat_bumpers,
        .centre = Vec3 (0.0f, 0.2f, 0.15f),
        // The carrying handle on the bow, or the transom's top.
        .front_tie = Vec3 (0.0f, 0.45f, 2.65f),
        .back_tie = Vec3 (0.0f, 0.35f, -0.95f),
        .ring = Vec3 (0.0f, 2.2f, 0.6f),
        // The bow handle and the transom's corners.
        .legs = { Vec3 (0.0f, 0.45f, 2.65f),
                  Vec3 (0.0f, 0.45f, 2.65f),
                  Vec3 (0.4f, 0.36f, -0.9f),
                  Vec3 (-0.4f, 0.36f, -0.9f) } },
      { .name = "the Long John",
        .take = "Ride the Long John",
        .at_hand = "Long John within reach",
        .leave = "Get off",
        .pedalled = true,
        .skids = true,
        // From its saddle, behind its middle.
        .reach_from = Vec3 (0.0f, 0.0f, -0.7f),
        .reach = 2.4f * u::m,
        .step_out = Vec3 (0.8f, 0.0f, 0.0f),
        // The rider sits over the back wheel, some 0.7 m behind the
        // reference point midway between the axles, eyes 1.45 m over it;
        // the camera stands back far enough to take in the box ahead.
        .view = { .eye = Vec3 (0.0f, 1.45f, -0.6f),
                  .pivot_height = 0.8f * u::m,
                  .distance = 5.6f * u::m,
                  .nearest = 2.0f * u::m,
                  .elevation = 15.0f * u::deg },
        .undergrowth_parting = 0.8f * u::m,
        .shadow_radius = 2.0f * u::m,
        .mark_height = 1.5f * u::m,
        .driver_mark = 1.4f * u::m,
        .bumpers = long_john_bumpers,
        .centre = Vec3 (0.0f, 0.2f, -0.2f),
        // The front bulkhead, or the seat post.
        .front_tie = Vec3 (0.0f, 0.3f, 0.82f),
        .back_tie = Vec3 (0.0f, 0.4f, -0.72f),
        .ring = Vec3 (0.0f, 2.2f, 0.1f),
        // The bulkhead's top corners and the hoop's.
        .legs = { Vec3 (0.19f, 0.39f, 0.81f),
                  Vec3 (-0.19f, 0.39f, 0.81f),
                  Vec3 (0.25f, 0.63f, -0.15f),
                  Vec3 (-0.25f, 0.63f, -0.15f) } },
      { .name = "the jet ski",
        .take = "Ride the jet ski",
        .at_hand = "Jet ski within reach",
        .leave = "Get off",
        .medium = Medium::water,
        // From its saddle; it too may lie a step out.
        .reach_from = Vec3 (0.0f, 0.0f, 0.1f),
        .reach = 3.6f * u::m,
        // Close and low behind it, as the thing is quick and small.
        .view = { .eye = Vec3 (0.0f, 0.72f, 0.12f),
                  .pivot_height = 1.0f * u::m,
                  .distance = 6.0f * u::m,
                  .nearest = 2.8f * u::m,
                  .elevation = 15.0f * u::deg },
        .mark_height = 1.6f * u::m,
        .driver_mark = 1.3f * u::m,
        .bumpers = jet_ski_bumpers,
        .centre = Vec3 (0.0f, 0.24f, -0.12f),
        // The eye in the stem, or the grab handle behind the saddle.
        .front_tie = Vec3 (0.0f, 0.36f, 1.86f),
        .back_tie = Vec3 (0.0f, 0.52f, -0.94f),
        .ring = Vec3 (0.0f, 2.3f, -0.1f),
        // The eye in the stem and the grab handle's feet.
        .legs = { Vec3 (0.0f, 0.36f, 1.86f),
                  Vec3 (0.0f, 0.36f, 1.86f),
                  Vec3 (0.13f, 0.41f, -0.885f),
                  Vec3 (-0.13f, 0.41f, -0.885f) } },
    };
    return kinds[static_cast<int> (machine)];
  }
}
