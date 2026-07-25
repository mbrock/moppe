# Surrounded

Most of what Moppe asks the player to do is about being somewhere and
being held by something. Ride, dismount, walk through a door, wade into
the shallows, deploy the wing, drop the bike, come down again. The
positions and velocities are well described already. What is not
described is the other half — what is around you, what holds you there,
and what that lets you do.

There is a good vocabulary for this, in Smith and Varzi's work on niches
and environments. This note tries it on. It proposes nothing; it is an
attempt to say what the game's verbs are already about.


## Tenant, medium, retainer

A niche, in their account, is not an abstract space of tolerable
conditions but a concrete thing with a three-part geometry. Their example
is a bear in a cave:

> In the center of this structure is the bear itself, which, by displacing
> air, at one and the same time creates and occupies a central hole in its
> niche — a hole that is precisely the right size and shape to be occupied
> by this very bear. As the bear moves, the hole moves too… Surrounding
> and supporting this medium is an enclosing structure, or what we shall
> call a **retainer**.

So: the **tenant** occupies a hole; the **medium** is the stuff around it,
which is "the carrier of the various properties" that make the place
liveable — air, water, temperature, light; and the **retainer** is the
structure that keeps those properties in range.

Two clarifications from the paper are worth keeping. A medium is medium
only relative to a kind of tenant — water is medium for a fish and not for
us. And there is an operational test:

> It seems to be a characteristic feature of such cases that the gap left
> by the tenant… is filled immediately by the surrounding medium… If Luigi
> is buried in a hole filled with concrete, the latter is not a medium…
> Luigi, accordingly, is not in a niche.

Something you can move through is a medium. Something you are set into is
not. Air and water qualify; rock and a wall do not.


## The rider, named

The paper states our case directly:

> The niche of the driver of a car is the interior of the car, the niche of
> the car itself is the road along which the car is driving. The niche of
> the astronaut is the interior of her spacesuit, the niche of the
> astronaut-plus-spacesuit is the interior of the spaceship, the niche of
> the spaceship is the relevant region of space.

Niches nest, and each level controls a different property of its medium.
For us: the rider's niche is the bike's seat and cockpit; the bike's niche
is the terrain it rides; the glider's niche is the air. When the bike
hangs tethered under the wing, the compound has the air as its niche while
the rider still has the bike.

And the constraint that comes with it is a modelling instruction, not a
philosophical nicety. The authors anticipate the objection that a
spaceship would then be both a niche and a tenant, and answer that these
are **different entities**: on one hand the interior including its inner
surfaces, on the other the whole ship including its outer walls. So
bike-as-shelter-for-a-rider and bike-as-tenant-of-the-terrain are two
things, not one thing in two roles. A model that conflates them will keep
producing questions with no good answer, of the form "is the rider inside
the thing that is on the ground?"


## How much is holding you

Niches are classified by how much of their boundary is real retainer and
how much is fiat:

1. fully bounded by a retainer — an egg, a submarine, **a car**;
2. mostly bounded — a nest, a kangaroo pouch, **a cabriolet**;
3. bounded by a partial retainer offering little protection — **by a
   floor**, for example, or a single wall;
4. no retainer at all — a bubble-like zone, like the water around a fish.

A motorcycle rider is squarely class 3: a seat and a road beneath, and
otherwise open air. A car driver is class 1. A hang-glider pilot is class
3 or 4 depending on how much the wing counts as shelter. That is a real
difference and the game already expresses it — the helmet camera, the
wind, the way a crash throws you — without having a word for it.

There is also a structural law worth knowing:

> class 4 niches can only be stationary: in the absence of a retainer
> there is no mechanism whereby the medium would follow the tenant when
> the latter moves from place to place.

With one exception, which is the interesting case: the personal-space
bubble, which *is* "carried around with you when you move," and which they
suggest every waking organism always has. So a rider at speed has two
things around them at once — a class-3 niche made of seat and road, and a
carried fiat bubble that moves with them.


## En route is a third state

> Organisms need not be in a niche at every moment of their existence.
> Rather, they may be **en route** from one niche to another… A salmon
> swimming upwards in a waterfall is not in a niche; rather it is striving
> to locate and to move into a new one.

This is the observation with the most immediate purchase, because the game
has a great deal of en route and no name for it. Airborne on a jump is
not being in a niche; nor is the moment between releasing the wing and
landing, nor a fall, nor a crash. Being mounted, being on foot, and being
in flight are not three symmetric modes: two are tenancies and one is a
transition between them.

That reframes the mode enum. `M_BIKE`, `M_FOOT`, and `M_CAR` name which
niche the player currently inhabits. `M_GLIDER` is closer to a sustained
transit — a controlled fall between one tenancy and the next, which is
exactly why it feels different to play.


## Retainers are affordances, and we already test for them

> The theory of retainers may therefore be seen as part of the theory of
> surface layout in J. J. Gibson's sense: **retainers are affordances**.

> A physical surface is part of the relevant niche-retainer only if it is
> relevant to the behavioral and survival patterns of the tenant.

