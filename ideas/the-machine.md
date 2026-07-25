# The machine

The Atelier tree made a small argument: an organism can keep its identity
while its presentation changes completely, provided you separate what the
organism *is* from where its parts happen to be. Three storeys — a
combinatorial topology with no positions, typed intrinsic bundles over that
topology, and an embedding into world space — and the wind-bent tree and the
labelled diagram become two projections of one thing.

The motorcycle wants the same treatment. It is the other kind of
articulated body in the game, and it is currently modelled the way the tree
was before the Atelier: as a pose with some numbers attached, whose
articulated structure exists only implicitly, inside the code that draws it.

This essay characterizes the machine — what it is made of, what holds it
together, what it can do, and what happens when it is ridden — using the
ontological vocabulary from `docs/ontology.md` and the relation names from
the Relation Ontology in `ontology/ro-full.obo`. It proposes nothing for
tomorrow. It is an attempt to say precisely what a motorcycle is, so that
if we ever build it properly we know what we are building.


## Two structures, not one

The first thing to notice is that "the parts of a motorcycle" names two
different structures, and confusing them is the original sin of scene
graphs.

**What is part of what.** The fork is part of the steering assembly; the
steering assembly is part of the motorcycle. This is a partonomy: nested,
loop-free, and definitional. A motorcycle missing its swingarm is not a
simpler motorcycle but a broken one.

RO is usefully strict here. Plain `part of` is transitive and cheap. The
relation that matters for an assembly is **has component** — "w has
component p if w has part p and w can be *directly disassembled* into n
parts of similar type." That is the bolt-apart relation, the one a parts
diagram draws, and — unlike parthood — it is deliberately *not*
transitive: the motorcycle has the wheel as a component and the wheel has
a spoke, but the spoke is not a component of the motorcycle. It names one
immediate level of decomposition. Distinct again is **member of**, "a
mereological relation between an item and a collection", which is what
stars and trees have to their populations and no component ever has to its
assembly.

The chassis wants a name of this kind too. RO's **skeleton of** — "the
maximal subdivision of material entities that provides structural support"
— is exactly the right idea, but it is defined for anatomical
subdivisions, so the honest move is a project relation *inspired* by it,
say `load-bearing structure of`, rather than claiming a motorcycle has a
biological skeleton.

**What can move relative to what.** The chassis is connected to the fork
through the steering head; the swingarm is connected to the chassis through
the pivot, and also through the shock. This is a kinematic topology: a
typed graph, and it is not a tree. A linkage with a triangulated shock
mount has loops, and no amount of good taste will make it nest.

RO's mereotopological relations are mechanically defined, which is a
pleasant surprise from an ontology built for anatomy:

> **connected to** — a and b are discrete structures, and there exists some
> *connecting structure* c such that c connects a and b.
>
> **connects** — when one structure connects two others, it *unites some
> aspect of the function or role they play within the system*.
>
> **attached to** — there are physical connections between a and b such
> that *a force pulling a will move b*.

Connection is mediated by a third entity: the joint is not an edge but a
thing, with its own qualities and its own incidence to the bodies it
joins. And the umbrella relation for the whole storey extends an explicit
invitation — **biomechanically related to**, "a relation that holds between
elements of a musculoskeletal system *or its analogs*."

So the combinatorial storey is two domains and an incidence between them:
rigid bodies, joints, and which joints join which bodies. Nothing here has
a position.

```
bodies   chassis, fork, swingarm, front wheel, rear wheel, nozzle×2
joints   steering head, front axle, swingarm pivot, rear axle,
         rear shock, nozzle gimbal×2
```

Each joint instantiates a universal — revolute, prismatic, spring-damper,
gimbal — and it is the universal that says how many degrees of freedom the
joint contributes and what coordinates parameterize them.


## The intrinsic storey

Over those two domains sit typed bundles, exactly as rest length, radius,
flexibility and water potential sit over the tree's vertices and edges.

