# Water visual benchmarks

The water benchmark is a deterministic visual corpus, not a single beauty
shot. It keeps the renderer, terrain recipe, camera selection, sun, and output
resolution explicit so that a rendering or erosion change can be reviewed
against several real hydrology structures at once.

Run the curated suite with:

```sh
make water-benchmark
open /tmp/moppe-water-benchmark/index.html
```

The default suite contains two headwater streams, two trunk rivers, two
confluences, two mouths, two waterfall candidates, and two lakes. The cases use
fixed fast-profile seeds selected because the relevant structures really exist;
in particular, visible confluences are uncommon enough that seed 123 alone does
not exercise them. Each run writes normalized PNGs, full logs, `manifest.csv`,
`summary.json`, and an HTML gallery. A requested but absent feature is a failed
case rather than a successful process with no image. Automated capture windows
remain inactive on macOS, so running the suite does not steal keyboard focus
from the application being used.

The stream cases select roots of the visible river network and look upstream
at their first section. Depending on the generated hydrology, that section is
either a drainage-threshold headwater emerging from damp ground or a lake-fed
spill handing off from standing water. This is intentional: both used to look
like the same unexplained full-width cutoff from the old mid-reach camera.

## Review layers

Review the suite in three layers. They answer different questions and should
not be collapsed into one score.

1. **Hydrological form:** Does the terrain contain a plausible channel, join,
   receiving body, lip, or shoreline? Are valleys and channels continuous and
   properly scaled? This is primarily an erosion and routing judgment.
2. **Geometric contact:** Does running water stay inside its bed, merge without
   overlap, meet standing water without a gap, and meet the bank without
   sawteeth, skirts, or floating slabs? This is the most useful renderer
   regression layer.
3. **Material and motion:** Do depth, current direction, foam, transparency,
   rapids, and falling-water cues agree with the geometry? Still images cover
   most material failures, but motion needs a later short clip suite.

Keep performance separate. The gallery reports generation-and-capture wall
time for operational diagnostics, but `--graphics-benchmark` remains the GPU
feature-cost benchmark. A beautiful result that exceeds the performance budget
must pass both suites rather than hiding one result inside a composite score.

## Erosion and broad exploration

Supplying any matrix option replaces the curated cases with a Cartesian
product. For example, compare how the terrain profiles present the same seed:

```sh
tools/water-benchmark /tmp/water-profiles \
  --seeds 126 --profiles fast,play,research \
  --features stream,river,confluence,mouth,waterfall,lake \
  --allow-missing
```

Missing cases are expected during exploration and remain visible in the
report. Omit `--allow-missing` in regression runs so a lost hydrology class
fails the command.

For an explicit before/after review, keep the old report directory and pass it
as a baseline:

```sh
tools/water-benchmark /tmp/water-after \
  --baseline /tmp/water-before
open /tmp/water-after/index.html
```

The report places matching stable filenames side by side. The CSV and JSON
manifests retain seed, profile, feature, selected cell and score, dimensions,
duration, status, and log path for future image-difference or metric tooling.

## Current acceptance questions

- Streams and rivers should be continuous ribbons contained by recognizable
  beds, with width increasing downstream rather than reading as overlapping
  planar sheets.
- Confluences should form one junction without a dark overlap wedge, gap, or
  duplicated ownership at the endpoint.
- Mouths should widen and dissolve into their receiving body without a block,
  hanging edge, or ribbon drawn across the lake.
- Waterfall candidates should read as a lip, falling sheet, and foot rather
  than a steep terrain-following strip.
- Lake boundaries should follow the terrain-scale shoreline without coarse
  triangular skirts, bright sawteeth, or checkerboard occupancy.

These are visual acceptance questions, not automatic pixel thresholds. Once a
specific defect is fixed and stable across machines, a cropped image metric can
be added for that defect without pretending that one global image score
measures hydrological plausibility.