Read the session's predicates in that light and they stop looking like ad
hoc rules. `can_deploy_glider` asks whether the bike is airborne and more
than three metres above the ground — that is a test for whether the air
here affords deployment. `can_drop_bike` asks whether there is still a
bike to release. Mounting requires the walker to be within five metres of
the bike, and dismounting places them a metre or two to the side: both are
statements about reachable and habitable places relative to a vehicle.

They are affordance tests, and naming them so suggests they belong
together as a family — one vocabulary of *what this place affords this
kind of tenant* — rather than as scattered booleans on a session.

The clearest existing case is the doorway. `Door::in_doorway` has a
walkable aperture of 1.1 metres, and the comment says exactly what makes
it interesting: **people fit through, motorcycles do not**. That is a
niche boundary defined relative to the kind of tenant, which is precisely
Smith and Varzi's point about media and retainers being organism-relative.
The wall is retainer for a bike and aperture for a walker, and the same
geometry answers differently depending on who asks.

Wading is another. The walker slows from six metres per second to two on
entering shallow water, and the bike drowns. The water is medium for
neither of them, but it is a differently costly medium for each. RO would
call the deep case `immersed in`: "wholly or substantially surrounded by a
fluid substance."


## Zones around moving things

Their maritime section reads like a specification for proximity logic:

> A **guard zone** is a zone of some selected geometry around a ship… The
> default guard zone is **elliptical, with the ship off-center because
> there is need for a larger guard zone in front of the ship in the
> direction of movement**. The function of a guard zone is to give a
> warning signal when a target crosses this boundary… When a ship has
> crossed the guard zone the captain can choose to set up another zone,
> this time around the plotted target ship, called the **collision zone**.
> This zone will follow the plotted ship dynamically as it is tracked.

A forward-biased ellipse scaled by speed is the right shape for anything
that moves and must not hit things — which is to say, for a bike at speed,
for city traffic if it ever exists, and for wildlife deciding when to
flee. The two-stage structure is the good part: a zone attached to *you*
that raises the alarm, and then a second zone attached to *the thing you
noticed*, tracking it. That is a cleaner account than a single radius,
and it is what a chase camera, a flee reflex, and a collision warning all
separately want.

They also sketch what happens when niches meet — fusion, overlap, and
symmetric or asymmetric deformation. Two riders on a narrow trail deform
each other's space asymmetrically; a herd's members' spaces fuse. Worth
remembering when there is more than one moving thing in the world.


## Felicitous and critical

> A token niche is **felicitous** if, for every dimension of the relevant
> hypervolume, the relevant variables are within the threshold values;
> otherwise it is **critical**.

Health, drowning, and the boost reserve are all this: a niche is felicitous
while its variables sit inside their ranges, and critical when one leaves.
It gives a single frame for conditions currently tracked as unrelated
scalars, and it puts them where they belong — as facts about the relation
between a tenant and its surroundings, rather than as properties of the
tenant alone.


## Boundaries that fade

Finally, the honest note about where niches end:

> there will standardly be no sharp line which constitutes a single (fiat)
> boundary of the niche in question. Rather, it may be that we have to
> deal with **families of nested regions which form dense concentric
> clusters**, each of which might at any given time qualify (perhaps to
> variable degrees) as the location of the relevant token niche.

Which is the same shape as everything else vague in this world: trail
influence, home-base influence, forest cover, the LOD morph band. Not one
boundary but a graded family. The game is already good at this; the
vocabulary just says why it is the right way to be good at it.

There is also a warning against reading containment geometrically. Smith
and Varzi list "the fish is in the river", "the river is in the valley",
"the car is in the garage", "the fetus is in the cavity in the uterine
lining", and conclude that these cannot be captured by any simple
geometrical reading of 'in' — "even geometry plus topology will allow us
to do justice to only some of the distinctions involved." Moppe has its
own list: in the water, in the city, in a doorway, on the bike, in the
home-base area, inside a lake's catchment. They are not one relation and
a single containment predicate would flatten them.

Donnelly and Smith's *Layers* makes the same point structurally. Objects
belong to strata which never mereologically combine, while their regions
nest freely, and the implication runs one way only: being part of
something puts you inside its region, but being inside a region says
nothing about being part of anything. That is the formal version of the
rule that a bounding box is not a whole.


## What this would be for

Nothing here needs building. What it offers is a way to talk about the
half of the game that the physics vocabulary misses: not where the rider
is, but what is around the rider, what holds them there, what that affords,
and when they are between places rather than in one.

If any of it became code, the first piece would be the smallest: gather
the affordance tests into one family with a shared shape, so that "can
this tenant do this here" is asked the same way everywhere — of doorways,
of deployable air, of mountable vehicles, of water shallow enough to wade.


## Sources

- Barry Smith and Achille Varzi, *Environmental Metaphysics* (2001) and
  *Surrounding Space: The Ontology of Organism-Environment Relations*
  (2002) — tenant, medium, retainer; niche classes; en route; guard zones.
- Barry Smith, *Toward a Realistic Science of Environments* (2009) — the
  Gibsonian background for affordances.
- Maureen Donnelly and Barry Smith, *Layers: A New Approach to Locating
  Objects in Space* (2003) — strata, coincidence versus overlap, and why
  region containment implies nothing about parthood.
- `docs/ontology.md` for the surrounding vocabulary; `ideas/the-machine.md`
  for the vehicle these niches are built around.
