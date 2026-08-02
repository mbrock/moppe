+++
id = "RTG-012"
title = "Measure and record the reflection geometry verdict"
rfc = "RFC-0004"
track = "reflection-geometry"
status = "done"
depends_on = ["RTG-011"]
order = 40
areas = ["rendering", "metal", "measurement", "docs"]
+++
# Measure and record the reflection geometry verdict

## Outcome

The repository says whether the geometry proof justifies one water-ray
experiment and records the constraints that must survive that continuation.

## Scope

Run the deterministic capture under Metal validation, measure multiple
process launches, inspect the raw panels, run portable tests and Apple build
targets, and record a narrow keep-or-drop verdict.

## Acceptance

- Timing includes cold-start variability instead of one favorable build.
- Memory distinguishes retained, temporary CPU, and transient scratch costs.
- macOS tests and iOS/tvOS simulator and device-SDK builds pass.
- The verdict does not imply production water, denoising, or frame generation.

## Evidence

The [findings](../findings.md) record seven build measurements, proxy error and
memory, the M2 Pro Metal/Metal 4 builder boundary, live validation, the Apple
build matrix, and the decision to keep only an atelier foundation for Goal 1.
