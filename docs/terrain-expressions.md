# Terrain expressions

Moppe's terrain-expression subsystem is an incremental, portable foundation
for constructing fields as runtime values.  It deliberately starts smaller
than the existing random terrain generator: the first slice supports scalar
constants, coordinates, addition, multiplication, subtraction expressed in
terms of those operations, and sine.

## Semantic graph

`moppe::terrain::ScalarField` is a small immutable handle to a
`std::variant` expression node.  Child links are
`std::shared_ptr<const Node>`, so reusing a field creates a DAG and does not
copy raster data.  The graph contains no cycles and therefore needs no
tracing garbage collector.

The graph is backend-neutral.  `CpuEvaluator` is the first evaluator: it
lowers unique nodes to a topologically ordered scalar register program, then
runs that program for every point in a `Domain2D`.  This avoids reevaluating
shared subexpressions and establishes a straightforward boundary for future
MSL, WGSL, SPIR-V, or other source/code-generation backends.

This is not intended to become a general tensor library.  New operations
should be added when a terrain experiment needs them, with unit tests and a
visual expression that exercises the new behavior.

## Tests and visual feedback

Run the unit suite with:

```sh
ctest --test-dir build --output-on-failure
```

Generate a directly inspectable field preview with:

```sh
./build/terrain-field-demo /tmp/field.png 512
```

The demo builds a 15-node DAG from coordinate, arithmetic, and sine nodes,
evaluates it on the CPU, and writes an 8-bit grayscale PNG.  The PNG writer
is part of the portable terrain library so future tools can produce visual
checkpoints without launching the game.

## Deliberate next boundaries

- Add algebraic nodes one small family at a time, beginning with the
  operations needed to express the geological noise composition.
- Keep stochastic seeds explicit in expression nodes.
- Distinguish lazy scalar/vector/mask fields from materialized rasters.
- Represent reductions and simulations such as normalization and erosion as
  explicit materialization barriers.
- Let the Terrain Lab and later script frontends retain and inspect named
  field values rather than mutate a single implicit current heightmap.
- Add code-generation evaluators alongside `CpuEvaluator`; do not introduce
  graphics API types into the expression graph.
