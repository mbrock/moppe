# Terrain expressions, recipes, and pipelines

Moppe's portable terrain subsystem separates four kinds of value:

1. `ScalarField` is a lazy expression DAG.
2. `GeologicalSource` retains the recipe and selected field to materialize.
3. `TerrainTransform` describes a terrain-to-terrain operation.
4. `TerrainProgram` composes one source with an ordered transform sequence.

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

Every backend implements the `FieldEvaluator` materialization boundary.
`CpuEvaluator` lowers unique nodes to a topologically ordered register program
and runs it for every point in a `Domain2D`.  The macOS backend lowers the same
DAG to a Metal function-stitching graph and executes it as a compute kernel.
Future WGSL, SPIR-V, or compiled CPU backends can traverse the same variants.

### Metal 4 function stitching

The Metal backend precompiles a small vocabulary of `[[stitchable]]` MSL
functions.  At runtime it maps scalar-field nodes to
`MTLFunctionStitchingFunctionNode` values, wraps the graph in an
`MTL4StitchedFunctionDescriptor`, and privately links it into one fixed
materialization kernel through `MTL4ComputePipelineDescriptor`.

Graph structure and graph data stay separate.  Constants and noise settings
occupy mutable GPU buffers.  A specialized stitchable loader bakes only each
value's stable buffer slot into the compiled graph.  Seeded Perlin permutation
tables are produced by shared host code, so CPU and GPU evaluators use the
same lattice.  The pipeline cache keys only the operation topology: changing
a recipe value or seed reuses the pipeline, while changing the expression
shape compiles another one.

The fixed stitched-function ABI contains position, scalar parameters, noise
descriptors, and permutation tables.  A small ordinary MSL kernel supplies
the sampling domain and output buffer.  This keeps thread indexing and storage
outside the field algebra and gives later backends an equally plain boundary.

Terrain Lab requests the accelerated evaluator lazily from the platform and
injects it into `map::TerrainEvaluator`.  macOS 26 uses Metal 4; unsupported
platforms return no accelerator and retain the CPU implementation.  The
current checkpoint reads the result back into the authoritative CPU heightmap
so all existing transforms and physics remain unchanged.

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

## Programs and materialized transformations

`TerrainProgram` contains a `GeologicalSource`, a reproducible random-stream
position, and an ordered `std::vector<TerrainTransform>`.  A transform is one
of these runtime variants:

- `NormalizeHeights`
- `PowerHeights`
- `HydraulicErosion`
- `ThermalErosion`

`map::TerrainEvaluator` materializes the source and applies those transforms
to concrete height storage.  It owns program order, random-stream position,
progress reporting, and exact resumable checkpoints.  `RandomHeightMap` no
longer interprets programs; it stores samples and provides the concrete
shaping kernels that the evaluator invokes.

Transforms are immutable values even when evaluating them reuses a mutable
buffer.  This lets erosion participate in the terrain language without
pretending that it is a pointwise `ScalarField`, and without requiring a new
2049-square allocation after every operation.

Every transform also reports two enum-valued semantic properties.  These are
descriptions for tools and evaluators, not a class hierarchy:

| Transform | `SpatialScope` | `EvaluationOrder` |
| --- | --- | --- |
| Power | `Pointwise` | `Direct` |
| Normalize | `Global` | `Reduction` |
| Thermal erosion | `Neighborhood` | `Iterative` |
| Hydraulic erosion | `Global` | `Iterative` |

In more abstract language these roughly separate timeless field algebra,
local context, whole-terrain knowledge, and historical evolution.  The code
keeps the plain operational names because the axes overlap: normalization is
global but not historical, while drainage is both global and evolving.

The canonical game profile is now exactly:

```text
geological source
  -> normalize
  -> power(1.15)
  -> hydraulic(1,500,000 droplets, batches of 256)
  -> thermal(2 iterations, talus 0.003)
```

Terrain Lab retains a program value too.  Game generation, command-line
experiments, and interactive inspection therefore use the same evaluator.
The saved random-stream offset preserves the former erosion sequence.

### Field Algebra Tycoon UI

The Terrain Lab window presents the system as a small construction game:

- the geological source and every materialized stage are selectable rows;
- normalization, power, hydraulic, and thermal stages can be appended;
- selected stages can be moved, copied, deleted, and edited in place;
- selecting the source exposes live steppers for warp, cycle count, mask, and
  blend parameters;
- changing the inspected layer or random seed preserves the downstream stack;
- reset returns to the canonical normalized base recipe;
- left-dragging outside the window orbits, right- or middle-dragging pans,
  and the mouse wheel zooms over a much wider range;
