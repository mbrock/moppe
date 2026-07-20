# Terrain expressions, recipes, and pipelines

Moppe's portable terrain subsystem separates six kinds of value:

1. `ScalarField` is a lazy expression DAG.
2. `GeologicalSource` retains the recipe and selected field to materialize.
3. `TerrainTransform` describes a terrain-to-terrain operation.
4. `TerrainProgram` composes one source with an ordered transform sequence.
5. `WorldRecipe` binds a program to its physical world and water datum.
6. `TerrainView` lends materialized samples to readings and analyses.

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

`Field<QS>` layers an mp-units quantity specification over a `ScalarField` as
a phantom type. Samples remain compact floats and evaluators consume the
erased DAG through `untyped ()`, but field combinators compose
dimensionally: `+` and `-` require matching kinds, `*` combines quantity
specifications, and bare numbers scale within a kind.

The scale-free field vocabulary now distinguishes `CoordinateField`,
`NoiseField`, `ProportionField`, `RelativeElevationField`, and
`RelativeUpliftField`. Noise is not yet relief, a mask is not a generic
scalar, and relative tectonic velocity is not elevation. Crossing meanings
must be explicit: procedural noise is cast to relative elevation when a
geological layer interprets it as relief, while warp noise is multiplied by
an explicitly coordinate-valued amplitude.

Materialization preserves this meaning in `Raster<R>`, where `R` is a full
mp-units reference including its unit. The underlying storage is still a
float array shared by CPU and Metal paths; requesting a sample reconstructs
the quantity at the API boundary. Data-dependent min/max normalization is an
explicit semantic conversion to `NormalizedRaster`: normalized elevation is
a relative normalized sample, not elevation in the original reference.

## Materialized surface sections

`spatial::Bundle<Domain, Quantities...>` is the eager counterpart to the lazy
field algebra. It stores one column per quantity specification over a shared
finite domain, exposes exact rows by domain index, and gives local rules a
`BundleFocus` whose neighbourhood comes from the domain rather than storage
traversal. `extend_into` applies one such rule at every focus and materializes
the next bundle.

`map::Surface` is the first deliberately small gameplay proof. Its
`SurfaceAtlas` has always-present geometry sections and named hydrology,
geology, ecology, and use views over the same domain. Each later section is
optional until its analysis materializes it, so a zero reading never stands in
for unavailable data. Together they contain an affine elevation point, a
vector-valued surface normal, snow support, channel flux, tree habitat, forest
cover, trail influence, home-base influence, moisture, waterline distance,
erosion exposure, and deposition cover at every applicable heightmap node.
Snow support is the upward component of a 24 m local support normal. The
shader can therefore let fine normals light the terrain folds without letting
every lattice-scale change in steepness punch a matching hole in the snow.
`--graphics-disable snow-support-filter` restores the detailed-normal
classification for direct comparisons.
The Association's home-base clearing is a distinct quantity from trail
influence: it is a place with a role, not merely a wide part of the path.
`SurfaceDomain` supplies a four-node bilinear stencil for a world position.
Generic `spatial::sample<QS>` then chooses its algebra from the mp-units value
category: quantities form weighted sums, while quantity points use one anchor
plus weighted point differences. Bounded and toroidal surfaces use the same
operation.

`map::WaterSurfaceSections` is a second bundle over the same lattice rather
than more columns in the ground bundle. Its water elevation reuses the ground
elevation specification and affine frame, so their difference is a physical
depth. Wave amplitude and planar water velocity remain properties of water.
Only `game::WaterPresentation` converts elevation back to the renderer's
normalized height and packs velocity into numeric x/z lanes.

The current heightmap remains authoritative. World generation explicitly
refreshes the surface materialization after recomputing normals, and spawn
selection plus glider terrain queries consume the continuous typed view.
Elevation and normal temporarily duplicate the heightmap columns so the
abstraction can be verified without rewriting generation kernels; a later
consolidation can make bundle
storage authoritative or introduce borrowed/chunked columns.

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
                                      `-> bounded relative uplift
```

Changing a recipe creates a different graph the next time it is built; it
does not mutate an existing graph or raster.  Golden tests lock the canonical
recipe's seven inspectable layers to the former generator bit for bit.

## Programs and materialized transformations

`TerrainProgram` contains a `GeologicalSource`, an explicit root seed, and an
ordered `std::vector<TerrainTransform>`. A transform is one
of these runtime variants:

