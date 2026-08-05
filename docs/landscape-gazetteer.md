# Landscape gazetteers

The landscape gazetteer is Moppe's repeatable visual evaluation corpus. It
does not sample a running ride or stop a cinematic at arbitrary times. It
reads one completed generated world, composes independently useful cameras,
and renders each frozen `FrameView` through the ordinary game presenters.

Run it with:

```sh
make gazetteer GAZETTEER_OUT=/tmp/moppe-gazetteer
open /tmp/moppe-gazetteer/index.html
```

The output directory must not already contain `frame-*.png`. The command
records the exact seed, generation profile, graphics quality, drawable size,
and settle count in `capture.txt`. Useful overrides are:

```sh
MOPPE_SEED=123 \
MOPPE_TERRAIN_PROFILE=fast \
MOPPE_GAZETTEER_GRAPHICS=high \
MOPPE_GAZETTEER_WINDOW=1600x900 \
MOPPE_GAZETTEER_SETTLE=18 \
  tools/capture-terrain-gazetteer /tmp/moppe-gazetteer
```

## What it selects

`plan_landscape_gazetteer` is a deterministic, renderer-free reading of the
active `SurfaceGeometry`, `SurfaceReadings`, standing water, lake census,
drainage, rivers, trails, and spawn point. It balances unlike evidence by
named category rather than collapsing everything into one generic score:

| Category | Representative readings |
| --- | --- |
| gameplay | trailhead rider and a scenic trail turn |
| habitat | forest floor, open meadow, and wetland edge |
| landform | eroded slope and highland vista |
| freshwater | headwater, river, confluence when present, mouth, waterfall, and lake |
| coast | along-shore establishment and an ocean horizon |
| aerial | basin-scale land, forest, and water structure |

Candidate scores only compare sites for the same named question. Physical
separation prevents the habitat and landform entries from repeatedly choosing
one photogenic hill. A world without a confluence, lake, or sea simply omits
that inapplicable entry; it does not fabricate a substitute.

Each selected shot retains typed positions, a typed angular field of view,
typed camera clearance, and the surface quantities at its subject cell. The
CSV is the deliberate numerical exit: unit suffixes in names such as
`eye_x_m`, `vertical_fov_deg`, and `waterline_distance_m` make the flattened
contract explicit. Image names and CSV rows correspond exactly.

Each capture also writes `terrain-summary.csv`, one global row measured from
the same completed world. It records the recipe, elevation and slope
percentiles, connected low-gradient land, visible-river density, water bodies,
and cumulative eroded, deposited, and inferred exported solid volume. This is
the physical counterpart to the camera-by-camera visual manifest.

## Frozen composition

Gazetteer mode freezes simulation, weather, and wind at a documentary time.
For each camera the application still runs ordinary terrain, forest,
undergrowth, actor, water, effect, and post presenters. HUD and dust are
suppressed. The renderer receives several settle frames so incremental nearby
forest meshes, auto-exposure, and other renderer-owned history reach the same
state before the requested screenshot. Temporal state is reset between
unrelated cameras.

This keeps the ownership sequence direct:

```text
GeneratedWorld + GameSession
  -> LandscapeGazetteer shot
  -> frozen FrameView
  -> focused presenters
  -> Renderer screenshot
```

`tools/gazetteer-report` then validates that every manifest image exists and
builds `contact-sheet.png` plus `index.html`. It can be rerun on an existing
capture without launching Moppe.

## Review practice

Use the contact sheet first: it makes a global palette shift, duplicated
composition, LOD discontinuity, or missing ecosystem immediately visible.
Open individual frames for silhouette and material detail. For an A/B change,
hold seed, generation profile, window, graphics quality, and settle count
fixed and capture into two new directories.

Review the corpus by questions rather than by an average beauty score:

- continuity: do explicit trees and grass retire into their filtered ground
  representation without a band, pop, or glitter?
- ecology: do snow, rock, water, paths, moisture, canopy, and vegetation agree
  about what can grow at a site?
- hydrology: do river joins, shallows, water depth, banks, lakes, and the ocean
  remain legible at ground, landscape, and aerial scales?
- composition: does every named view answer a different documentary question,
  with clear foreground, subject, and horizon?
- atmosphere: do shadowed ravines and distant basins retain readable form
  without discarding haze or the time of day?
- cost: after a visual change, use the graphics feature benchmark or a native
  trace; the gazetteer is visual evidence, not a frame-time benchmark.

The older `capture-terrain-survey` remains useful for continuity along the
cinematic drone route. It and the gazetteer answer different questions: one
samples a path; the other constructs a small landscape atlas.
