+++
id = "MFX-012"
title = "Validate and record the spatial MetalFX result"
rfc = "RFC-0004"
track = "pixel-reconstruction"
status = "done"
depends_on = ["MFX-011"]
order = 40
areas = ["rendering", "metal", "measurement", "docs"]
+++
# Validate and record the spatial MetalFX result

## Outcome

The repository records whether spatial MetalFX improved Moppe rather than
merely compiling.

## Scope

Compare linear and spatial reconstruction at identical input and output
dimensions using deterministic water and gameplay views. Build all Apple
platform projects, run tests and the Metal debug layer, and record timing,
captures, hashes, fallbacks, and the keep-or-drop verdict.

## Acceptance

- macOS build/tests and iOS/tvOS simulator builds pass.
- A spatial capture completes without Metal validation errors.
- Linear and spatial captures share logical frame and dimensions.
- The track records the visual/performance verdict and retained fallback.

## Evidence

[The findings](../findings.md) record matched 960x600-to-1920x1200 lake
captures and hashes, 512 samples per mode across the 64-configuration graphics
benchmark, the +0.539 ms median paired cost, and the keep verdict. macOS,
iOS/tvOS simulator, and iOS/tvOS device-SDK builds pass; debug-layer captures
resolve spatial without validation errors. Linear remains selectable and is
the explicit unsupported/tvOS fallback.
