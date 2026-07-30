+++
id = "ENG-071"
title = "Use a first-class lake domain in existing hydrology consumers"
rfc = "RFC-0002"
track = "engine-consolidation"
status = "backlog"
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
