+++
id = "LIV-003"
title = "Let habitat set the forest mosaic's threshold"
rfc = "RFC-0003"
track = "living-world"
status = "backlog"
depends_on = ["LIV-002"]
order = 30
areas = ["ecology"]
+++
# Let habitat set the forest mosaic's threshold

## Outcome

Forest cover reads as caused by the ground rather than decorated onto it.

## Scope

`analyze_forest_cover` currently multiplies habitat by a two-scale periodic
noise mosaic. Multiplication makes the noise an equal author: a riparian strip
and a dry ridge are equally likely to be bare. Invert the relationship so
habitat sets the threshold the noise must clear — a wet corridor closes canopy
on a low roll, a dry ridge needs a high one. Keep the noise: stochastic
recruitment, fire, and blowdown are real, and a perfectly deterministic canopy
edge reads as a contour line.

## Acceptance

- Canopy follows drainage corridors visibly from the saddle.
- Stand boundaries remain irregular; no visible noise-lattice period.
- Trail and settlement clearance behave as before.

## Research

Deussen et al. (Sheaf `#GBXEP3`): the mosaic should be the residue of
competition, not an independent layer.
