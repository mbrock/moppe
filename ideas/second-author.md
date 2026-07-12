# The Second Author

*Notes toward an inhabited world — a design brief for the layer above geology.*

---

## Where we stand

The world currently has one author. A `GeologicalSource`
expands into a `ScalarField`; the field materializes into a raster; a
sequence of `TerrainTransform`s — normalize, power, droplets, talus —
gives it a history; the whole thing is a `TerrainProgram`, a value you
can hold, edit in the Tycoon window, and replay deterministically on
the current evaluator. Stable serialization and cross-machine golden
terrain are goals, not present contracts. The random world wraps: it
is a flat torus with no edge to apologize for. `DrainageGraph` already
records dry receivers and contributing area; `FloodField` and the lake
census record standing water. Depression-aware routing is still the
first author's active frontier—rock proposed, water disposing, the
score kept in strata.

And the first author's work, when it's done well, is already alive in
a specific, checkable sense: erosion manufactures most of what
Alexander called the fifteen properties. Levels of scale come from the
stream hierarchy, strong centers from peaks and confluences,
boundaries from ridgelines, gradients from the slope-area law, echoes
from dendritic self-similarity, roughness for free. Twelve properties,
maybe thirteen, from nothing but rain and time.

But a raw landscape, however alive, is not *inhabitable*. You can ride
it, the way you can walk into a wilderness, but nothing in it is *for*
anyone. There are no lines to learn, no somewheres to arrive at, no
reason this valley rather than that one. The three properties erosion
cannot supply — alternating repetition, good shape, deliberate calm —
are precisely the human ones: rhythm, silhouette, restraint. The
mountain gives the material; someone's judgment gives the line. This
document is about building the someone.

We are not going to hand-place a village. We are going to build the
**second author**: a system that inhabits the first author's world the
way people inhabited the real one — by noticing good places, walking
between them, wearing paths, improving the paths into roads, and
settling where the ways of moving meet. Every step is a
structure-preserving transformation: it takes centers the geology
already made and makes them more themselves. The second author never
overwrites; it intensifies. That is the constitution, and like the
first author's constitution, it will be enforced by tests.

## The chain of unfolding

The whole layer is one causal chain, and each link is a value in the
terrain language:

**readings → centers → movers → paths → carving → settlements → the player.**

Geology is measured by *readings*; readings elect *centers*; centers
attract *movers*; movers wear *paths*; paths get *carved* into
infrastructure; where incompatible movers meet, *settlements*
precipitate; and the player, last of the movers, keeps writing after
we stop. Each stage consumes the previous stage's values and the
original terrain, and should produce new first-class values—inspectable
in the lab, eventually serializable with the program, judged by the
rider.

## Readings: the world measured

A *reading* derives numbers, rasters, or structured analyses from
terrain without mutating it—we already have two hiding inside
`NormalizeHeights`, plus `DrainageGraph`, `FloodField`, and the lake
census. The inhabitation layer runs on a stack of them. Some share
drainage traversals; others require independent global work:

**Flow accumulation** (how much world drains through here), **slope**
and **curvature** (steep, convex, concave — spurs and couloirs are
just curvature read at fine scale), **prominence** (how much a summit
commands), **viewshed** (what a point sees, sampled), **wetness**
(accumulation over slope — where water lingers), **shelter** (negative
openness — hollows, lee sides), **aspect** (which way a slope faces;
sun is a field too), and eventually **distance-to-water**,
**distance-to-sea**, and per-cell **traffic** once anything moves.
Several of these—especially prominence and viewshed—need algorithms of
their own; they do not fall out cheaply from one drainage traversal.

Some of this reuses existing machinery. Strahler orders, basin
coloring, and the slope–area law can come from related drainage walks;
other readings need their own passes. The rating card and the
center-placement system should nevertheless consume the same reading
values rather than duplicate judgment. Readings are also where the
lab's honesty lives: every one of
them should be a toggleable overlay, because the second author's
judgment must be *visible* to be trusted.

## Centers: the world noticed

A *center* is a place the world was already pointing at. Every settled
culture has employed specialists to find them — the Thai spirit-house
siters, the feng shui readers of dragon-veins, the Roman keepers of
the genius loci — and the embarrassing empirical fact is that their
sites cluster on computable features: confluences, saddles, sheltered
benches above water, prominences that see and are seen. The spirit
vocabulary was an interface to a regression. We have the regression's
inputs as rasters.

