# RFC-004: The erosion performance path

- Status: Superseded on 2026-07-16
- Area: terrain simulation, performance
- Interacts with: RFC-001 (reduces droplet load), RFC-003 (ledger on GPU)

## Problem

This proposal is retained as a historical design record. RFC-001's orogeny
model replaced droplet erosion in ordinary world generation, and the unused
droplet implementation was removed. None of the droplet GPU work below is on
the active roadmap; source-field acceleration remains independently useful.

World generation time is dominated by terrain work that the codebase has
already architected for acceleration but not yet wired up.  The pointwise
source field runs on the CPU interpreter during the loading screen even
though a Metal evaluator exists and is ~10x faster; droplet erosion runs
serially inside its lockstep batches even though the batch structure was
explicitly designed as a GPU work boundary
(`docs/terrain-expressions.md`: "lower a whole lockstep batch to a GPU
compute kernel").

## Current situation

Three distinct tiers, smallest first:

1. **Wiring gap.**  Terrain Lab injects the accelerated evaluator
   (`moppe/game/terrain_lab.cc:508` calls
   `platform::create_field_evaluator()`), but the game's world generation
   constructs `map::TerrainEvaluator (m_map)` with the default null source
   evaluator at both call sites (`moppe/game/game.cc:487` and `:509`), so
   the loading screen always uses the parallel CPU interpreter.  The docs
   measured ~200 ms CPU vs ~16 ms stitched Metal for the 2049^2 combined
   field.
2. **Serial droplets.**  `erode_hydraulically`
   (`moppe/map/generate.cc:554`) advances droplets in deterministic
   lockstep batches with sparse delta commits, but evaluates them serially
   within each batch.  A CPU worker-team prototype preserved output but
   was slower (barrier overhead vs tiny per-step work) -- the documented
   conclusion was "lower a batch to a GPU kernel."
3. **Stream-state RNG.**  Droplet spawns draw from a threaded `mt19937`
   whose position is part of every checkpoint.  Dust already solved this
   pattern better: particles are pure functions of a counter hash
   (`docs/game-state.md`), needing no stream state at all.

## Proposal

### Tier A -- wire the Metal evaluator into world generation (trivial)

Create the platform evaluator once in `MoppeGame` (it returns null on
unsupported platforms, selecting the CPU path by design) and pass it to
both `TerrainEvaluator` constructions.  Verify thread affinity: the
evaluator owns its compute pipeline and can dispatch from the generation
queue; Metal buffer/texture work off-main is already relied on elsewhere
(`docs/renderer-design.md`).  Note the one deliberate CPU-path virtue to
preserve: the interpreter leaves a core free for the loading screen -- the
GPU path frees *all* cores, so this is strictly better.

### Tier B -- counter-based droplet randomness

Replace stream draws with `hash(seed, droplet_index) -> spawn position`
(PCG-style mixing, as dust does).  Consequences: any droplet is
computable independently (a GPU prerequisite), checkpoints shrink (no
mt19937 state), and a droplet subrange can be replayed in isolation for
debugging.  This changes generated worlds for a given seed; do it in the
same release as Tier C and re-bless the profiles.

### Tier C -- lockstep batches as a Metal compute kernel

One thread per droplet per step; heights in a device buffer; per-step
deltas written to per-droplet slots (each droplet stamps at most 4 cells
per step), then a deterministic commit pass: sort stamps by cell, reduce
each cell's stamps **in droplet-index order**, apply.  Ordered segmented
reduction gives bit-stable results independent of GPU scheduling --
determinism by construction rather than by serialization.

One semantic decision must be made explicit: the current CPU code lets a
droplet's `PathMonotone` clamp observe *earlier droplets in the same
step* (`add_change` reads `get(x,y) + changes[index]`), i.e. semantics are
serial-within-step.  The GPU version should clamp against the step's
opening snapshot, with the floor enforced (and the ledger corrected
proportionally) in the ordered commit pass.  This is a versioned semantics
change: document it, re-run the erosion-lifetime experiment
(`research/terrain/erosion-lifetime-experiment.md`), and re-bless goldens.
The sediment ledger and RFC-003's rasters carry over unchanged -- the
commit pass has all deltas in hand.

## Consequences

- Loading-screen source materialization drops by an order of magnitude
  for free (Tier A).
- The research profile's 300k x <=512 steps of droplet work becomes a
  sub-second GPU job (Tier C), and `N` (new world) approaches instant.
- If RFC-001 lands, droplet counts fall to detailing levels and Tier C's
  kernel is comfortably overpowered -- a good position to be in.

## Risks and alternatives

- GPU float atomics would be simpler than sort-and-reduce but are
  schedule-nondeterministic; the codebase treats determinism as an axiom
  (replays, caches, goldens), so ordered reduction is the right cost.
- Fixed-point integer atomics are a middle road (deterministic sums,
  no sort) but change results vs float accumulation and cap precision;
  keep as a fallback if the sort pass measures badly.
- Tier A alone is safe and immediate; B and C ship together behind a
  profile version bump.

## Implementation sketch

1. Tier A + a `MOPPE_PROFILE_CPU=1` before/after measurement.
2. Tier B with a fixed-seed A/B image comparison; update
   `terrain-erosion-experiment` tooling.
3. Tier C kernel + ordered-commit pass; equivalence harness comparing GPU
   vs CPU-with-snapshot-clamp semantics bit-for-bit; then promote.
