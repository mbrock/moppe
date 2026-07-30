# Current status

Moppe is a motorcycle game in generated worlds and the repository's main
product. Its active engine grows one periodic landscape from a seed, analyzes
water, ecology, and trails over that landscape, starts a mutable riding
session, and presents each frame through Metal or WebGPU.

The game is working and its world model is explicit. The consolidation that
made it so is complete; current work is the
[living-world track](../planning/tracks/living-world/README.md), which spends
the fields the generator already computes on what the ground, the plants, and
the water are allowed to say about themselves.

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

## Current work

[RFC-0003](../planning/rfcs/0003-a-world-that-reads-as-alive.md) is the active
campaign: spend what the generator already knows before adding more simulation.
Its finding was that the engine computed far more than it showed, and the first
six items bore that out — material bands that compared a metre elevation to a
normalized threshold, a tree line fixed at 145 m on a world with 500 m of
relief, a moisture field with no slope term, and an erosion law with no
hillslope regime at all, which is why every hillside combed.

Those are done. The ground now grades against its own relief, tree habitat
reads a topographic wetness index, the forest mosaic answers to the ground
instead of overruling it, the wind runs on three clocks, and the river surface
no longer breathes in unison.

The near-ground material has had its loudest fault removed: relief below the
lattice is now one band-limited world-space field with analytic gradients,
rather than a micro-normal read out of the screen derivatives of the albedo.
That signal had no wavelength, so near ground resolved to a carpet of
one-pixel static at every distance and crawled with the camera. What remains
open there is character rather than legibility — the splat sources still
carry all the close-range identity, and the metre-scale geometry of
[RFC-008](../plan/rfc-008-geology-conditioned-tessellation.md) is untouched.
Also still open: water kinds (junctions, lips, shorelines, the rider in the
water), and lakes in the browser.

The corrugation investigation is written up in
[hillslopes and channels](hillslopes-and-channels.md), negative results
included; where the water's GPU cost actually sits, and two optimizations
that measured as nothing, are recorded in
[renderer design](renderer-design.md).

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
