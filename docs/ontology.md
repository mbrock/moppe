# What Exists in Moppe

Moppe keeps careful accounts of *how much*: heights in meters, thrust in
newtons, sink rates in meters per second. The type system refuses to add an
airspeed to an altitude, and the codebase is better for it. This document
starts a parallel set of accounts about *what kinds of things there are*.
It is not a specification and it proposes no work. It is a vocabulary — a
way of talking about the game world that stays truthful about the structure
of what the code simulates, the way `units.md` stays truthful about
magnitudes.

The vocabulary is borrowed, mostly from the ontologist Barry Smith and his
collaborators, whose papers live in `research/` (readable in sections at
`https://m.sheaf.less.rest/` with `Accept: text/markdown`). Their subject is
ordinary reality — mountains, headaches, county borders, orchestras — but a
game world turns out to be built from the same kinds of things, and it is
clarifying to call them by their proper names.


## Fields and things

Ask what the terrain *is* and two honest answers come back.

To the simulation, the terrain is a field: elevation as a value at every
position, and alongside it moisture, forest cover, snow support, channel
flux — each a quantity distributed over the same ground. This is the
scientist's answer. Hydrology and erosion are computed this way because
they are field phenomena; nothing in a drainage calculation ever needs to
know where one mountain ends and the next begins.

To the player, the terrain is things: a mountain, a valley, a river with a
mouth, a waterfall worth flying past. This is not a lesser answer. People —
and cameras, and quest designers — deal in objects, because objects are
what you can name, visit, and point at.

Smith and Mark, in *Do Mountains Exist?*, show how both answers hold at
once. The mountain is real, but it is a **fiat object**: a portion of the
elevation field set into relief and named, the way Mount Everest is a
demarcation drawn on geophysical reality rather than a self-bounding thing
like a planet. Fiat objects have graded, vague boundaries — nobody can say
exactly where a mountain stops, and nobody needs to. The water-feature
namer (stream, river, confluence, mouth, waterfall, lake) and the cinematic
landmark planner are the game performing exactly this projection: carving
nameable things out of fields so that the camera can tour them and the
player can be somewhere. Trail influence and home-base influence do the
same job with honest gradedness — membership that fades instead of ending.

The rule of thumb this yields: **simulate in fields, experience in
objects**, and treat the projection from one to the other as real work with
a real name.


## Six kinds of entity

Aristotle sorted what exists into a small table; Smith's *Against
Fantology* extends it to six cells, and the game fills every one of them.

There are **substances**: things that exist on their own and persist —
this bike, this glider, this cedar, the walker. A substance has an
essence: being a bike is not a state the bike is in, it is what the thing
*is*, and it is true the whole time the bike exists.

There are **qualities**: things that exist only *in* something else — this
bike's boost charge, the moisture of this patch of ground, the bank of this
glider right now. A quality cannot float free; there is no boost charge
without a bike to have it. The bundle types make this dependence
structural: a moisture value exists only at a site of a domain, and the
compiler will not let you write one down otherwise.

And there are **processes**: things that happen — this jump, this landing,
this cinematic flight, the afternoon's slow clouding-over. A process is
not a thing that changes; a process *is* a change, stretched over time,
with the bike as its participant.

Each of the three comes in particular and universal. *This* jump is a
particular; *jump* is the universal it instantiates. The quantity specs
are exactly the universals of the quality column: `airspeed` and
`rate_of_climb` are both measured in meters per second, but they are
different universals, which is why they are different specs. Keeping the
spec vocabulary curated — one spec per genuine quality, none for arbitrary
combinations — is the ontological discipline that Smith calls resisting
*Booleanism*: reality does not contain a quality for every expression you
can form, and neither should the game.

The reason to keep all six cells distinct is what Smith's paper is about.
Flatten them — treat "is a bike" and "is airborne" as the same sort of
runtime fact, or reduce every entity to a bare id with property cells, as
an entity-component spreadsheet does — and you get a world of unknowable
particulars inspected through null checks. The six-fold structure is what
the null checks were compensating for.


## What persists and what happens

Smith's SNAP/SPAN framework says a changing world needs two linked books
of account. One book inventories **continuants**: everything that exists
wholly at an instant and endures — the bike, its boost charge, the trail
network. The other inventories **occurrents**: everything that unfolds —
rides, jumps, landings, a session's whole history. Neither book reduces to
the other, and each continuant appears in the second book once, as its
**life**.

