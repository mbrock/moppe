+++
id = "ENG-070"
title = "Specify lake identity as a graph-domain forcing case"
rfc = "RFC-0002"
track = "engine-consolidation"
status = "done"
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

## Evidence

`docs/lake-domain.md` grounds one finite body domain, cell membership,
measurement rows, boundary cells, inlet reaches, and downstream continuation
in current flood, drainage, rendering, capture, and inspection consumers. It
selects the checked cell-to-body relation as the first replacement and
explicitly defers generic graph machinery and cross-generation identity.
