# Terrain expressions, recipes, and pipelines

Moppe's portable terrain subsystem separates five kinds of value:

1. `ScalarField` is a lazy expression DAG.
2. `GeologicalSource` retains the recipe and selected field to materialize.
3. `TerrainTransform` describes a terrain-to-terrain operation.
4. `TerrainProgram` composes one source with an ordered transform sequence.
5. `TerrainView` lends materialized samples to readings and analyses.

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

## Materialized readings and drainage

`TerrainView` is a borrowed description of concrete terrain samples. It
carries physical horizontal spacing, vertical scale, and bounded-versus-torus
topology, which are deliberately absent from the normalized sampling
`Domain2D` used by pointwise field evaluation. `RandomHeightMap::terrain_view`
is the shared bridge used by transforms, tools, and the Lab.

A reading consumes that materialized view without changing it. The first
small reading, `measure_height_range`, returns the minimum and maximum that
normalization already needed. Drainage is a larger structured analysis:

```text
TerrainView -> DrainageGraph
                 |-> receiver per cell
                 |-> physical slope
                 |-> contributing area
                 |-> basin / sink identity
```

The periodic D8 analysis works on the unique torus samples, omitting the
duplicated rendering seam. Each cell stores one `uint32_t` receiver rather
than an adjacency matrix. Receivers must be strictly lower; a cell with no
lower neighbor points to itself and is an explicit sink. Sorting cells by
height then gives deterministic upstream-area accumulation and basin
assignment without graph cycles. Depression filling or breaching is a future
policy, not a hidden part of this first reference interpretation.

Standing water is a second structured reading over the same samples:

```text
TerrainView + sea level -> FloodField
                           |-> water surface w
                           |-> standing depth w - z
                           |-> spill receiver per cell
                           |-> sea-level outlets
```

A deterministic D8 priority flood begins at every submerged ocean cell and
propagates the lowest possible spill elevation over the unique torus. An
all-land torus uses its global minimum as an explicit endorheic fallback.
Every spill-receiver chain is acyclic and reaches one of those roots. This is
currently an observational reading: dry drainage and hydraulic erosion do not
yet route across its water surface.

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
  -> hydraulic(300,000 droplets, batches of 256,
               1% water cutoff, 512-step safety cap, settle at death)
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
- continuous values use synth-style rotary controls with vertical dragging;
- natural-number values such as periodic wave counts, erosion droplets,
  batches, and passes use explicit digital minus/value/plus counters;
- changing the inspected layer or random seed preserves the downstream stack;
- reset returns to the canonical normalized base recipe;
- left-dragging outside the window orbits, right- or middle-dragging pans,
  and the mouse wheel zooms only while it is over the terrain;
- Fit restores an overview appropriate to the selected view;
- Tile View shows exactly one fundamental square;
- Cover View repeats the square around the camera through the existing
  gameplay LOD path and fades the finite draw horizon into distance haze;
- Donut View embeds the periodic heightfield as an actual torus on the GPU.

The separate **Map Readings** window keeps geometry and interpretation
independent. Material restores the ordinary terrain textures; Height and
Slope drape scalar palettes over the current surface; Flow shows logarithmic
contributing area; Streams thresholds that reading at 64 upstream cells;
Basins colors shared sink catchments; Sinks marks local minima; Delta shows
signed height change across the selected pipeline stage; and Water shows
priority-flood standing depth. Trace accepts a click on terrain in Tile or
Cover view, follows receiver links to a sink, and highlights the complete
basin faintly beneath the path.

These are all presentations of reusable analysis values. The renderer knows
only an R32F scalar overlay, value range, opacity, and palette; it has no
drainage-specific API. `MOPPE_LAB_OVERLAY` (`height`, `slope`, `flow`,
`streams`, `basins`, `sinks`, `delta`, `trace`, or `water`) makes the same
views scriptable; `lakes` is an alias for `water`. `MOPPE_LAB_STAGE` selects
a stage for Delta, while
`MOPPE_LAB_TRACE_X` and `MOPPE_LAB_TRACE_Y` select a screen point for Trace.
`MOPPE_LAB_EROSION=drops,batch,steps` appends a conservation-closed water
stage for automated Lab captures.

The three `WAVES` counters are integer spatial frequencies: how many periods
fit around one fundamental side of the torus.  They are not literal counts of
continents, plains, or mountain ranges; integer frequencies are what make the
noise join seamlessly at the world boundary.