Bodies bear mass, a centre of mass in body coordinates, an inertia tensor,
a collision shape, a material, and attachment sites. Joints bear an axis
expressed in each incident body's frame, a rest transform, a coordinate
range, a stiffness, a damping coefficient. Wheels bear a radius, a
rotational inertia, a tyre compliance.

These are the machine's constitution. Translating or rotating the whole
motorcycle changes none of them, which is exactly the test the tree's
intrinsic storey passes.

But "intrinsic" covers four different kinds of dependent thing, and RO
separates them in a way worth adopting:

- **has quality** — actual and present: mass, rest length, current
  compression, tyre temperature. A quality is simply *there*.
- **has disposition** — a potential, realized only in a process: the spring
  *resists compression*, the tyre *grips*, the fuel *burns*.
- **has function** — what the part is *for*: the shock exists in order to
  keep the wheel on the ground. Every function is a disposition; not every
  disposition is a function. A tyre's disposition to wear is not its
  purpose.
- **has role** — conferred from outside, not intrinsic at all: *this* body
  is the root of the kinematic chain. Choose a different root and the
  machine is unchanged.

The bridge between the first two is **realizable has basis in**: stiffness,
a quality, is the basis for resist-compression, a disposition. And the
bridge from disposition to what happens is **realizes** — "a relation
between a process and a realizable entity … where the realizable entity
comes to be realized in the course of the process."

One relation from the same family deserves special mention, because it
gives us critical components for free:

> **determined by** — f is part of system s, f exerts a strong causal
> influence on the functioning of s, and *the removal of f would cause the
> collapse of s*.

Lose a mirror and you still have a motorcycle. Lose the swingarm pivot and
there is no mechanism at all.


## Configuration is not embedding

Here is where the tree's lesson transfers most cleanly.

At an instant, the machine has a **configuration**: the value of every
joint coordinate. Steering angle, front and rear suspension travel, the
rotation of each wheel, the gimbal angle of each nozzle. Call the vector
of them q, and their rates q̇. Configuration says nothing about where the
motorcycle is; it says how the motorcycle is *arranged*.

Placing it in the world takes one more thing: a pose for the body that
carries the root role. Then forward kinematics does the rest — walk the
kinematic graph from the root, composing each joint's rest transform with
its current coordinate, and every body acquires a world pose.

```
embedding : (mechanism, intrinsic, configuration, root pose) → body poses
```

which is `embed_tree` for machines, with the same prohibition: an embedding
may not change the topology or the intrinsic bundles.

The separation earns its keep immediately. Configuration is what the
simulation integrates and what a checkpoint stores. Embedding is what
collision, rendering, and contact consume, and it is *derived* — never
authoritative, never stored as truth, recomputed whenever the
configuration changes. Today the derived poses are computed inside the
draw call and discarded; the physics keeps its own compressed copy of q.
Neither half can see the other's version of the machine.


## The spring, all the way down

One part exercises every storey, which is why it is the right worked
example.

A rear shock is a **component of** the motorcycle. It **connects** the
chassis and the swingarm; more precisely it is the connecting structure by
which those two are **connected to** one another, and it is **attached to**
both. RO's muscle relations name the asymmetry that matters mechanically:
**has muscle origin** is the end that does not move when the element acts,
**has muscle insertion** the end that does. Under mechanical names —
`has fixed attachment`, `has driven attachment`, both subrelations of
attachment — the shock's fixed end is the chassis mount and its driven end
the swingarm. (RO also offers `has muscle antagonist` for opposed pairs,
but the spring and damper are not antagonists: the spring stores and
returns energy while the damper dissipates it, and they cooperate on the
same degree of freedom.)

It **has quality** stiffness k, damping coefficient c, and rest length L₀.
It **has disposition** to resist compression, a disposition which
**has basis in** those qualities. It **has function**: keep the rear wheel
in contact with the ground.

