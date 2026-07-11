# The structure of space

*Notes toward a cellular tissue for places, construction, and memory.*

## The proposition

Moppe's landscape is currently represented with great success as fields:
height at a position, water over rock, slope, drainage, material, and the
successive results of terrain transformations.  This is the right language
for geology at landscape scale.  It is not by itself the right language for
everything that may later inhabit the land.

Paths, crossings, rooms, courtyards, bridges, property, construction stages,
names, and remembered events want discrete identity.  They want adjacency,
containment, boundaries, and persistence.  They want to say *this place*,
*this edge*, and *these neighbors*, even while their physical realization
remains smooth and irregular.

The proposed middle layer is a **draped cellular tissue**: a mostly
quadrilateral, locally rhythmic, irregular two-dimensional cell complex
embedded in the smooth terrain.  It exists lightly across the world and
grows sparse three-dimensional cellular structure where construction takes
hold.

The terrain remains continuous where continuity matters.  The tissue becomes
discrete where composition matters.

This is not the claim that space is ultimately made of cells.  The tissue is
a provisional interpretation of differentiated space and a constructive
measure laid into it.  Its concise constitution is:

> Space is not made of cells.  The cellular tissue gives making a meter.

## Two views of space

Ordinary game geometry begins with a neutral carrier:

```text
world space = coordinates
object      = transform + geometry
```

Every empty location is equivalent until a field or object assigns it a
property.  This abstraction is indispensable.  Rendering, physics, terrain
evaluation, distance, erosion, and vehicle trajectories all require a stable
metric space.  Moppe's periodic heightfield and its universal cover are exact
and useful laws of the world.

They are not an exhaustive account of inhabited space.

An articulated space also contains:

- regions with different intensities of coherence;
- relations of approach, enclosure, support, and visibility;
- thresholds, bottlenecks, crossings, and seams;
- larger and smaller centers which overlap and contain one another;
- several geometries of movement for several kinds of body;
- histories through which use and construction acquire meaning.

A saddle is not merely a low point on a ridge.  It joins two valleys and
separates two summits.  A headland helps enclose a harbor.  A crest and a
descending face can form one flight.  A quiet opening among several paths
can become a place to wait.  These structures are partly metric, but they are
not captured by coordinates alone.

Moppe can therefore retain two simultaneous truths:

```text
carrier space
  continuous coordinates, terrain, water, distance, velocity

articulated space
  centers, cells, boundaries, support, approach, use, history
```

The cellular tissue mediates between them.  It gives articulated space
enough discrete form to be inspected, remembered, and changed while staying
embedded in continuous land.

## Centers before objects

In an object-first world, a bridge object is placed and connectivity is
derived from it.  In an Alexandrian account, the crossing may be present as a
latent center before a bridge exists.

Two routes approach opposite banks.  The river narrows.  Foundations are
stable.  People can see the far side.  Traffic repeatedly converges and
perhaps already uses a ford.  Several spatial structures support the same
relation:

```text
place A <- latent crossing -> place B
```

The bridge does not originate this linkage.  It recognizes, strengthens, and
materializes it.  Once built, it differentiates the crossing into further
centers: abutments, span, midpoint, space beneath, framed river view,
approaches, waiting places, and bridgeheads.  An inn, shrine, market, or town
may later intensify the same center at other scales.

The same account applies elsewhere:

- a path makes an already viable line durable;
- a temple gives material form to a place of attention;
- a monument fixes an event into public memory;
- a harbor articulates the seam between land and water movement;
- a town condenses where several movement systems repeatedly meet;
- a jump develops a latent relation among approach, takeoff, flight,
  landing, and continuation.

Buildings should not create places from nothing.  They should give material
form to spatial structures which have begun to exist.

## What cells mean

The tissue is not merely an efficient construction grid.  Its elements are
hypotheses about the current structure of space.

A face says:

> For now, this region behaves coherently enough to be treated as one place.

An edge says:

> Something changes, separates, joins, or passes here.

A vertex says:

> Several spatial relations meet here.

A subdivision says:

> This place has acquired enough internal structure to become several more
> definite places.

