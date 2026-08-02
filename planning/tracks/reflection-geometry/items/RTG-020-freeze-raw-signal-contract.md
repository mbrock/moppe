+++
id = "RTG-020"
title = "Freeze the raw water-reflection signal contract"
rfc = "RFC-0004"
track = "reflection-geometry"
status = "done"
depends_on = ["RTG-012"]
order = 50
areas = ["rendering", "metal", "measurement"]
+++
# Freeze the raw water-reflection signal contract

## Outcome

Goal 1 has one small, independently inspectable signal and cannot silently
become a water-compositing or temporal-reconstruction project.

## Scope

Use the deterministic lake camera and Goal 0 terrain proxy. Publish actual
standing-water origin, optical normal, raw radiance, hit normal, hit distance,
and validity at quarter linear drawable resolution. Exclude running water,
instances, composition, history, denoising, and frame interpolation.

## Acceptance

- The ordinary scene and six-panel diagnostic are separate artifacts.
- Every raw signal remains independently inspectable.
- A miss uses the existing procedural sky and ordinary water remains fallback.

## Evidence

`tools/capture-water-reflection-signal` freezes the seed, lake feature,
settling interval, scene artifact, diagnostic, and machine-readable report.
