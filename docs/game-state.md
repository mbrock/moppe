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
then one main-thread activation transitions the world/session pair while
preserving terrain-borrow ordering. The loading screen observes status text,
not candidate terrain, so neither it nor `GameState` owns an in-progress
world.

`game::GameSession` is the concrete owner of running gameplay state. It owns
`game::GameLogicState` (the clock, weather and camera effects, player mode and
inputs, health/scoring values, gameplay timers, and effects RNG) along
with both vehicles, the glider, walker, chase camera, stars, and dust.
`game::GameState` is its copyable checkpoint value: it combines that logic
with snapshots of those mutable subsystems. Restoring it changes only mutable
state; immutable terrain and obstacle references, vehicle physical parameters,
and renderer resources remain attached to live objects on the same completed
world.

`game::FrameView` is deliberately outside `GameState`: it is derived anew for
each presentation frame from a completed world, a session reading, the active
camera, and graphics settings. Its frozen visible poses and camera/light
readings make rendering independent of later simulation mutation, but they are
not replay checkpoint state.

Ordinary playable simulation has one public fixed-step operation:
`game::advance_game_session(world, surface, obstacles, session, input,
seconds_t)`. Its inputs supply only the completed world's parameters,
geometry, and collision obstacles; it does not expose `GeneratedWorld`,
loading, platform, or renderer types. The operation applies the `InputFrame`,
advances actors and effects, updates score, camera, and FOV, and reports the
small set of application-side effects it cannot realize itself.
`MoppeGame::tick` selects live or recorded input, continues the global clock
and weather through the paused cinematic mode, then delegates
ordinary play through that operation.

Ordinary play converts the platform's presentation interval into phase-locked
120 Hz steps before calling that operation. A slightly early display callback
therefore remains one simulation step rather than producing a zero-step frame
followed by two steps. Delayed callbacks can catch up by at most six steps;
excess elapsed time is discarded instead of feeding one unstable large step
into vehicles, the glider, or the chase camera. Graphics-benchmark replay and
deterministic cinematic capture retain their deliberate contract of one
logical step per rendered frame.

A completed-world handoff retires the old session before its old generated
world, then constructs a fresh session against the new world. In particular,
pressing `N` begins a new world session rather than carrying a player, score,
or effect history across incompatible terrain. A `GameState` checkpoint is
therefore portable between sessions prepared against the same world, not
between generated worlds.

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
   replay the same fixed-step input tape through `advance_game_session`,
   discard settling frames, and measure the remaining frames.
4. Tag every timing sample with the graphics-feature mask, replay epoch, and
   logical frame number.

The built-in graphics benchmark implements this loop at a fixed 120 Hz. It
constructs the checkpoint after a deterministic scripted prelude, then visits
all Boolean combinations of a graphics-feature partition in Gray-code order.
The standard riding partition keeps forest and undergrowth separate, groups
ocean with waterfall curtains as `water`, groups bloom with automatic exposure
as `post`, and identifies the remaining hot presentation controls as
`other-features`. This gives the ordinary sweep five blocks and 32
configurations without pretending the underlying features no longer exist.
`--benchmark-partition detailed` refines water and post back into their four
component blocks for an explicit 128-configuration run. Every
epoch restores `GameState`, resets renderer temporal history, replays the same
input segment, discards settling frames, and records command-buffer GPU time
for the remaining frames. For example:

```sh
./build/moppe.app/Contents/MacOS/moppe \
  --graphics-benchmark /tmp/moppe-gpu.csv \
  --windowed --seed 123 --terrain-quality fast
```

The CSV contains epoch, the resolved feature mask, the quotient-space partition
mask, logical frame, GPU milliseconds, the partition name, and encoded Boolean
columns such as `feature_0_forest` and `block_0_forest`. Those column names make
the file self-describing: DuckDB and Tracy derive feature and block bit tables
from the capture rather than maintaining copies. Defaults are 480 prelude
frames, 30 settling frames, and 120 measured frames per configuration. The
three counts can be overridden with `--benchmark-prelude`, `--benchmark-settle`,
and `--benchmark-frames` for quick smoke runs.

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
`DetailedRidingGraphicsPartition` is a refinement of that default quotient;
both export their schema through the same benchmark output path.

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