- `NormalizeHeights`
- `PowerHeights`
- `AnalyticalErosion`
- `OrogenyEvolution`
- `ThermalErosion`
- `TrailFormation`
- `HillslopeDiffusion`

`map::TerrainEvaluator` materializes the source and applies those transforms
to concrete height storage. It owns program order, progress reporting, and
exact resumable checkpoints. `RandomHeightMap` no
longer interprets programs; it stores samples and provides the concrete
shaping kernels that the evaluator invokes.

`WorldRecipe` is the immutable construction input around that program. It
travels with the physical extent, sample resolution, topology, root seed,
water datum, and named generation profile. The canonical factory calibrates
the source, orogeny, and trail sea levels from the physical water datum before
any evaluator sees the program. The normal game, the Terrain Lab preview, and
the pipeline tool therefore begin from the same world vocabulary without
carrying renderer state into terrain construction.

Transforms are immutable values even when evaluating them reuses a mutable
buffer.  This lets erosion participate in the terrain language without
pretending that it is a pointwise `ScalarField`, and without requiring a new
2049-square allocation after every operation.

`TrailFormation` is the first intentional post-geology pass in the world
program. Its `TrailPlan` chooses a dry, locally buildable home base near water,
with nearby shelter and distant relief, then chooses a scenic lake or mountain
focus. Four control sites express the decision to go around that feature. Two
terrain-costed rides join home base to the far side by different flanks,
forming one directed cycle by construction. Routing prices steep grades,
standing water, departure from useful valley floors, and reuse of the first
half of the circuit; it does not begin with a dense natural network that must
later be pruned.

The plan retains home base, scenic focus, control sites, and ordered circuit as
decisions. `TrailNetwork` is their graph materialization: one connected cycle
whose receiver relation returns to home base. A damped periodic Hermite
alignment refines that searched corridor into a continuous constructed route.
Its arc-length profile is projected toward a motorcycle-friendly maximum grade
under bounded cut and fill, then stamped as a narrow core with soft shoulders.
The physical earthwork delta remains available separately from the composed
heightmap. Continuous `trail_influence` and `home_base_influence` readings are
materialized in the `SurfaceAtlas` use view and uploaded to the terrain shader.
Thus
intention begins discretely, while the resulting alignment, earthworks, and
compacted base become physical, sampleable parts of the world. A retained
feature-local ribbon uses the same alignment to give the compacted core smooth
sub-heightmap geometry and explicit along/across coordinates.
See [Trail system](trails.md) for the complete routing, grading, material, and
runtime data flow.

## Materialized readings and drainage

`TerrainView` is a borrowed description of concrete relative-elevation
samples. Its `TerrainGrid` carries horizontal spacing and vertical scale as
length quantities, plus bounded-versus-torus topology; these are deliberately
absent from the normalized sampling `Domain2D` used by pointwise field
evaluation. `relative_elevation_at` reads the scale-free sample, while
`elevation_at` performs the explicit calibration into metres.
`RandomHeightMap::terrain_view` is the shared bridge used by transforms,
tools, and the Lab.

A reading consumes that materialized view without changing it. The first
small reading, `measure_height_range`, returns the minimum and maximum that
normalization already needed. Drainage is a larger structured analysis:

```text
TerrainView -> DrainageGraph
                 |-> receiver per cell
                 |-> SlopeRaster (dimensionless physical gradient)
                 |-> ContributingAreaRaster (square metres)
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
cell. These remain readings and do not mutate terrain. In random-world
gameplay, the
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

Every reach owns a dense, unwrapped `RiverAlignment`. A damped cubic Hermite
curve preserves routed endpoints while removing D8 corners; samples carry
area, slope, waterfall, standing-water blend, and water level. Local arc
length serves geometric consumers, while a negative distance-to-final-mouth
coordinate is continuous through confluences and drives flow animation. The
alignment is a reading: it cannot alter routing or move an inlet or spill.

Every transform also reports two enum-valued semantic properties.  These are
descriptions for tools and evaluators, not a class hierarchy:

| Transform | `SpatialScope` | `EvaluationOrder` |
| --- | --- | --- |
| Power | `Pointwise` | `Direct` |
| Normalize | `Global` | `Reduction` |
| Thermal erosion | `Neighborhood` | `Iterative` |
| Analytical erosion | `Global` | `Iterative` |
| Orogeny evolution | `Global` | `Iterative` |
| Trail formation | `Global` | `Iterative` |
| Hillslope diffusion | `Neighborhood` | `Iterative` |

In more abstract language these roughly separate timeless field algebra,
local context, whole-terrain knowledge, and historical evolution.  The code
keeps the plain operational names because the axes overlap: normalization is
global but not historical, while drainage is both global and evolving.

Each concrete transform also owns its validation, stable identity, Lab title,
detail text, and editable property descriptions. `TerrainProgram` keeps the
same variant value and only visits those local operations; it does not grow a
second validation or UI switchboard. `ParameterDomain` lives with the terrain
metadata, so a consumer can distinguish continuous values from natural-number
counters without knowing a transform's concrete type. The transform semantics
still decide whether a continuous control can be scrubbed interactively.

The world program is deliberately short:

```text
orogeny source
  -> orogeny evolution(profile duration)
  -> trail formation