A group of cells says:

> These regions participate in a larger center.

A vertical sprout says:

> This center has become materially articulated and inhabitable.

Cells need not be visible.  They are stable addresses for relationship and
history.  A road may flow smoothly through a sequence of cells.  A forest
may use cells only for ecological state while individual trees remain
continuously placed.  A river may cross cell boundaries according to its own
heightfield law.  The tissue is a shared substrate, not a universal visual
style.

## Two complementary algebras

The terrain system is a field algebra:

```text
position -> value
```

It is good at height, uplift, moisture, temperature, material suitability,
continuous masks, and gradients.

The tissue suggests a place algebra:

```text
cell + neighbors + history -> structured possibility
```

It is good at occupancy, adjacency, routes, districts, ownership,
construction, typed relationships, and persistent events.

Fields inform cells:

```text
cell slope        <- sample the slope field
cell moisture     <- integrate the moisture field
cell material     <- classify geological fields
cell buildability <- interpret several local readings
```

Cells can later propose explicit transforms back into fields:

```text
road cells        -> cut-and-fill corridor
canal edges       -> incision and water routing
foundation cells  -> local grading and retaining
garden cells      -> soil and vegetation change
```

Neither representation should swallow the other.  A reading remains a
reading; a change to terrain remains an explicit transform.

## Why rough quadrilaterals

Perfect square grids make composition tractable but impose a global axis and
visible repetition.  Regular hexagons distribute neighbors evenly but tend
to produce sixty-degree path habits and awkward building footprints.
Arbitrary polygons adapt freely but make every conjunction special.

Roughly rectangular cells occupy a fruitful middle.

Rooms, walls, roofs, and neighboring buildings benefit from approximate
right angles.  They fit, furnish, subdivide, and extend without leaving thin
wedge-shaped remainders.  Yet exact orthogonality is unnecessary and often
hostile to land, use, and existing centers.  A useful tissue would prefer:

- mostly four-sided cells;
- angles broadly near right angles, not exactly ninety degrees;
- moderate local aspect ratios;
- variable scale;
- no thin slivers or unusable exterior remnants;
- no globally privileged north-south axis;
- boundaries that may follow landforms and existing centers;
- occasional triangles, pentagons, or junction cells where the whole
  genuinely calls for them.

The irregularity is not noise applied to a grid.  It is the accommodation by
which the measure belongs to its site.

## Meter, not ontology

The constructive value of the tissue is analogous to rhythm in typography
and music.

A typographic baseline grid does not claim that language consists of
horizontal lines.  It lets headings, paragraphs, captions, lists, and images
participate in one vertical rhythm.  Musical meter does not require a strict
metronome.  It creates shared temporal expectations within which phrases can
stretch, accents can move, and syncopation can become meaningful.

Minecraft succeeds in part because every act inherits a spatial beat.  One
block, two blocks, and three blocks immediately become comprehensible
measures.  Openings align.  Repetitions can be counted by eye.  Several
people can extend, repair, or vary one another's work without manipulating
splines, control points, or specialist modeling tools.  The result may be
cubic, but the act of composition is unusually tractable.

Terminal interfaces and monospace technical documents gain a related
integrity from shared cells, baselines, columns, indentation, and a small
vocabulary of separators.  Ordinary HTML supplies much more continuous
freedom and therefore no automatic rhythm; good web design must reconstruct
a spacing scale, type scale, baseline, columns, and component proportions.

Moppe can retain the compositional help without retaining literal cubes.
The player can perform cell-like acts while contextual rules deform and
articulate them into irregular geometry.

The lattice makes alignment, repetition, and cooperation easy.  Its
exceptions then acquire meaning.  A larger central bridge arch matters
because the other bays establish a rhythm.  A ceremonial approach matters
because ordinary streets follow the land.  A tower matters because normal
buildings share a comprehensible height scale.

Without expectation, deviation is merely noise.  With expectation,
roughness becomes life.

## A fluid local tempo

The tissue should not repeat one module everywhere.  It is better understood
as a spatial tempo map with a local scale, direction, and degree of
regularity.

Conceptually, continuous fields might guide it:

```text
target cell size      s(x)
preferred direction   theta(x)
directional stretch   A(x)
desired detail        d(x)
```

On a broad plain the rhythm may be slow and calm.  Along a valley, cells may
stretch with the land.  Around a shore or junction, the rhythm may tighten.
At a settlement it becomes finer and more articulated.  Around a temple it
may acquire local symmetry and ceremonial measure.

This rhythm should be hierarchical:

```text
small unit       stone, timber bay, step, opening
room unit        inhabitable cell
building unit    group of rooms and courts
street unit      facades, crossings, public space
district unit    routes and major centers
```

These are spatial counterparts to subdivisions, beats, bars, and phrases.
Their ratios need not be exact, but they should remain perceptibly related.

Different regions can develop different meters according to material,
terrain, climate, craft, and history.  This is vernacular as an inherited
constructive rhythm rather than a catalogue of visual styles.

## An induced and adaptive tissue

The tissue should not be a neutral overlay generated once and mistaken for
the world.  It may begin from a coarse periodic seed mesh because computation
must begin somewhere, but its meaningful form should be induced by what the
world contains.

Relaxation and later adaptation can respond to:

- ridges and watershed divides;
- channels, shores, and flood surfaces;
- benches, saddles, and stable construction ground;
- movement corridors and repeated traces;
- existing centers and construction;
- the boundaries of positive outdoor spaces;
- local demand for finer articulation.

Edges may migrate toward meaningful boundaries.  Important crossings may
become vertices or short edge chains.  Cells may subdivide where a place
becomes important and remain coarse where the land is quiet.

This makes remeshing a semantic operation.  When one cell becomes several,
names, traffic, events, ecology, ownership, and center relationships must be
transferred deliberately.  It should feel like one place becoming several
more definite places, not like data being regenerated.

The initial mesh is scaffolding.  Adaptation gives it meaning.

## Sparse vertical growth

A full voxel world would make construction simple but would fight the smooth
landscape, burden the terrain scale with empty air cells, and make the visual
language unnecessarily cubic.

Instead, the two-dimensional tissue exists everywhere and three-dimensional
cellular structure sprouts only where required.

A surface cell can acquire a vertical stack:

```text
surface cell
  -> foundation
  -> occupied floor cells
  -> walls and openings
  -> roof cells
  -> attachments and ornament
```

A group of surface cells can seed a building complex containing rooms,
courtyards, arcades, stairs, towers, and roofs.  A bridge anchors to cells on
both banks and grows an elevated chain between them.  A retaining wall
articulates an edge between differently fitted surface cells.

Uninhabited country remains a light surface complex.  Occupation causes the
world to differentiate vertically.

## Deformed modules

The interaction can remain block-like while the result remains irregular.
The player or simulation makes simple gestures:

- select this cell;
- continue from this edge;
- raise this group one level;
- enclose these cells;
- open this wall;
- support this span;
- strengthen this boundary.

The construction system maps a curated vocabulary of components into the
irregular cells.  At its simplest, a unit-square module can be mapped into a
convex quadrilateral by bilinear interpolation among its corners.  More
careful component rules preserve what should not deform: straight timber,
wall thickness, column section, roof pitch, arch thrust, and material size.

Different materials absorb irregularity differently.  Rough stone tolerates
shape variation.  Timber imposes straight members and repeated bays.  Brick
prefers another module.  Trim, infill, and craft resolve small mismatches.
These constraints create vernacular character from construction rather than
from decorative skin.

Townscaper demonstrates the humane division of labor: the person controls
mass, void, adjacency, height, and color; contextual rules articulate roofs,
arches, stairs, supports, gardens, and small life.  Moppe's version must also
listen to terrain, water, movement, material, and history.

The player indicates and judges.  The system fits and differentiates.

## A bridge as the complete example

A bridge exercises nearly every part of the proposal.

