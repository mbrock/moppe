+++
id = "ENG-070"
title = "Specify lake identity as a graph-domain forcing case"
rfc = "RFC-0002"
track = "engine-consolidation"
status = "backlog"
depends_on = ["ENG-063"]
order = 50
areas = ["hydrology", "domains", "design"]
+++
# Specify lake identity as a graph-domain forcing case

## Outcome

A checked-in design names the smallest typed domains and relations needed to
represent water-body identity, membership, measurements, inlets, spills, and
downstream continuation.

## Scope

Ground the design in current `FloodField`, `LakeCensus`, `DrainageGraph`, and
`RiverNetwork` consumers. Avoid a generic graph abstraction and defer
time-varying lake processes.

## Acceptance

- Every proposed relation has an existing rendering, inspection, or erosion
  consumer.
- Identity and quantity properties have debugger-readable storage.
- The first implementation slice can replace an existing ad hoc structure
  without an adapter.