```

Terrain Lab retains a program value too. Game generation, command-line
experiments, and interactive inspection therefore use the same evaluator.

Gameplay selects an explicit `TerrainGenerationProfile`: **Fast** evolves a
1025-square Orogeny world for 750,000 years, **Play** evolves a 2049-square
world for 1,500,000 years, and **Research** evolves the reference 2049-square
world for 2,000,000 years. `--fast` is shorthand for the fast profile;
`--terrain-quality fast|play|research` selects all three directly. Pressing
`N` during play increments the seed and builds a new world behind the loading
screen. The completed-world handoff starts a fresh game session against that
new terrain. Game generation derives the normalized erosion base level from the
current world's physical water level and vertical extent, so tectonic
evolution and the subsequently materialized sea and lakes share one datum.
The Orogeny source uses separate land and submarine relief scales: low initial
land relief leaves mountain building to uplift, while deeper bathymetric
relief gives fixed ocean outlets a real bed instead of a water-level plateau.

Finished random worlds are cached automatically. The cache key includes the
profile, resolution, topology, seed, and a runtime hash of the linked
executable. An unchanged binary reuses its last seed and heightmap; any relink
gets a new identity and cannot trust an older binary's terrain. The explicit
`MOPPE_MAPCACHE` path remains an override for controlled experiments. On
startup Moppe removes automatic terrain caches belonging to obsolete build
identities, so ordinary source iteration does not accumulate abandoned maps.

### Terrain Lab UI

Terrain Lab has one instrument vocabulary with two levels of disclosure:

- **Observe** is the default, narrow, translucent window. It keeps the terrain
  visible and puts every existing surface reading in one palette. World
  presets remain explicit one-shot comparisons rather than draggable
  parameters;
- **Build** adds the source-field sections, retained pipeline, stage reports,
  and parameter editing without changing the reading names or visual skin;
- direct continuous values use rotary controls because they can update at
  interactive speed. Neighborhood, reduction, and iterative values use
  explicit stepped changes, so an expensive transform cannot be scrubbed
  through obsolete intermediate rebuilds;
- the geological source and every materialized stage are selectable rows;
- normalization, power, analytical age, orogeny, thermal, trail, and
  diffusion stages can be appended independently and combined in any order;
- selected stages can be moved, copied, deleted, and edited in place;
- natural-number values such as periodic wave counts, routing passes, and
  relaxation iterations use explicit digital minus/value/plus counters;
- changing the inspected layer or random seed preserves the downstream stack;
- reset returns to the canonical Orogeny world recipe;
- left-dragging outside the window orbits, right- or middle-dragging pans,
  and the mouse wheel zooms only while it is over the terrain;
- Fit restores an overview appropriate to the selected view;
- Tile View shows exactly one fundamental square;
- Cover View repeats the square around the camera through the existing
  gameplay LOD path and fades the finite draw horizon into distance haze;
- Donut View embeds the periodic heightfield as an actual torus on the GPU.

The shared **Map Readings** palette keeps geometry and interpretation
independent. Material restores the ordinary terrain textures; Height and
Slope drape scalar palettes over the current surface; Flow shows logarithmic
contributing area; Streams shows extracted river reaches; Basins colors
shared outlet catchments; Outlets marks terminal wet routes; Delta shows
signed height change across the selected pipeline stage; Water shows every
priority-flood standing depth; Lakes applies the permanence census used by
gameplay; Falls marks steep high-flow steps; and Eroded and Deposit expose the
lifetime sediment ledgers. Trace accepts a click on terrain in Tile or Cover
view, follows receiver links to an outlet, and highlights the complete basin
faintly beneath the path. Drop releases one ordinary erosion-model droplet.

These are all presentations of reusable analysis values. The renderer knows
only an R32F scalar overlay, value range, opacity, and palette; it has no
drainage-specific API. `MOPPE_LAB_OVERLAY` (`height`, `slope`, `flow`,
`streams`, `basins`, `sinks`, `delta`, `trace`, `water`, `lakes`, `falls`,
`eroded`, or `deposited`) makes the same views scriptable. `MOPPE_LAB_STAGE`
selects a stage for Delta, while
`MOPPE_LAB_TRACE_X` and `MOPPE_LAB_TRACE_Y` select a screen point for Trace.
`MOPPE_LAB_EROSION=drops,batch,steps` appends a conservation-closed water
stage for automated Lab captures. `MOPPE_LAB_ANALYTICAL=1` appends the
finite-time stream-power stage. `MOPPE_LAB_OROGENY=1` selects the shallow
continent source and calibrated fast orogeny program. `MOPPE_LAB_EXPERT=1`
opens Build directly for deterministic UI captures.

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

Live knob motion updates direct program values immediately, while evaluation
is coalesced on the frame tick. Costly stepped edits run once per click. Each
completed preview morphs from the previous height texture over 120 ms, with
normals derived from the interpolated surface.  Its shadow is rebuilt
immediately at 1024-square resolution from every second terrain sample, then
crossfaded from the previous shadow on the same 120 ms clock.  This keeps
light and geometry together while dragging without paying for the gameplay
shadow pass.  `MOPPE_PROFILE_SHADOW=1` reports the GPU time of either path.
The UI itself is Moppe's small immediate-mode `InspectorUi` drawn through
`DrawList`, not an external widget library. `UiWindow` owns persistent panel
placement, title-bar dragging, viewport constraints, and the conversion from
screen input to local widget coordinates. Observe, Build, Map Readings, and
the in-game World Feel tool therefore share one translucent frame and input
model. `UiFlow`, `ui_inset`, and `ui_grid_cell` provide the small flex-like
layout vocabulary used by both drawing and hit testing. Drag any window by
its title bar; its position persists while switching disclosure levels.

For UI iteration, `--terrain-lab-preview` uses a deterministic-capable
1025-square field and skips canonical erosion, stars, fish, and
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
auto program = moppe::terrain::make_world_program (
  123, moppe::terrain::TerrainGenerationProfile::Play);
program.source.recipe.mountains.cycles = 8;
program.source.recipe.blend.mountain_weight = 0.9f;
auto& orogeny = std::get<moppe::terrain::OrogenyEvolution> (
  program.transforms.front ());
orogeny.evolution.duration =
  650000.0f * mp_units::astronomy::Julian_year;
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
when a depression route crosses a dry saddle. Ocean cells remain fixed at
their bed elevation.

`OrogenyEvolution` reverses the older source semantics. It starts from a
continent and bathymetry around the configured sea level, interprets the
recipe's bounded mountain pattern as `RelativeUpliftField`, scales it by a
physical maximum uplift velocity, and evolves relief through

```text
dz/dt = U(x) - v_ref (A(x) / A_ref)^m S(x) + D laplacian(z).
```

Here `v_ref` is an incision velocity measured at the typed reference area
`A_ref`. This keeps the runtime calibration dimensionally stable as `m`
changes. The default `A_ref = 1 m²` and `v_ref = 2e-5 m/year` preserve the
previous numerical `K A^m` calibration.

Every geological step recomputes the standing-water surface and constructs an
Orogeny-local fractional drainage reading. Its domain contains only the
heightmap's unique lattice samples, owns each cell's zero-, one-, or
two-receiver route and deterministic topological order, and is materialized as
a `Bundle` of typed direction, slope, contributing area, channel tangent, and
area-weighted tangent-flux columns. Dry strict descent uses D-infinity
triangular facets. Lake flats, depression spills, and ocean identity continue
to come from the established wet D8 graph. This makes fractional accumulation
conservative and acyclic without changing the graph used by lakes, rivers,
waterfalls, rendering, or the final `SurfaceAtlas`.

The channel tangent is the horizontal vector form of drainage direction after
incoming area fluxes have been combined at confluences. It is persisted across
geological steps. At the next step, `channel_persistence` multiplies each
candidate's physical slope by `1 + p dot(d, d_previous)` before route
selection. Only already-downhill candidates enter that comparison, so the
height ordering and deterministic DAG remain intact. This is a lagged coupling:
the completed routing pass produces the memory used by the following pass,
never a same-pass routing feedback loop. A value of zero exactly recovers
memoryless D-infinity; the default is 0.35 and Terrain Lab exposes the value as
`CHANNEL MEMORY`.

The typed tangent survives `TerrainEvaluator` checkpoints and is available at
the evaluator boundary. World assembly reads the final drainage bundle,
combines its tangent with log-compressed contributing area, and materializes
that reading as the planar `channel_flux` section. `SurfacePresentation` is the
single numeric bridge that turns this typed vector column into the shader's
interleaved x/z texture payload.

A downstream-to-upstream sweep interpolates the new elevation along a
two-receiver facet edge, then solves the backward-Euler incision step exactly
for that discrete step. It is unconditionally stable for the linear `n = 1`
term, but is not an exact continuous-time solution for an arbitrarily long
step. Explicit stable hillslope-diffusion sweeps are interleaved after
incision. Ocean cells and receiver roots retain their bed elevation rather
than snapping terrain to the water surface, and an uphill depression route
cannot raise a cell above uplift alone. `AnalyticalErosion` is deliberately
outside this boundary and retains its experimental fixed-graph solver.

The fixed-seed 513-square stress comparison shows a narrower result than
"fractional routing removes the grid." D-infinity softens several hard
herringbone steps and changes individual channel paths, but visible combing
remains because incision is still materialized at lattice samples and the
surface is still reconstructed bilinearly. The `d8` comparison mode remains
available while that later raster-to-surface boundary is investigated.

The calibrated maximum uplift is 1 mm/year, with `v_ref = 2e-5 m/year` at
`A_ref = 1 m²`, `m = 0.4`, `D = 1e-4 m²/year`, and a 50,000-year step. Fast,
Play, and Research orogeny programs run for 750,000, 1,500,000, and 2,000,000
years respectively. Ordinary world generation uses these programs, selected
by the existing terrain quality profile. The report separates prescribed
tectonic uplift and implicit incision volumes from net raised/lowered volume,
and exposes the last step's mean and maximum change as a convergence reading.

The pass is deterministic, but it is not the paper's complete solver yet.
Fixed routing produces cell-scale
discontinuities; relaxed routing passes improve agreement but do not replace
the paper's coarse-to-fine iteration and hillslope correction. Terrain Lab
therefore exposes analytical age separately from Talus and reports lowered
and raised volume plus mean and maximum physical change.

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
./build/terrain-pipeline-demo /tmp/world.png 257 123 combined world
./build/terrain-pipeline-demo /tmp/orogeny.png 257 123 combined \
  orogeny=1000000,50000,0.001,2e-5,0.4,0.0001
```

The orogeny option is
`duration,dt,uplift,reference_rate,m,D[,sea,land_relief,coastline,
bathymetry,routing]`, where routing is `d8` or `d-infinity`. Unlike the
unit-scale legacy preview, this mode uses the game's 11 km by 11 km horizontal
and 650 m vertical calibration so all physical rates retain their meaning.

The initial preview program starts with normalization. `raw` clears its
transforms; `normalize` appends normalization explicitly; `world` selects the
Research Orogeny program. Recipe overrides include `warp-amplitude`, the three layer
frequencies, mask edges, and blend weights. Geological layer IDs are
`combined`, `continent`, `plains`, `mountains`, `mask`, `warp-x`, and `warp-y`.

## Next boundaries

- Add a stable serialization format for sources and programs, then layer a
  lightweight scripting language over the same values.
- Keep the interactive Metal result GPU-resident through normalization,
  normal generation, and rendering; read back only when CPU transforms or
  gameplay need an authoritative map.
- Add a compiled/SIMD CPU backend and portable GPU lowerings while keeping
  graphics API types outside the semantic graph.

This remains a terrain system rather than a general tensor library.  New
operations should arrive in small, tested slices immediately usable from both
interactive and noninteractive paths.
