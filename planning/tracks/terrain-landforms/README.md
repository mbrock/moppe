# Terrain landforms

This is the executable track for
[RFC-0005](../../rfcs/0005-depositional-landforms.md). It begins with the
conservative sediment foundation already landed, separates uplift from
relaxation, then builds hillslope, channel, floodplain, and lake geography in
that dependency order.

```mermaid
flowchart LR
  TER000["TER-000: conservative sediment foundation"]
  TER001["TER-001: finite uplift schedule"]
  TER002["TER-002: forcing matrix"]
  TER010["TER-010: conservative hillslopes"]
  TER011["TER-011: channel initiation"]
  TER020["TER-020: cover-aware transport"]
  TER021["TER-021: valley-width deposition"]
  TER022["TER-022: lakes and deltas"]
  TER025["TER-025: nonlinear hillslopes"]
  TER030["TER-030: trunk-river scale"]
  TER040["TER-040: calibrate Play"]

  TER000 --> TER001 --> TER002 --> TER010 --> TER020 --> TER021
  TER021 --> TER022 --> TER025 --> TER011 --> TER030 --> TER040
  TER022 --> TER040
```

## Program, not queue

The track keeps the long argument visible while individual commits stay
small. Its gates are causal:

- forcing is chosen before a hillslope law is tuned against it;
- a conservative hillslope regime exists before transport is coupled to
  mobile cover;
- mobile-cover feedback and valley-floor deposition exist before channel
  heads suppress the incision that currently drains small basins;
- valley floors exist before river width is judged; and
- the default profile changes only after the whole seed suite passes.

The fixed reference is seed 123 at the ordinary Play extent and resolution.
The accepted seed suite later includes varied coast, lake, relief, and trail
conditions. Captures use an optimized build; an unoptimized 2048-square
generation is not performance evidence.

## Current findings

`c3ae995` closes the fluvial solid-volume ledger and proves stable caching.
Its rate-limited deposition removes impossible one-cell towers. The resulting
two-million-year world is coherent but still nearly all alpine, with narrow
valleys and little rolling land.

TER-001 separates the clocks: seed 123 with 500 ky of uplift followed by
1.5 My of unforced relaxation produces broad forested ridges, connected
rolling country, and materially lower relief in the actual renderer. Fine
fluting remains. TER-002 compared 250, 500, and 750 ky and selected 500 ky:
250 ky loses convincing mountain groups, while 750 ky reaches 407 m of relief
and leaves only 4.4% of land below ten degrees. The full finding is recorded
in [`findings.md`](findings.md). TER-010 now addresses the remaining fluting
with conservative hillslope transport. TER-010 preserves that shape while
making every creep transfer part of the solid ledger. The first TER-011 matrix
then exposed a dependency error: physically meaningful channel-head scales
produce perched basins and sheer channel walls before cover feedback and
valley-floor deposition exist. The typed experiment and evidence remain, but
TER-020 and TER-021 now precede the final channel-scale selection.

TER-020 now closes that first dependency. Incoming load consumes a typed
discharge-based capacity, mobile cover is re-entrained before bedrock is cut,
and a four-candidate optimized matrix selects 0.00002 as the Play
concentration. Lower capacity preserves the knife-edge terrain under a cover
blanket; twice the selected capacity collapses most upland relief. The
selected world shows rivers and local depositional plains while retaining
mountain groups. That calibration supplied the centerline volume carried into
TER-021's physical valley-width footprints.

TER-021 now performs that conservative lateral placement. A meter-scaled,
downstream-widening footprint equalizes its lowest receiving cells and stops
at local valley walls. At seed 123 it triples the largest connected gentle
region with almost no loss of total relief and no material generation-time
regression. The cover and valley dependencies are therefore closed; TER-011
is ready to revisit physical channel initiation against the improved process
model.

The post-TER-021 channel matrix rejected every physically resolved
threshold. At 25 m2 the median slope nearly doubles and inland water area
nearly triples; at 100 m2 median slope reaches 70 degrees; at 400 m2 no
complete trail circuit exists. Valley floors can receive sediment but do not
provide basin accommodation or evolve outlets. That made lake-bed storage and
mouth deposition the missing process gate rather than another threshold
calibration.

TER-022 closes that gate. Lake loads now consume a shared, accommodation-aware
body budget and spread across the available bed; mouth loads form bounded fans
and retain explicit export. The optimized seed-123 world keeps 199 m of relief
while growing land below ten degrees from 10.4% to 27.3% and the largest
connected gentle region from 0.423 to 2.50 km2. Generation remains at the
TER-021 performance baseline after compacting the mouth footprint work.
The renewed TER-011 matrix reveals one more missing gate. The attractive 7 and
10 m2 worlds coincide with a single 2048-grid cell and lose the confluence
target. At 25 and 100 m2, inland water and upper-tail slope rise sharply again.
The optimized spillway A/B changed inland water by only 1%, and a subsequent
0--20% diffuse-wash sweep left gentle-land coverage flat, reduced its largest
connected region, retained parallel fluting, and at 20% lost the confluence
target. Both experiments were removed rather than kept as ineffective special
cases. TER-025 now returns to the RFC's actual distinction: below channel
heads, conservative hillslope transport accelerates near a critical gradient
without granting every small catchment stream-power bedrock incision.

TER-025 and TER-011 now close that dependency together. A bounded multiplier
of four above a 0.6 critical gradient turns the first resolution-independent
100 m2 channel head into rolling country at both 2048 and 1024 while retaining
roughly 260 m of relief, completed trails, river reaches, lakes, and mouths.
The 2048 world resolves every gazetteer target; the 1024 world loses only the
confluence selection. TER-030 is therefore ready to align physical catchment,
river extraction, valley width, and rendered water scale before any values
become Play defaults.

TER-030 found that the missing confluence was not an erosion failure. Visible
river extraction began when rendered width reached two cells, which silently
raised the physical catchment threshold fourfold at 1024. It now begins at a
resolution-independent five-metre channel. In the selected seed-123 world,
that means 0.174 km2 of catchment and a 22.7 m depositional floor at the lower
boundary; the 13--14 km2 trunk renders 24 m wide and 2.5 m deep in an
approximately 150 m floor. Both 1024 and 2048 now produce all 17 gazetteer
targets. TER-040 can therefore evaluate the complete recipe across seeds.

## Deferred until the shape works

Grain classes, porosity, compaction, stratigraphy, and detailed suspended-load
physics remain outside this track. They may refine a successful geography;
they are not allowed to substitute for one.
