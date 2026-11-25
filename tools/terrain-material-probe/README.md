# Moving ground and road material experiment on Metal

Build the desktop app in this checkout. The probe generator reads the MSL
emitted by the ordinary Lisp shader build. Run these commands from the root.

```sh
cmake --build build --target moppe
d=$(mktemp -d)
python3 tools/terrain-material-probe/moving.py "$d"
for round in 1 2 3; do
  case $round in
    1) modes="live ground roads"; scales="0.5 0.75";;
    2) modes="ground roads live"; scales="0.75 0.5";;
    3) modes="roads live ground"; scales="0.5 0.75";;
  esac
  for scale in $scales; do
    for mode in $modes; do
      name=$scale-$round-$mode
      tools/terrain-material-probe/ride "$d" "$mode" "$scale" 496 \
        "$d/$name.png" > "$d/$name.log" 2>&1
    done
  done
done
python3 tools/terrain-material-probe/ride-times.py "$d"
```

The camera follows the generated bus loop's paved centreline at 20 m/s,
starting 95 m along it, 2.2 m above the surface and looking 10 m ahead.
The world runs 80 fixed 60 Hz steps, then holds while the camera moves
one third of a metre per frame. The summary compares route frames
97 through 480. A labelled scene pass identifies each frame as its
GPU timestamp heap resolves. Total sums all timed passes including dirty
strips. Each launch supplies its own mean, 95th percentile and maximum.
The final table takes their medians across launches and reports the
first fill separately.

The cache has four 512² levels covering 16, 64, 256 and 1,024 m, and a
1024² periodic whole-world texture per output. Each local origin snaps
to eight texels. A moving level redraws newly exposed, non-overlapping
horizontal and vertical strips in a single load/store pass. Hardware
repeat wraps bilinear samples at physical texture edges. The shader
blends to the next level near the logical boundary. Both terrain and
ribbon sample the same outputs while using the original geometry.

The ground mode stores GroundSurface in two RGBA16F textures per level.
The roads mode stores the dry colour and roughness, dry detail slope
and powder pair, and macro normal XZ, wet darkening and puddle potential
in three. The producer uses the tread elevation and road normal inside
its footprint. This approximates the live camera-distance blend on far
roads. The latter mode also approximates compacted snow roughness at the
boundary between road and snow. The lighting, rain, moving swash, tracks
and view-dependent snow effects are evaluated on screen.

Set MOPPE_PROBE_STILLS to an existing directory to capture frames 160–175.
Set MOPPE_PROBE_REFILL=1 to repaint each entire level whenever its origin
moves, for a check on the incremental strip updates. The fixed-camera
experiment with two packed atlas layouts is preserved at 4a0712c4.
