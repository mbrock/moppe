+++
id = "LIV-004"
title = "Give trees a species preference and let competition sort them"
rfc = "RFC-0003"
track = "living-world"
status = "backlog"
depends_on = ["LIV-003"]
order = 40
areas = ["ecology"]
+++
# Give trees a species preference and let competition sort them

## Outcome

Riparian galleries and dry-ridge conifer emerge from the self-thinning loop
that already runs, instead of from an elevation switch.

## Scope

`ForestForm` is already `{broadleaf, conifer}` and `TreeStand` already runs
Deussen's circle model — `crown_has_room` is the competition test, minus the
competition. Add a per-species wet/dry preference; compute per-site vigor as a
seeded function of wetness and preference; make the room test compare
competitive ability (vigor times relative size) rather than radius alone.

The global `ForestLandscape` picks form by elevation today. It should pick it
the same way the stand does, so the near and far forests agree about what
grows where.

## Acceptance

- Species segregate along the wetness gradient without a rule naming rivers.
- The near stand and the global forest agree at their boundary.
- Determinism holds: same seed, same forest.

## Research

Deussen et al. (Sheaf `#GBXEP3`): five species parameters including "a
preference for wet or dry areas"; vigor as a randomized function of water
concentration and preference; segregation as an emergent result.
