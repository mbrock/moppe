+++
id = "LIV-010"
title = "Make wind a hierarchy instead of one scalar"
rfc = "RFC-0003"
track = "living-world"
status = "done"
depends_on = []
order = 60
areas = ["rendering", "vegetation"]
+++
# Make wind a hierarchy instead of one scalar

## Outcome

A tree bends at the trunk, sways at the branch, and flutters at the leaf, on
three different frequencies, so it reads as a tree rather than as a flag.

## Scope

`moppe_wind` is one gust sine pair plus one flutter sine, phased on world XZ,
driven by a single `uint8_t wind` lane. There is exactly one spare
`uint8_t reserved` byte beside it in the 40-byte vertex, which is enough: lane
A carries the bend weight (height along the trunk), lane B the flutter weight
(distance to branch), and the amplitude ratio between the two terms is what
the eye reads as woodiness.

The Atelier tree already derives radius from supported mass and carries
flexibility as an intrinsic edge quantity, so the weights can eventually be
*derived* from the organism rather than authored. That is the better end
state; this item only needs the two lanes and the two terms.

Shrub-class geometry wants the bend removed and replaced with a saturating
ramp, or it leans like a trunk.

## Acceptance

- Trunk, branch, and leaf motion are visibly on different clocks.
- No vertex format growth.
- The graphics benchmark shows no regression on the vegetation block.

## Research

*Horizon Zero Dawn* vegetation (Sheaf `#ABD2B8`): tree movement keyed to
object height, branch to distance-to-trunk, leaf to distance-to-branch, with a
global wind field sampled at object centre; the soft-clamp ramp for plants.

## Evidence

`moppe_wind` now runs three clocks -- gust, bough, flick -- and a vertex says
how much of each it takes through two lanes: `wind` for the lean, growing up
the plant, and `flutter` for the shake, growing out from the stem. The spare
`reserved` byte carried the second lane, so the vertex is still forty bytes.

The weights come from where a vertex sits rather than from a number chosen for
it: trunk sides take zero flutter, a broadleaf's ring hangs out on branches and
takes all of it, a conifer's leader stands on the stem while its skirt does
not, and the hero stand's leaf blobs shake where its wood only leans. The bough
term is driven by the gust's own magnitude, so a branch swings further in a
strong wind instead of fidgeting through the calm.

WebGPU animates no vegetation at all today, so this is Metal only. That gap
predates the item and is recorded here rather than hidden.
