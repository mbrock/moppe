+++
id = "LIV-003"
title = "Let habitat set the forest mosaic's threshold"
rfc = "RFC-0003"
track = "living-world"
status = "done"
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

## Evidence

`analyze_forest_cover` no longer multiplies habitat by the mosaic. Habitat
chooses the threshold the mosaic must clear -- a sheltered damp hollow closes
canopy on a low roll, a dry shoulder needs a high one -- and then says only how
thickly the stand grows. Canopy count on seed 123 rises from 39,392 to 73,569
representatives and the stands visibly follow the drainage and the shaded
slopes rather than sitting on the noise's own lattice.
