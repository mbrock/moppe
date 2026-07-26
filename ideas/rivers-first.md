# Rivers first

*Notes toward a landscape simulation that is not a heightfield: the
skeleton as the state, the terrain as a report.*

`ontology.md` gives Moppe's rule: simulate in fields, experience in
objects, and treat the projection as real work. This essay imagines
the dual system — call it the inverted landscape — whose rule is:
**simulate in objects, derive the field.** The heightmap stops being
the state and becomes what rendering always was in the ledger
picture: a report. The cadastre stops being an analysis of the
terrain and becomes the terrain's very substance.

## The inversion

In the lattice paradigm, the elevation field is authoritative and
everything nameable — rivers, lakes, divides, waterfalls — is
censused out of it after the fact, with the genidentity problems
`near-and-far.md` describes. But geomorphology itself keeps saying
that the landscape's *causal* structure is a small set of objects:
the drainage network is the skeleton that organizes everything; the
divides partition it; the knickpoints carry change through it;
hillslopes are just flesh relaxing toward the nearest channel.

So let the state be the skeleton:

- **The drainage tree** — reaches and confluences, with each reach
  carrying typed accounts: discharge, gradient, sediment in transit.
- **The divide network** — the dual boundary complex, fiat edges
  genuinely shared between catchments.
- **A population of knickpoints** — waterfalls and oversteepened
  steps as *resident agents* that migrate upstream along the tree at
  a pace set by stream power. In the lattice paradigm a waterfall is
  an emergent, fragile census artifact; here it is a first-class
  individual with a birth, a path, and a death (reaching a divide,
  or annihilating a rival).
- **The event journal** — captures, avulsions, pit-merges, divide
  migrations: discrete topological rewrites of the tree, each one a
  posting. Stream piracy — one basin capturing another's headwaters
  — becomes the headline event of deep time rather than an invisible
  reconnection of flow directions.

Elevation is then *derived, on demand, at any resolution*: walk from
the outlet up the tree integrating the slope–area law (slope as a
power of discharge), then flesh hillslopes by relaxation toward
their channel — distance fields from the skeleton, an eikonal
solve, or the diffusion the lattice already knows, but run locally
and lazily. The heightfield inherits the derivation regime of
`by-derivation.md`: any window of the world materializes from the
skeleton plus the seed, at whatever granularity regard requests.
LOD stops being a tower built over the state and becomes the state's
native mode of address.

## What the books look like

The accounting is cleaner than the lattice's, because the objects
are the accounts. Each reach is a sediment ledger: supply posted
from its hillslopes and upstream neighbours, capacity from
discharge and slope, the balance deciding incision (debit the
bedrock) or aggradation (credit the bed). Mass conservation is a
walk of the tree — exact, because transport is edge-posting by
construction. A capture event is a zero-sum transaction of *area*:
the pirating basin debits precisely the catchment the victim
credits, and Hack's law and the Horton ratios become trial-balance
style diagnostics — statistics the books should keep if the
simulation is honest.

Lakes are no longer censused; they are *born*: a pit in the tree is
a lake by construction, with capacity to its spill, and its
overflow is an edge of the tree like any other. The fiat objects of
`by-fiat.md` stop being the solver's economizing projection and
become the primitives — which dissolves the genidentity problem,
since identity now lives where the dynamics acts.

## Other ways off the lattice, briefly

**Material tokens.** Go Lagrangian: simulate the rock itself as a
hive of parcels with provenance — each grain remembering its source
outcrop — and let height be a derived reading (the stack of tokens
standing at a place). Transport is literally moving tokens between
accounts; the journal is distributed into the material. Real
geology reads sediment provenance exactly this way. Expensive, but
it makes the landscape's memory *tangible*: dig anywhere and the
strata are the ledger, legible in cross-section — path-dependence
you can excavate.

**The sandpile.** State the constraints instead of the process:
angle of repose, graded rivers, isostasy — an allowable-balance
region in the accounting sense — and let the landscape be a
feasible point that tectonic postings repeatedly perturb, with
relaxation cascades (avalanches, self-organized criticality)
restoring feasibility. Simulation becomes constraint repair;
realistic power-law statistics fall out of the cascade dynamics.