Every action edits the `TerrainProgram` or `GeologicalRecipe` value.  The lab
keeps exact height-and-random-state checkpoints at stage inputs, so a stage
edit only replays the affected suffix.  The final output already lives in the
working map and is not copied into redundant history.  The lab does not
maintain a parallel shadow representation of the recipe itself.  Leaving the
lab restores the exact playable heightmap snapshot.

Knob motion updates the program value immediately, while evaluation is
coalesced on the frame tick: direct fields update frequently, iterative stages
use a slower cadence, and releasing the knob commits the newest value.  Each
completed preview morphs from the previous height texture over 120 ms, with
normals derived from the interpolated surface.  Its shadow is rebuilt
immediately at 1024-square resolution from every second terrain sample, then
crossfaded from the previous shadow on the same 120 ms clock.  This keeps
light and geometry together while dragging without paying for the gameplay
shadow pass.  `MOPPE_PROFILE_SHADOW=1` reports the GPU time of either path.
The UI itself is Moppe's small immediate-mode `InspectorUi` drawn through
`DrawList`, not an external widget library.

For UI iteration, `--terrain-lab-preview` uses a deterministic-capable
1025-square field and skips canonical erosion, vegetation, stars, fish, and
wildlife setup.  The full build-and-capture loop is scriptable:

```sh
make terrain-lab-shot
tools/capture-terrain-lab /tmp/lab.png
```

The capture command defaults to seed 123, reads the completed Metal drawable
back directly, writes an 8-bit sRGB PNG, and exits.  It does not need window
automation or screen-capture tooling.  `MOPPE_SEED` and `MOPPE_RENDERSCALE`
override its deterministic seed and output scale.

In C++, a scripted experiment is ordinary value manipulation:

```cpp
auto program = moppe::terrain::make_geological_program (123);
program.source.recipe.mountains.cycles = 8;
program.source.recipe.blend.mountain_weight = 0.9f;
program.transforms.emplace_back (moppe::terrain::HydraulicErosion {
  .droplets = 100000,
  .batch_size = 256,
  .max_steps = 512,
  .minimum_water = 0.01f,
  .sediment_at_termination =
    moppe::terrain::SedimentDisposition::Deposit
});
moppe::map::TerrainEvaluator (map).evaluate (program);
```

Hydraulic batches advance their droplets in lockstep.  Every droplet reads
the same heightfield for one simulation step, sparse erosion/deposition deltas
are accumulated, and the batch is committed before the next step.  This is a
deterministic CPU implementation of the work boundary a future compute kernel
can execute in parallel.  Terrain Lab exposes batch-size presets of 1, 64,
256, and 1024 as well as the numeric knob, making the visual effect of more
simultaneous erosion directly comparable.

Maximum lifetime is a first-class natural-number parameter too. The Lab has
one-click experiment presets for droplet count (100K, 300K, 1M, 1.5M), maximum
lifetime (64, 128, 256, 512), and batch size. Droplet plus/minus controls move
through coarse 1-3-5-style values so each expensive rebuild changes the
experiment materially.

Every hydraulic evaluation returns a `HydraulicErosionReport`. The Map
Readings window displays eroded, deposited, and discarded sediment; mean
lifetime and final water; and termination counts for the safety cap, water
cutoff, and flat terrain. The compatibility defaults still reproduce the old
64-step/discard behavior when a stage omits the new fields. Newly added stages
and the canonical world instead stop naturally at 1% water and settle their
remaining load, closing the sediment ledger.

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
vertex shader; use conservative chunk bounds; and maintain a live
reduced-quality shadow map.
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
  power=1.15 hydraulic=10000,256,512,0.01,deposit thermal=2,0.003
```

The controlled lifetime sweep used to choose those defaults is reproducible:

```sh
./build/terrain-erosion-experiment 513 1783728698 300000 1 \
  64,128,256,305,512
./build/terrain-erosion-experiment 513 1783728698 300000 1 \
  512 0.01 settle
```

The command reports runtime, sinks, stream cells, maximum drainage area,
longest receiver path, the sediment ledger, mean lifetime, and termination
causes as CSV. The full measurements and falsified footprint experiment live
in `research/terrain/erosion-lifetime-experiment.md`.

Every default program starts with normalization.  `raw` clears its transforms;
`normalize` appends normalization explicitly; `world` selects the complete,
slow canonical game profile.  Recipe overrides include `warp-amplitude`, the
three layer frequencies, mask edges, and blend weights.  Geological layer IDs
are `combined`, `continent`, `plains`, `mountains`, `mask`, `warp-x`, and
`warp-y`.

## Next boundaries

- Promote the current continuous/natural UI domains into terrain-level
  parameter metadata so tools can generate controls without hard-coded rows.
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
