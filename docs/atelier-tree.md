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

The intrinsic organism lives in `atelier/tree.*`. Both the Atelier and Moppe
compile that source, so changing topology, transport, growth, or thickening
changes the organism in both clients. The Atelier remains the place for
inspecting the complex in isolation; Moppe supplies a second extrinsic
embedding in `moppe/game/tree_stand.*`.

The game first derives `tree_habitat` as another quantity in the surface
bundle. Moisture, height above standing water, tree line, and surface normal
all contribute. A deterministic site planner samples that field, preserves a
minimum spacing between organisms, and roots every chosen collar at the exact
surface elevation and normal. The seed of each site produces a related but
distinct topology and rest configuration.

The forest system adds two population scales between habitat and geometry.
First, broad and local periodic noise turns potential habitat into a global
`forest_cover` mosaic. Trails and the home-base footprint clear that cover.
A jittered lattice then converts cover into tens of thousands of stable,
seed-addressed sites across the whole periodic world. Dense cover produces a
stand rather than a uniform scatter; low cover becomes an ecotone or opening.

Second, one or two suitable sites near the arrival area become detailed
recruitment centers. Seeds fall in clusters around them; a mixture of canopy
trees, young trees, and saplings is proposed; and larger crowns self-thin
overlapping competitors. This follows the important shape of Deussen et al.'s
ecosystem model without pretending that a single startup pass is a complete
succession simulation. The observatory mode frames that detailed population
for deterministic inspection.

All branches and leaf clusters in the detailed stand are baked into one
retained world-space mesh. Branch generation and intrinsic flexibility become
per-vertex wind weights, which the existing Moppe scene shader animates.

## What a crown says beyond where it is

`moppe/game/foliage.*` is the vocabulary every plant in the world is drawn
with, and it exists because a cheap crown fails in two ways that have nothing
to do with its triangle count. It lights by facet, so a five-sided drum reads
as folded paper; and it carries one flat colour, so a hillside reads as one
painted green. Both are decided when the mesh is baked and cost nothing to
draw.

So a plant vertex carries the outward direction of the volume it belongs to
rather than of its triangle, and how much sky its part of the canopy sees.
Species, ground moisture, canopy cover, and plain individual variation pick
the two ends of the colour ramp that exposure runs along; bark is the same
mechanism with a wood ramp, dark at the root and lit where the stem rises
clear. Twenty triangles shaded this way read as a mass of leaves, which is
the whole reason the global population can stay cheap.

## Three distances

`ForestLandscape` presents the population at three scales, and the interesting
one is what it refuses to draw.

Within a couple of hundred metres a tree is an organism: a stem that flares
into its own root plate and tapers and bends, limbs leaving it at heights you
can see, and a crown of two or three separate masses with sky between them.
Beyond that only volume and colour still carry, and a tree becomes a handful
of triangles. Beyond about a kilometre it is smaller than the pixel it lands
in, and the terrain's own filtered canopy is the more honest representation:
drawing individual trees there produced a horizon-wide band of dark specks on
haze-whitened ground, which reads as dirt on the lens rather than as forest.
The scene shader converges foliage albedo on that canopy tone as distance
grows, so the handover is a change of texture and not of colour.

Only the near representation is expensive, and at any moment about a dozen
chunks of the world's lattice are close enough to want it. So the cheap mesh
is built once for the whole world and kept, and the near mesh is built when a
chunk comes within reach and released when it leaves — a bounded number per
frame, so arriving somewhere costs a few frames of coarser trees rather than
one long stall. Baking the near mesh for every chunk of the world instead cost
287 MB that was almost entirely idle; the residency scheme peaks around 156 MB
in play while giving the near tree roughly three times the geometry.

Streamed full organisms and mesh-shader expansion remain later refinements
rather than prerequisites for a forested world.

## The floor: geometry that is never stored

`moppe/shaders/metal/undergrowth.metal` grows mostly grass, with occasional
ferns in damp shade, and nothing about it is a stored mesh. An object stage
walks a window of ground tiles around the camera and keeps the ones the
world's own fields say something grows on: light and water in the soil, no
trail worn across it, ground roots can hold. A mesh stage turns each surviving
tile into plants — a hash decides where each one stands and what it is, the
height and normal textures root it on the terrain by construction, and the
same gust function the trees use moves it. Each generated root samples the
trail field again, so grass grows down at a worn edge instead of exposing the
coarser tile that proposed it.

That is the shape the vegetation shelf's `ideas/geometry-from-fields.md`
proposes, and its payoff is not only that the plants cost no memory. They
cannot drift out of step with the ground they grow on, because they are read
from it rather than placed against it; and their count, size, and species can
change every frame, because nothing is kept that could go stale. The distance
level of detail is that freedom used directly: the object stage hands each
tile a smaller plant budget as it recedes, and the mesh stage widens surviving
blades without making them taller, so the floor keeps the projected coverage
the thinned-out plants were carrying. Each world tile owns a stable phase for
spending that fractional budget, and a plant grows through a short transition
at its threshold. A sward therefore thins plant by plant into the terrain's
filtered grass material without either a contour line or a camera-window
boundary reshuffling the floor.

In a 1280x800 high-quality riding feature cube, the denser grass pass adds a
median 0.47 ms of command-buffer time (0.54 ms mean). That is a deliberate
visual cost, but remains small beside the 8.33 ms 120 Hz frame budget. It needs
Metal mesh shaders; backends without them grow nothing, and the `undergrowth`
graphics feature turns it off.

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
