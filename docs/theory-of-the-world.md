# Theory of the world

This document is the conceptual model underneath the terrain
code. Nothing here is required to compile; all of it is required to
make good decisions.

## The world is a torus

The generated random landscape is a flat torus: finite, unbounded,
locally ordinary Euclidean space with gravity pointing down, closed by
identifying opposite edges. City and Pico modes remain explicitly
bounded exceptions. Nothing bends; only neighborliness changed.
Consequences recur everywhere: noise must be periodic (integer wave counts —
wavelengths must divide the world); there are no boundary cells, so
nothing may be seeded, clamped, or special-cased "at the edge";
periodic position differences should use minimum-image deltas
(`topology.hh`); physics keeps *unwrapped* coordinates (the universal
cover) and wraps at terrain lookup—the ring-buffer discipline. This
makes winding numbers possible, although the game does not yet expose
a general circumnavigation reading. A useful law: on a torus, every
operator should commute with translation. Symmetry
violations are how residual edge-thinking announces itself.

## Two authors, and a referee

Terrain is the current score in an argument between construction and
destruction. **Uplift** (tectonics, in our world the geological recipe
and any painted uplift fields) proposes relief; **water** (erosion in
all its forms) disposes of it; gravity referees. Raw noise is
statistically fair and therefore dead — nothing has ever happened to
it. Erosion is what installs *history*: channels are a rich-get-richer
instability (flow attracts flow), and the resulting dendritic
structure is why eroded terrain reads as alive. The eye is a detector
of exactly this. Most of what Alexander called the fifteen properties
of living structure (levels of scale, strong centers, boundaries,
gradients, echoes, roughness…) are produced wholesale by drainage; the
few it cannot produce are the job of the future human layer described
in `second-author.md`.

## The world has two surfaces

The rock `z` and the standing water `w`. Water has two behaviors: it
**runs** on slopes and it **pools** in hollows, rising until it finds
the basin rim's lowest saddle and spilling there. The drainage graph
(per-cell receivers) is a theory of the *dry* surface only; a **sink**
is a cell where "which way downhill" has no answer. Local carving can
accidentally add sinks; pooling represents them without pretending the
rock surface drains. The `FloodField` computes `w` (priority-flood from
sea level); lakes are where `w > z`. The current census records a
deterministic spill candidate per water body, but exact per-basin spill
identity remains a frontier contract. Such a spill—once routing
crosses lakes—carries the basin's entire discharge and is therefore
the most important cell of its catchment. Sinks are not
errors: a young landscape is legitimately riddled with them (compare
post-glacial Scandinavia), and the world's sink count over erosion
time is a biography — rising through adolescence, cresting, falling as
water is allowed to finish its arguments, converging not to zero but
to the number of inland seas the world honestly keeps.

## Erosion is the propagation of news

Base-level changes travel *upstream* along rivers as waves
(knickpoints), fast on trunks, slow on tributaries; terrain beyond the
wavefront still remembers its initial state. Every erosion method is a
postal system, and every lifetime/step/batch cap is a
**speed-of-information limit**. The two method families have
complementary blind spots. *Droplet simulation* (Lagrangian; our
`WATER EROSION` transform) excels at mid-story texture but each drop
is alone — it never feels confluence, so discharge doesn't scale, and
its lifetime bounds its reach; lakes coupled into erosion act as
relays (drop dies at inlet, authority resumes at the outlet). *Stream
power / analytical methods* (Eulerian; Braun–Willett, FastFlow,
Tzathas et al.) get global structure right — graded concave profiles
(slope ∝ area^−θ), accordant junctions, an honest AGE parameter — but
degenerate at ridges where drainage area vanishes, needing
hillslope/thermal terms. The long-run architecture is a sequence, not
a choice: analytic bones, flood-field truth maintained throughout,
conservation-closed droplets as skin.

## Operator kinds, and two columns of dials

Every operation belongs to a kind, and the kinds are load-bearing (see
`program.hh`): **field expressions** (pointwise, timeless, evaluable
anywhere — the applicative layer), **neighborhood operators**
(stencils, bounded support), **evolution operators**
(history-dependent raster transitions — erosion, the monadic layer),
and **readings** (terrain → numbers/overlays — measurement, never
mutation: *readings color the surface; geometry stays
terrain*). Distinguish two kinds of parameters and typeset them
differently in UI and docs: **world dials** (age, uplift, sea level,
erodibility, permanence policy — facts about the world) and **numerics
dials** (droplet count, batch size, lifetime — facts about how hard we
squint). The project's history is the migration of dials from the
second column to the first.

## Conservation is constitutional

Every gram eroded must be deposited, exported, or explicitly residual;
the sediment ledger is displayed, and it balances (double-entry
thinking pervades the project: ring buffers, unwrapped odometry,
sediment, and someday economies are all the same
bookkeeping). Deposition is half the physics, not decoration — fans,
valley fills, deltas where moving water meets still, ponds silting
into flats. A leaky ledger starves all of it.

## Scale is nesting, not extent

The felt size of the world comes from levels of structure discovered
in sequence — regions containing places containing spots — and from
terrain that can say *no* (slopes, water, boundaries create places by
making routes matter). Speed is a world-shrinking machine;
time-to-traverse is the currency in which size is paid. This is why
hydrological honesty is gameplay: lakes are boundaries, spill points
are gates, valleys are routes, and an eventual road/settlement layer
can convert drainage structure into human meaning.
