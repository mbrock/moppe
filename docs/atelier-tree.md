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

## Relationship to the production forest

The intrinsic organism remains in `atelier/tree.*`: it is an inspectable model
of topology, transport, growth, and thickening, and a plausible source of
future simulation ideas. It is deliberately not a game presentation. The old
`TreeStand` embedding and its special home-base grove were removed because
they created a second, unrelated tree population and baked every branch and
leaf cluster into one retained mesh.

The forest system adds two population scales between habitat and geometry.
First, broad and local periodic noise turns potential habitat into a global
`forest_cover` mosaic. Trails and the home-base footprint clear that cover.
A jittered lattice then converts cover into tens of thousands of stable,
seed-addressed sites across the whole periodic world. Dense cover produces a
stand rather than a uniform scatter; low cover becomes an ecotone or opening.

Each stable site now also owns an age class. Sapling, young, mature, and ancient
cohorts select distinct typed size ranges before the renderer is involved.
`ForestLandscape` converts the finished plan once into `ForestInstance`
records whose position, height, crown radius, ground normal, canopy cover, and
moisture retain their domain or quantity types at the API boundary.

## Assemblies rather than complete meshes

The Metal backend narrows each individual to one aligned 64-byte GPU record.
`forest.metal` then uses an object shader to frustum-cull it and measure its
actual projected height. Subpixel individuals disappear into the terrain's
filtered canopy signal; distant trees schedule one crown proxy, middle-distance
trees schedule three organs, and near trees schedule a trunk plus seven crown
organs. Those organs are reusable algorithms -- conifer bough tiers and
broadleaf lobes -- rather than copied vertex buffers. The mesh shader expands
only the selected organs, so no complete production tree mesh exists in CPU or
GPU memory.

The crown geometry is opaque and three-dimensional. It does not use the old
fractal alpha cards, so receding needles cannot turn into jagged black cutouts.
A dedicated forest material keeps albedo separate from illumination, combines
sky and ground hemisphere fill with wrapped direct light, and adds thin-leaf
sun transmission. Moisture, cover, species, age, and stable individual hashes
vary height, crown proportions, colour, and assembly placement without making
the forest scintillate.

The same individuals participate in the world's one-time shadow pass. Shadow
submission expands one coarse opaque crown per periodic tree image into the
existing 4096-square terrain shadow map. Scene detail therefore stays
view-dependent while the ground still receives forest-scale occlusion.

## The floor: geometry that is never stored

`moppe/shaders/metal/undergrowth.metal` grows the ground flora — grass,
fern rosettes, and flowering drifts — and nothing about it is a stored
mesh. An object stage walks a window of ground tiles around the camera and
keeps the ones the world's own fields say something grows on: light and
water in the soil, no trail worn across it, ground roots can hold. Each
sub-metre tile grows up to 32 independently rooted shoots. Four
cross-sections give each shoot a curved silhouette, while a shared
C++/Metal meshlet contract proves at compile time that the tile remains
within Metal's output limits. A hash decides where each shoot stands and
what it is, the height and normal textures root it on the terrain by
construction, and the same gust function the trees use moves it. Each
generated root samples the trail field again, so grass grows down at a
worn edge instead of exposing the coarser tile that proposed it.

A family is a way of spending one shoot's fixed vertex allowance, and the
families partition habitat rather than compete for it. Grass is the
substrate everywhere, and a closed canopy starves it hard, so the forest
floor is sparse short blades over dark litter rather than a shaded meadow.
A fern is one frond of a rosette: a coarse damp-shade lattice proposes
crown points in stands, every shoot rooted near a crown re-roots beside it
and radiates outward, and the same rosette therefore assembles from the
same fronds no matter where the camera window falls. A flower is a stem
whose last two cross-sections become a petal head, a disc tilted between
sky and camera. Flowers arrive in single-species drifts owned by a warped
world lattice (`grass_medium.h`), the drift's colour also washes the
terrain substrate beneath it, and a head whose pixels run out collapses
its chroma toward that wash — so a receding drift dissolves into a
hillside that still reads as flowering, and nothing scintillates.

That is the shape the vegetation shelf's `ideas/geometry-from-fields.md`
proposes, and its payoff is not only that the plants cost no memory. They
cannot drift out of step with the ground they grow on, because they are read
from it rather than placed against it; and their count, size, and species can
change every frame, because nothing is kept that could go stale.

The distance level of detail is that freedom used directly, as a ladder
each family descends on its own schedule. Geometry owns only features wide
enough to remain repeatable image features, and each family measures its
OWN signature feature: the blade its 1.8 cm width, the flower its head
diameter, the fern its frond width. The object stage prices a tile at the
largest of its families' budgets and the mesh stage re-prices each shoot
against its family's own resolved fraction, so the meadow does not lose
its flowers at the distance it loses its blades, and one family's
retirement never culls another's. Each world tile owns a stable phase for
spending its fractional budget, and a plant grows through a short
transition at its threshold. The ladder's middle rung collapses
sub-resolvable detail to ensemble means — flutter fades, the glint streak
widens, a flower head widens to the smallest footprint a jittered sample
can revisit while dimming in proportion and collapsing its chroma to the
drift's wash. The last rung is the terrain substrate itself, which
carries the grass cover colour and the drift's part-desaturated wash
through the same `grass_medium.h` fields — the retiring feature's final
colour and the substrate's are one number, so the hand-off has no seam. A
sward therefore thins plant by plant into the terrain's filtered grass
material without either a contour line or a camera-window boundary
reshuffling the floor, and the camera window itself is only a cost bound,
sized for the farthest-resolving family rather than acting as the LOD.

The dense field also has two temporal rules. Fine flutter fades before an
individual blade becomes subpixel, leaving the slower coherent gust instead
of distant glitter; and high-frequency blade-to-blade colour differences stay
subordinate to the continuous moisture and canopy fields. In gameplay the
current bike, car, or walker contributes one small interaction footprint.
Roots remain fixed, but upper sections lean out and lie down as the mover
passes, so the field participates in motion without acquiring a retained
plant simulation.

In a 1280x800 high-quality riding feature cube, the denser grass pass adds a
median 0.75 ms of command-buffer time (0.84 ms mean). The fully enabled
configuration remains below 10 ms at the median. That is a deliberate visual
cost for about 3.2 times the prior blade density, but it leaves ample room in a
60 Hz frame. It needs Metal mesh shaders; backends without them grow nothing,
and the `undergrowth` graphics feature turns it off.

The landscape gazetteer and ordinary deterministic screenshot paths exercise
the real forest renderer; there is no special hero-tree or observatory path.
