# Terrain expressions

Moppe's terrain-expression subsystem constructs scalar fields as immutable
runtime values.  It is shared by the game, Terrain Lab, tests, and command-line
previews; it does not depend on the renderer or a platform graphics API.

## Semantic graph

`moppe::terrain::ScalarField` is a small handle to a `std::variant` expression
node.  Child links are `std::shared_ptr<const Node>`, so reusing a field creates
a DAG without copying raster data.  The graph is acyclic and reference counted,
so it does not need tracing garbage collection.

The current nodes cover constants, coordinates, arithmetic, fused
multiply-add, sine, smoothstep, Perlin noise, fractal Brownian motion, and
ridged noise.  Seeds and fractal parameters are explicit data in the graph.
`MultiplyAdd` is a semantic node rather than an optimizer detail because the
historical generator relied on fused floating-point results.
The Perlin shuffle also uses an explicit unbiased sampler, preserving the old
macOS sequence without depending on a standard library's distribution policy.

`CpuEvaluator` lowers unique nodes to a topologically ordered scalar register
program, then runs that program for every point in a `Domain2D`.  A later MSL,
WGSL, SPIR-V, or other backend can traverse the same node variants.

## The geological recipe

`make_geological_fields()` builds the game's pre-erosion terrain as named,
shared fields:

```text
coordinates -> two warp fields -> warped coordinates
                                  |-> continent -> mountain mask
                                  |-> plains
                                  `-> ridged mountains

continent + plains + mountains + mask -> combined terrain
```

`RandomHeightMap::randomize_geologically()` now evaluates this graph and
normalizes the resulting raster.  That method is used both by normal world
generation and by Terrain Lab, so the two paths cannot drift.  Golden tests
lock all seven inspectable layers to the previous generator's output for the
reference seed.

Hydraulic and thermal erosion still operate on the materialized
`RandomHeightMap`.  This is the intentional migration boundary: algebraic
field composition is lazy, while normalization and stateful simulation are
explicit raster barriers.

## Tests and visual feedback

Run the unit suite with:

```sh
ctest --test-dir build --output-on-failure
```

The preview tool uses the same field recipes and CPU evaluator:

```sh
./build/terrain-field-demo /tmp/combined.png 512 combined 123
./build/terrain-field-demo /tmp/mountains.png 512 mountains 123
./build/terrain-field-demo /tmp/mask.png 512 mask 123
```

Available geological preset IDs are `combined`, `continent`, `plains`,
`mountains`, `mask`, `warp-x`, and `warp-y`; `waves` is a small algebra demo.
The output is a normalized grayscale PNG, which gives tools and scripts a fast
visual checkpoint without launching the game.

## Migration direction

- Turn a geological recipe plus explicit parameters into a first-class value
  that Terrain Lab can retain, inspect, and edit.
- Move stateless height transforms into graph nodes only when an experiment
  needs them.
- Model normalization, erosion, and similar whole-raster operations as an
  ordered pipeline of explicit materialization stages.
- Add a small stable command/script surface over those recipe and pipeline
  values instead of exposing renderer UI actions.
- Add source or compute backends alongside `CpuEvaluator`; keep Metal, Vulkan,
  WebGPU, and other API types out of the semantic graph.

This remains a terrain system rather than a general tensor library.  New
operations should arrive in small, tested vertical slices that are immediately
usable from both Terrain Lab and the noninteractive preview path.