**Water as walkers.** Moppe already contains a second erosion
paradigm and calls it the trail system: agents post wear, the
medium posts recovery, the balance steers future agents. Rivers
*are* trails worn by water — the Helbing active-walker model and
droplet erosion are one mechanism at different paces. A unified
system would have one feedback-ledger law with pluggable walkers:
riders wear trails, droplets wear valleys, glaciers wear cirques —
Alexander's centers strengthening themselves at three timescales,
one piece of code.

**The adaptive tissue.** The halfway house already in the tree:
`structure-of-space.md`'s irregular cells and the Atelier hex sheet,
where resolution follows activity — channels refine, plateaus
coarsen — and the lattice itself becomes a population with births
and splits. Less radical than rivers-first, compatible with it as
the flesh between the skeleton's bones.

## The embedding storey

The Atelier tree already knows the architecture this wants. An
organism there is three storeys: combinatorial topology, intrinsic
qualities, and an embedding into space — and the tests insist the
storeys stay separate (embeddings leave the intrinsic partition
unchanged; the seed changes the organism, not only its embedding).
Rivers-first is the same building with water in it. The drainage
tree is the organism: a manageable discrete structure with lengths,
discharges, gradients as intrinsic qualities, and captures and
knickpoints as its dynamic life. The *landscape* is the embedding —
and nearly all of the visual richness lives in that map, not in the
domain it maps.

This is worth saying with full weight, because it relocates where
complexity comes from. A drainage network is combinatorially simple
— a tree, with statistics — yet basins look inexhaustibly intricate.
The intricacy is the embedding's: a tree embedded densely into a
basin is nearly space-filling, and the divides are the complement
the filling leaves behind, boundary complexity conjured from
planarity rather than stored. The embedding is built the way the
Atelier grows a canopy or a turtle draws an L-system: each
confluence carries a branching angle, each reach a developed length,
and the geometry unfolds by composing simple local moves — turns,
folds, rotations. A landscape is then a *word* in a group of local
moves, the way a Rubik's cube position is a word in face turns:
enormous configuration space, tiny generator set, and the seed
derives the word (`by-derivation.md` again — the embedding is
implied; only its revisions are posted).

Two beautiful consequences follow. First, **meanders are
corrugations**: when a reach's intrinsic length exceeds the straight
distance its valley allows, the embedding must wrinkle to absorb the
excess — which is literally how sinuosity works (sinuosity *is* the
ratio of intrinsic to extrinsic length) and formally how isometric
embeddings absorb excess metric à la Nash: spiral corrugations at
the scale the mismatch demands. Meander migration becomes a flow on
the embedding storey — curvature-driven motion of the reach's curve
— while the organism above it holds still. Second, **the two paces
separate cleanly**: topology changes rarely and discretely (a
capture is a posting), the embedding flows continuously (meanders
migrate, divides creep), and the fast storey can never corrupt the
slow one's identities, because it cannot even express them.

And one organism admits many embeddings, exactly as the Atelier's
trees do — diagram, wind, flat, bridge. The water organism's
embeddings are the game's views: the geographic embedding is the
landscape; a schematic embedding is the subway-map of the basin that
Terrain Lab wants; the long-profile embedding is the elevation
graph a knickpoint migrates along. All projections of one machine,
transparent in the `ontology.md` sense, because they share the
organism they project.

## What the game would feel like

Deep time becomes watchable in a new way: the camera can follow *a
knickpoint* up its river for ten thousand years, or sit on a divide
while two basins negotiate it, because these are individuals with
lives, not transient census rows. Terrain Lab's scrubbing becomes
cheap — the skeleton's journal is small, and any epoch's heightfield
is derivable — and the landmark namer stops reverse-engineering
what the simulation already knew and simply reads the register. The
landscape's beauty thesis from `insjo.md` sharpens: the world is
path-dependent because its state *is* its history's residue — a
tree of rivers is nothing but the record of every capture that
built it.

The cost is honest: fields are wonderfully uniform to compute, and
skeletons are not — tree rewrites, migrating agents, and lazy
flesh-derivation are more intricate than a double-buffered sweep,
and GPU-friendliness moves from trivial to earned. The lattice
paradigm buys simplicity with anonymity; the skeleton paradigm buys
identity with bookkeeping. Moppe chose the first and censuses
objects out; the inverted landscape chooses the second and derives
fields in. A full engine likely wants the dialectic: skeleton
authoritative for structure and story, fields derived for physics
and rendering, with the projection between them — in both
directions now — treated as the real work it always was.
