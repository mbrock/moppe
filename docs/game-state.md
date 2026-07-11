# Game state and replay

Moppe separates the generated world and resident resources from the mutable
logical state that advances each tick. The latter is intended to become a
copyable checkpoint: restore a value, replay the same fixed-step inputs, and
observe the same sequence of game states while rendering it under different
graphics settings.

`game::GameLogicState` currently gathers the game clock, derived weather and
camera effects, player mode and inputs, health/fuel/scoring values, gameplay
timers, and the effects random-number generator. `game::GameState` combines
that value with snapshots of both vehicles, the walker, the chase camera, and
the mutable portion of the generated star set, and a bounded dust-emission
event log.
Restoring those subsystem values changes only their mutable state; immutable
terrain references, obstacle references, and vehicle physical parameters stay
attached to the live objects.

This is the first replayable slice, not yet a claim of complete determinism.
City actors and renderer history are not in `GameState`. World generation,
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
all Boolean combinations of hot graphics features in Gray-code order. Every
epoch restores `GameState`, resets renderer temporal history, replays the same
input segment, discards settling frames, and records command-buffer GPU time
for the remaining frames. For example:

```sh
./build/moppe.app/Contents/MacOS/moppe \
  --graphics-benchmark /tmp/moppe-gpu.csv \
  --windowed --seed 123 --terrain-quality fast
```

The CSV contains epoch, mask, logical frame, GPU milliseconds, and one Boolean
column per hot feature. Defaults are 480 prelude frames, 30 settling frames,
and 120 measured frames per configuration. The three counts can be overridden
with `--benchmark-prelude`, `--benchmark-settle`, and `--benchmark-frames` for
quick smoke runs.

Analyze a completed CSV with DuckDB:

```sh
tools/graphics-benchmark-analyze /tmp/moppe-gpu.csv
```

The resulting directory contains `graphics-benchmark.duckdb` plus CSV exports
for configuration statistics, the 1,024 directed edges of the eight-dimensional
Boolean cube, average feature effects, pairwise interactions, balanced-feature
correlations, logical-frame sensitivity, and 120/60 Hz deadline summaries.
Every feature effect pairs the same logical frame across configurations before
aggregation; every interaction is a difference of differences over one square
face of the cube.

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
