# What must be computed together

A world made of many interacting parts has to be computed in some order,
by some number of threads, and the parts do not politely wait their turn.
Cells of terrain read their neighbours while writing themselves. Bodies
in a mechanism push on each other. Every such system faces the same
question — *which pieces of this can be done independently, and which have
to be worked out together?* — and the answers turn out to be a small,
recurring family of structures.

This is a note about that family. It is not a proposal. It collects what
Moppe already does, what a mature physics engine does with the same
problem, and what the two have in common, because the correspondences are
close enough to be useful and interesting in their own right.

The engine read for comparison is Erin Catto's Box3D, a 3D rigid-body
engine in portable C. Its island and constraint-graph code turns out to
be doing, for machinery, what our hydrology code does for landscape.


## One hazard, three cures

The hazard is always the same shape: many small updates, each of which
reads and writes state shared with its neighbours. Run them in the wrong
order and an update sees a stale value; run them at the same time and two
updates clobber one another.

There are three ways out, and which one is available is not a matter of
taste.

**Sequencing.** Fix an order in which every update's inputs are already
final by the time it runs. This needs the dependency graph to be
*acyclic*. It is the best cure when you can have it: one pass, exact, no
iteration.

**Independence.** Arrange the work into groups within which no two
updates touch anything in common. Order inside a group is irrelevant
because there is no interaction to order. This needs a partition into
non-conflicting sets, and it is what you fall back on when there is no
valid sequence.

**Iteration.** Sweep the whole coupled set repeatedly and let it converge.
This is what remains when the parts are genuinely mutually dependent and
no ordering can resolve them.

Landscape mostly gets the first. Machinery mostly gets the second and
third. The reason is gravity: water flows downhill, so the drainage graph
inherits an order from the height function for free. A stack of crates
has no downhill — each body constrains the others — so its constraint
graph is undirected and full of cycles, and no order exists to be found.


## The coarsest partition: what is coupled at all

Before any of that, both domains ask a prior question: which parts are
coupled *to each other* at all, and which are simply unrelated?

The answer in both is connected components. In Box3D these are called
islands, and the header says so plainly, linking the graph-theory article
and the one on dynamic connectivity. One line of its comment is worth
keeping:

> Contacts and joints may connect to static bodies, but static bodies are
> not in the island.

That rule is load-bearing. If anchored bodies propagated connectivity,
everything resting on the ground would collapse into a single world-sized
component and the decomposition would be worthless. Anchored things
participate in constraints without joining what they touch. The Atelier
hex sheet has the same idea under the name `is_anchor`: the four pinned
corners of the open sheet take part in the wave equation without being
part of what it solves for.

Moppe's landscape has the same partition under a different name. Separate
river basins are disjoint subtrees of the flow graph; nothing upstream of
one outlet can affect anything upstream of another. And the terrain code
already computes such components with the standard algorithm — the
path-halving union-find in `moppe/terrain/merge_tree.cc`, which is the same
primitive as Box3D's island find-parent, applied to sublevel sets of the
heightfield instead of a constraint graph.

Two problems, one algorithm, arrived at independently.


## Merging is easy; splitting is not

The interesting difference between the two is what happens to the
partition over time.

Water components only ever **merge**. As the level rises, two lakes may
join at a saddle, but a lake never divides. Because merges are the only
event, the entire history fits in a tree — which is exactly what a merge
tree is: components born at minima, joining at saddles, recorded once in
one deterministic sweep. Every water level is then a view of that tree
rather than a fresh computation.

Mechanical components both merge and split. Two things start touching and
their islands unite; they stop touching and an island may fall apart.
Union-find is a merge-only structure, so the split has no cheap
counterpart, and Box3D's design shows what that costs: each island counts
its removed constraints, becomes merely a *candidate* for splitting, and
the actual union-find re-run happens in its own task, at most one island
per step. **Merge eagerly, split lazily.**

So the same structure appears in two regimes:

| | index | history | maintenance |
| --- | --- | --- | --- |
| flooding terrain | water level, totally ordered | a tree | one offline sweep |
| coupled bodies | time, unpredictable | merges and splits | incremental, online |

The merge tree is what island history would be if islands never split.
When the parameter you sweep is monotone, you can afford to compute the
whole family at once.


## Chains and walks in the lattice of partitions

That distinction has a proper home. David Ellerman's work on the
subset–partition duality treats the partitions of a set as a lattice
ordered by refinement: at the top the discrete partition where every
element is its own block, at the bottom the indiscrete one where
everything is a single block. Join is common refinement; meet is common
coarsening.

