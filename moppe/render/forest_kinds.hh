#ifndef MOPPE_RENDER_FOREST_KINDS_HH
#define MOPPE_RENDER_FOREST_KINDS_HH

// The kinds of tree and the ages of a stand: one universal, planted by the
// forest plan (game/forest_plan.hh) and drawn by the renderer, with
// nothing else to include so the plan need not see the renderer.

#include <cstdint>

namespace moppe::render {
  // Birch, spruce, the Scots pine of the heaths and bogs, the fell birch
  // of the treeline, low and many-stemmed, and the snag: a tree a fire
  // killed, standing grey and bare until it falls. The renderer gives a
  // pine the broadleaf's clump topology, shaped flat and high on a clear
  // trunk; Conifer draws as stacked cones, and FellBirch as clumps on
  // stems springing from one low base, and a Snag as the broadleaf's
  // trunk and boughs with no leaves on them.
  enum class ForestSpecies : std::uint8_t {
    Broadleaf,
    Conifer,
    Pine,
    FellBirch,
    Snag
  };
  enum class ForestAge : std::uint8_t { Sapling, Young, Mature, Ancient };
}

#endif
