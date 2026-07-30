# Why the hillsides combed, and what fixed it

This is the finding for
[LIV-005](../planning/tracks/living-world/items/LIV-005-measure-the-corrugation.md).
The relief-graded material bands uncovered a corrugation the snow had been
hiding: every hillslope carried a regular washboard of near-uniform wavelength
and amplitude, with no branching hierarchy anywhere.

## What it was

Not a rendering artifact. The instrument is a radial and angular power
spectrum of the cached elevation column, read straight out of the Arrow bundle
(`tools/` has no wrapper for this; the analysis lives in the item's history).

| World | Spectral peak | Excess over the power law | Anisotropy |
| --- | --- | --- | --- |
| 1024 samples, 4.88 m spacing | 7.5 cells = 36 m | +0.73 dex | 1.53 at 90° |
| 2048 samples, 2.44 m spacing | 11.1 cells = 27 m | +1.31 dex | 1.44 at 135° |

Doubling the resolution roughly doubled the wavelength *in cells* and left it
alone *in metres*. The corrugation was therefore a physical length the erosion
was choosing, not the lattice printing itself onto the ground.

That rules out the first suspect. So does the anisotropy: a uniform
triangulation diagonal would put a spike at 45° or 135°, and 1.5 either way is
not a spike. [RFC-010](../plan/rfc-010-diagonal-bias.md) describes a real
artifact, but it was not this one.

Two more suspects fell to direct experiment:

- **Channel persistence.** Setting it to zero changed the spectrum by 0.07 dex
  and 0.16 cells. The prior step's channel tangent is not holding parallel
  rills apart.
- **Rock strength.** Giving erodibility a five-fold spatial range moved the
  ground by 2.2 m on average across a landscape with 500 m of relief, and left
  the highest peak identical to the tenth of a metre. At 750 ky this world is
  nowhere near the steady state where relief goes as uplift over erodibility,
  so a parameter that governs steady-state relief has almost nothing to say
  about it. The experiment was reverted; the finding is the useful part.

## What it actually was

There were no hillslopes. The mean slope of the whole world was **36°**, and a
hillshade of the bare heightfield showed parallel grooves from ridge to valley
with no smooth ground anywhere between them.

The stream-power law was being applied to every cell, including cells that
drain nothing but themselves. A cell with one cell of catchment was cutting its
own channel, and the only thing opposing it was a hillslope diffusion whose
length scale over the whole run was 8.7 m — less than two lattice cells. So
every cell became its own rill, and the wavelength the spectrum found was
simply where incision and creep balanced, a few cells across.

Raising the diffusivity ten-fold confirmed the balance — the wavelength moved
from 36 m to 94 m, close to the square-root scaling the geomorphology predicts
— but it did not help. It moved the comb; it did not remove it, and it cost
the fine drainage detail.

## The fix

Real landscapes have a channel head: above some catchment, running water cuts a
channel, and below it the ground is a hillslope shaped by creep. Moppe had no
such threshold, so `StreamPowerEvolution` gains one:

```text
channel_initiation_area = 1200 m²
```

Fluvial incision now scales by a share that rises across a factor of four in
catchment around that area — a channel head is not a sharp place on the ground,
so it is not a sharp threshold here. Below it, a cell receives uplift and
diffusion and nothing else.

The diffusivity did not need to change. The threshold alone does the work:

| | mean slope | spectral peak | excess |
| --- | --- | --- | --- |
| before | 36.0° | 36 m | +0.73 dex |
| after | 28.4° | 37 m | +0.45 dex |

The spectral peak barely moved, which is the point: the wavelength was never
wrong. What was wrong was that the whole world sat at that one wavelength.
With the hillslope regime restored, the same spacing survives as the drainage
texture of real valleys, and the excess above the power law falls by half.
A hillshade now shows trunk valleys with tributaries feeding them, and smooth
ground in between.

## What is still open

- Anisotropy rose from 1.53 to 1.76 and moved to 135°. The fine rills were
  masking a directional bias in the routing, and with them gone it is the
  next thing visible. This is RFC-010's territory after all, just not as the
  cause.
- The near-field ground texture aliases badly at close range — a separate
  material problem, unrelated to the shape of the land.
- Erodibility remains a real thing that real landscapes have. It would need a
  world old enough for steady-state relief to mean something, and that is a
  question about generation time rather than about rock.