Put a flooding landscape into that lattice and it moves in one direction
only. As the water rises, bodies merge, blocks fuse, the partition
coarsens, and the world descends toward the indiscrete partition — which
is exactly one global ocean. As the water falls, it climbs back toward
finer partitions. A merge tree is therefore a **chain** in the lattice: a
totally ordered family of partitions, monotone in the refinement order,
indexed by level. That is why the whole history fits in one structure.

Coupled bodies trace a **walk** instead. Islands merge and split, so the
trajectory wanders up and down without monotonicity, and there is no
single object that records it in advance.

This also says exactly what union-find is. It can only ever fuse blocks,
so it is a **monotone** structure in the refinement order: it moves in the
coarsening direction and has no reverse gear. Merging is nearly free
because it is the operation the structure exists for; splitting is
expensive because it means travelling the other way. Box3D's merge-eagerly
and split-lazily is not an engineering compromise so much as the shape of
the data structure showing through.

Ellerman's duality is worth keeping in mind for its own sake. A subset is
described by the elements it *contains*; a partition by the pairs it
*distinguishes*. Refinement is inclusion of those distinguished pairs.
Where the subset lattice measures size, the partition lattice measures
information — which gives us a genuinely useful reading of a landscape.


## Measuring how differentiated a world is

Following Rota's slogan that probability is to subsets what information is
to partitions, the measure of a partition is its **logical entropy**:

    h = 1 - sum over blocks of (block size / total size)^2

which is simply the probability that two randomly drawn elements fall in
different blocks. For the water-body partition it reads directly: *the
chance that two random cells of the world belong to different bodies of
water.*

A merge tree already carries component sizes at every level, so this is
one pass over data we compute anyway, giving `h` as a function of sea
level. Two things follow, and neither needs any new machinery.

The **maximum** of that curve is the most hydrologically differentiated
version of a world: the level with the most distinct water bodies,
weighted by their size. That is a plausible criterion for choosing a sea
level, or for judging a generated world before anything is rendered — the
alternative extremes being one drowned plain and one dry rock, both of
which score near zero.

The **steep drops** are the dramatic merges, where two large basins join
at a saddle. Those are the beats of a rising-water spectacle: rather than
animating the level linearly, animate it through the entropy drops.

The quantity itself is the familiar Simpson index. What the partition
reading adds is knowing what it measures and why it is the right measure
to reach for.


## The other partition, on the other rank

Within one island the parts are coupled, so the solver must iterate over
all of them. That still leaves the question of what may run *at the same
time*, and the answer is a second partition with nothing to do with the
first.

Box3D colours its constraints so that no two constraints in a colour share
a body. Then every constraint in a colour can be solved in parallel, since
they write to disjoint bodies; the colours run one after another. Each
colour keeps a bitset of the bodies it has claimed, and adding a
constraint is a greedy first-fit scan for a colour whose bitset contains
neither endpoint. Static bodies are never tested, because nothing writes
their velocity — a hundred contacts against the ground may share one
colour. Constraints that cannot be placed go to an overflow colour solved
single-threaded.

The two partitions answer different questions about different things:

| | islands | colours |
| --- | --- | --- |
| partitions | bodies | constraints |
| the question | what must be solved **together**? | what may be written **at once**? |
| answerable to | the world | the machine |
| getting it wrong | wrong physics | data races |

In the ranked language the Atelier already uses — bodies as 0-cells,
joints as 1-cells — islands partition the 0-cells by connectivity, and
colours partition the 1-cells so that no block contains two cells meeting
at a common 0-cell.

The everyday version: constraints are meetings, bodies are people,
colours are time slots. Two meetings sharing a person cannot run at once,
and the number of slots you need is the busiest person's meeting count.


## Sequencing and independence are duals

Colouring and topological order look unrelated and are not. They are
opposite cures for the same hazard: colouring removes the interaction,
sequencing schedules it.

Moppe's erosion takes the sequencing route, and can, because water gives
it a direction. `FractionalFlowDomain` stores an order running from
donor-free sources toward outlets; `stream_power_evolution.cc` walks that
order **backwards**, so outlets are solved first and every cell reads its
receiver's already-final height. One pass, closed form, no iteration.