The game already keeps both books; it just never said so. `GameState` is a
SNAP inventory: the copyable value of everything that exists at this tick.
The fixed-step input tape and the replay machinery are SPAN: a session's
history as a first-class thing you can store and re-run. The philosophical
footnote that earns its keep here is that *processes cannot change* — a
process is timelessly whatever it turns out to be — which is why a replay
tape is immutable by nature and not merely by implementation choice.

Where a process crosses a threshold there is an **event**: an instantaneous
boundary. Touchdown. Star collected. Glider deployed. The scattered
airtime floats and pop-on-read impact values in the current code are events
and processes recorded without their proper category; the vocabulary at
least lets us see them as such.


## Parts, attachments, places

The word "hierarchy" hides three different relations, and scene graphs
earn their bad reputation by melting them into one pointer.

**Parthood** is definitional. The wheel is part of the bike the way the
heart is part of the body: a bike without a wheel is not a sparser bike
but a damaged one. Parthood belongs in the type: a bike *is* frame and
wheels and steering, the way a product type says.

**Attachment** is circumstantial but grammatical. The rider mounts the
bike; the bike hangs tethered beneath the glider; the glider is dropped.
These are configurations drawn from a small closed set, with rules about
which transitions are possible when. The mode flag and the
`bike_attached` boolean are this grammar written in shorthand.

**Location** is neither. The bike is not part of the terrain and not
attached to it; it is *on* it, which is a relation you query — sample the
field under the wheels — not a link you store. Keeping location out of
the structural relations is what keeps them small, and it is the mistake
scene graphs make when the player gets reparented under the boat.

A body, incidentally, has the same shape all the way down: Smith's paper
on bodily systems describes an organism as a nested spatial-functional
hierarchy whose subsystems are fiat demarcations. The walker's limbs, a
tree's branching, a river's confluence tree — one directed-tree vocabulary,
as the Atelier notes already observe about drainage and organisms.


## Multitudes

Some things come in populations: the pickup stars, the grove of trees, the
dust emissions, someday traffic. A population is not an arbitrary set —
Smith is scornful of set theory's willingness to collect numbers and popes
together — but a collection *of a kind, in a context*: many instances of
one universal sharing one setting. That is why a fleet can be total, every
member having every quality of its kind, with none of the spreadsheet's
empty cells.

Members of a population are born and die, and identity across that
churn — being *the same star* between one snapshot and the next — is what
the philosophers call **genidentity**. A handle with a generation counter
is genidentity implemented; a recycled slot resurrecting the wrong star is
a genidentity bug.


## Systems, magnitudes, models

Landgrebe and Smith's ontology of physics adds the top floor. A **system**
is a portion of reality delimited *by fiat*: you choose a granularity and
a boundary — the planets, or the planets with their moons — because you
choose which interactions you are modeling. The vehicle system, the
soaring model, the drainage network: none of these carve the game world at
pre-given joints, and none need apologize for it. Delimiting a system is
a modeling act, and it is done well or badly, not truly or falsely.

A **magnitude** is a measurable phenomenon — mass, sink rate, boost
energy — and each magnitude is a dimension of the system's **phase
space**. This gives the right way to see a state value: not a struct that
accumulated fields, but a phase space that should be able to say what its
dimensions are. A bag like the current logic-state struct is a phase
space with unlabeled axes.

A **model**, finally, is a human-made representation of a system —
equations, drawings, code — and models approximate by nature. Their
worked example of an idealized model is, delightfully, a weight on a
spring: the harmonic oscillator, template for every refinement. The game
is full of these honest small models — the comment in `glider.hh` calls
its physics "a deliberately compact soaring model," which is exactly the
right register. The code is the model stratum of the game: laws written
as update rules, idealization as license rather than lapse.


## Pictures of the world

