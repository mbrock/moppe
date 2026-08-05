# RFC-0005: Build terrain from erosion and deposition

Status: accepted

## Decision

Moppe will turn its terrain generator from a continuously uplifted
detachment field into a landscape-evolution model with distinct tectonic,
hillslope, channel, and depositional regimes. The work proceeds in four
movements:

1. **Separate mountain building from landscape relaxation.** Uplift has its
   own finite schedule inside the longer geological evolution. Same-seed
   comparisons choose the schedule from rendered terrain and physical
   measurements.
2. **Give hillslopes and channels different work.** Soil moves conservatively
   across cell faces on hillslopes. Concentrated drainage initiates channels
   only above a physical catchment scale, revisited after the hillslope model
   exists rather than copied from the earlier negative experiment.
3. **Make sediment create geography.** Mobile cover inhibits bedrock incision,
   transport capacity responds to discharge and slope, deposition occupies a
   valley-width footprint, and lakes receive sediment over beds and mouths
   rather than at one routing cell.
4. **Calibrate a world worth riding.** The final choice is judged across fixed
   seeds by relief, slopes, drainage hierarchy, connected low-gradient land,
   visible trunk rivers, conservative ledgers, gazetteer views, and riding.

The execution track is
[`terrain-landforms`](../tracks/terrain-landforms/README.md).

## Why this direction

The two-million-year Play world exposed two separate failures.

First, the former stream-power pass was detachment-only. It lowered cells but
did not route or deposit the removed material. Commit `c3ae995` added one solid
sediment reservoir, exact D-infinity postings, mobile cover, deposition,
ocean export, and a checked ledger. The first unconstrained result then made
the missing spatial scale obvious: a single 2.4 m routing cell could swallow a
whole catchment's excess load during a 50,000-year step, producing impossible
vertical needles. Rate-limited aggradation removed the instability, but it is
a stability boundary rather than a complete depositional model.

Second, the coherent result remained alpine everywhere. That is expected from
the forcing: uplift continues for all two million years, while stream power is
allowed to incise nearly every drained cell. More elapsed time therefore means
more mountain building as well as more erosion. Deposition alone cannot make
rolling country while the generator continually renews relief everywhere.

The earlier channel-initiation experiment in
[`docs/hillslopes-and-channels.md`](../../docs/hillslopes-and-channels.md)
correctly diagnosed the every-cell fluvial regime and reduced measured
corrugation. It nevertheless shipped off because the resulting world was
blobby and dull to ride. This RFC treats that as a sequencing result, not a
reason to keep every cell a channel forever: a threshold without a convincing
hillslope transport law merely deletes detail. Hillslope form, channel
initiation, and depositional width must be judged together.

## Geomorphic model boundary

The first complete model remains deliberately small:

- one solid sediment material;
- surface elevation and mobile sediment thickness;
- bedrock elevation derived from those two fields;
- tectonic uplift applied only during its scheduled interval;
- conservative, face-posted hillslope transport;
- bedrock detachment reduced by mobile cover;
- downstream sediment flux with explicit capacity and ocean export;
- deposition spread over channel, floodplain, lake, and delta footprints.

It does not initially model grain classes, suspended versus bed load, density,
porosity, compaction, dissolved load, lithologic stratigraphy, groundwater, or
continental-scale climate. Those refinements cannot rescue a landscape whose
forcing and spatial regimes are wrong.

## Scale

The ordinary Play domain is about 5 km by 5 km. A realistic catchment inside
it can make substantial streams and small mountain rivers, not a continental
river. The product has two honest choices:

- keep the riding-scale domain and deliberately map its trunk catchments to
  visually generous channels and valley floors; or
- enlarge physical extent, accepting coarser ground samples or introducing a
  separate multiresolution problem.

The track measures the current scale before making that choice. It does not
hide the mismatch by widening only the rendered ribbon while leaving the
landform unchanged.

## Constraints

- **Solid volume is explicit.** Every sediment-producing pass reports where
  its material was deposited, retained, or exported. Conservation residuals
  remain signed physical volumes.
- **Time-step changes do not change the forcing.** A scheduled uplift interval
  contributes the same integrated uplift when a geological step crosses its
  boundary.
- **Stability controls are not geography.** The current maximum aggradation
  rate may remain as a guard, but valley width, settling distance, and lake
  accommodation must become explicit spatial rules.
- **One causal change per comparison.** Fixed seed, physical extent,
  resolution, and renderer settings remain constant while a forcing or process
  law is judged.
- **Measure and ride.** Slope spectra and volume budgets catch failures, but a
  fixed-camera capture in the actual renderer and a ride decide whether the
  terrain is useful.
- **Cache identity follows world identity.** Any change that can alter a
  finished world invalidates or fully participates in the stable cache key.
- **No terrain framework.** The implementation remains direct finite passes
  over the existing typed bundles and drainage domain.

## Acceptance landscape

The RFC is complete when the default Play profile reliably contains all of
the following across the accepted seed suite:

- mountain massifs and steep headwaters without an all-world alpine surface;
- contiguous rolling uplands and low-gradient land large enough to ride;
- a branching channel hierarchy rather than parallel every-cell grooves;
- connected trunk rivers whose valleys visibly widen downstream;
- depositional footslopes, valley floors, lake margins, and mouths;
- no one-cell sediment towers, dams, or repeated raster-scale terraces;
- a closed fluvial and hillslope sediment ledger, apart from explicit ocean
  export; and
- a stable finished-world cache that does not rerun geological evolution on
  an unchanged recipe.

Exact numerical targets are findings of the forcing matrix, not invented in
this RFC. The track records them before the final calibration item can close.

## Verification discipline

Each movement uses the same evidence ladder:

1. synthetic conservation and time-boundary tests;
2. deterministic generated-world tests;
3. same-seed physical summaries;
4. frozen gazetteer and water-feature captures;
5. an optimized Play generation followed by a cache-hit launch; and
6. a riding verdict recorded in the completed work item.
