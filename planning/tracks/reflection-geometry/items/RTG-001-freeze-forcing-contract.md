+++
id = "RTG-001"
title = "Freeze the reflection geometry forcing contract"
rfc = "RFC-0004"
track = "reflection-geometry"
status = "done"
depends_on = []
order = 10
areas = ["rendering", "metal", "measurement"]
+++
# Freeze the reflection geometry forcing contract

## Outcome

Goal 0 has one deterministic camera, one source surface, and an artifact
contract that cannot silently turn into a water-rendering rewrite.

## Scope

Use the existing seed-123 lake capture. Require an untouched ordinary scene,
an independently inspectable ray-query image, and a machine-readable text
report. Keep every new path opt-in and out of ordinary water composition.

## Acceptance

- The forcing command names its seed, feature, and external artifacts.
- The ordinary scene and diagnostic are separate outputs.
- Frame interpolation, denoising, and water compositing remain out of scope.

## Evidence

`tools/capture-reflection-geometry` wraps the lake capture with sixty settling
frames and writes scene, diagnostic, and report artifacts. The renderer only
activates the path when `MOPPE_REFLECTION_GEOMETRY` names an output path.
