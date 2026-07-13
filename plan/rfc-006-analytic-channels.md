# RFC-006: River channels as analytic fields, not raster carves

- Status: Draft
- Area: terrain representation (simulation + rendering + physics)
- Interacts with: RFC-007 (waterline), RFC-008 (tessellation), RFC-013
  (reconstruction discipline); flagship application of the "field, not
  raster" thesis

## Problem

Rivers are the place where the 2.44 m lattice is most visible and most
harmful.  A channel narrower than a cell cannot exist; `carve` quantizes
every cross-section to lattice cells; the watercourse painter then works
hard to reconstruct what the carve *meant* (bank-crest probing at three
ray extents, `moppe/terrain/watercourse.cc:186`) because the raster no
longer says.  Banks are staircases, widths jump in cell increments, and
no downstream system can render or reason about the channel at better
than lattice resolution -- even though the `RiverNetwork` knows the
centerline, width law, and depth law exactly.

## Current situation

- `moppe/terrain/carve.cc` lowers raster samples along each reach using
  `channel_width_m` / `channel_depth_m` laws of contributing area.
- `paint_watercourses` stamps water levels and flow into per-cell sheets,
  reverse-engineering the carved channel from bank probes.
- The ribbon renderer (`game/river_surface.cc`) already resamples reach
  cells with bounded cubic tangents -- a smooth centerline exists.
- Physics samples the carved raster bilinearly
  (`HeightMap::interpolated_height`), ~10 queries/frame.

## Proposal

Represent each channel as a **vector feature evaluated at sample time**:
per reach, a smoothed centerline polyline (the ribbon resampler's curve),
the width/depth laws it already has, and an analytic cross-section
profile.  The terrain height seen by rendering *and physics* becomes

    z(x) = z_base(x) - depth(s) * profile(d(x) / halfwidth(s))

where `d` is distance to the centerline, `s` the arc position, and
`profile` a smooth bed shape (flat-bottomed parabola with bank fillets),
composited min-wise over the base heightfield.  The channel is never
rasterized; it is sampled at whatever density the consumer brings --
lattice vertices today, tessellated vertices under RFC-008, physics
queries always.

Evaluation strategies:

- **CPU (physics, analyses)**: a spatial hash of reach segments over the
  grid; a query visits at most a few segments.  Ten queries a frame is
  nothing; drainage-scale sweeps visit each cell's nearby segments once.
- **GPU (terrain vertex/tessellation stage)**: bake a compact
  channel-parameter raster at 2-4x lattice resolution -- signed distance
  to nearest centerline, arc-interpolated halfwidth and depth (3-4
  channels of RG16F/RGBA16F) -- refreshed only when the network changes.
  The *shape* stays analytic; only the addressing is precomputed.  True
  per-vertex segment lists are a later option.

## Consequences

- Sub-cell channel widths, clean parametric banks, and cross-sections
  that hold their shape at any resolution or tessellation density.
- `paint_watercourses` stops probing: the sheet level derives from the
  analytic bed exactly, deleting the subtlest code in the painter.
- Physics and rendering share one composite by construction -- the same
  invariant the height-texture design already enforces for the base
  terrain (`docs/renderer-design.md`: "sim and render cannot diverge").
- Resolution independence: the Fast profile's 1025^2 worlds get the same
  channels as Play's 2049^2.

## Risks and alternatives

- Two sources of height truth (base raster + channel field) is the real
  cost.  Every consumer must composite: rendering, physics, shadow pass,
  drainage/flood *if* they are to see channels.  Mitigation: define the
  composite as a `TerrainView` wrapper (one choke point, RFC-013's
  vocabulary), and migrate consumers incrementally -- rendering + physics
  first behind a flag, analyses later or never (analyses may reasonably
  keep operating on the un-carved surface plus the network, which is in
  fact the more semantic reading).
- The vehicle can enter a channel narrower than its wheelbase; the
  profile function keeps bank slopes bounded, and the existing carve
  never guaranteed otherwise either.
- Keep `carve` as a fallback path until captures
  (`tools/capture-water` river/confluence/mouth/waterfall/lake) confirm
  parity or better at every feature.

## Implementation sketch

1. Channel feature extraction: reuse the ribbon resampler's smoothed
   centerline; store per-reach arrays (points, halfwidth, depth).
2. CPU composite in a `ChannelField` + `TerrainView` wrapper; physics
   flag `MOPPE_ANALYTIC_CHANNELS=1`; verify ride-through feel.
3. Channel-parameter raster bake + `terrain.metal` vertex composite;
   shadow pass uses the same term.
4. Painter simplification; capture comparisons; retire raster carve from
   the default program once profiles are re-blessed.
