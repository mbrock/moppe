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
- `AnalyticalErosion`
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
than an adjacency matrix. Receivers in the dry reference reading must be
strictly lower; a cell with no lower neighbor points to itself and is an
explicit sink. Sorting cells by height then gives deterministic upstream-area
accumulation and basin assignment without graph cycles.

Standing water is a second structured reading over the same samples:

```text
TerrainView + sea level -> FloodField
                           |-> water surface w
                           |-> standing depth w - z
                           |-> spill receiver per cell
                           |-> one outlet for the global ocean
```

On a torus there is no exterior boundary to identify the ocean. The largest
connected component at or below sea level is therefore the explicit global
ocean, with scan order breaking equal-size ties. A deterministic D8 priority
flood begins across that component and propagates the lowest possible spill
elevation over the unique torus. Enclosed terrain below nominal sea level is
allowed to rise to its own spill instead of becoming a false ocean. An
all-land torus uses its global minimum as an explicit endorheic fallback.
Every spill-receiver chain is acyclic and reaches the chosen root.

The lake census labels connected wet bodies and records physical area,
maximum and mean depth, volume, surface level, ocean connectivity, and the
route-proven pair of last wet cell and first dry spill cell. The wet drainage
interpretation chooses strict downhill D8 routes on the filled surface, gives
each inland body one deterministic tree leading to its spill, and uses a
general topological pass to carry the full upstream area across equal-height
water. A `WaterNetwork` then records every dry-to-water inlet edge and its
accumulated catchment area, plus each inland body's outflow area and downstream
cell. These remain readings and do not mutate terrain. Hydraulic erosion does
not yet consume them. In random-world gameplay, however, the
same `FloodField::water_level` raster drives the animated water grid: vertices
sample the local lake elevation, dry fragments are discarded, and wave
amplitude fades toward each shore. This changes presentation and spawn-site
selection without yet changing vehicle or sediment physics.

A `RiverNetwork` is the first consumer of the body-aware graph. Given a
physical contributing-area threshold, it selects dry flowing cells outside
the global ocean and groups them into directed reaches. Reaches split at
sources and confluences, terminate at water-body inlets, restart at proven
spills, and link to their downstream reach or ocean. Each retains its ordered
cells, upstream and downstream catchment area, and maximum physical slope.
The arbitrary receiver tree used to carry bookkeeping across a flat lake is
therefore absent from the visible stream reading.

The same value clusters visible-channel receiver edges above physical drop
and slope thresholds into deterministic `Waterfall` candidates. Adjacent
qualifying steps become one cascade represented by its strongest edge. Each
candidate records its lip and foot cells, reach, drop, run, slope, and
contributing area. Terrain Lab's FALLS reading marks the candidates and TRACE
reports their measurements. Rendering currently treats the signal as stronger
cascade foam on the continuous ribbon. A tested vertical-quad prototype was
discarded because a heightfield step is still a continuous slope: the quad
intersected terrain and visually disconnected the river.

The retained ribbon renderer resamples those ordered cells twice per edge with
bounded cubic tangents. It preserves exact reach endpoints and recomputes dry
height and normals from the terrain, so this smoothing cannot alter routing or
move an inlet, spill, or confluence. Interpolated flow distance and attributes
drive the same shader material along the smoother presentation curve.

Every transform also reports two enum-valued semantic properties.  These are
descriptions for tools and evaluators, not a class hierarchy:

| Transform | `SpatialScope` | `EvaluationOrder` |
| --- | --- | --- |
| Power | `Pointwise` | `Direct` |
| Normalize | `Global` | `Reduction` |
| Thermal erosion | `Neighborhood` | `Iterative` |
| Analytical erosion | `Global` | `Iterative` |
| Hydraulic erosion | `Global` | `Iterative` |

In more abstract language these roughly separate timeless field algebra,
local context, whole-terrain knowledge, and historical evolution.  The code
keeps the plain operational names because the axes overlap: normalization is
global but not historical, while drainage is both global and evolving.

The reproducible research profile remains exactly:

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

Gameplay selects an explicit `TerrainGenerationProfile`: **Fast** uses a
1025-square map and 30,000 droplets, **Play** uses a 2049-square map and
100,000 droplets, and **Research** uses the reference 2049-square map and
300,000 droplets. `--fast` is shorthand for the fast profile;
`--terrain-quality fast|play|research` selects all three directly. Pressing
`N` during play increments the seed and builds a new world behind the loading
screen.

Finished random worlds are cached automatically. The cache key includes the
profile, resolution, topology, seed, and a runtime hash of the linked
executable. An unchanged binary reuses its last seed and heightmap; any relink
gets a new identity and cannot trust an older binary's terrain. The explicit
`MOPPE_MAPCACHE` path remains an override for controlled experiments. On
startup Moppe removes automatic terrain caches belonging to obsolete build
identities, so ordinary source iteration does not accumulate abandoned maps.

