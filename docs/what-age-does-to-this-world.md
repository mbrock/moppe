# What "age" actually does to this world

A note on why the terrain reads as over-eroded, written after riding it and
disagreeing with it.

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

| Duration | Relief | Spectral excess at the rill wavelength |
| --- | --- | --- |
| 200 ky | 137 m | +0.42 dex |
| 400 ky | 260 m | +0.65 dex |
| 750 ky (current) | 440 m | +0.78 dex |

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

## What does

The duration. It is the parameter that was changed to get here, and turning it
back down is the parameter that undoes it. This is a worldbuilding decision
rather than a correctness one: how tall should a five-kilometre world be?

If mature *and* tall is wanted, the honest route is to make the world actually
reach steady state — incision strong enough to keep up with uplift over the
run — rather than to bank uplift for longer. That is a real piece of work and
it starts by asking what relief the game wants, then solving for the
erodibility that holds it, instead of setting a clock and seeing what comes
out.

## Note on the earlier white world

Until the material bands were fixed, every world was uniformly snow-covered,
which hid all of this. The rilling is not new. It became visible when the
ground did.
