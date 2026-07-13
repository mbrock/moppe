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