So a **center type is a recipe**: a weighted combination of readings,
plus constraints.

A *summit marker* wants prominence and viewshed. A *waterhole* wants
high wetness, low slope. A *campsite* wants flat, sheltered, near
water, above the flood line, morning sun. A *shrine* wants the strange
pair of visibility and threshold — saddles, spurs, the places a path
would naturally pause. A *harbor* wants the seam itself: sheltered
water against usable shore. A *magic mirror* — our one licensed
miracle, the heir of the old edge-of-the-world bounce — wants a high
committing face at the end of a long fast fall-line, because a mirror
is a viewpoint dispenser and the launch should show you the world.

### The shrine above its river

A shrine may want something more particular than a high viewshed. The
absolute summit is often too universal: it sees several valleys and belongs
experientially to none of them. Move a little way down one shoulder of the
ridge and the site acquires an allegiance. Rain falling at the shrine joins
one drainage basin. The ridge behind it hides the neighboring country. The
shrine looks outward over the valley whose water passes below it.

Its hydrological territory and visual territory are not the same object, but
their fit can become a generative rule. For a candidate site `s`, let `B(s)`
be its catchment and let `V(s)` be the cells from which the shrine can be
seen. A valley shrine should command a consequential part of `B(s)` without
simply seeing the whole world. It may reward:

- visibility from the river, paths, settlements, and usable valley floor;
- a viewshed concentrated inside its own catchment;
- local prominence and a clear silhouette against the ridge;
- an unambiguous step onto one side of the watershed divide;
- repeated disappearance and recovery along an upstream approach;
- a shoulder, spur, or near-summit bench on which a person might actually
  stop and build.

It may penalize the exact summit, visual leakage into every neighboring
basin, an inaccessible final slope, and a viewshed so complete that discovery
has no sequence. The shrine should not merely be visible; it should organize
an approach. A rider following the water upstream glimpses it, loses it behind
local relief, finds it again, and gradually understands which side of the
mountain it addresses.

There is a suggestive opposition here. Water follows a local downstream
relation until it reaches an outlet. Attention can follow a visible uphill
or prominence relation until it reaches a shrine. Cells attracted to the
same shrine would form a visual or pilgrimage catchment, coupled to but not
identical with the fluvial catchment. The resemblance is generative rather
than exact: line of sight is symmetric and non-transitive, so its raw relation
is a graph, not automatically a partition. A particular rule for visual
succession, landmark allegiance, or approach would be what turns it into
regions.

Different relationships between drainage and visibility suggest different
center types:

- a **valley shrine** addresses one catchment from just inside its divide;
- a **ridge shrine** deliberately belongs to the threshold between two;
- a **confluence shrine** gathers several tributary valleys within one larger
  basin;
- a **source shrine** sits above the headwaters and looks downstream;
- an **outlet shrine** stands low and looks back into the country that drains
  toward it;
- a **hidden shrine** has strong local presence but almost no distant
  viewshed.

This would join three readings without claiming they are isomorphic: the
drainage partition says where the shrine belongs, the visibility graph says
whom it may address, and the path or pilgrimage relation says how it is
encountered. The resulting placement would be derived from the particular
terrain rather than scattered over it by a themed random-number generator.

The algorithm proposes; the human disposes. Candidate fields render as
overlays in the Tycoon window; the ride-to-judge loop (one keypress
between god view and rider view) is the acceptance test, because a
center that scores well on the heightmap can still feel wrong when you
stand on it. The part of the old geomancers' knowledge that never made
it into any notation is still the largest part, and the design honors
that by keeping a person in the loop exactly where the theory is
weakest. What ships is a `CenterSet`: a small, named, serializable
list of blessed places—the world's proper nouns. `CenterSet` is a
proposed value, not an implemented type.

## Movers: the plurality of physics

There is no such thing as "the difficulty of terrain." There is only
difficulty *under a cost functional*, and every way of moving has its
own. A **mover** is a value describing one:

