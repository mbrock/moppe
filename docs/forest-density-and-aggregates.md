# Forest density and aggregates: the road to "it feels like a forest"

Status: design intent, August 2026. The implemented LOD system this builds
on is recorded in [forest LOD](forest-lod.md). The reference image remains
the golden-hour spruce mockup (dense canopy, long shadows, sunbeams, dark
interior); the gazetteer's forest-sunward / forest-shadowplay /
forest-interior studies are the standing comparison views.

## The thesis: density is an unlock, not a cost

The current stands are open enough to see through, so every tree presents
as an individual silhouette -- the world reads as "a bunch of spiny trees",
not a forest. A real spruce forest at any distance is mostly occlusion: a
closed canopy surface on hillsides, dark interior gloom between trunks up
close, individuals legible only at stand edges.

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

Occlusion is also free perceptual masking for the LOD system: in a closed
stand most of every tree is hidden, so organ arrivals happen behind
foliage instead of against open sky.

## Plan, in order

1. **Close the canopy.** Raise planting density and per-crown fullness in
   the near field; darken crown interiors (cheap ambient-occlusion-like
   shading inside the crown volume -- half of what makes the mockup's
   interior read). Judge against the gazetteer forest studies and the
   mockup, on foot with the P-key series. Near-field hero cost is the
   thing to measure.
2. **Let occlusion pay for it.** Dense stands only stay cheap if early-Z
   kills interior fragments; verify draw order is roughly front-to-back in
   a GPU trace rather than assuming.
3. **The aggregate far field.** Replace the per-tree proxy band on distant
   hillsides with a stand-level representation driven by the forest plan's
   density map: a displaced canopy shell or coarse voxel bricks, lit like
   felt. Past the mid-field this makes tree count free -- a hillside with
   4,000 trees costs the same as one with 400. This is the largest
   remaining structural piece of the forest project.

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

For the forest, the next proof should therefore be a stand-density atelier on
one finished density map: a closed canopy height/occupancy field that replaces
only the unresolvable crown population, preserves gaps and stand edges, and
leaves near trunks and understorey explicitly navigable. It should not begin by
making the existing sparse per-tree proxy band larger or denser.

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
