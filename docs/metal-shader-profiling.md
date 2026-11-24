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
