# Orogeny at 10 km

Desktop play covers 10,000 m on each side with 4096 periodic samples per side,
or 2.4414 m between samples. The play recipe evolves through 40 steps of
50,000 years. Uplift acts for the first 500,000 years. On macOS, local
generation uses Metal. The CPU solver supplies the reference land, the
finished worlds distributed by the baker, and the fallback on other devices.

## Measurements on the 16 GiB M2 Pro

In a native 1280×720 game with an empty cache, the 4096² multigrid run spent
2.38 s in orogeny and reached play in 80.4 s. Roads, surface analysis,
vegetation, cache writes and renderer activation account for the rest.
Peak process RSS was 4.29 GiB and peak physical footprint was 9.04 GiB.
The full CPU benchmark was running concurrently during this cold-cache
measurement. Seed 123 had 18 places, 25 ways, a 13.0 km paved circuit,
18 stops, and three buses. All 9,770 road profile points met the grade limit.
The 2048² fast path over the same extent reached play in 31.7 s with 1.04 s
of orogeny, a 4.31 GiB peak physical footprint and 4.88 m spacing.
Select it with `--terrain-resolution 2048`.

## The Metal solver

The backend boundary takes a typed surface, an uplift field and the same
geological parameters as the CPU. It commits all four land fields after
successful completion. A device error restores the CPU path on the original
surface. A complete world or matching reference land from an asset takes
precedence. Local CPU, Metal and Metal multigrid worlds have separate cache
keys. The SDL loading screen draws at five frames per second while its
event loop continues to handle input.

Each resolution starts by finding the ocean on the host. Bed, uplift, cover,
receivers, material fluxes and work lists then stay on the device between
geological steps. Each step performs the following operations.

1. Choose a steepest D8 receiver on the periodic grid and pointer-jump to
   basin roots. Across basin boundaries choose the lowest saddle, breaking
   ties by undirected edge ID. Borůvka contraction joins components. One
   thread reverses the path from each selected saddle to its component's pit.
2. Represent donors by first-child and next-sibling links. Rake leaves and
   compress chains in parallel, gathering integer contributing areas in
   logarithmically many dispatch rounds. The basin contraction has a
   logarithmic number of merging rounds with pointer jumping inside each.
   Path reversal within a component can take a number of operations
   proportional to its path length. This follows the flow and depression
   routing algorithms in [FastFlow](https://m.sheaf.less.rest/NV2YRW/document.md).
3. Bin channels by the integer base-two logarithm of their catchment area.
   At most one donor can have the same order as its receiver. Each order
   consists of disjoint chains. The implicit incision and spill-level solve
   walks them downstream-first. The sediment solve walks them upstream-first.
   Capacity limits detachment, cover is entrained first, and submerged beds
   store sediment up to the spill level and deposition-rate limit. The
   outlet exports what its bed cannot hold. Opposed face fluxes diffuse the
   hillslope with a spacing-dependent stability substep.
4. Verify each cell's catchment equals one plus its donors' catchments and
   all roots are outlets. Report total outflow area, material export and
   the volume discrepancy between final heights and integrated sources.

GPU command buffers share one queue. The host waits at resolution boundaries.
The CPU reference uses fractional D-infinity flow, previous-step channel
tangents, and distributed valley, lake and mouth deposits. The Metal model
uses a single receiver and deposits along the connected flow path. These
choices affect valley shape and the locations of individual tributaries.

## Coarse-to-fine evolution

The multigrid option divides the 40 steps among 512², 1024², 2048² and
4096² as 20, 10, 5 and 5. Area averaging restricts initial heights and
uplift. Periodic cell-centred bilinear interpolation transfers elevation
*change*, sediment, erosion and deposition back to finer source geology.
Each finer level retains its original small-scale relief. Geological time
advances continuously across levels. An evolved surface with pre-existing
sediment uses the single-resolution solver. The coarse-to-fine experiment
is motivated by §5.2 of [Analytical Erosion](https://m.sheaf.less.rest/DWXKYQ/document.md).

## Reproducing the comparison

Use a RelWithDebInfo build with developer tools. Each process writes one
CSV row and a row-major float32 height grid in metres. The internal timer
covers evolution, readback and final surface update. The process time also
includes procedural source generation, writing the height grid and hashing
it. On macOS the last line of interest from time is peak physical memory
footprint, which accounts for Metal allocations differently from RSS.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DMOPPE_BUILD_DEVELOPER_TOOLS=ON
cmake --build build --target terrain-orogeny-benchmark
mkdir -p build/orogeny-10km
for backend in cpu metal metal-multigrid; do
  /usr/bin/time -l build/terrain-orogeny-benchmark 4096 123 40 1 "$backend" \
    "build/orogeny-10km/$backend.f32" >"build/orogeny-10km/$backend.csv"
done
tools/orogeny-hillshade build/orogeny-10km --output build/orogeny-10km/hillshade.png
```

The quality tool uses a common light, physical height scale and detail window.
It reports hypsometry on all native samples and local relief across 625 m
squares. It routes a separate priority flood on each 512² overview, including
both periodic seams, and verifies that every cell reaches a measured outlet.

At 1024², seed 123, 40 play steps on the M2 Pro, explicit spill-level
deposition reduced the full Metal median-elevation difference from 20.9 m to
4.9 m. The CPU, Metal and multigrid median elevations were 168.1, 163.3
and 163.8 m. Their largest basins covered 56.81, 56.89 and 56.81 km².
They had 12, 12 and 10 coastal mouths draining at least 0.1 km².

## World scale and cache identity

The finest warp and continent octaves span 48 cycles, plains span 192 and
mountains span 256, the limit of the permutation lattice. The 10 km CLI
limit protects that bound. The route planner keeps 16 m trail cells and
20 m road cells, so the road graph contains 250,000 nodes. Directed edge
earthwork and traversal costs can be reused across route searches. Place
limits scale with physical area. On 100 km², up to eight sanctuaries and
ten lake outlets can become destinations, depending on accessible terrain.
An inland trailhead can anchor a winding bus road where there is no gentle
coastal landing. A feasible circuit and its travel time determine service.

Finished-world cache version 41 covers the inland bus route and the finished
baker's river-bed stage. Land version 8 already includes the physical extent
in the recipe key. CPU land produced at the former extent retains its own
identity. A full CPU fingerprint at 257 samples and 5 km remains in CI.
The device-independent numerical contract is in [Determinism](determinism.md).
