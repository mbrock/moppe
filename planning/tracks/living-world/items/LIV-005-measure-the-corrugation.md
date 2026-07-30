+++
id = "LIV-005"
title = "Diagnose the hillslope corrugation before fixing it"
rfc = "RFC-0003"
track = "living-world"
status = "ready"
depends_on = ["LIV-001"]
order = 50
areas = ["terrain", "rendering", "analysis"]
+++
# Diagnose the hillslope corrugation before fixing it

## Outcome

A written finding that names the cause of the regular washboard ridging on
hillslopes, with a measurement, and recommends one fix.

## Scope

With the snow no longer covering everything, hillslopes visibly carry a
corduroy of near-uniform wavelength and amplitude running down the slope, with
no branching hierarchy. Evenly spaced ridges and valleys are a real
geomorphological phenomenon, so the artifact is not that they exist; it is
that they are monotone — one wavelength, one amplitude, no trunk collecting
them.

There are at least three candidate causes and they need separating before
anyone edits a solver:

- the terrain triangulation's uniform diagonal, which
  [RFC-010](../../../plan/rfc-010-diagonal-bias.md) already describes as "a
  directional texture laid over the whole world";
- single-receiver D8 incision cutting parallel grooves that never compete;
- the incision-to-diffusion ratio in `StreamPowerEvolution` selecting one
  characteristic wavelength.

Measure before choosing: a directional power spectrum of elevation over a
hillslope region separates a lattice-aligned artifact from an isotropic
physical wavelength, and a run with the diffusion coefficient varied separates
the third cause from the first two.

## Acceptance

- A capture-backed note in `docs/` with the spectrum and the verdict.
- The recommendation names one change, not a list.
- A negative result is a result: if the corrugation is physically correct and
  only reads badly, say so and route the fix to shading or to detail.

## Research

Perron on evenly spaced ridges and valleys, via `ideas/reading-map.md` §14.
