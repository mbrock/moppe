# Spatial MetalFX findings

Date: 2026-08-02

Verdict: keep spatial MetalFX as the default request on supported macOS and
iOS Metal 4 devices. Preserve `--upscaling linear` as the exact comparison and
fallback path. Do not infer anything about temporal reconstruction or ray
tracing from this result.

## System and baseline

The implementation and live comparison used an Apple M2 Pro (16 GPU cores),
macOS 26.5.2, Xcode 26.5, and the Metal 4 renderer at `3378512` before the
target and pass changes. Every capture used seed 123, the fast terrain profile,
and `MTL_DEBUG_LAYER=1`. The logs contain `Metal API Validation Enabled` and no
validation error.

The pre-change lake captures froze the preset behavior:

| preset | drawable | scene | scale | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| high | 2560x1600 | 1280x800 | 0.5 | `4bedc0d26d511fcc4b46930fc84d01abc5d6db6c387886bfa0c1590830333f0e` |
| balanced | 2560x1600 | 853x533 | 0.333333 | `05887349511f7d8049c65672a350bdb859ff68b7d2b5da49e95eb48015bb547e` |
| low | 2560x1600 | 640x400 | 0.25 | `12f5849b68b08ccbe16990934dbc017d0a1b484361f77e974bb4b7ebd506837d` |

The files and logs remain outside the repository under
`/tmp/moppe-metalfx-baseline`. They can be reproduced with
`tools/capture-water OUTPUT lake`, `MOPPE_SEED=123`,
`MOPPE_TERRAIN_PROFILE=fast`, `MTL_DEBUG_LAYER=1`, and each graphics preset.

Those first captures also exposed a pre-existing startup race: the view's
reported drawable size could disagree with the acquired drawable texture. One
PNG consequently had a different output size despite the logged target size.
The renderer now derives target and scale dimensions from the acquired
drawable. The baseline hashes remain useful for freezing the old settings and
camera, but the matched comparison below is the quality evidence.

## Matched visual comparison

The final comparison uses two frozen gazetteers after six settling frames.
Both render the same world and cameras from a 960x600 linear HDR scene into a
1920x1200 drawable with high graphics and 4x MSAA. The only changed input is
`--upscaling linear` versus `--upscaling spatial`. Diagnostics resolve the two
modes to `linear` and `spatial`, respectively.

| view | linear SHA-256 | spatial SHA-256 |
| --- | --- | --- |
| lake shore | `f7067abe329e5dfade5ca99985952a720f536df1ba012b4a5370ba629c1fc369` | `d50265cf7c7e5867b506c7f2d6737617a6fcc79121316045887706c946039cfb` |
| lake overview | `4fe759dbf45e3cba68b42c1ca1f6190e90d36f70a0c03d05bdb874c8de91f62a` | `2acc4acb5fe04cbb13a604bae4391979ea783dbbb021ba67c6c49c8f18caac6a` |

The external artifacts are under `/tmp/moppe-metalfx-gazetteer/{linear,spatial}`.
Reproduce either half with the gazetteer executable arguments
`--windowed --window-size 960x600 --inactive --seed 123 --terrain-quality fast
--graphics-quality high --gazetteer-settle 6`, `MOPPE_RENDERSCALE=0.5`, and
the corresponding upscaling mode.

Inspection finds a material improvement rather than a merely different
filter: tree crowns, the far forest, shore boundaries, and mountain detail are
sharper in the spatial result. Water color, exposure, bloom, and the final
grade remain consistent, with no obvious still-frame ringing. The scaler sees
linear HDR; tone mapping, grading, EDR output, lens treatment, and the HUD stay
in the native-resolution present pass.

A final four-second, 120-frame cinematic at 30 fps exercised the spatial path
under the debug layer. Eight consecutive inspected frames retain coherent
terrain, trees, sky, flare, and text during the camera move, without an obvious
edge discontinuity or newly visible flicker. The external video is
`/tmp/moppe-metalfx-spatial.mp4` (SHA-256
`0c23493ca54350f63743d3ae663fc8c6c44dd02703785839fff5731c359bd7be`).

## Matched GPU comparison

The partition benchmark used the same 960x600 input and 1920x1200 output,
seed, terrain, high preset, and autopilot for both modes. It measured eight
frames for each of 64 hot-feature configurations after a 30-frame prelude and
two-frame settle: 512 samples per mode. `tools/graphics-benchmark-analyze`
wrote the summaries under `/tmp/moppe-metalfx-benchmark-fixed`.

| measure | linear | spatial | delta |
| --- | ---: | ---: | ---: |
| all features, median | 7.971 ms | 8.880 ms | +0.909 ms |
| fastest configuration median | 6.613 ms | 7.139 ms | +0.526 ms |
| slowest configuration median | 8.558 ms | 8.880 ms | +0.322 ms |
| median of 64 paired median deltas | | | +0.539 ms |
| mean of 64 paired median deltas | | | +0.480 ms |

The per-pass counter stream now names `upscale`, but Metal 4 relaxed timestamp
intervals can overlap and are too coarse here to attribute the sub-pass cost
reliably. The matched whole-frame A/B is the cost evidence. This short run is
enough to keep the mechanism, not to generalize its cost across Apple devices.

## Integration result

- One renderer-lifetime `MTL4Compiler` creates scalers only when target
  dimensions or formats change.
- Scaler-reported input and output usage flags participate in allocation and
  residency.
- An explicit fence orders the untracked raster writer, MetalFX encoder, and
  native present reader inside Moppe's existing Metal 4 command buffer.
- Requested and resolved modes, dimensions, and fallback reasons are printed.
  Native-size scenes resolve `native`; unsupported/scaler-failure cases and
  tvOS retain linear enlargement.
- macOS tests and builds, iOS/tvOS simulator builds, and actual iOS/tvOS device
  SDK builds complete. The device builds compile the production renderer that
  the simulator intentionally replaces with a Metal 4 unavailable stub.

Spatial reconstruction therefore clears this track's quality, ownership,
fallback, and performance gate. Temporal inputs and ray-traced water remain
separate follow-on tracks under RFC-0004.
