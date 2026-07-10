# Terrain expressions, recipes, and pipelines

Moppe's portable terrain subsystem separates three kinds of value:

1. `ScalarField` is a lazy expression DAG.
2. `GeologicalRecipe` contains the values used to construct related fields.
3. `TerrainPipeline` orders operations that require a materialized heightmap.

The game, Terrain Lab, unit tests, and command-line tools share these types.
None of them contains renderer or platform graphics API state.

## Scalar-field graph

`ScalarField` is a small handle to an immutable `std::variant` node.  Child
links are `std::shared_ptr<const Node>`, so reusing a field creates a DAG
without copying raster data.  Reference counting is sufficient because the
graph is acyclic.

Current nodes cover constants, coordinates, arithmetic, fused multiply-add,
sine, smoothstep, Perlin noise, fractal Brownian motion, and ridged noise.
Seeds and fractal parameters are explicit graph data.  `MultiplyAdd` is a
semantic node because the historical generator relied on fused floating-point
results.  The Perlin shuffle also has an explicit unbiased sampler, avoiding
standard-library-dependent sequences.

`CpuEvaluator` lowers unique nodes to a topologically ordered register program
and runs it for every point in a `Domain2D`.  Future MSL, WGSL, SPIR-V, or
other backends can traverse the same variants.

## Geological recipes

`GeologicalRecipe` is a copyable value containing all current generator
parameters:

- three component seeds;
- domain-warp frequency, octaves, lacunarity, gain, offsets, and amplitude;
- continent and plains noise plus output scale and bias;
- mountain frequency, octaves, lacunarity, and gain;
- mask edges and the three final blend weights.

`make_geological_fields(recipe)` expands that value into named, shared fields:

```text
coordinates -> two warp fields -> warped coordinates
                                  |-> continent -> mountain mask
                                  |-> plains
                                  `-> ridged mountains

continent + plains + mountains + mask -> combined terrain
```

Changing a recipe creates a different graph the next time it is built; it
does not mutate an existing graph or raster.  Golden tests lock the canonical
recipe's seven inspectable layers to the former generator bit for bit.

## Materialized pipelines

`TerrainPipeline` contains a recipe, selected output layer, a reproducible
random-stream position, and an ordered `std::vector<PipelineStage>`.  A stage
is one of these runtime variants:

- `NormalizeHeights`
- `PowerHeights`
- `HydraulicErosion`
- `ThermalErosion`

`RandomHeightMap::run_pipeline()` materializes the source field and replays
those stages in order.  Whole-raster reduction and stateful simulation are
therefore visible barriers rather than pretending to be pointwise field
expressions.

The canonical game profile is now exactly:

```text
geological source
  -> normalize
  -> power(1.15)
  -> hydraulic(1,500,000 droplets)
  -> thermal(2 iterations, talus 0.003)
```

Terrain Lab retains a pipeline value too.  Game generation and interactive
inspection therefore use the same interpreter.  The saved random-stream
offset preserves the former erosion sequence.

### Field Algebra Tycoon UI

The Terrain Lab window presents the system as a small construction game:

- the geological source and every materialized stage are selectable rows;
- normalization, power, hydraulic, and thermal stages can be appended;
- selected stages can be moved, copied, deleted, and edited in place;
- selecting the source exposes live steppers for warp, frequency, mask, and
  blend parameters;
- changing the inspected layer or random seed preserves the downstream stack;
- reset returns to the canonical normalized base recipe;
- dragging outside the window or right-dragging orbits the landscape, while
  the mouse wheel zooms; the keyboard camera controls remain available.

Every action edits the `TerrainPipeline` or `GeologicalRecipe` value and then
replays it.  The UI does not maintain a parallel shadow representation of the
pipeline.  Leaving the lab restores the exact playable heightmap snapshot.

In C++, a scripted experiment is ordinary value manipulation:

```cpp
auto pipeline = moppe::terrain::make_geological_pipeline (123);
pipeline.recipe.mountains.frequency = 9.0f;
pipeline.recipe.blend.mountain_weight = 0.9f;
pipeline.stages.emplace_back
  (moppe::terrain::HydraulicErosion { 100000 });
map.run_pipeline (pipeline);
```

## Tests and command-line feedback

Run both the pure terrain tests and map integration tests with:

```sh
ctest --test-dir build --output-on-failure
```

The field preview evaluates one lazy field:

```sh
./build/terrain-field-demo /tmp/mountains.png 512 mountains 123
```

The pipeline preview uses the same recipe value and `RandomHeightMap`
interpreter as the game:

```sh
./build/terrain-pipeline-demo /tmp/base.png 257 123 combined
./build/terrain-pipeline-demo /tmp/tuned.png 257 123 combined \
  warp-amplitude=0.28 mountain-frequency=9 mountain-weight=0.9
./build/terrain-pipeline-demo /tmp/eroded.png 257 123 combined \
  power=1.15 hydraulic=10000 thermal=2,0.003
```

Every pipeline starts with normalization.  `raw` clears its stages;
`normalize` appends normalization explicitly; `world` selects the complete,
slow canonical game profile.  Recipe overrides include `warp-amplitude`, the
three layer frequencies, mask edges, and blend weights.  Geological layer IDs
are `combined`, `continent`, `plains`, `mountains`, `mask`, `warp-x`, and
`warp-y`.

## Next boundaries

- Add parameter metadata so Terrain Lab can generate suitable sliders and
  numeric controls from recipe members rather than hard-code each widget.
- Add a stable serialization format for recipes and pipelines, then layer a
  lightweight scripting language over the same values.
- Generalize hydraulic constants into their own first-class parameter value.
- Add compiled CPU and GPU evaluators alongside `CpuEvaluator`, while keeping
  graphics API types outside the semantic graph.

This remains a terrain system rather than a general tensor library.  New
operations should arrive in small, tested slices immediately usable from both
interactive and noninteractive paths.