### Field Algebra Tycoon UI

The Terrain Lab window presents the system as a small construction game:

- the friendly Stream Power preset contains analytical erosion but no
  droplets, and its Age slider reaches zero for a same-recipe before/after
  comparison;
- the geological source and every materialized stage are selectable rows;
- normalization, power, analytical age, droplet, and thermal stages can be
  appended independently and combined in any order;
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
contributing area; Streams thresholds that reading at 1,024 upstream cells;
Basins colors shared sink catchments; Sinks marks local minima; Delta shows
signed height change across the selected pipeline stage; Water shows every
priority-flood standing depth; and Lakes applies the permanence census used
by gameplay. Trace accepts a click on terrain in Tile or
Cover view, follows receiver links to a sink, and highlights the complete
basin faintly beneath the path.

These are all presentations of reusable analysis values. The renderer knows
only an R32F scalar overlay, value range, opacity, and palette; it has no
drainage-specific API. `MOPPE_LAB_OVERLAY` (`height`, `slope`, `flow`,
`streams`, `basins`, `sinks`, `delta`, `trace`, `water`, or `lakes`) makes the
same views scriptable. `MOPPE_LAB_STAGE` selects
a stage for Delta, while
`MOPPE_LAB_TRACE_X` and `MOPPE_LAB_TRACE_Y` select a screen point for Trace.
`MOPPE_LAB_EROSION=drops,batch,steps` appends a conservation-closed water
stage for automated Lab captures. `MOPPE_LAB_ANALYTICAL=1` appends the
finite-time stream-power stage.

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
tools/capture-game /tmp/game.png
```

The capture command defaults to seed 123, reads the completed Metal drawable
back directly, writes an 8-bit sRGB PNG, and exits.  It does not need window
automation or screen-capture tooling.  `MOPPE_SEED` and `MOPPE_RENDERSCALE`
override its deterministic seed and output scale.
`capture-game` uses the Fast profile unless `MOPPE_TERRAIN_PROFILE` selects a
different one. `MOPPE_REGENERATE_ONCE=1` exercises one complete in-process
new-world cycle before its screenshot.

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
    moppe::terrain::SedimentDisposition::Deposit,
  .carving_rule = moppe::terrain::CarvingRule::PathMonotone
});
moppe::map::TerrainEvaluator (map).evaluate (program);
```

`AnalyticalErosion` is the first finite-time `n = 1` stream-power slice from
Tzathas et al. It computes one depression-aware drainage graph, traverses its
trees from fixed ocean cells toward ridges, and evaluates the characteristic
solution

```text
z(x,t) = z0(D(x,t)) + integral[D(x,t), x] u(s) / (k A(s)^m) ds
```

where travel time from `D` to `x` is `t`. Age, uplift, `k`, `m`, sea level,
fixed-point routing passes, and relaxation are explicit transform parameters
in physical units. The detachment-limited erosion term cannot raise terrain,
so the implementation caps change at the prescribed tectonic uplift even
when a depression route crosses a dry saddle. Ocean cells remain fixed.

The pass is deterministic and much cheaper than the droplet stage, but it is
not the paper's complete solver yet. Fixed routing produces cell-scale
discontinuities; relaxed routing passes improve agreement but do not replace
the paper's coarse-to-fine iteration and hillslope correction. Terrain Lab
therefore exposes analytical age separately from Talus and reports lowered
and raised volume plus mean and maximum physical change.

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
cutoff, and flat terrain. Explicit 64-step, discard-at-death, unconstrained
values reproduce the historical behavior. Generation profiles instead stop
naturally at 1% water, settle their remaining load, and carve path-monotonely,
closing the sediment ledger while avoiding footprint pits.

Carving policy is another explicit enum. `Unconstrained` retains the earlier
bilinear footprint behavior. `PathMonotone` clamps every affected sample so a
drop cannot leave its footprint below the downstream handoff elevation; the
clamp observes pending changes from every droplet in the current batch. This
prevents the carve itself from minting a pit while preserving deterministic
batch semantics and the sediment ledger.

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

The fixed-seed method comparison replays every mode from one physical-world
checkpoint and can also write its height images:

```sh
./build/terrain-stream-power-experiment \
  257 123 200000 4 30000 /tmp/moppe-stream
```

It reports source, analytical, analytical-plus-talus, droplets, combined, and
combined-plus-talus metrics. The first recorded result and its negative
findings live in `research/terrain/stream-power-experiment.md`.

Every default program starts with normalization.  `raw` clears its transforms;
`normalize` appends normalization explicitly; `world` selects the complete,
slow reference research profile. Recipe overrides include `warp-amplitude`, the
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