1. Routes and mover geometries reveal demand for a crossing.
2. Hydrology supplies water depth, flood behavior, and channel structure.
3. Banks and geology supply candidate abutments and foundations.
4. Surface cells give stable identities to the approaches and crossing.
5. A smooth macro curve establishes alignment and elevation.
6. The curve is divided into structural bays according to the local meter.
7. Bays become deformed construction cells and select vernacular modules.
8. Piers, arches, beams, rails, stairs, and abutments adapt to local facts.
9. Terrain transforms fit the approaches while preserving the surrounding
   drainage and landform.
10. Use, repair, flood, and later additions continue the bridge's history.

The scales divide responsibility cleanly:

```text
spline      says where the bridge goes
cells       say how the span is composed
modules     articulate local relationships
history     says what the bridge becomes
```

The bridge may begin as stepping stones, a ferry, or a timber span.  Later
stonework can retain the old ford, repaired footings, flood marks, a shrine
to safe passage, or a desire path beneath an arch.  Construction becomes
geological in its own way: buildings are strata.

## Positive space and settlement

Object placement optimizes buildings and inherits whatever space remains
between them.  A cellular construction language can shape occupied and
unoccupied space together.

- a loop of built cells creates a courtyard;
- a widened route creates a square;
- two offset buildings make a gateway;
- an arcade mediates between interior cells and a public route;
- a bridgehead leaves a place to wait;
- a temple enclosure creates a calm void;
- a row of houses strengthens the street they face.

The empty cell is not missing content.  It may be the strongest center in the
composition.

Towns should likewise precipitate rather than spawn.  A ford becomes a
bridge; the crossing acquires a keeper, shelter, stable, workshop, market,
houses, shrine, and secondary paths.  The main street remembers the trail.
The square remembers the widened junction.  The temple addresses the center
which caused the settlement to exist.

The cell tissue gives this incremental history stable units without forcing
the final town onto a perfect grid.

## Several effective geometries

The continuous carrier supports more than one articulated space.

For a pedestrian, a shallow ford may join two banks.  For a cart they remain
far apart.  For a boat the river is a route, not a barrier.  For the
motorcycle the same gap may be a jump.  Visual space, drainage space, and
ritual space have still other adjacencies.

```text
walking space
cart space
water space
motorcycle space
visual space
drainage space
```

Infrastructure is powerful because it changes several of these spaces at
once.  A bridge shortens terrestrial routes, affects water, frames a view,
creates shelter, and may become a stunt line.  A strong center often
condenses several geometries into one place.

The tissue should therefore preserve typed relationships rather than reduce
every adjacency immediately to one universal distance.

## A world which remembers

Cells and edges provide stable addresses for histories which fields alone do
not naturally hold:

- passages in each direction;
- braking, wheelspin, takeoffs, and landings;
- dwell time and repeated stopping;
- construction, repair, damage, and abandonment;
- flooding, erosion, and vegetation succession;
- names, ownership, stewardship, and events;
- membership in overlapping centers.

A desire path can emerge as a flow across edges while its visible trace stays
smooth.  When the flow stabilizes, the world recognizes a route corridor.
When several routes meet repeatedly, their shared cells can become a center.
Construction then has somewhere meaningful to take hold.

The tissue is interpretive, constructive, and historical at once.

## Interaction as soft measure

The player need not see or obey a hard grid.

- a wall gently aligns with nearby edges;
- a bridge prefers comprehensible bay spacing;
- a room settles toward a good rough rectangle;
- a path width tends toward the local module;
- courtyard boundaries negotiate with their neighbors;
- a deliberate gesture can break the suggestion when the exception matters.

This is soft spatial quantization.  The player supplies gesture, the local
meter supplies measure, and the existing whole supplies correction.

The tissue can appear when useful as a planning overlay, a temporary
construction scaffold, a land-use reading, or a visualization of centers.
In ordinary play it should usually disappear into the world it helped make.

## The torus

The base tissue must be as honest about topology as the terrain.  Opposite
boundaries of the fundamental square are the same neighborhood.  Initial
generation, relaxation, adjacency, pathfinding, centers, and later remeshing
must all respect that identification.

A periodic irregular tissue would remove the last temptation to treat the
world edge as an exceptional construction boundary.  Roads, districts, and
settlements can cross the seam because the cells themselves do.

