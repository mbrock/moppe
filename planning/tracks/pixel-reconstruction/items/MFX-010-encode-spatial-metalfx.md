+++
id = "MFX-010"
title = "Encode spatial MetalFX inside the Metal 4 frame"
rfc = "RFC-0004"
track = "pixel-reconstruction"
status = "done"
depends_on = ["MFX-001"]
order = 20
areas = ["rendering", "metal", "metalfx"]
+++
# Encode spatial MetalFX inside the Metal 4 frame

## Outcome

A supported Apple device can reconstruct the linear HDR scene to drawable
resolution before final color treatment and the native HUD.

## Scope

Own one Metal 4 compiler on the renderer and one spatial scaler at target
configuration lifetime. Allocate inputs and outputs with the scaler-reported
usage flags, retain them in residency, synchronize the untracked MetalFX work,
and encode it into the existing command buffer. Keep the current linear sample
as the named fallback.

## Acceptance

- The scaler is not created per frame.
- HDR scene values reach the existing tone-map, grade, EDR, and HUD sequence.
- GPU timing reports spatial reconstruction separately.
- Unsupported devices and tvOS select linear enlargement explicitly.

## Evidence

`MetalRenderer` owns one compiler and target-lifetime scaler, applies the
scaler-reported usage flags, keeps its output resident, and uses one fence to
order raster, MetalFX, and present work in the existing Metal 4 command
buffer. Tone mapping, grade, EDR, lens treatment, and HUD remain in the native
present pass. [The findings](../findings.md#integration-result) record the
fallback and platform results.