- Fit restores an overview appropriate to the selected view;
- Tile View shows exactly one fundamental square;
- Cover View repeats the square around the camera through the existing
  gameplay LOD path and fades the finite draw horizon into distance haze;
- Donut View embeds the periodic heightfield as an actual torus on the GPU.

Every action edits the `TerrainProgram` or `GeologicalRecipe` value.  The lab
keeps exact height-and-random-state checkpoints at stage inputs, so a stage
edit only replays the affected suffix.  The final output already lives in the
working map and is not copied into redundant history.  The lab does not
maintain a parallel shadow representation of the recipe itself.  Leaving the
lab restores the exact playable heightmap snapshot.

In C++, a scripted experiment is ordinary value manipulation:

```cpp
auto program = moppe::terrain::make_geological_program (123);
program.source.recipe.mountains.cycles = 8;
program.source.recipe.blend.mountain_weight = 0.9f;
program.transforms.emplace_back (moppe::terrain::HydraulicErosion {
  .droplets = 100000,
  .batch_size = 256
});
moppe::map::TerrainEvaluator (map).evaluate (program);
```

Hydraulic batches advance their droplets in lockstep.  Every droplet reads
the same heightfield for one simulation step, sparse erosion/deposition deltas
are accumulated, and the batch is committed before the next step.  This is a
deterministic CPU implementation of the work boundary a future compute kernel
can execute in parallel.  Terrain Lab exposes batch-size presets of 1, 64,
256, and 1024 as well as the numeric stepper, making the visual effect of more
simultaneous erosion directly comparable.

The present CPU implementation still evaluates droplets serially inside that
logical batch.  A prototype using a persistent CPU worker team and two barriers
per droplet step preserved output exactly but was substantially slower because
the work between barriers is too small.  The next performance pass should
therefore lower a whole lockstep batch to a GPU compute kernel, or redesign the
CPU algorithm around much coarser per-worker tiles and private delta buffers.

## Tests and command-line feedback

Run both the pure terrain tests and map integration tests with:

```sh
ctest --test-dir build --output-on-failure
```

On macOS 26 this also runs `moppe-metal-tests`, comparing every field operation
and the complete geological source against `CpuEvaluator`.  The standalone
Metal timing and agreement check is:

```sh
./build/terrain-metal-demo 2049 123
```

On an M2 Pro, the initial checkpoint measured the 2049-square combined field
at about 200 ms in the parallel CPU interpreter and 16 ms in a cached stitched
Metal dispatch, including allocation, synchronization, and CPU readback.
Profiling the actual Terrain Lab controls then reduced a recipe-parameter click
from about 102 ms to roughly 23--25 ms.  Interactive previews reuse evaluator
buffers, height and normal textures, and terrain index buffers; normalize in
two CPU passes; derive preview normals from the height texture in the terrain
vertex shader; use conservative chunk bounds; and debounce shadow refreshes.
The exact CPU normal map is rebuilt when returning to gameplay.

The field preview evaluates one lazy field:

```sh
./build/terrain-field-demo /tmp/mountains.png 512 mountains 123
```

The pipeline preview uses the same program value and `TerrainEvaluator` as the
game:

```sh
./build/terrain-pipeline-demo /tmp/base.png 257 123 combined
./build/terrain-pipeline-demo /tmp/tuned.png 257 123 combined \
  warp-amplitude=0.28 mountain-frequency=9 mountain-weight=0.9
./build/terrain-pipeline-demo /tmp/eroded.png 257 123 combined \
  power=1.15 hydraulic=10000 thermal=2,0.003
```

Every default program starts with normalization.  `raw` clears its transforms;
`normalize` appends normalization explicitly; `world` selects the complete,
slow canonical game profile.  Recipe overrides include `warp-amplitude`, the
three layer frequencies, mask edges, and blend weights.  Geological layer IDs
are `combined`, `continent`, `plains`, `mountains`, `mask`, `warp-x`, and
`warp-y`.

## Next boundaries

- Add parameter metadata so Terrain Lab can generate suitable sliders and
  numeric controls from recipe members rather than hard-code each widget.
- Add a stable serialization format for sources and programs, then layer a
  lightweight scripting language over the same values.
- Generalize hydraulic constants into their own first-class parameter value.
- Keep the interactive Metal result GPU-resident through normalization,
  normal generation, and rendering; read back only when CPU transforms or
  gameplay need an authoritative map.
- Add a compiled/SIMD CPU backend and portable GPU lowerings while keeping
  graphics API types outside the semantic graph.

This remains a terrain system rather than a general tensor library.  New
operations should arrive in small, tested slices immediately usable from both
interactive and noninteractive paths.
