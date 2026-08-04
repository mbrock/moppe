# Temporal MetalFX implementation findings

Date: 2026-08-04

## Result

Moppe now has an explicit `--upscaling temporal` path on supported Metal 4
Apple GPUs. It reconstructs the untouched linear HDR world before post
processing and retains spatial MetalFX and exact linear enlargement as ordered
fallbacks. Spatial remains the default until broader physical-device and
long-session evidence justifies changing policy.

The implemented temporal inputs are:

- a Halton-jittered, single-sample RGBA16F color scene;
- persistent reversed-Z Depth32Float;
- RG16Float motion in input-pixel coordinates, excluding projection jitter;
- a private 1x1 R16Float exposure texture;
- an R8Unorm material-reactive mask;
- explicit history reset on target, camera, world, and replay transitions.

Static geometry uses current/previous unjittered camera matrices. Dust and
wind-driven undergrowth evaluate their previous simulation time. Retained
moving meshes use stable IDs and previous model transforms; the immediate
actor stream retains previous vertices when its topology matches and rejects
history when it does not. Water, waterfalls, alpha particles, foliage, and
procedural growth use increasing reactive weights according to how unreliable
their geometric motion is.

## Local validation

The implementation was built with Xcode 26.5 and exercised on an Apple M2,
macOS 26.5. The normal macOS build and unit suite pass. Fresh iOS and tvOS
simulator apps build against their explicit Metal 4-unavailable stub, and
fresh unsigned `iphoneos` and `appletvos` app builds compile/link the shared
production Metal/MetalFX backend. A temporal gameplay
capture under `MTL_DEBUG_LAYER=1` resolved to temporal at 858x482 into a
1280x720 drawable, selected 1x scene rasterization, completed without a Metal
validation error, and wrote `/tmp/moppe-temporal-validation.png`.

A deterministic 30-frame, 30 fps cinematic at the same resolution exercised
camera translation/rotation, fine terrain, distant trees, animated sky, and
the history path. Six evenly spaced inspected frames remained coherent with no
obvious disocclusion trail, edge discontinuity, or exposure pulse. The local
video is `/tmp/moppe-temporal-1s.mp4`, SHA-256
`f1b7cbc208574443f1a827d883d96a842b3aaa3d8bd6c6ba419b24a9a96cbf83`.

A short matched 64-configuration GPU smoke benchmark used a 1280x720 drawable,
858x482 scene, seed 123, one settle frame, and two samples per configuration.
The all-feature temporal samples were 17.456 and 17.225 ms; spatial samples
were 19.549 and 20.799 ms. Overall medians were 16.716 ms temporal and 18.580
ms spatial. This is proportional smoke evidence, not a cross-device claim; the
expected local advantage comes largely from temporal's 1x scene replacing
spatial's 4x MSAA while paying for depth, motion/reactivity, and history.

## Follow-up evidence

Before making temporal the default, repeat the deterministic cinematic and
feature benchmark on representative iPhone/iPad and Apple TV hardware, inspect
fast close actors, waterfalls, spray, thin foliage, camera cuts, and dynamic
resolution changes, and collect longer GPU/thermal traces. The existing
requested/resolved logging makes unsupported-device and scaler-creation
fallbacks directly observable.
