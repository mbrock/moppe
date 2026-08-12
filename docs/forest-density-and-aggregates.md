# Forest density and aggregates: the road to "it feels like a forest"

Status: implementation checkpoint, August 2026. The individual-tree system
and its handoff to the stand quotient are recorded in
[forest LOD](forest-lod.md). The reference image remains the golden-hour
spruce mockup (dense canopy, long shadows, sunbeams, dark interior); the
gazetteer's forest-sunward / forest-shadowplay / forest-interior studies are
the standing comparison views.

## The thesis: density is an unlock, not a cost

The failure mode was an open planting lattice in which every tree presented
as an individual silhouette: "a bunch of spiny trees", not a forest. A spruce
forest at any distance is mostly occlusion: a closed canopy surface on
hillsides, dark interior gloom between trunks up close, individuals legible
at stand edges. The world plan now draws a deterministic uniform proposal
stream over the toroidal world and priority-thins it with a two-metre
hard-core. There is no final planting lattice. High-suitability habitat is
dense enough to close while marginal woodland retains approximately its old
spacing; the conifer crown radius is 0.23 of height. The v9 smoke reference
contains 98,311 stable individuals.

Density also changes what the far field is allowed to be. A sparse stand
seen from 500 m is still "several distinguishable individuals", and only
per-tree geometry renders that honestly. A *closed* canopy seen from 500 m
is statistically uniform -- a bumpy dark-green surface with known
roughness and lighting -- and can be rendered as essentially one thing at
a cost proportional to pixels covered, not trees represented. Density is
the prerequisite that makes aggregate rendering truthful: with gaps a blob
lies; with closure the blob is the truth. This is Bruneton's forest
decomposition (individuals near, statistical aggregate far), and the same
conclusion Nanite foliage reaches from the other side: below the
resolvable scale, geometry should stop being triangles and become density
(their voxel bricks). Our per-tree crown proxy is a first crude step; the
destination is stand-level.

Occlusion is also perceptual masking for the LOD system: in a closed stand
most of every tree is hidden, so organ arrivals happen behind foliage instead
of against open sky.

## The implemented stand quotient

`MetalRenderer::set_forest` rasterizes the actual retained `ForestInstance`
population into one periodic RGBA moment field. It is not a second forest
guessed from the habitat texture:

- R is optical closure, accumulated as crown area and converted with
  `1 - exp(-depth)`;
- G is optical-depth-weighted mean crown height;
- B is upper crown height; and
- A is optical-depth-weighted moisture.

The v9 fast-profile seed-123 population contains 102,047 individuals and
occupies 13.3 percent of the 1024-square world field. Its whole-field mean
closure is 0.058; within occupied texels the median is 0.463, the ninetieth
percentile is 0.795, and the maximum is 0.990. The smaller smoke reference has
98,311 individuals. These are population diagnostics, not image targets:
changing a threshold to improve them without improving the ride would be
reward hacking.

The renderer now retains a second RGBA field of vertical optical depth. Every
tree is splatted independently into four height strata. Each stratum has its
own normalized horizontal footprint -- broad low boughs through a narrow
leader -- and the four area weights sum to one tree's original projected crown
area. The logged seed-123 field has zero saturated strata, so texture clamping
does not conceal a conservation failure.

The mesh stage reads this volume through a nested four- and eight-metre world
lattice. One meshlet carries a 24-metre patch and one height stratum; patch
ownership is only a work partition. While a four-metre cell crosses from 3.5
to 2 scene pixels, all thirty-six children and all nine parents coexist in the
same meshlet and divide one optical-depth claim. Outside that interval only the
live partition is rasterized. Both levels explicitly sample the retained
moment and density mip chains at their own footprint. This is a small
projected-error hierarchy over the actual population, not more distant tree
objects and not a camera-centred density ring.

Each cell is a soft camera-facing ellipsoid section and receives deterministic
world-cell jitter. The previous camera orientation is used for its previous
position, so temporal reconstruction sees billboard rotation rather than an
unexplained appearance change. Stable object-stage compaction preserves local
input-grid order for translucent patches. Four independently placed height
bands give real parallax. This replaced both the old checkerboard of
independent solids and the later connected-roof prototype, which remained a
dark horizontal shelf from a glider. Fine closure remains available to the
fragment. The representation is finite, world-anchored, and rendered without
depth writes after explicit individuals, so it fills population gaps behind
them without erasing nearer organisms or becoming an infinite sheet.

The handoff is expressed in crown pixels, not tree height or camera altitude.
The aggregate and individual transfer share the mean crown's
eight-to-thirty-two-pixel interval. A separate 24-metre closure sample decides
whether the local population is stand-like at all: sparse woodland retains
individuals, while closed canopy continuously contracts explicit foliage
toward the crown top as the field takes ownership. Fine closure controls
coverage in both cases. Actual terrain height and three-dimensional distance
drive the decision; horizontal distance only bounds work, so walking, riding,
jumping, and flying do not receive different hidden rules.

Resolved individuals use the same rule before GPU dispatch. The renderer
conservatively tests the whole organism against the actual world-to-clip
matrix, rejects crowns that cannot reach the earliest four-pixel retirement
threshold, and submits the survivors in front-to-back depth bins. The GPU
still owns seeded retirement and organ detail. Population density therefore
no longer implies one object threadgroup for every tree in the world on every
frame, and the optimization makes no assumption about camera height or a
ground horizon.

The moment field also replaced the obsolete habitat-cover reading in terrain
and undergrowth shading. The stand volume, forest floor, blades, ferns, and
flowers now consume the closure produced by the same retained crowns.
Understorey light is a bounded Beer--Lambert-style power of the open fraction,
so a closed stand is sparse beneath while actual gaps remain occupiable.

