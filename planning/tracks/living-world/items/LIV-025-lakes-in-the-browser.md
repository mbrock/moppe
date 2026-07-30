+++
id = "LIV-025"
title = "Give the browser its lakes"
rfc = "RFC-0003"
track = "living-world"
status = "ready"
depends_on = []
order = 170
areas = ["rendering", "webgpu", "water"]
+++
# Give the browser its lakes

## Outcome

The WebGPU build shows standing water where the world has standing water.

## Scope

`WebGpuRenderer::set_ocean` takes the water-sheet texture and discards it,
baking one flat translucent quad at the sea datum. Every inland body — every
tarn, every lake the hydrology worked to discover and classify — is invisible
in the browser. Eight terrain reading setters are likewise base-class no-ops
there.

WebGPU is allowed a cheaper presentation. It is not allowed to silently
disagree with the world about what exists. Bind the sheet elevation and
amplitude and displace to it, as Metal does; then decide, deliberately and in
the atlas, which of the eight readings the browser will carry and which it
will not.

## Acceptance

- A browser capture of a seed with mountain lakes shows them.
- The engine atlas states which readings WebGPU carries, accurately.

## Research

None needed; this is the atlas telling the truth.