The bridge between the two cures is worth knowing. Group the nodes of a
directed acyclic graph by depth, and every group is a set of mutually
independent nodes — a colouring, derived from the order rather than
searched for. Groups run in sequence; within a group, anything goes. If
the erosion sweep ever wants threads, that is the shape it would take, and
it would be the same structure as a constraint graph's colours arrived at
from the opposite direction.


## All three already live in one erosion step

The pleasing part is that a single step of the terrain evolution contains
the whole taxonomy, and splits it exactly where the physics splits.

- **Components.** Separate basins are independent; nothing crosses a
  divide.
- **Sequencing.** Within a basin, stream-power incision follows the
  drainage order, downstream first.
- **Iteration.** Then hillslope diffusion runs, and returns a *sweep
  count*, because diffusion is a Laplacian: undirected, cyclic, with no
  order to exploit.

The advective half of erosion is a directed acyclic graph and gets
sequenced. The diffusive half is mutually coupled and gets swept. That is
not an implementation accident; it is the structure of the two operators
showing through into how they can be computed at all. A constraint solver
is in the same position as hillslope diffusion, permanently.


## What the flood actually answers

One consequence of all this deserves stating on its own, because it is
easy to misremember.

The world is a torus and has no map edge to call "outside", so `flood.cc`
identifies the ocean as the **largest connected component of cells below
sea level**, with ties broken deterministically by scan order. Every cell
of that component is seeded at sea level and the flood proceeds outward,
producing a *minimax* surface: each cell's water level is the elevation of
the lowest saddle it would have to cross to escape. Other below-sea
components do not get to be ocean; they must, in the code's own phrase,
earn their own higher spill level.

So every lake in the current model sits exactly at its brim. Being at
spill level *is* having an outlet, and the spill receivers form one forest
rooted at the single ocean. The only endorheic case is global: a world
with no cells below sea level at all, whose surface is rooted at the
deterministic global minimum instead.

This is the right answer to the question being asked — *pour in unlimited
water; where does it stand?* — and it is what guarantees every cell a
downhill path, which is what makes the flow graph acyclic, which is what
lets erosion be sequenced rather than iterated. The equilibrium flood pays
for the cheap erosion sweep.

But it is an equilibrium answer, and there is a transient question it
cannot express: *this much water has arrived so far; where is it now?* A
partly filled lake is not representable, and neither is a basin that keeps
its water because inflow never reaches the saddle.

Both are the same missing thing — a lake's level is pinned to its spill
elevation — and both become expressible with a merge tree, where a basin
is a node and its parent's birth height is the saddle. A partially filled
lake is that node at a level strictly below its parent's birth; an
endorheic lake is one that never reaches it. In the vocabulary of
`docs/ontology.md`, the lake stops being a fiat object whose boundary the
code chooses and becomes one whose level is a parameter, with the whole
family of candidates kept and the query choosing among them.


## Which partitions mean something

A closing distinction, because these structures are not all the same kind
of thing.

Islands and basins correspond to something real. These bodies genuinely
constrain one another; that water genuinely cannot reach this outlet. If
the partition is wrong, the simulation is wrong.

Colours correspond to nothing in the world at all. No fact about crates
makes two contacts the same colour; the grouping exists because memory
cannot be written twice at once. If it is wrong, the answer is still
right in principle and merely raced in practice.

Both are legitimate, and it is worth keeping which is which. The ontology
says what the parts are and how they depend on one another. The
partitioning says how this frame's work is organized. A structure in the
model need not be answerable to the world — but it should be clear which
kind it is.


## Sources

- Erin Catto, Box3D — `island.h`, `constraint_graph.c`, `solver_set.h`,
  `body.h`, `joint.h`. Islands as connected components, deferred
  splitting, constraint-graph colouring, solver sets by liveness.
- `moppe/terrain/merge_tree.cc` — union-find over sublevel components;
  `plan/rfc-014-merge-tree-hydrology.md` for what it would replace.
- `moppe/terrain/flood.cc` — the minimax surface, the ocean vote, and the
  endorheic fallback.
- `moppe/terrain/stream_power_evolution.cc` and
  `moppe/terrain/fractional_drainage.hh` — the drainage order and its
  reverse sweep.
- `atelier/hex_sheet.cc` — anchors as boundary, and a neighbourhood whose
  coupling is preserved when the partition changes.
- David Ellerman, *A Fundamental Duality in the Mathematical and Natural
  Sciences* (2024), in `research/logic/` — the lattice of partitions,
  refinement, logical entropy, and the selectionist/generative pair.
- `docs/ontology.md` and `ideas/the-machine.md` for the vocabulary used
  here.
