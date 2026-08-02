+++
id = "MFX-001"
title = "Freeze the post-overhaul Metal 4 baseline"
rfc = "RFC-0004"
track = "pixel-reconstruction"
status = "done"
depends_on = []
order = 10
areas = ["rendering", "metal", "measurement"]
+++
# Freeze the post-overhaul Metal 4 baseline

## Outcome

The last linear-upscaling renderer has reproducible settings and captures
against which spatial MetalFX can be judged.

## Scope

Record the device, drawable and scene dimensions, preset, seed, water camera,
validation state, and artifact hashes before changing frame targets or pass
order. Keep large image and trace artifacts outside the repository.

## Acceptance

- `high`, `balanced`, and `low` use seed 123 and the same lake forcing view.
- A live macOS run completes with the Metal debug layer enabled.
- The baseline record says how to reproduce its external artifacts.

## Evidence

[The findings](../findings.md#system-and-baseline) record the M2 Pro, OS and
SDK, seed 123 lake camera, resolved high/balanced/low dimensions, debug-layer
state, external artifact location, hashes, and reproduction inputs. The
baseline also exposed the acquired-drawable sizing race fixed by this track.
