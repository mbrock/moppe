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
at stand edges. The world plan now plants at five-metre spacing, with broad
jitter inside each cell, and the conifer crown radius is 0.23 of height. This
keeps roughly 74,000 stable individuals in the reference world while making
their near-field occlusion credible.

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

The reference population occupies 16.3 percent of the world field. Within
occupied texels its median closure is 0.325, its ninetieth percentile is
0.573, and its maximum is 0.763. These are useful population diagnostics,
not image targets: changing a threshold to improve them without improving the
ride would be reward hacking.

The renderer turns that field into eight-metre canopy cells, grouped into
three-by-three mesh patches. A cell has a slightly irregular roof and emits
side geometry only where its neighbour has less closure or a lower roof. The
representation is finite, world-anchored, and rendered without depth writes
after the undergrowth, so its translucent coverage cannot become a dark
infinite sheet or erase nearer organisms. Its work window is radial only as a
cost bound; frustum culling and the representation decision use the actual
terrain height and three-dimensional camera distance. Walking, riding, and
flying therefore ask the same geometric question.

The handoff is expressed in crown pixels, not tree height or camera altitude.
The aggregate begins while a mean crown is twelve pixels wide and is mostly
present by four to five pixels. Each individual retires, with seed-staggered
thresholds, over the same four-to-five-pixel interval. The overlap preserves
coverage while the image can still resolve the individual; below it the stand
field owns the population instead of a grid of subpixel conifers.

The moment field also replaced the obsolete habitat-cover reading in terrain
and undergrowth shading. The canopy roof, forest floor, blades, ferns, and
flowers now consume the closure produced by the same retained crowns.
Understorey light is a bounded Beer--Lambert-style power of the open fraction,
so a closed stand is sparse beneath while actual gaps remain occupiable.

## What remains

- Inspect the dense individual pass in a GPU trace and make front-to-back
  rejection deliberate if overdraw remains dominant.
- Replace the coarse roof's remaining cell-scale faceting with richer
  stand-shape and lighting moments without reintroducing per-tree identity.
- Remove the last visible planting correlations; wider jitter reduced the
  rows, but a jittered lattice is not a blue-noise population.
- Judge longer walking, riding, and gliding sequences. The short aerial strip
  proves that the field is world-anchored, but not that every transition is
  yet perceptually quiet.
- Give hero trunks roots, base flare, litter, and local ground agreement. That
  is a near-field forest-floor problem, not an excuse to retain distant trees.

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