Its current compression x is not intrinsic. It is a *reading of the
embedding* — the distance between two attachment sites, both of which are
derived from the configuration. Compression is where the extrinsic storey
answers back to the intrinsic one.

And then the force. During a landing there is a compression process, which
**has participant** the spring, **realizes** its elastic disposition, and
is **causally upstream of** the process by which the chassis is supported.
The force it exerts,

    F = −k·x − c·ẋ

is a magnitude of that ongoing interaction: k and c intrinsic, x and ẋ read
from the embedding, F belonging to neither storey but to the process that
joins them.

Compare this with a float named `susp` and a float named `susp_v`. The
numbers are the same. What the ontology adds is the account of why they are
those numbers, and where each one comes from.


## Contact belongs outside the machine

The motorcycle's topology says nothing about the ground. Contact is a
relation between an embedded part of the machine and an embedded part of
the world, and it changes constantly without the machine changing at all.

The safe way to say it is as a process with two participants and a place:

```
rolling  occurs at        the contact interface
rolling  has participant  tyre
rolling  has participant  supporting surface
```

Contact is not a property of the tyre and not a property of the terrain.
(RO's `occurs across` — "a process occurring in a region spanning a
barrier" — is tempting, but it is meant for transport *through* a barrier,
like a membrane, which is not what a tyre does to the ground.)

The boundary itself deserves Brentano's care, which Smith reconstructs in
*Boundaries*: the tyre and the terrain do not share one identical
boundary. Each has its own, and in contact the two **coincide**. That
distinction is what lets contact begin and end without either body losing
a part. For the wading walker and the drowned bike there is also
**immersed in**, "wholly or substantially surrounded by a fluid substance."

This gives the clean stack the current code half-implements:

```
mechanism      what can move relative to what
configuration  how it is arranged now
embedding      where its parts are now
contact        how those parts meet the world
dynamics       how forces change the configuration
presentation   how the result is drawn
```

The grip model, the ground normal query, and the wall collision each live
at exactly one of those levels, and today they are interleaved in a single
update because there is no vocabulary that keeps them apart.


## Motion is a process

A ride is not a sequence of positions. It is one temporally extended
occurrent whose participant is the machine, and whose successive
configurations are its snapshot readings — SNAP inside SPAN, in the
vocabulary of `docs/ontology.md`.

That gives velocity a better description too. What the game calls "speed"
is one **process profile** among several: the chassis's linear and angular
velocity, the steering rate, the suspension's compression rate, each
wheel's angular velocity. They are different dimensions of selective
abstraction over the same ride, exactly as a heartbeat has separate
pressure, volume, and electrical profiles.

Sub-processes compose with the relations RO defines formally in terms of
start and end points: a jump **starts with** a launch, **ends with** a
touchdown, and **happens during** the ride. A landing is **immediately
causally upstream of** the compression it produces. A crash **has input**
the intact machine and **has output** the damaged one — RO's inputs are
"present at the start, and the state modified during", outputs "present at
the end, not present in the same state at the beginning", which is a
surprisingly exact description of what damage is.

The threshold-crossings inside these processes are events: touchdown,
traction lost, jets exhausted, wheel leaves ground. Today they exist as
scattered floats and consume-on-read scalars.


## Many projections of one machine

The tree's proof was that one organism supports a wind-bent embedding and a
diagrammatic one without becoming two models. A first-class mechanism
would support more:

- the ordinary rendered vehicle, with its lean and squash and visual
  suspension;
- a physical embedding for collision, at a coarser granularity;
- an engineering diagram — labelled bodies, joints, and coordinate ranges,
  the machine's equivalent of `--tree-diagram`;
- a force visualization, showing contact patches, spring forces, and the
  thrust vector;
- an exploded partonomy;
- a HUD schematic of suspension travel and contact state.

Each is a projection. The machine does not change. And by the argument in
`docs/ontology.md`, each is a grid whose virtue is *transparency*:
faithful to the same underlying thing, in its own granularity.


## What the code already is