a **grade cap** (walkers tolerate what carts cannot; mules split the
difference; water tolerates none), a **turn penalty** (a pedestrian
pivots freely; a truck buys each hairpin with engineering — this one
dial spans the whole family from wandering packhorse zigzag to
Trollstigen's eleven named folds), a **surface model** (what counts as
passable ground — for boats the mask inverts entirely: water is the
road and the shore is the wall), a **width**, a **speed curve**, and
optionally a **schedule**, because admissibility can be time-dependent
— the frozen couloir at dawn and the same chute at noon are different
terrain.

Foot, mule, cart, motorcycle, boat: five movers, five geometries, one
pathfinder. The deep payoff of pluralism comes later, at the seams —
but it starts here, in the honest admission that the mountain is a
different place for every body that moves through it. (Ask the
quadriceps.)

## Paths: the world worn

Paths are not designed. They are *selected*. The mechanism is the same
one that carved the valleys, with the potential field swapped: release
**pilgrims** — droplets whose gravity is desire — from spawn regions
toward centers, routed by anisotropic least-cost search under a
mover's functional, and let use be reinforcing: cells that carry a
path get a cost discount, so later journeys opportunistically merge
into earlier ones and branch as late as possible. Run the centers in
order of importance and the network consolidates into trunk and
tributary — the far-more-small-things-than-large-things signature —
without anyone drawing it. This is stigmergy; the ant-colony people
have theorems about it; the improvisers would say the landscape is
reincorporating its travelers.

Two disciplines keep the network honest. First, **the water's veto**:
a used path is compacted and slightly sunken — an excellent riverbed —
so any leg that runs too close to the fall line gets captured by the
drainage network in the first hard rain, gullied, and
killed. Concretely: seasonal alternation between the pilgrim pass and
a light erosion pass, with path cells whose grade exceeds roughly half
the local sideslope being degraded or destroyed. The surviving network
hugs contours and reverses grade the way old mountain paths do, and it
looks wise for the same reason they do — the wisdom is in the veto,
not the walkers. Second, **succession**: paths carry traffic ledgers,
and sustained flux promotes a path up the mover lattice — game trail
to footpath to bridle path to cart road — each promotion re-running
the route under the stricter functional, so the road inherits the
trail's alignment and then *re-negotiates* the parts the stricter
mover cannot accept. Stairs are the honest exception: where the ground
is steep enough, the pedestrian optimum and the wheel optimum diverge
into separate geometries — the stair-stitch straight down the fall
line, the folded road off to the side — and both should exist,
crossing at memorable oblique junctions.

Implementation-wise this would be the second derived global value: after
`DrainageGraph`, a **`RouteNetwork`** — nodes at centers and
junctions, edges with mover class, alignment, and traffic; produced by
an evolution operator (global and iterative, with randomness carried
explicitly in a `RandomSequence`) that consumes terrain, drainage, and
a `CenterSet`. Like everything else it is a value: inspectable as an
overlay, diffable, serializable, and subject to its own laws
(translation equivariance; determinism under a fixed `RandomSequence`;
conservation of pilgrims in the ledger sense).

## Carving: the world amended