The engine is full of representations: the height texture, the trail-map
HUD, the frame snapshot handed to the renderer, the benchmark CSV, a saved
checkpoint. Smith's *True Grid* and the granular-partition papers give a
single account of what all of these are: a **grid** of cells stands in a
**projection** relation to reality, and when projection succeeds each
object is **located** at its cell. A grid whose projection and location
agree — where the map says what is there, and what is there is what the
map says — is **transparent**: you look through it at the things
themselves. Transparency is the correctness condition of every bridge in
the codebase, from bundle columns packed into texture lanes to poses
frozen into a frame. The two classic ways a representation goes wrong are
exactly the two familiar bugs: cells projecting onto nothing (a dangling
handle) and cell structure disagreeing with object structure (a mirror
that drifted out of sync).

Three details of the theory pay immediate rent. Grids may hold **empty
cells** without being false — the periodic table kept labeled boxes for
undiscovered elements — so spare fleet capacity and absent optional poses
are respectable. A fixed grid may be **re-projected over time** — their
example is a territorial grid sampling birds from moment to moment — which
is precisely what a frame is: the same cells every frame, aimed anew sixty
times a second. And every grid has a **direction of fit**. Most of the
engine's grids fit world-to-map: the render, the HUD, the CSV must conform
to the world, and their virtue is fidelity. The trail system fits both
ways at once, like a cadastre: walkers wear the trail, the trail steers
the walkers, and its virtue is stable convergence. And one grid fits
map-to-world: the recipe.


## The unreal, made real on demand

A game world is, in the terms of Smith's paper on the unreal, fiction: its
representations are about things that do not exist. Fiction's cells
project into thin air — his older example is a catalogue of Aztec gods.
But procedural generation is fiction with a private amendment: the recipe
is a plan whose execution *manufactures its referents*. A seed names a
world the way "Mount Everest" names a mountain — rigidly — except that
uttering the name is what brings the mountain into being. Generation is a
performative map, and determinism is simply the demand that the
performance be repeatable: same seed, same world, so that the name never
dangles.

This is why the game can hold itself to a standard that ordinary fiction
cannot: within a generated world, every well-formed representation can be
transparent, because the world and its pictures issue from the same act.


## The vocabulary, briefly

- **field** — a quantity everywhere over a domain (elevation, moisture)
- **fiat object** — a named, vaguely-bounded demarcation of a field
  (a mountain, a river mouth, a trail)
- **substance** — an independent persisting thing (the bike, this tree)
- **quality** — a dependent thing, existing only in its bearer
  (this bike's boost charge); its universal is a quantity spec
- **process / event** — what happens / its instantaneous boundary
  (this jump / touchdown)
- **continuant / occurrent (SNAP / SPAN)** — what persists at an instant /
  what unfolds over time; checkpoint / replay
- **parthood, attachment, location** — is made of / is configured with /
  is at; type structure / closed grammar / field query
- **population, genidentity** — many of a kind in a context; identity
  through birth, death, and reuse of slots
- **system, magnitude, model** — fiat-delimited subject matter; a
  dimension of its phase space; the code that approximates it
- **grid, projection, location, transparency, direction of fit** — what a
  representation is, and when it is faithful
- **performative map** — a representation that creates its target; a
  recipe with its seed

None of this obliges any refactoring. It is here so that when we discuss
whether something deserves a struct, a spec, an event, or a query, the
discussion can be about what the thing *is* — and so that the answer, once
found, has a name.


## Sources

Barry Smith and collaborators; all in `research/`, browsable at
`m.sheaf.less.rest`:

- *Against Fantology* (2005) — the six-category table; against bare
  particulars and Booleanism.
- *Do Mountains Exist?* (Smith & Mark, 2003) — fields, objects, and fiat
  landforms.
- *SNAP and SPAN* (Grenon & Smith, 2004) — continuants, occurrents,
  lives, and change.
- *Classifying Processes* (2012) — process profiles; determinable and
  determinate.
- *True Grid* (2002) — projection, transparency, directions of fit.
- *A Theory of Granular Partitions* (Bittner & Smith, 2003) — cells,
  location, empty cells, granularity.
- *Ontologies of Common Sense, Physics and Mathematics* (Landgrebe &
  Smith, 2023) — systems, magnitudes, models, phase space.
- *About the Unreal* (Beverley, Logan & Smith, 2025) — fiction,
  blueprints, and simulation.
- *Bodily Systems and the Spatial-Functional Structure of the Human Body*
  (2004) — organisms as nested fiat systems.