None of this is far from what exists — which is the encouraging part.

`Vehicle::State` already carries a compressed configuration: position and
velocity for the root pose, `yaw` for steering, `susp` and `susp_v` for
rear suspension travel and its rate, `wheel_spin` for wheel rotation.
Those are generalized coordinates with the joints they belong to left
unnamed.

`vehicle_render.cc` already contains a complete embedding function. It
builds the axle transform, drops the wheels by `wheel_drop` derived from
suspension travel, rotates the steering cluster about the head by a
fraction of yaw, runs fork legs from clamp to axle, stretches the swingarm
links and shock to meet the moved axle, and gimbals the nozzles by boost
drive. Every joint in the mechanism appears there — as a matrix
multiplication inside a draw call.

So the engineering topology of the motorcycle is not missing from Moppe.
It is written twice: once as unnamed floats on the physics side, once as
procedural transforms on the drawing side, with no shared value connecting
them. The bike renders correctly because two independent transcriptions of
the same mechanism happen to agree.

That is the same condition the tree was in before `DirectedTreeTopology`
gave its branching an explicit home, and the same failure mode
`docs/ontology.md` names for representations: two grids that must
correspond, kept in step by hand.


## What it would buy, and what it would cost

It would buy: one place where the machine is described; a physics that
integrates real joint coordinates instead of visual approximations; a
renderer that reads poses instead of deriving them; diagrams and
inspection views for free; damage and modification expressed as changes to
intrinsic bundles rather than new special cases; and a bicycle, a car, and
a glider-with-tethered-bike described in the same vocabulary as the
motorcycle rather than as separate code paths.

It would cost: a mechanism value and its two domains; a forward-kinematics
pass; and the discipline to keep configuration and embedding apart when it
would be quicker to cache a world pose somewhere convenient.

The honest note is the one the Atelier charter strikes about drainage:
this is a proposal and a description, not a claim that Moppe has been
rewritten. The motorcycle rides well today. What it lacks is not
behaviour but an account of itself.


## Relations used here

From `ontology/ro-full.obo`, with RO's own senses:

| relation | sense |
| --- | --- |
| `has component` | part, of similar type, directly disassemblable; not transitive |
| `member of` | item in a collection, not component in an assembly |
| `skeleton of` | maximal structural support; anatomical — adapt, don't import |
| `connected to`, `connects` | joined via a connecting structure |
| `attached to` | force on one moves the other |
| `biomechanically related to` | musculoskeletal system *or its analogs* |
| `has muscle origin` / `insertion` | the end that stays / the end that moves; adapt as fixed/driven attachment |
| `has quality` | actual, present characteristic |
| `has disposition` | potential, realized only in process |
| `has function` | the disposition the part exists for |
| `has role` | conferred from outside; e.g. kinematic root |
| `realizable has basis in` | structural ground of a disposition |
| `realizes` / `realized in` | process ↔ disposition |
| `determined by` | part whose removal collapses the system |
| `occurs in` | process located in a material entity; transitive over `part of` |
| `immersed in` | surrounded by a fluid substance |
| `has participant`, `has input`, `has output` | process ↔ continuant |
| `starts with`, `ends with`, `happens during` | Allen-style process composition |
| `causally upstream of` | one process precedes and affects another |

RO also ships composition laws worth imitating: `occurs in` is transitive
over `part of`, and `has participant` holds over the chain `has part` +
`has participant` — if the wheel participates in the rolling and the wheel
is part of the bike, the bike participates too. These chains are one-way
implications, and their order matters: R₁(x,y) ∧ R₂(y,z) ⟹ R(x,z) says
nothing about the converse. A relation vocabulary that carries its own
inference rules is doing more than naming.

It is worth not overstating the analogy, though. A property chain derives
another *proposition*; forward kinematics computes a *transformation*.
Both walk paths through structured relations, and one vocabulary might
organize both, but logical closure and numerical evaluation are not the
same operation and should not be quietly identified.