A real mountain road is not a texture; it is an *edit*. The carving
pass takes `RouteNetwork` edges of sufficient rank and commits them to
the heightmap: the **bench** — cut into the uphill side, fill on the
downhill side, a locally level ribbon inheriting every curve from the
mountain; the **switchback rules**, which are real engineering and
cost nothing to honor — spend the folds on *spurs* (convex-plan cells:
dry, flat-ish, out of the line of fire), cross *couloirs*
(concave-plan cells: the face's gutters) perpendicular and armored,
stagger stacked traverses laterally so no leg sits in the fall-shadow
of another's cut; and the **drainage deference** — the road joins the
drainage graph rather than fighting it, handing intercepted water back
to the mountain's own channels at the crossings. Done right, the
carved road reads as *revealed* rather than imposed, because every one
of its curves is the mountain's curve. This is the strictest
structure-preserving transformation in the whole system. Its future
acceptance tests should be visual and statistical both: terrain outside
the corridor must be untouched to the bit, and the road's grade
histogram must respect the mover that ordered it.

Carving is also where the three missing properties finally
enter. Alternating repetition: the syncopated rhythm of traverses and
folds, born from the rockfall-stagger constraint. Good shape: the
terrace, the bench, the bridge at the waterfall — legible artifacts
against organic ground. Inner calm: what the road *doesn't* touch,
which is almost everything, enforced by the corridor test.

## Settlements: the world convened

Cities do not arise where land is pleasant. They arise where movement
systems **fail to compose** — the geographers' *break-of-bulk points*:
the harbor where the boat's cargo must become the cart's, the ford,
the pass, the head of navigation, the foot of the stairs. Everything
that must pause needs everything: storage, trade, shelter, tax,
worship. So settlement seeding is almost embarrassingly simple once
the mover networks exist: find the seams — nodes where edges of
incompatible mover classes meet, weighted by the traffic ledger — and
precipitate. The hierarchy follows on its own: hamlets at small
junctions, villages at confluences, the market town where trunk meets
water, with Christaller's central-place spacing and something like the
Zipf rank-size law emerging as *checks* (readings for the rating card:
does our settlement-size distribution have the head/tail signature of
real ones?) rather than as rules imposed.

Each settlement is then itself a `CenterSet` at the next scale down —
the square, the church knoll, the harbor front — which is where this
system hands off to the city mode and its art director. The pattern
ladder runs from region to room; we are building the top rungs and the
boy is building the bottom ones; the meeting point is somewhere around
the pattern where the path arrives at the door.

## The player: the last droplet

Everything above runs at generation time, but the mechanism does not
stop when the player arrives — the player *joins* it. Per-cell traffic
ledgers (passages, braking flux, wheelspin) let sustained riding
compact lines, wear ruts, thin the grass on a repeated approach:
**desire paths in live play**, the racing line emerging as an eroded
gully of attention. Jumps stay deliberately outside the authored
network — the excited spectrum, trajectories that leave the constraint
surface, checked only at takeoff and landing — and the world's only
concession to them is memory: enough repeated hits on the same lip and
the takeoff shows wear, rubber on the landing, a rumor the observant
can read. Chalk, not ramps. The ground network is written by pilgrims
and rain and belongs to everyone; the air network is written in wear
marks and belongs to whoever can decode them. Beta, in the climbers'
sense: the terrain is public, the line is knowledge.

## Shape of the work

Nothing above requires a new semantic category beyond fields,
transforms, readings, and structured values. It does require substantial
new algorithms. They should arrive in the same small tested slices as
everything else:

1. **Readings** as first-class values (the drainage traversal already
   computes most of them), each with a lab overlay. The rating card
   falls out here.
2. **`CenterSet`** and center recipes: candidate-field overlays in the
   Tycoon window, a blessing workflow, ride-to-judge.
3. **`RouteNetwork`** as latent overlay only — polylines draped on
   terrain, no edits — where we tune the reuse discount, the veto, and
   the succession thresholds until the consolidation behavior looks
   right.
4. **Carving** as a `TerrainTransform` with the corridor golden.
5. **Settlement seeding** from modal seams, handing `CenterSet`s down
   to the city mode.
6. **Live ledgers** for the player's stratum.

Plain names in code, the friendly abstractions in comments and in this
document; enum-valued semantics, not capability booleans; every stage
a value, every value an overlay, every overlay one keypress from the
motorcycle.

## The acceptance test

There is a criterion for this whole layer, and it is not a
metric. Somewhere on the Amalfi coast there is a terrace that made it
into a book, and the book, half-remembered decades later, made a
stranger drive an hour up a mountain to stand on it, and the visit
left the place holding more of its own history than before. Call it
the Palumbo test: **a generated world passes when one of its places
could survive that** — when a paragraph about the saddle with the
cairn, or the harbor town at the foot of the stairs, or the mirror on
the north face, would be worth writing; when finding it again would
feel like arriving.

The first author makes the world true. The second author makes it
*somewhere*. The player, if we do this right, makes it remembered —
and the system's last and best output is not terrain at all, but the
sentence a rider says to another rider, years from now, about a place
neither of us placed: *you have to see it. I'll give you the beta.*

The companion `structure-of-space.md` essay develops a possible cellular
substrate for this unfolding. It is not an implemented architecture: it asks
how continuous terrain might become discrete places, construction, and
memory without reducing the smooth world to a voxel grid.