## What remains

- Inspect the surviving dense individual pass in a GPU trace. Conservative
  candidate filtering and front-to-back submission are now deliberate, but
  the ordinary temporal benchmark still attributes 11.1703 ms median to the
  forest block and the all-features frame is not within 60 Hz.
- The spatial hierarchy is deliberately only 4-to-8 metres. A trial 16-metre
  rung was rejected because the current 2.4 km reach still presents an
  eight-metre carrier at about 2.4 scene pixels. Add another spatial parent
  only when a longer reach or smaller scene makes that parent necessary.
  Likewise, replace the four fixed height strata with an error-selected
  vertical hierarchy only when motion shows their separation becoming a real
  problem. This is inspired by voxel foliage's quotient, not a claim to be a
  general voxel renderer. Do not reintroduce distant individual spikes to hide
  a weak aggregate.
- Judge longer walking, jumping, and player-controlled gliding sequences. The
  current proof includes both a 90-frame actual forest ride and a 120-frame,
  42 m/s translating aerial view with glare effects disabled. With the
  profiler extended from its old grass-scale 200-metre ceiling to 2.6 km, the
  hierarchy and fixed-lattice reference both peak at 0.0095
  motion-compensated residual near 266 metres. The hierarchy adds no event
  spike at its transition; its sparse-event fraction is 0.00594 versus 0.00583
  at 0.9--1.4 km and 0.00441 versus 0.00414 at 1.4--2.0 km. Those two paths
  still cannot certify all play.
- Give hero trunks taper, roots, base flare, litter, and local ground
  agreement. Bark is now visible under indirect light and hero branchlets no
  longer form metre-wide shelves, but the near tree is still deliberately
  unfinished.
- Attribute the positive forest-understorey interaction in a pass-timed trace;
  the ordinary 32-case run shows it, but cannot say which shared scene work is
  responsible.

The broadleaf form was removed from the world (the blob-lollipop
placeholder had received none of the conifer's assembly work); it returns
only after getting the same treatment, or as part of the aggregate.

## Grass: a completed representation proof, not a forest template

The grass continuation now proves the specific quotient described by
[RFC-0006](../planning/rfcs/0006-a-continuous-grass-medium.md): resolved blades,
a ray-integrated terrain-following density column, and a bounded far optical
response consume one leaf-area claim. The result did not come from extending
blade distance or scattering larger plants. It came from preserving the
population's projected coverage, vertical path, lighting moments, texture
spectrum, and broad gust while individual identity became unrepeatable.

That proof sharpens the forest plan without supplying its implementation. A
grass column is shallow, terrain-bound, and dominated by a continuous leaf
distribution. A spruce stand has crowns, gaps, trunks, a high canopy roof, and
an occupiable dark interior. Its quotient therefore needs a height interval
and at least two spatial claims -- canopy occupancy above and navigable
understorey below -- not a renamed grass shader.

The reusable discipline is narrower:

- decide which population measure is conserved before choosing geometry;
- let projected error decide when individual structure is repeatable;
- make the coarse representation reproduce the fine representation's mean,
  coverage, variance, and broad motion;
- anchor every field in the world, never the camera; and
- use a finite aggregate path rather than an infinite dark sheet.

The forest checkpoint follows that discipline: a closed canopy
height/occupancy field replaces only the unresolvable crown population,
preserves gaps and stand edges, and leaves near trunks and understorey
explicitly navigable. It does not make the old per-tree proxy band larger or
scatter decorative tufts into the distance.

## Trees influence the surface: analytic ground deformation

Big trees should raise a root bulge in the ground itself. The terrain's
5 m height lattice cannot store a 1-2 m feature, but that lattice is an
*information* budget, not a vertex budget: the near field already renders
a subdivided lattice (`mesh_coord` finer than `grid_coord`), with
Catmull-Rom reconstruction deliberately clamped so interpolation cannot
invent features the physics heightmap does not know. That faithfulness
principle -- the rendered ground never shows what the wheel cannot feel --
is the constraint to preserve, and an analytic bulge preserves it by
lifting the single source of truth from shared *data* to a shared
*function*:

    ground height = heightmap + sum of nearby root bulges,

where each bulge is a pure smooth radial function of tree position, age,
and distance to trunk -- data both sides already have. The terrain vertex
shader adds the term on the subdivided near field; the CPU height sampler
adds the identical term for physics. The bike then genuinely rides over an
ancient spruce's root plate, the walker's feet plant on it, and no texture
resolution was ever involved. Agreement by construction, the same move as
the typed quantities.

Required plumbing, none of it deep:

- A coarse spatial bin of trees per terrain tile so a vertex evaluates a
  handful of bulge terms, not twelve thousand (the forest instances are
  already resident on the GPU).
- The same query in the CPU surface sampler.
- A fade coherent with the terrain LOD rings, so the bulge appears with
  the subdivision that can express it and never pops a ring boundary.
- A cost check in the terrain vertex shader, which is currently cheap.

Complements, not prerequisites: a trunk-base flare in the wood organ
(a few vertices in `forest.metal`, hero tier only) and needle-litter /
moss darkening of the ground under canopy through the surface
presentation lanes -- the bulge reads as roots when the ground around it
reads as forest floor.

## If the mesh stage becomes the wall

The pure generate-every-frame position pays full price on every still
frame. The hybrid worth prototyping before any retreat to baked meshes:
cache a tree's generated meshlets and regenerate only when its bough count
changes. It keeps every visible property of the continuous-LOD design
while cutting steady-state cost to nearly nothing, at the price of
reintroducing retained state and its invalidation. Decide from a GPU
trace, not from intuition.
