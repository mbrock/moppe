# Reflection geometry findings

Date: 2026-08-02

Verdict: keep this code as an opt-in atelier foundation and proceed to one
raw, low-resolution water-reflection signal. Do not enable it in ordinary
gameplay yet. The terrain representation and ray traversal are coherent and
small enough to continue, but the cold build cost and worst-case decimation
error rule out treating this exact proxy as a finished production choice.

## Forcing case and ownership

`tools/capture-reflection-geometry OUTPUT.png` runs seed 123, the fast terrain
profile, the existing deterministic lake camera, sixty settling frames, and
the normal water screenshot path. It writes the untouched scene PNG, a
four-panel reflection-geometry PNG, and a text report beside it. The large
artifacts remain outside the repository; the final validated set is under
`/tmp/moppe-reflection-geometry-goal0`.

The proxy is backend presentation data. `MetalRenderer::set_terrain` borrows
the completed typed surface and retains its heights only when the atelier is
requested. It samples a bounded periodic window, uploads a triangle soup,
builds one terrain-only primitive acceleration structure, then releases the
CPU height copy. `MetalTerrainResources` retains the proxy and acceleration
structure until terrain replacement or renderer teardown. No simulation,
water-domain, tree, actor, or whole-world geometry moved into the backend.

The ordinary water path is structurally untouched. The opt-in query writes a
separate shared diagnostic buffer after the ordinary drawable has already
been copied for capture; no diagnostic value enters scene color, water
shading, MetalFX, exposure, or presentation.

## Proxy and traversal evidence

The forcing proxy uses source stride 8 over a roughly 4.1 by 4.1 kilometre
window ahead of the lake camera:

| measure | result |
| --- | ---: |
| coarse cells | 105 by 105 |
| triangles | 22,050 |
| vertex buffer | 793,800 B |
| acceleration structure | 3,881,984 B |
| retained GPU total | 4,675,784 B |
| transient build scratch | 4,273,664 B |
| peak build GPU total | 8,949,448 B |
| temporary authoritative height copy | 4,194,304 B |
| height error RMS / p95 / max | 1.383 / 2.745 / 22.716 m |
| projected error p95 / max at 2560x1600 | 2.694 / 21.600 px |

The raw diagnostic is intentionally closer to the input inspection shown in
Apple's 2025 path-tracing and MetalFX session than to a beauty render. Its
quadrants are world-space hit normal, logarithmic hit distance,
primitive/barycentric topology, and hit/miss mask. Inspection shows a coherent
terrain silhouette, continuous facets and normals, a clean sky miss region,
and the expected rectangular proxy boundary. Nothing is hidden behind a
denoiser or water composite.

The portable tests prove a flat surface is exact, discarded relief is
measured, and a proxy window crossing the toroidal seam wraps its source
samples while preserving unwrapped world positions. All 211 macOS tests pass.

## Construction timing and hardware boundary

Seven separate M2 Pro launches measured proxy generation near 9.1--10.0 ms.
Acceleration-structure builds were 600.0, 31.6, 21.2, 132.1, 29.9, 19.3, and
20.3 ms: median 29.9 ms, with a material first cold initialization and
occasional process-launch variability. The atelier performs this work only on
the requested capture after the forcing frame is encoded, so it cannot become
an unnoticed first-gameplay-present stall. A production continuation needs an
explicit loading/publication schedule and its own device measurements.

The M2 Pro supports Metal ray tracing but rejected a Metal 4 acceleration-
structure descriptor at runtime. The working path therefore builds through
the established Metal acceleration-structure command encoder and queries the
result from Moppe's Metal 4 compute command buffer and argument table. This is
an actual capability distinction: a later hardware-specific Metal 4 builder
may be added, but it is not required to keep the representation proof honest.

## Validation and next boundary

The final seed-123 lake capture completed with `MTL_DEBUG_LAYER=1`, printed
`Metal API Validation Enabled`, and reported no validation error. macOS build,
CTest, iOS and tvOS simulator builds, and iOS and tvOS device-SDK builds all
pass. Every Apple SDK compiled the new Metal 4 ray query shader.

Goal 1 should now ask one visual question: does a sparse, low-resolution ray
signal from actual water origins produce a materially better bank and terrain
reflection than Moppe's analytical environment? It should reuse this BLAS and
publish raw reflected radiance, hit distance, and hit normal before any
composite. It should not yet add actors, trees, refits, denoising, temporal
upscaling, frame interpolation, shadows, ambient occlusion, or global
illumination.
