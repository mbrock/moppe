# Near and far

*Notes on levels of detail as movement through a lattice of domains,
the camera as a focus, and the two kinds of population.*

Companion to `multitudes.md`, which proposes population domains, and
to `computed-together.md`, which walks the lattice of partitions on
one rank of the world. This essay is about the vertical direction:
the same world seen at many granularities, and why the machinery that
does this — terrain LOD, forest imposters, mip chains — works at all.

## Domains as objects, maps as the structure

`multitudes.md` notes that dynamic domains force equality to become
identity-plus-epoch rather than structural comparison. That is not a
concession; it is a promotion. Once domains are identities, the
interesting structure is no longer inside any one domain but in the
**maps between them**, and the codebase already uses four kinds
without naming them:

- **Quotients** — coarsenings. A mip level, a consolidated report, a
  half-resolution physics grid. Ellerman's partition lattice and
  Smith's granular partitions are both taxonomies of this one
  relation: which partitions refine which.
- **Inclusions** — subdomains. The boundary mask, a slice of sites, a
  loaded region.
- **Memberships** — the stored projection from a field domain into a
  population: the `WaterBodyId` column is a map from terrain cells to
  lakes, kept as data.
- **Embeddings** — a domain placed into another's space: the hex
  sheet into the world, an organism's tree into its trunk.

Bundles are sections assigned to these objects, and the verbs
collected across the storage work are functorial along the maps:
`join` and `slice` act within one domain; **pushforward** along a
quotient is aggregation (a mip chain is a quotient tower whose
pushforward preserves the measure); **pullback** along an inclusion
is restriction. A "level of detail" is then simply a floor of a
quotient tower, and the tower is a chain in the partition lattice.

## Why level-of-detail works, and when it doesn't

Rendering the lattice with dynamically shifting refinement feels
miraculous because it quietly satisfies a strong condition: where two
regions sampled at different floors meet, their shared boundary must
agree. Terrain renderers enforce this empirically — skirt geometry,
stitched index patterns, morph regions — and every T-junction crack
is the same bug: **the gluing condition failed**. The sheaf-theoretic
reading is not decoration; it says exactly what data a refinement
scheme owes at each seam, which is why clipmap papers keep
rediscovering the same fixes. The domain-of-domains picture makes the
obligation legible before the artifact appears: choosing floors
per-region is free; the seams between choices carry proof burden.

## The camera is a focus

`BundleFocus` is already the comonadic `extract`: a field with a
distinguished site. The camera is a focus in the world's domain, and
level of detail is a **granularity assignment derived from the
focus** — every region told which floor of the tower to be sampled
from, as a function of its relation to the point of view. Rendering
is then a report (a homomorphic, effect-free projection of the books)
taken along a focus-dependent quotient. That the assignment changes
every frame while the seams keep their promises is the extraordinary
part — the same cells, aimed anew sixty times a second, at sixty
different quotients.

## The far end changes register, not just resolution

The deepest property of the forest's LOD is not that distant trees
are cheaper. It is that the far forest is **not a population at
all** — it is a field, a green density evaluated in a shader, while
the near forest is individual organisms. Distance moves the world
along the fiat-projection axis of `ontology.md`: nearby, fields
resolve into objects; far away, objects relax back into the fields
they were carved from. The forest already implements both registers
(tree organisms and the hash-based covering); the tower picture says
they are two floors of one structure with the camera choosing the
floor, and the projection between them is the same analysis that
placed the trees from the cover field in the first place — run in
either direction.

Stars generalize this exactly. A handful of stars is a population;
a hundred thousand near the camera is an instanced population with a
spatial index; the same hundred thousand at the horizon is a
luminosity field. A galaxy is the far LOD of a star population —
which is not a metaphor but a description of the night sky.

## Two kinds of population

Lakes show that populations come in two ontological kinds, and the
difference is where identity lives.

**Resident populations** — stars, trees, vehicles — are substances.
They are born into the world, carry their own identity (a
generational handle), and relate to the fields by *location*: a
query, not a link.

**Projected populations** — lakes, rivers, someday named mountains —
are fiat objects carved out of fields. They exist as a **fibered
pair**: a handle-valued field (the `WaterBodyId` cell column) and a
population of wholes (the census rows), with the projection stored as
data. `census_lakes` is π₀ — connected components, the coarsest
partition of the flooded region — which is to say the population *is*
a quotient object of the field, one more inhabitant of the domain
lattice.

Projected populations have the hard genidentity problem. A star is
the same star because its handle says so. A lake is the same lake
only by a tracking judgment across re-analysis: after another
geological step the flood is recomputed, the components re-found, and
nothing but overlap connects yesterday's lake to today's. Terrain
Lab's time-scrubbing will meet this problem the moment it wants a
lake to keep its name while the eons run. The honest shape of the
solution is known from segment tracking: match components across
steps by overlap in the base domain, and let the population's account
book record merges and splits as first-class events — a lake that
divides is a posting, not an identity crisis.

## Guardrails

The category of domains should be discovered, not built. No abstract
`DomainMorphism` framework: the fibered pair earns a name when water
bodies get their population domain; the quotient tower earns one when
two floors genuinely share code (mips and readings consolidation are
the candidates); the gluing obligation earns one when a second
renderer feature needs seams (the forest's chunk boundaries are
next). The camera-comonad reading is a way of seeing, not an API to
be introduced. What the essay commits to is only the direction of
regard: when near and far disagree about what a thing is, that is not
a hack to hide but the world changing floors — and the projection
between floors is real work with a real name, exactly as
`ontology.md` said of fields and objects.
