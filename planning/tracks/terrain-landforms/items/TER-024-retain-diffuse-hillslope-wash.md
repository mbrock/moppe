+++
id = "TER-024"
title = "Retain diffuse wash below channel heads"
rfc = "RFC-0005"
track = "terrain-landforms"
status = "dropped"
depends_on = ["TER-022"]
order = 76
areas = ["terrain", "hydrology", "sediment"]
+++
# Retain diffuse wash below channel heads

## Outcome

The physical channel threshold controls the concentrated increment of fluvial
incision and transport without turning rainfall-driven hillslope wash off
entirely below that scale.

## Acceptance

- Diffuse wash and concentrated channel share are explicit dimensionless
  parts of the same conservative transport law.
- Zero diffuse share reproduces the current threshold experiment exactly.
- Synthetic low-area cells retain bounded cover-first transport without
  receiving full channel incision.
- A threshold resolved at both 1024 and 2048 preserves drainage and rolling
  land without recreating all-cell parallel fluting.

## Evidence

The conservative implementation and its recipe/cache plumbing passed the
optimized suite, but the full seed-123 matrix rejected the process. At the
first resolved 25 m2 channel threshold, raising diffuse share from zero to
20% lowered P90 slope only from 49.7 to 47.2 degrees, left the fraction of
gentle land near 35%, and reduced the largest connected gentle region from
1.98 to 1.51 km2. The 20% world also lost the confluence target. All candidates
retained the same parallel fluting in the fixed views.

The experiment was removed. Scaling the complete stream-power law down still
lets every small catchment cut bedrock, so it weakens rather than establishes
the hillslope/channel distinction. TER-025 instead strengthens the explicitly
conservative hillslope process only where slopes approach failure.

Decision artifacts are under `/tmp/moppe-diffuse-wash.aUfftR`.
