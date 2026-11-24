# Fixed-camera Metal terrain material experiment

Build the macOS app in this checkout and run the following from its root.
The generated Metal source comes from the current production Lisp via the
normal build. The probe substitutes code at two existing material outputs.
It uses Metal-only binding slots outside the portable sampled texture set.

```sh
cmake --build build --target moppe
dir=$(mktemp -d)
python3 tools/terrain-material-probe/shaders.py "$dir"
python3 tools/terrain-material-probe/shaders.py "$dir/nested" --nested
tools/terrain-material-probe/run "$dir" caddy 2350 "$dir/end.png" > "$dir/caddy.log" 2>&1
python3 tools/terrain-material-probe/timings.py "$dir/caddy.log"
```

Use `play`, `caddy` and `snowmobile` for the three measured cameras.
Give the runner the nested directory to use the five-scale layout. To
capture matched stills, pass 160 frames and a final mode argument.
Modes 0 and 5 draw the original program. Mode 1 looks up the base ground
colour, mode 2 looks up the whole GroundSurface, mode 3 makes the
fragment's road search miss everywhere, and mode 4 outputs a flat colour.
Terrain geometry and the separately drawn near road ribbons stay live.

Each run advances 80 fixed 1/60 s simulation steps and holds the view.
Frame 96 bakes the atlas in a fullscreen pass with the gameplay uniforms
and material textures. The upper square covers 128 m, the lower one
covers the periodic world. In the nested layout the upper square holds
four 512² windows covering 8, 64, 512 and 2,000 m. The baker samples height
from the height texture and evaluates the material with a top-down texel
footprint. The consumer uses bilinear filtering within a level.

After one warmup cycle, the probe alternates six modes in blocks of 32
frames, forward then backward, rotating the starting mode each cycle.
Only the middle 16 frames of each block enter the log. The summary uses
complete cycles, counts each distinct resolved timing once per mode and
cycle, then averages cycle means. GPU total is the sum of timestamped
passes. To collect compiler statistics, set `MOPPE_MATERIAL_ALL=1` and
capture after the log prints `TILE`. This draws successive chunks with
different fragment pipelines in one frame and serves only for the Metal
compiler report.
