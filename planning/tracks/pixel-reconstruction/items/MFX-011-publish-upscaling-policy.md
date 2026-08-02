+++
id = "MFX-011"
title = "Publish typed requested and resolved upscaling policy"
rfc = "RFC-0004"
track = "pixel-reconstruction"
status = "done"
depends_on = ["MFX-010"]
order = 30
areas = ["rendering", "game", "configuration", "tests"]
+++
# Publish typed requested and resolved upscaling policy

## Outcome

Upscaling is a supported graphics choice rather than backend folklore.

## Scope

Carry a typed `linear | spatial` request from launch settings through
`FrameView` to the renderer. Print the request and the backend's resolved
`native | linear | spatial` mode with dimensions and fallback reason. Make
spatial the default request while preserving an exact CLI comparison path.

## Acceptance

- `--upscaling linear|spatial` parses and rejects unknown values.
- Graphics settings output contains the requested mode.
- Target-recreation diagnostics contain requested and resolved modes.
- Unit tests cover defaults, parsing, printing, and propagation.

## Evidence

`render::UpscalingMode` flows from graphics settings and
`--upscaling linear|spatial`, through `FrameView`, into `FrameParams`.
Startup settings print the request; target diagnostics print requested and
resolved `native|linear|spatial` modes plus dimensions and fallback reason.
The graphics-settings, launch-options, and frame-view tests cover the public
contract.
