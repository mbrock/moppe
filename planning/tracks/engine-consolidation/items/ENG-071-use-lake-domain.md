+++
id = "ENG-071"
title = "Use a first-class lake domain in existing hydrology consumers"
rfc = "RFC-0002"
track = "engine-consolidation"
status = "done"
depends_on = ["ENG-070"]
order = 60
areas = ["hydrology", "domains", "render"]
+++
# Use a first-class lake domain in existing hydrology consumers

## Outcome

Lake census and at least one existing consumer share one typed water-body
domain and relation-specific storage.

## Scope

Choose the smallest coherent replacement established by ENG-070. Preserve the
current flood and routing results and their deterministic tests.

## Acceptance

- Water-body identity is not a bare interchangeable integer outside its
  relation storage.
- Existing lake selection or watercourse painting reads the new domain
  directly.
- Deterministic hydrology tests and a feature-targeted lake capture pass.

## Evidence

`LakeCensus` now establishes a `WaterBodyDomain`, validates
`WaterBodyMembership`, and checks body-row identity before publishing a
census. Shoreline extraction and moisture borrow the relation directly;
drainage, painting, capture, and cinematic selection use checked census
queries. The old public parallel vectors are gone. All 200 tests pass,
including rejection of an out-of-domain membership target, and
`tools/capture-water /tmp/moppe-lake-domain.png lake` produced the selected
lake through the Metal runtime.
