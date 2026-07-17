# Refactoring seams

RFC-0001 moves proven boundaries one at a time. This page names the observable
contracts that must survive those moves. It is a characterization baseline,
not a second architecture proposal.

## Surface

`map::Surface` is the materialized ground reading over one `SurfaceDomain`.
Its `SurfaceAtlas` owns geometry plus named optional hydrology, geology,
ecology, and use sections; `game::SurfacePresentation` is the only bridge that
turns them into renderer lanes.

| Contract | Characterization owner |
| --- | --- |
| Continuous elevation and normal reads agree with the authoritative heightmap, including the periodic seam. | `surface_reconstruction_matches_bounded_heightmap_interpolation` and `surface_reconstruction_matches_periodic_seam_interpolation` in `tests/map/surface_test.cc` |
| Mutating the heightmap does not change surface reads until `Surface::refresh`; refresh clears dependent materialized sections. | `surface_refresh_is_an_explicit_materialization_barrier` and `surface_presentation_is_the_numeric_bridge_for_typed_sections` in `tests/map/surface_test.cc` |
| Trail, home-base, channel-flux, moisture, waterline, geology, and ecology readings remain typed until presentation. | The focused materialization tests in `tests/map/surface_test.cc` |
| Ground and water use the same domain while retaining distinct section bundles. | `tests/map/water_surface_test.cc` |

ENG-010 and ENG-011 may change the concrete bundle and grouping mechanism, but
not these sampling, refresh, and ownership contracts. ENG-012 owns any change
to the numeric presentation bridge.

## Terrain replay

`map::TerrainEvaluator::checkpoint()` is the generation replay seam. A
checkpoint restores the evaluator's mutable channel-memory state, then the
remaining program suffix must produce the same completed height field.

`orogeny_channel_memory_survives_a_checkpoint` and
`checkpoint_resume_matches_complete_replay` in
`tests/map/terrain_evaluator_test.cc` own this contract. ENG-020 and ENG-021
may expose recipes and transform editing more directly, but must preserve the
checkpoint/resume result.

## Running-game replay

`game::GameState` is a copyable checkpoint of mutable running state. It
intentionally excludes generated terrain, resident resources, renderer
history, and asynchronous loading. Each system that participates exposes a
plain `state()` / `restore()` pair.

`tests/game/game_state_test.cc` owns the restoration contracts for vehicle,
camera, walker, glider, stars, dust, and independent `GameState` copying.
The fixed-step benchmark loop described in [game-state.md](game-state.md)
uses this seam; ENG-030 through ENG-033 will make its input and advance
boundary explicit without expanding what a checkpoint owns.

## Completed-world smoke path

`tools/capture-water OUTPUT river` is the deterministic integration smoke
path. It starts the normal loading flow, waits for world generation, terrain
and hydrology analysis, main-thread world assembly, and renderer activation,
then writes a river screenshot. Its deterministic inputs are seed `123` and
the `fast` terrain profile unless overridden by `MOPPE_SEED` and
`MOPPE_TERRAIN_PROFILE`.

The screenshot is deliberately **not a golden**: it proves that loading
reaches a usable generated world, while visual look remains under the
feature-specific water-capture workflow. Do not commit captures from this
command. A future image golden must name its owning test/tool, fixed inputs,
comparison tolerance, and explicit update command before entering the tree.
