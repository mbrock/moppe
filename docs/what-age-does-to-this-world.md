# What "age" actually does to this world

A note on why the terrain reads as over-eroded, written after riding it and
disagreeing with it.

## Current decision: separate mountain building from landscape age

On 2026-08-05, Play keeps the requested 2 My of erosion, transport, deposition,
and hillslope evolution, but tectonic uplift is active only for the first
500 ky. That turns uplift from a perpetual source of new relief into a finite
orogeny followed by 1.5 My of relaxation. Geological steps crossing 500 ky
integrate only the active fraction, so the result does not depend on whether
the forcing boundary happens to divide the configured time step.

This is the first controlled move in
[the depositional-landforms program](../planning/rfcs/0005-depositional-landforms.md),
not its final calibration. The profile schedule is an explicit starting
hypothesis. The next work item compares forcing windows before changing
hillslopes, channel initiation, or sediment capacity.

## Settled by the screenshot gallery

`screenshots/` was captured on 2026-07-19, the day before the change below,
and the terrain in it is what the world is supposed to look like: tall alpine
ridges with smooth interfluves and dendritic flutes running down them, forest
on the mid slopes, snow above.

Building that commit and generating seed 123 at the Play profile settles it.
Its heights were still normalized against the world's declared 320 m vertical
extent and ran 0 to 1.174, so the land reached 375 m with a median of 171 m.
The current engine at the same seed and profile, with the durations restored,
gives a maximum of 375.5 m and a median of 170.8 m. The same world, to within a
tenth of a metre.

Nothing else about the erosion had to change. The duration was the whole of it.

## The complaint

The landscape looks hyper-eroded: fine parallel rilling over every hillside,
with the ridge-to-valley relief of an alpine range and none of the middle
scales a real landscape has. It does not read as country.

## What changed, and when

Nothing in the erosion *model* has drifted. Uplift has been 1 mm/yr since the
orogeny source landed, the initial reliefs are still 20 m of land over 240 m of
bathymetry, the diffusivity is still 10⁻⁴ m²/yr, and the incision calibration
is the same 2×10⁻⁵ m/yr at the same reference. The conversion of stored heights
from a normalized fraction to metres (`407e4bb`) rescaled its inputs correctly.

What changed is **`f5c8f30`, "Evolve terrain into mature mountains"**, on
2026-07-20. It tripled the erosion durations: Fast from 200 ky to 750 ky, Play
from 500 ky to 1.5 My, Research from 1 My to 2 My.

The world got taller, not older.

## Why age is really altitude here

Land relief above the datum, seed 123, everything else held:

| Fast, 1024 samples | Relief | Spectral excess at the rill wavelength |
| --- | --- | --- |
| 200 ky | 137 m | +0.42 dex |
| 400 ky | 260 m | +0.65 dex |
| 750 ky | 440 m | +0.78 dex |

The profile the game actually runs is Play at 2048 samples, where the same
tripling took the world from 325 m of relief to **755 m** — and 755 m across
five kilometres is not a landscape at all. Ridden, it is a badlands of bare
grey rock and near-vertical ravines, with no grass and no forest, because
every material band sits below the ground.

Relief grows very nearly linearly with time. That is the signature of a
landscape that is **uplift-limited** — nowhere near the steady state where
incision balances uplift and relief stops growing. Until it reaches that state,
running the clock longer does not mature the drainage network; it just banks
more uplift for the same rills to cut into.

Two earlier experiments say the same thing from other directions. Giving rock
strength a five-fold spatial range moved the ground by 2.2 m and left the
highest peak identical to a tenth of a metre — at steady state, relief goes as
uplift over erodibility, and erodibility did nothing, because this world never
gets there. And the fine rilling scales up with the relief rather than
organizing into a hierarchy, because the incision that would organize it is
saturated at the cell scale and starved at the hillslope scale.

440 m of relief across a 5 km world is an average gradient near nine percent,
with a mean slope of 36°. That is alpine. The 200 ky world is rolling country
with a river in it.

## What does not fix it

- **Raising the area exponent** (`m` from 0.4 to 0.7) erases the mountains:
  relief drops to 90 m and the rilling stays. With the reference area at 1 m²
  — a catchment no cell in the world has, since the smallest is 23.8 m² —
  `m` sets the overall rate as much as the contrast, so it cannot concentrate
  incision without also multiplying it everywhere.
- **Pivoting the law around a real catchment scale** (reference area 10⁴ m²,
  rate recalibrated) reproduces the current world exactly at `m = 0.4`, which
  confirms the reparameterization. But raising `m` from there makes the trunk
  valleys so deep that world generation fails outright: seed 123 at `m = 0.6`
  reports that no home-base expedition could close a trail circuit.
- **A channel-initiation threshold** turns the rilling into blobs. Tried,
  judged worse from the saddle, reverted; see
  [hillslopes and channels](hillslopes-and-channels.md).

## What did, for the earlier target

The duration, restored. Fast returns to 200 ky, Play to 500 ky, Research to
1 My.

And there is a principled answer to "how tall should it be" rather than a
taste: `WorldParams::map_size` already declares the world's vertical extent as
320 m. Before the change the generated relief filled that extent and slightly
exceeded it — the July 19 heights topped out at 1.174 of it. After, the world
stood two and a third times past its own declared bound. The durations are
the world's height, and the height they should be chosen for is the one the
world already says it has.

If tall *and* mature is ever wanted, the honest route is to raise the declared
extent and make the world actually reach steady state — incision strong enough to keep up with uplift over the
run — rather than to bank uplift for longer. That is a real piece of work and
it starts by asking what relief the game wants, then solving for the
erodibility that holds it, instead of setting a clock and seeing what comes
out.

## Note on the earlier white world

Until the material bands were fixed, every world was uniformly snow-covered,
which hid all of this. The rilling is not new. It became visible when the
ground did.
