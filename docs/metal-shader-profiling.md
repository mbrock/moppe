# Profiling Moppe shaders on the M5

`tools/metal-profile INPUT.gputrace OUTPUT.gputrace` opens a captured
frame in Xcode, replays it with profiling enabled, exports it with its
performance data, and prints the ranked shaders as JSON. With one argument
it reads the embedded profile from an existing export. Both forms check
that the complete shader table contains a positive cost.

## Capture one frame

From the repository root on macOS, start a capture-enabled game. Pick the
camera, time of day, and scene you want before capturing. This example uses
the trailhead and a fixed render scale. The window size is in points.

```sh
cmake --build build --target moppe
run=$(mktemp -d "${TMPDIR:-/tmp}/moppe-profile.XXXXXX")
MTL_CAPTURE_ENABLED=1 MOPPE_OPENING=none MOPPE_CLOCK=12 \
  ./build/moppe.app/Contents/MacOS/moppe --windowed \
  --window-size 960x540 --render-scale 0.5 >"$run/game.log" 2>&1 &
game=$!
```

Wait for `world ready` in the log and for the view to settle. In the same
shell, capture one present-to-present frame. With Moppe's single Metal layer,
`gpucapture` picks the layer boundary by default. If it asks which boundary
to use, run `gpucapture boundaries --pid "$game"` and pass the **Layer**
ID with `--boundary ID`. Once capture finishes you can quit the game.

```sh
gpucapture start --pid "$game" --count 1 --output "$run/frame.gputrace"
```

For comparisons, keep the revision, local shader edits, seed, camera,
drawable size, render scale, and warmup with each capture. The game log
records drawable pixels and world initialization. A single gameplay frame
here occupied 1.1 GiB before export. Use the scene recipes in
[features.md](features.md) for a particular effect.

## Replay, export, and check

```sh
tools/metal-profile "$run/frame.gputrace" "$run/profiled.gputrace" \
  > "$run/shaders.json"
jq '.[:5]' "$run/shaders.json"

# Read the same samples again later.
tools/metal-profile "$run/profiled.gputrace"
```

The two-argument form uses Swift, an English-language Xcode, and a logged-in
Mac desktop. Grant the terminal or agent that runs it **Accessibility**
in System Settings → Privacy & Security. Keep Xcode in front for the save
panel. The helper matches the input document URL, checks the profiling
checkboxes, waits for analysis and archive writing, then stops the Xcode
replay. It expects a new output name in an existing directory. The
one-argument form only needs Python 3 and Apple's `gpudebug` on a compatible
Mac. It uses a one-shot session and calls `profile load` to reuse the samples.

