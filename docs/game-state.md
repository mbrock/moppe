# Game state and replay

Moppe separates the generated world and resident resources from the mutable
logical state that advances each tick. The latter is intended to become a
copyable checkpoint: restore a value, replay the same fixed-step inputs, and
observe the same sequence of game states while rendering it under different
graphics settings.

`game::GeneratedWorld` now owns the stable renderer-free terrain, surface,
water, and analysis artifacts outside that checkpoint; its construction and
borrowing rules are documented in [Generated worlds](generated-world.md).
World construction is single-flight: a worker completes an owned candidate,
then one main-thread activation retires and reconstructs terrain borrowers
before making that candidate live. The loading preview observes copied height
snapshots only, so neither it nor `GameState` owns an in-progress world.

`game::GameLogicState` currently gathers the game clock, derived weather and
camera effects, player mode and inputs, health/fuel/scoring values, gameplay
timers, and the effects random-number generator. `game::GameState` combines
that value with snapshots of both vehicles, the glider, the walker, the chase
camera, the mutable portion of the generated star set, and a bounded
dust-emission event log.
Restoring those subsystem values changes only their mutable state; immutable
terrain references, obstacle references, and vehicle physical parameters stay
attached to the live objects.

This is the first replayable slice, not yet a claim of complete determinism.
Renderer history is not in `GameState`. World generation,
terrain analysis,
uploaded meshes and textures, window state, and asynchronous loading state do
not belong there. Renderer history should get its own reset boundary rather
than being copied into logical game state.

The intended experiment loop is:

1. Generate and prepare one fixed world.
2. Advance with a fixed timestep to an interesting point and copy
   `GameState`.
3. For each graphics combination, restore the state, reset renderer history,
   replay the same fixed-step input tape, discard settling frames, and measure
   the remaining frames.
4. Tag every timing sample with the graphics-feature mask, replay epoch, and
   logical frame number.

The built-in graphics benchmark implements this loop at a fixed 120 Hz. It
constructs the checkpoint after a deterministic scripted prelude, then visits
all Boolean combinations of a graphics-feature partition in Gray-code order.
The current riding partition preserves ocean, river ribbons, bloom, and
automatic exposure as separate blocks and identifies particles, vehicle
effects, star effects, lens flare, and fragment terrain normals as one
`small-effects` block. This reduces the ordinary sweep from 512 configurations
to 32 without pretending those features no longer exist. Every
epoch restores `GameState`, resets renderer temporal history, replays the same
input segment, discards settling frames, and records command-buffer GPU time
for the remaining frames. For example:

```sh
./build/moppe.app/Contents/MacOS/moppe \
  --graphics-benchmark /tmp/moppe-gpu.csv \
  --windowed --seed 123 --terrain-quality fast
```

The CSV contains epoch, the resolved feature mask, the quotient-space partition
mask, logical frame, GPU milliseconds, and one Boolean column per hot feature.
Defaults are 480 prelude frames, 30 settling frames, and 120 measured frames
per configuration. The three counts can be overridden with
`--benchmark-prelude`, `--benchmark-settle`, and `--benchmark-frames` for quick
smoke runs.

Analyze a completed CSV with DuckDB:

```sh
tools/graphics-benchmark-analyze /tmp/moppe-gpu.csv
```

The resulting directory contains `graphics-benchmark.duckdb` plus CSV exports
for configuration statistics, the directed edges of the five-dimensional
quotient cube, average block effects, pairwise block interactions,
balanced-feature correlations, logical-frame sensitivity, and 120/60 Hz
deadline summaries. Every block effect pairs the same logical frame across
configurations before aggregation; every interaction is a difference of
differences over one square face of the quotient cube.

The general `Partition<Map, Element, Block>` concept in `moppe/partition.hh`
represents a partition by its quotient map. Each particular partition chooses
its own equality-comparable block type; no global block-ID enumeration is
required. `RidingGraphicsPartition` refines that algebraic idea with a finite,
stably ordered, named block set needed by the benchmark UI and traversal.

For a synchronized CPU and Metal trace of the cube, run:

```sh
make tracy-benchmark-capture
```

This archives the raw `.tracy` file and benchmark CSV by content hash, imports
CPU zones, Metal encoder zones, benchmark coordinates, and command-buffer
samples into `build-tracy/traces.duckdb`, and writes a Tracy correlation
summary beside the ordinary cube analysis.

Subsystem state structs should remain plain values. When another mutable
system joins the checkpoint, it should expose `state()` and `restore()` while
keeping configuration and resource ownership outside the returned value.

Dust is deliberately represented by emission events rather than mutable
particles or an RNG stream. Particle variation is a counter hash of emission
id, particle index, and property index; position, rotation, size, and lifetime
are evaluated from event age. Metal expands those values into billboard quads
with a mesh shader and retains an instanced vertex fallback. Rewinding dust
therefore restores logical time and the small event log, not hundreds of
individually integrated particles.
