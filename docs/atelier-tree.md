# The Atelier tree

The Atelier tree is a small proof that an organism can remain itself while
its presentation changes completely. Run it as a wind-bent object:

```sh
cmake --build build --target atelier
./build/atelier.app/Contents/MacOS/atelier --tree
```

or as a diagram of the same organism:

```sh
./build/atelier.app/Contents/MacOS/atelier --tree-diagram
```

Deterministic stills can be made without opening a window:

```sh
./build/atelier.app/Contents/MacOS/atelier \
  --tree --capture /tmp/tree.png 7
./build/atelier.app/Contents/MacOS/atelier \
  --tree-diagram --capture /tmp/tree-diagram.png 7
```

## Three storeys

`DirectedTreeTopology` is the combinatorial storey. It owns vertices, edges,
incidence, generation, lineage, branch order, and the distinction between the
shoot and root trees. It has no positions.

`Tree::VertexState`, `Tree::EdgeState`, and `TreeEdgeForm` are the intrinsic
storey. They are typed `Bundle`s over the vertex and edge domains. Rest length,
radius, flexibility, azimuth, elevation, water potential, sugar potential, and
bud vigor all belong here. Radius is derived from the terminal mass supported
by an edge, so thickening is a property of the organism rather than of its
mesh.

`embed_tree` is the extrinsic storey. The diagram embedding gives each half of
the organism a centered leaf order and places generations at regular vertical
intervals. The living embedding integrates the same rest directions and
lengths under wind and gravity. Neither embedding is allowed to change the
topology or intrinsic bundles.

## Two trees and two flows

The crown and roots are two directed trees glued at the collar. Their edges
share one representation and one thickening law. Root endpoints are rendered
in ochre and shoot endpoints in green in the diagram, but that color is a
presentation choice rather than a second model.

Xylem and phloem are signed cochains on every edge. Their signs oppose one
another, and both reverse across the collar. The renderer carries the two
values in diagnostic lanes on each ligament; the shader turns them into subtle
blue and amber pulses whose travel direction follows the sign. There is no
root-specific animation path.

`accumulate_along_tree` is the important reusable mechanism. Folding terminal
load against the arrows gives the mass supported by every branch and therefore
its radius. Calling the same operation with the orientation reversed is the
shape needed for contributing drainage area and channel width. The current
Atelier tree is one client and its tests exercise both directions. Moving the
terrain drainage client onto the same directed-tree vocabulary is deliberately
left as the next proof of the abstraction, not claimed here prematurely.

## Plants are not one storage class

This model is intentionally for plants whose identity and history matter. A
tree is a finite complex with lineage and inspectable form. Grass still wants
to be a density or population field responding to moisture, disturbance, and
light. Rendering both as collections of individually generated meshes would
erase the useful distinction before simulation even begins.

## The production embedding

The intrinsic organism is built as the `atelier_botany` library. Both the
Atelier and Moppe link that library, so changing topology, transport, growth,
or thickening changes the organism in both clients. The Atelier remains the
place for inspecting the complex in isolation; Moppe supplies a second
extrinsic embedding in `moppe/game/tree_stand.*`.

The game first derives `tree_habitat` as another quantity in the surface
bundle. Moisture, height above standing water, tree line, and surface normal
all contribute. A deterministic site planner samples that field, preserves a
minimum spacing between organisms, and roots every chosen collar at the exact
surface elevation and normal. The seed of each site produces a related but
distinct topology and rest configuration.

The first forest prototype adds a population process between habitat and
geometry. One or two suitable sites become recruitment centers; seeds fall in
clusters around them; a mixture of canopy trees, young trees, and saplings is
proposed; and larger crowns self-thin overlapping competitors. This follows the
important shape of Deussen et al.'s ecosystem model without pretending that a
single startup pass is a complete succession simulation. In ordinary play the
same process grows a forest near the arrival area. The observatory mode frames
that population for deterministic inspection.

All branches and leaf clusters in a stand are baked into one retained
world-space mesh. Branch generation and intrinsic flexibility become
per-vertex wind weights, which the existing Moppe scene shader animates. This
keeps the prototype to one draw without adding a new renderer abstraction.
Instancing or a mesh-shader expansion path becomes worthwhile when trees
graduate from dozens of organisms near play into thousands across the world.

Run a quiet camera in the game renderer with:

```sh
./build/moppe.app/Contents/MacOS/moppe --tree-demo --tree-count 9
```

Deterministic terrain-rooted screenshots use the same mode:

```sh
make tree-shot
tools/capture-trees /tmp/tree.png 1
tools/capture-trees /tmp/grove.png 9
MOPPE_SEED=777 tools/capture-trees /tmp/other-grove.png 9
```

The portrait and grove are deliberately the same rendering path. The count
only changes site planning and camera composition; there is no special hero
tree asset.