**Manual fallback.** Open the raw trace in Xcode, select **Profile after
replay**, and click **Replay**. In the Summary, wait for **Show Performance**
and open it to check shader costs, sampled cores, GPU state, and execution
mode. Choose **File → Export…** (the Summary's **Export** also works), check
**Embed performance data**, and save a new `.gputrace`. Check the box on
each export. If automation stops on a changed control or a focus change,
finish in that window, then run the one-argument command on the export.
For replay authorization trouble, check `DevToolsSecurity -status`. Debugger
authorization and desktop Accessibility are separate settings.
[Apple’s Metal debugger guide](https://developer.apple.com/documentation/xcode/optimizing-gpu-performance)

## Inspect a shader, encoder, or heatmap

`cost_percent` is a shader's share of sampled shader cost. The
`draw_or_dispatch_uses` column counts command uses. Hardware fragment
invocation counts live in the encoder counters. Record sampled-core
coverage, execution mode, and performance state alongside any comparison.
Overlapping stage durations can cover the same time interval.

Look up shader IDs in the JSON and encoder IDs in the first command below.
The example IDs came from one capture and can change between sessions.
For repeated queries, start a session without `--oneshot`, reuse its ID
with `gpudebug -s ID`, and finish with `gpudebug --terminate ID`.

```sh
gpudebug --oneshot -t "$run/profiled.gputrace" -c 'profile load' \
  -c 'go performance/encoders' -c 'list --all'
gpudebug --oneshot -t "$run/profiled.gputrace" -c 'profile load' \
  -c 'go performance/shaders/frag0' -c 'info'
gpudebug --oneshot -t "$run/profiled.gputrace" -c 'profile load' \
  -c 'go performance/encoders/re1/heatmap/fragment' \
  -c 'fetch cost --out /tmp/moppe-fragment-cost.png'
```

For source-line costs, select a draw in Xcode's Debug navigator, then its
fragment function under **Bound Resources → Render Pipeline State**. The
shader editor shows sampled costs alongside the generated Metal source.
Optimized code can attribute many samples to a function declaration.

## Where this came from

On October 23, 2026, the investigation on yawl (M5, Xcode 27.1 build
27A9275) compared Moppe, Luft, a small shader, and Apple's deferred lighting
sample. Direct CLI profiling produced timings and counters but zero shader
weights and unavailable heatmaps. Xcode's embedded export gave Moppe
12.70% sampled cost for `terrain_fragment`, with 2 of 10 GPU cores sampled.
The original exported Moppe bundle on yawl is at
`~/luv/.amp/in/artifacts/moppe-profile/gui-profiled.gputrace`.
[Original runbook](https://github.com/mbrock/luv/blob/5ee6831477e047cc2565942072fc4a83777d0092/.agents/metal-profiling.md) ·
[Archived Moppe shader report](https://github.com/mbrock/luv/blob/5ee6831477e047cc2565942072fc4a83777d0092/wiki/measurements/yawl-xcode-profile-20261023/moppe-terrain.txt)

On November 24, 2026, Xcode 27.2 (27B52) and `gpudebug` 1.0 read the
October export. On a new NHAL gameplay frame, a direct medium-state,
overlapping `profile run` reported successful collection and an empty
shader table. Xcode replay and export on that same frame yielded 67 shader
rows. The table and positive-cost check distinguish usable samples from a
successful command exit. The reason for the collection difference is open.

## M5 trailhead and Caddy measurements

On November 25, 2026, yawl ran the same two scenes before and after mesh
indexing, visibility culling, wet-patch batching, shader changes and far
shadow reuse. The baseline was `a1b7de21`. Each run used a 960×540 scene,
shader temporal resolve, a noon clock and no opening sequence. The Caddy
view used `MOPPE_DEMO=caddy`. The table gives mean NHAL GPU pass times
across the last 600 of 720 frames in the final paired runs.

| Scene | GPU total before | GPU total after | Sun shadow before | Sun shadow after |
| --- | ---: | ---: | ---: | ---: |
| Trailhead | 13.72 ms | 8.79 ms | 3.39 ms | 1.06 ms |
| Caddy | 12.30 ms | 8.75 ms | 2.80 ms | 0.85 ms |

In the trailhead replay, water went from 483 draws to 42, and the skinned
character batch went from 149,112 vertices to 49,704. The far shadow was
cached in this frame. The live timings above include its refresh frames.
The remaining largest sampled shader was `terrain_fragment`, at 29% of
shader cost with 126 temporary registers and 304 bytes of spill per thread.

## Snow-free terrain experiment

Commit `9ab09982` builds two fragment programs from the same ground
material. Its default selects the snow-free program where the height,
shelter and road bounds exclude snow. On that commit,
`MOPPE_TERRAIN_FULL=1` selects the full program everywhere. Both use
the same vertex calculation. The production renderer keeps the snow
material in a separate Lisp file and uses the full fragment program.

On yawl's M5, the trailhead replay reported 8,143 instructions and 24
texture reads for the full fragment, versus 6,380 instructions and 19
texture reads for the snow-free fragment. Both used 126 temporary
registers. Spilled bytes per thread rose from 304 to 320.

| View | Visible terrain pixels using the snow-free program | Scene pass, full | Scene pass, automatic | GPU total, full | GPU total, automatic |
| --- | ---: | ---: | ---: | ---: | ---: |
| Trailhead | 1.0% | 6.081 ms | 6.080 ms | 10.065 ms | 10.123 ms |
| Caddy at the shore | 97.9% | 5.813 ms | 5.745 ms | 10.418 ms | 10.467 ms |
| Snowmobile | 0.0% | 3.927 ms | 3.851 ms | 7.294 ms | 7.146 ms |

Coverage came from a diagnostic render that tagged the two terrain
programs in the motion attachment and counted the surviving pixels.
The timings used an untagged 960×540 scene with shader temporal resolve
and a fixed noon clock. The test froze simulation after 80 ticks and
alternated automatic, full, full, automatic in 32-frame blocks inside
one process. Each number averages frames 13 through 28 of each block
over ten complete cycles after warmup. Even on the snowfield the means
varied. The split gave a small shoreline scene-pass saving and no
consistent total GPU-time saving.

After extracting the snow material, the production fragment matched its
original M5 counts of 8,152 instructions, 126 temporary registers and
304 bytes of spill per thread. Fixed-frame captures at the trailhead,
shoreline, mountain-bike snowline and snowmobile tracks also showed
close visual agreement with the original renderer.

## Evaluating the ground material in world space

The fixed-camera [Metal experiment](https://github.com/mbrock/moppe/tree/4a0712c4/tools/terrain-material-probe)
reuses the current Lisp material at two points. One version stores the
ground colour through the worn-trail blend (`trodden`), then applies
snow, fine mottling, relief and lighting live. The other stores all of
`GroundSurface` (albedo, roughness, relief and the two powder values).
Road surfacing, track deformation and view-dependent lighting still run.
The experiment changes only terrain fragments. It draws the original
geometry, near road ribbons, shadow passes and vegetation in every mode.

The M5 replay gives the following static compiler counts with two atlas
scales. Each pipeline shaded different chunks in that capture, so its
sampled cost is unsuitable for comparing the pipelines. The live timings
below draw the entire terrain with a single pipeline per measured block.

| Terrain fragment | Instructions | Texture reads | Temporary registers | Spilled bytes/thread |
| --- | ---: | ---: | ---: | ---: |
| Original | 8,152 | 24 | 126 | 304 |
| Cached base colour | 6,470 | 22 | 126 | 80 |
| Cached ground surface | 5,952 | 16 | 104 | 0 |
| Road lookup forced to miss | 4,592 | 24 | 126 | 208 |

On yawl's M5 each scene used 960×540 internal pixels, noon light and the
shader temporal resolve. The game advanced 80 fixed steps, then held the
camera still. Each run measured five complete cycles of 32-frame blocks,
with order reversed and rotated, a second copy of the original as a
control, and repeated reports of the same GPU timestamp discarded.
The following pairs are scene pass / summed GPU pass time in ms for the
two-scale atlas. Bake time is outside the measured blocks.

| Camera | Original | Cached base colour | Cached ground surface | Road lookup forced to miss | Repeat original |
| --- | ---: | ---: | ---: | ---: | ---: |
| Trailhead | 5.30 / 8.98 | 4.69 / 8.33 | 4.49 / 7.99 | 4.61 / 8.22 | 5.29 / 8.93 |
| Caddy at the shore | 4.86 / 8.95 | 3.90 / 7.94 | 3.68 / 7.74 | 4.52 / 8.66 | 4.86 / 9.01 |
| Snowmobile | 3.44 / 6.50 | 2.88 / 5.95 | 2.71 / 5.82 | 3.53 / 6.70 | 3.50 / 6.60 |

The base-colour saving appeared in every complete cycle at all three
cameras. Road removal gave a comparable gain at the trailhead and a
visible change to the road. It had little effect on the snowfield.
These experiments overlap in the work they remove, so their savings
cannot be added.

The producer evaluates the ground in a top-down pass at a texel's
footprint. Each atlas is 1024×2048. In the first layout one half spans
128 m and the other the entire periodic world. The second layout packs
four 512² tiles spanning 8, 64, 512 and 2,000 m into the local half.
Both layouts use 8 MiB for the base-colour texture. The experiment
also writes two RGBA16F textures to evaluate the full-surface option,
for 40 MiB combined. The one-time bake of all three outputs took
3.57–3.80 ms in the two-scale runs.

| Output pixels differing from the original by more than 8 levels in any 8-bit channel | Two-scale base colour | Five-scale base colour | Five-scale full surface |
| --- | ---: | ---: | ---: |
| Trailhead | 0.294% | 0.159% | 0.581% |
| Caddy at the shore | 9.046% | 3.751% | 5.169% |
| Snowmobile | 0.161% | 0.021% | 0.659% |

These are whole presented frames, including water, vegetation and HUD.
The repeated original shoreline capture differed by more than eight
levels at fewer than 0.001% of pixels. In the shoreline foreground the
two-scale colour lookup blurs individual stones and the edge of the
sand. Reallocating the same texture area to the five scales recovers
much of that detail.

The five-scale base-colour run also reduced scene time, by 0.55 ms at
the trailhead, 1.15 ms at the shore and 0.65 ms on the snowfield. Summed
GPU pass time fell by 0.45, 1.25 and 0.71 ms respectively. Each difference
uses its own run's original. For the snowfield comparison only the first
four complete cycles are included because a build overlapped the end
of that run.

This is evidence for giving the terrain a world-space material
evaluation with an explicit footprint. The existing ground recipe mixes
metre-scale plant distribution with centimetre-scale stones and uses
screen derivatives for many transitions. A cache has to evaluate those
at the footprint of its texels and preserve the detail visible from the
camera. The prototype also inherits distance-dependent road normals
and approximates road elevation with heightfield elevation during the
bake. It demonstrates reuse on a fixed view. Moving windows, refresh
cost under travel, transitions between levels and edits to the ground
remain to be measured before choosing a production layout.
