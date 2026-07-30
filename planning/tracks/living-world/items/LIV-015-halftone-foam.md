+++
id = "LIV-015"
title = "Give foam clumps that pop instead of a fade"
rfc = "RFC-0003"
track = "living-world"
status = "backlog"
depends_on = ["LIV-014"]
order = 110
areas = ["rendering", "water"]
+++
# Give foam clumps that pop instead of a fade

## Outcome

Foam breaks into clustered bubbles that shrink and vanish, and one mask serves
shore swash, river churn, and the foot of a cascade.

## Scope

Threshold a foam saturation against a Gaussian-clustered halftone mask rather
than fading alpha. The clustering is the whole point: an unclustered mask reads
as noise. Bias the saturation toward the wave front instead of symmetrically,
or the foam sits on the back of the wave too.

The saturation input is deliberately not tied to ocean height. Moppe has three
better drivers already: swash phase at the extracted waterline, turbulence from
the longitudinal bed profile, and discharge at a fall's foot. One mask, three
consumers.

Foam is the only water feature that carries history — it shows where the water
has been — which is why it ranks this high for its cost.

## Acceptance

- Shore foam clusters and pops rather than fading uniformly.
- The same mask drives river churn and cascade foot without a second path.
- Under 5% of the water block in the graphics benchmark.

## Research

*Very Fast Real-Time Ocean Wave Foam Rendering Using Halftoning* (Sheaf
`#869NHK`): front-biased saturation, Gaussian-clustered mask, the shrinking
variant, measured at under 3% over texture fading.
