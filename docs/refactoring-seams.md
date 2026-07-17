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
| Trail, home-base, channel-flux, moisture, waterline, geology, and ecology readings remain typed until presentation; a Terrain Lab rebuild uses that same path bridge. | The focused materialization tests, including `surface_presentation_materializes_preview_trails_at_the_bridge`, in `tests/map/surface_test.cc` |
| Ground and water use the same domain while retaining distinct section bundles; water datum normalization and ocean setup happen only in `WaterPresentation`. | `tests/map/water_surface_test.cc` |

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

`game::GameSession` owns mutable running state against one completed world.
Its copyable `game::GameState` checkpoint intentionally excludes generated
terrain, resident resources, renderer history, and asynchronous loading. Each
system that participates exposes a plain `state()` / `restore()` pair.

`advance_game_session(context, session, input, seconds_t)` is the ordinary
fixed-step seam. Its context names the completed-world values simulation reads
without coupling it to `GeneratedWorld`; the application still selects input,
advances weather during paused modes, and realizes platform effects. The
central playable branch of `MoppeGame::tick` is only that delegation.

`tests/game/game_state_test.cc` owns the restoration contracts for vehicle,
camera, walker, glider, stars, dust, independent `GameState` copying,
same-world `GameSession` checkpoint replacement, and an adapter-recorded input
tape replayed through that public advance operation.

`GraphicsBenchmarkReplay` materializes distinct prelude and replay tapes and
describes the pre-step epoch, Gray-code partition mask, logical frame, and
measurement flag. `MoppeGame` owns the session checkpoint, renderer-history
reset, graphics-mask application, and GPU-drain lifecycle around that
renderer-free schedule; benchmark state therefore does not enter ordinary
simulation. `graphics_benchmark_replay_reuses_the_public_session_tape` drives
the schedule through `advance_game_session` against one completed world.

## Frame presentation

`game::FrameView` is the immutable presentation reading of a finished world
frame. `MoppeGame` selects the active cinematic, Terrain Lab, inspection, or
chase camera, then composes camera, lighting, weather, graphics choices,
mode-specific visibility, benchmark coordinates, mover poses, HUD values, and
cinematic overlay readings before encoding renderer parameters. It is not a
scene graph and owns no renderer or platform type.

The view intentionally carries two camera readings: the unshaken body and
culling direction used for terrain and forest selection, plus the shaken view
basis in `FrameEnv` used by billboards. `FrameHud` and `FrameOverlay` freeze
every gameplay-HUD, trail-map, and cinematic-prompt value alongside those
camera and mover readings.

After application-state selection and capture requests, `MoppeGame::render`
encodes `FrameParams` and delegates the ready-world frame in explicit order:
world, actors, water surfaces, effects, then overlays. Each routine receives
the same `FrameView`; only `Stars` and `Dust` remain read-only session
presentation resources. The temporal lens-flare accumulator updates in
`tick()` after the active camera advances; `compose_frame_view` and rendering
are read-only.

`tests/game/frame_view_test.cc` owns the pure-composition contract: no session
mutation, frozen mover poses, shake behavior, cinematic/Terrain Lab selection,
HUD and overlay snapshots, and flare-sightline readings.

## Metal frame encoding

`MetalRenderer` is a private backend facade, not a second renderer API or a
generic render graph. It retains four concrete ownership values:
`MetalTerrainResources` for one completed world's terrain, shadow, and
terrain-presentation textures; `MetalWaterResources` for the matching ocean,
standing-water, and flow state; `MetalFrameTargets` for resizeable scene,
ping-pong, feedback, bloom, probe, and exposure targets; and
`MetalFrameEncoding` for one drawable command-buffer submission.

The Terrain, Water, Scene, Post, and HUD operations take explicit borrows of
those values. Terrain, Water, and Scene intentionally encode through the same
lazy scene render encoder, so their names do not create independent clears,
resolves, or depth histories. Post closes that common encoder before its
ping-pong sequence, and HUD performs the final composite even for an empty
2D list. The fixed order remains game-shaped; no generic scheduling framework
is introduced.

The command-buffer lifecycle remains outside individual passes. The facade
owns drawable acquisition, in-flight stream selection, timestamp-span labels,
benchmark completion, ready-world Metal capture, screenshots, commit, and
temporal-target reset. A move across these ownership boundaries must preserve
the existing `GpuPass` timing names, capture-after-ready-world window, and
benchmark sample completion contract. `./build/moppe-testbed`, the compact
graphics benchmark, feature water captures, and an `MOPPE_METAL_CAPTURE`
trace are the integration checks for this seam; portable unit tests alone do
not exercise the Metal backend.

## Completed-world smoke path

`tools/capture-water OUTPUT river` is the deterministic integration smoke
path. It starts the normal loading flow, waits for world generation, terrain
and hydrology analysis, the single main-thread completed-world handoff, and
renderer activation, then writes a river screenshot. Its deterministic inputs
are seed `123` and the `fast` terrain profile unless overridden by `MOPPE_SEED`
and `MOPPE_TERRAIN_PROFILE`.

`MOPPE_REGENERATE_ONCE=1` exercises the same path twice: while the second
candidate builds, the first active world and its session remain owned and
valid. At activation, the old session releases its terrain borrows before the
old world retires, and a fresh session binds to the completed world. Generation
is intentionally single-flight rather than cancellable asynchronous
infrastructure.

The screenshot is deliberately **not a golden**: it proves that loading
reaches a usable generated world, while visual look remains under the
feature-specific water-capture workflow. Do not commit captures from this
command. A future image golden must name its owning test/tool, fixed inputs,
comparison tolerance, and explicit update command before entering the tree.
