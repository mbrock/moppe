# Current status

Moppe is a motorcycle game in generated worlds and the repository's main
product. Its active engine grows one periodic landscape from a seed, analyzes
water, ecology, and trails over that landscape, starts a mutable riding
session, and presents each frame through Metal or WebGPU.

The game is working while its world model is becoming more explicit. Current
work is consolidation, not replacement: finish the migrations opened by the
terrain harmonization, keep the workshops as experiments, and make lake
identity the first concrete relational world model.

## What exists

- Direct finite terrain construction: geology, stream-power evolution, and
  trail formation over one `terrain::TerrainDomain`.
- Typed `SurfaceGeometry`, `SurfaceReadings`, and `WaterSheets` bundles.
- Standing-water, lake, drainage, fractional-channel, river, trail, moisture,
  geology, and ecology analyses.
- One completed `GeneratedWorld`, one mutable `GameSession`, and one immutable
  `FrameView` per visible frame.
- A game-shaped renderer with the full Metal presentation on Apple platforms
  and a playable lower-cost WebGPU backend.
- Motorcycle, car, walking, hang-glider, generated trail circuit, cinematic
  tour, forests, rivers, lakes, atmosphere, and post-processing.
- Deterministic captures, graphics replay benchmarks, source analysis, typed
  Arrow bundle persistence, and 200 focused tests.

## Completed consolidation

The realized
[engine-consolidation track](../planning/tracks/engine-consolidation/README.md)
records the closure sequence:

1. publish the post-harmonization architecture accurately;
2. make water-sheet presentation borrow typed columns directly;
3. retire the staged numerical-exit helpers;
4. restore Moppe as the ordinary repository workflow; and
5. establish lake identity as the first production graph domain.

The completed
[current-engine-refactoring track](../planning/tracks/current-engine-refactoring/README.md)
remains the history of the ownership work. It is not an active backlog.

## Deliberate gaps

- The generated world is deterministic for supported fixed inputs and
  toolchains, not promised bit-identical across every machine and compiler.
- `GameState` replays a session on one world; it does not capture world
  generation, loading, window state, or renderer history.
- WebGPU has a deliberately cheaper presentation than Metal. Android remains
  unimplemented.
- Water-body identity is a first-class per-world domain, but not yet a
  persistent identity across regeneration, split, merge, or evolving levels.
- Roads, settlements, enduring player traces, and evolving inhabited places
  remain later design work.

## Workshops

Atelier, Lavoir, and Étalon are proofs beside the engine:

- Atelier explores registered spaces, embeddings, and organisms.
- Lavoir proves page-aligned Arrow ownership lent directly to Metal.
- Étalon tests a smaller compile-time quantity and bundle vocabulary in Zig.

None is a second implementation of Moppe. A workshop result enters the game
only when a current Moppe consumer makes the smaller mechanism worthwhile.

## Verification

The normal local checks are:

```sh
make
make test
make web
make check-format
make plan
```

A short real world-generation and renderer handoff uses:

```sh
MOPPE_TERRAIN_PROFILE=smoke tools/capture-game /tmp/moppe-smoke.png
```

Feature-targeted water captures and the graphics benchmark provide the
stronger visual and performance acceptance paths described in `AGENTS.md`.

## History

The dated [project notebook](project.org) records the July 2026 terrain and
water campaign. Its old present-tense subsystem descriptions are historical;
this page, the [engine atlas](engine-atlas.md), and executable planning tracks
describe the current repository.