The flat torus remains the metric law.  The articulated tissue grows within
it and may acquire winding centers and routes of its own.

## Possible values

Names and boundaries remain speculative, but the eventual code might need
plain values resembling:

```text
SurfaceTissue
  vertices, edges, cells, topology, embedding

SurfaceCell
  terrain reading, ecology, traffic, construction, history

SurfaceEdge
  boundary kind, route flux, intercepted water, crossing

SpatialCenter
  weighted region, contained centers, typed supports

ConstructionComplex
  foundations, levels, walls, openings, roofs, attachments
```

These should not be forced into `TerrainProgram`.  The terrain program says
how rock and water were produced.  The tissue interprets a materialized world
and carries later inhabitation.  Explicit transforms mediate whenever
construction changes terrain.

As always, values should be serializable, diffable, inspectable, and subject
to deterministic replay within their declared evaluator.

## First proofs

The first implementation should prove the representation before attempting a
town generator.

1. Generate one deterministic periodic irregular-quad tissue over a finished
   terrain.
2. Drape it onto the heightfield and display it faintly in Terrain Lab.
3. Compare a flat topological view with its sloping world embedding.
4. Let cell scale and orientation respond mildly to shores, drainage, ridges,
   and slope.
5. Select cells and inspect their terrain readings and neighbors.
6. Express a smooth route corridor through a cell sequence without making
   the route look cellular.
7. Represent one lake, saddle, and approach as overlapping supported centers.
8. Sprout a few simple plaster or clay masses from chosen cells.
9. Generate one terrain-fitted bridge or shrine from a blessed center.
10. Ride the result and judge whether the tissue helped it belong.

Before procedural architecture, the first visual proof is simply a beautiful
mesh: calm on broad slopes, more articulate around water and crossings,
roughly rectangular without a global axis, and continuous across the torus.
If a few extruded cells already appear to belong to the landscape, the
representation is alive enough to continue.

## Guardrails

- The tissue is not the ontology of space; it is a changing interpretation.
- Cells are semantic addresses, not compulsory visible tiles.
- Metric physics and smooth terrain remain authoritative where appropriate.
- Adaptation should follow centers, not add decorative irregularity.
- Calm regularity is necessary for meaningful exceptions.
- Remeshing must preserve identity and history explicitly.
- Readings precede mutations; construction edits terrain only through named
  transforms.
- A global score never replaces pairwise judgment in the actual place.
- New architecture should strengthen existing centers and help form larger
  wholes.
- The system must leave room for unbuilt land and inner calm.

## The larger picture

The proposal fills a missing middle in Moppe's language:

```text
fields make land
cells make places actionable
construction makes centers visible
use and memory make them irreplaceable
```

The smooth heightfield is the world's continuous body.  The cellular tissue
is the locally adapted rhythm through which the world recognizes places,
coordinates acts separated by people and time, and learns how to build.

The bridge is not an asset placed into empty coordinates.  It is space
becoming conscious of its own crossing.

## Further reading and play

- Christopher Alexander, *The Nature of Order*, especially Books 1 and 2 on
  centers, wholeness, and structure-preserving transformations.
- Christopher Alexander,
  [Harmony-Seeking Computations](https://www.cs.york.ac.uk/nature/workshop/papers/Harmony-Seeking_Computation.pdf),
  for value-aware transformation of an existing whole.
- Bin Jiang,
  [Geography Founded on the Third View of Space](https://arxiv.org/abs/2108.02493),
  for the contrast among absolute, relational, and organismic space.
- Oskar Stalberg,
  [Organic Towns from Square Tiles](https://compaec.github.io/news/2020/06/29/StalbergTalk.html),
  and Tommy Thompson,
  [How Townscaper Works](https://www.gamedeveloper.com/game-platforms/how-townscaper-works-a-story-four-games-in-the-making).
- Boris the Brave,
  [Townscaper Grid](https://www.boristhebrave.com/docs/sylves/1/articles/tutorials/townscaper.html)
  and [Editable WFC](https://www.boristhebrave.com/2022/04/25/editable-wfc/),
  for practical irregular grids and local contextual regeneration.
