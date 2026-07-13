# RFC-011: Fields as functions over an explicit domain

- Status: Draft
- Area: semantic foundations (terrain expression language)
- Interacts with: RFC-012, RFC-013; simplifies the evaluators and the
  Metal pipeline cache

## Problem

The field language is *almost* a language of functions, but the domain
is implicit and the consequences leak.  Three symptoms:

1. **Warping is hand-threaded.**  Domain warp is expressed by building
   warped coordinate expressions and feeding them into each noise node's
   x/y children (`make_geological_fields`,
   `moppe/terrain/geological.cc:66`).  The *idea* -- precompose the
   field with a distortion of space -- has no name in the language.
2. **Periodicity is duplicated machinery.**  `FbmNoise` and
   `PeriodicFbmNoise` (likewise ridged) are separate node types with
   separate evaluator cases and separate MSL stitchable functions,
   differing only in whether the sampling lattice wraps
   (`moppe/terrain/field.hh:84`).  Periodic lacunarity is forced to
   `int` by an unstated theorem (octaves must map the torus lattice
   into itself) that the type system cannot currently state.
3. **fBm and ridged are opaque primitives** carrying octaves,
   lacunarity, and gain as baked scalars, when fBm is structurally a
   sum of rescaled single-octave noises and ridged is an accumulating
   fold.  Nothing else in the language can reuse their internal
   structure (e.g., per-octave amplitude fields, erosion-modulated
   octaves).

## Current situation

- `ScalarField` is an untyped DAG; `Field<QS>` layers quantity kinds
  over it (this part is good and stays -- see RFC-012 for extensions).
- The sampling domain arrives from outside as `FieldSamplingGrid2D`
  bounds in the evaluator call; topology (plane vs torus) lives in
  `TerrainGrid`, *outside* the field algebra, while periodic noise
  nodes encode it *inside*.  Two homes, no single truth.
- Both evaluators already effectively compose coordinate programs when
  they lower warped expressions -- the machinery for precomposition
  exists; only the concept is missing.

## Proposal

Commit to the semantics `Field a = Domain -> a` and give the language
three things:

### 1. Explicit domain values

`Domain = Plane | Torus(lattice)` -- the torus as a genuine quotient
R^2 / lattice.  A field is constructed *over* a domain; evaluation
supplies sample points of that domain.  `TerrainGrid.topology` then
*derives from* the field's domain instead of duplicating it.

### 2. One contravariant operator: remap

    remap : (Domain -> Domain) -> Field a -> Field a

with endomorphisms built from a small vocabulary: affine maps
(translate/scale/rotate where the domain allows) and displacement by a
coordinate-kind field -- which is exactly domain warp:

    warp(f, amplitude, w) = remap(x -> x + amplitude * w(x), f)

On the torus, affine maps are checked at construction: scaling must map
the lattice into itself (integer frequency vectors), which turns the
current "periodic lacunarity is an int" convention into an enforced
theorem with an error message.

### 3. Noise as a domain-respecting primitive; fBm as a combinator

One `noise(seed)` primitive whose lattice wraps iff the domain is a
torus (the permutation-table sampling already supports both --
`PerlinTable::noise` takes periods).  Then:

    fbm(seed, octaves, lacunarity, gain) =
      sum_k gain^k * noise(seed_k) . scale(lacunarity^k)   / norm

as a *derived* construction the DAG stores expanded (octaves is small),
and ridged as the analogous accumulating fold -- kept as a single node
initially if the weight-feedback scan proves awkward to expand, without
blocking the rest.

## Consequences

- `PeriodicFbmNoise` / `PeriodicRidgedNoise` and their evaluator and
  MSL cases are deleted; the stitchable vocabulary shrinks while the
  expressible language grows (per-octave modulation, warped single
  octaves, rotation on the plane).
- `make_geological_fields` reads as it is spoken: "warp space, then
  sample continent/plains/mountains over the warped space."
- The Metal pipeline cache keys on smaller, more shareable topologies.
- Cover View / seamless tiling become facts about the domain rather
  than conventions scattered across `wrap_coordinate` call sites.

## Risks and alternatives

- **Golden tests.**  Expanding fBm changes summation order; bit-exact
  parity with the historical generator may not survive.  Options:
  (a) keep the fused nodes as the initial lowering and expand only
  behind new combinators; (b) move goldens to tolerance-based
  comparison plus one archived bit-exact legacy path.  Decide before
  migration, not during (see also RFC-012's note on `MultiplyAdd`).
- Scope discipline: this is a refactor of meaning, not behavior.  Land
  it as a layer over the existing nodes, port `geological.cc`, prove
  image parity, then delete the old spellings.

## Implementation sketch

1. `Domain` value + domain-checked constructors; `remap` node with
   affine + displacement cases; evaluator lowering by coordinate-program
   composition (both backends already do this implicitly).
2. Port the geological recipe; image and golden comparisons via
   `terrain-field-demo` / `terrain-pipeline-demo`.
3. Collapse periodic noise variants; delete dead node types and MSL.
4. Optional: expanded fBm behind a feature flag; measure Metal compile
   and dispatch cost before making it the default lowering.
