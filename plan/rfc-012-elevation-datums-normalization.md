# RFC-012: Elevation datums, calibrated rasters, and honest normalization

- Status: Draft
- Area: semantic foundations (heights, units, calibration)
- Interacts with: RFC-011, RFC-013; touches shaders, physics, hydrology
  thresholds

## Problem

A height in Moppe is a bare `float`, and what it *means* depends on
where you are standing in the pipeline.  Before normalization it is in
raw noise units; after `NormalizeHeights` it is in [0, 1]; multiplied by
`height_scale` it is metres above the model datum; sea level exists
simultaneously as metres (`WorldParams::water_level = 50 m`), as a
normalized fraction (`TerrainParams::sea_level_norm`), and as raw floats
compared directly against samples (`AnalyticalErosion::sea_level`,
`analyze_standing_water`'s `sea_level` -- both in whatever units the
terrain happens to be in *at that stage of the program*).  Reordering
pipeline stages, or handing a metric threshold to a pre-normalization
stage, silently changes meaning.  The codebase that types row indices
as `quantity_point`s (`moppe/terrain/discretization.hh`) leaves its
single most important scalar untyped.

There is also a conceptual gap worth naming: **elevation is affine, not
linear**.  Differences of elevations are lengths; elevations themselves
are positions relative to a datum.  And **normalization is
data-dependent**: min/max over *samples* is not min/max over the
continuous field, so normalization does not commute with sampling --
it is a measurement of a particular raster, and its output only means
something relative to that measurement.

## Current situation

- `TerrainView::relative_elevation_at` vs `elevation_at` already
  perform the calibration explicitly at one boundary
  (`docs/terrain-expressions.md`), and `NormalizedRaster` exists as "an
  explicit semantic conversion."  The concepts are half-born.
- `height_scale` threading is pervasive: `watercourse.cc` converts raw
  samples, while the flood epsilon, shaders' `params0.y`, and physics
  `m_scale[1]` carry the same conversion independently.
- `measure_height_range` is already the min/max reading normalization
  needs -- currently fused into the transform.

## Proposal

### 1. Typed elevations and datums

    using Elevation  = mp_units::quantity_point<si::metre, model_datum>;
    // differences are plain meters_t

with named origins: `model_datum` (the storage zero) and derived
`sea_datum = model_datum + water_level`.  APIs that today take a float
"sea level" take an `Elevation`; depths and drops stay `meters_t`.  The
compiler then rejects handing a normalized fraction to a metric
threshold -- the exact bug class the current pipeline permits.

### 2. Calibrated rasters

A raster of heights carries its calibration -- the affine map from
stored floats to `Elevation`:

    struct HeightCalibration { float scale; float offset; };  // typed
    // stored * scale + offset = elevation above model_datum

`RandomHeightMap` keeps storing floats exactly as now; the calibration
travels with the view.  `TerrainView::elevation_at` becomes the *only*
crossing, and every consumer (flood, carve, watercourse, analytical
erosion, shader-uniform upload) asks the view instead of multiplying by
a smuggled `height_scale`.

### 3. Normalization = reading + reparameterization

`NormalizeHeights` splits into what it already secretly is:

1. a **reading**: `measure_height_range` over this raster (recorded in
   the transform report, visible in the Lab);
2. an **affine rewrite** of stored values *and* the calibration
   together, so the represented elevations are unchanged unless the
   program explicitly asks for a rescale (the historical behavior --
   "stretch relief to fill [0,1] * height_scale" -- becomes an honest,
   named operation: `RescaleReliefToRange`, distinct from a pure
   storage renormalization).

This distinction is currently invisible and is exactly where meaning
slips: today "normalize" both re-parameterizes storage *and* changes
the world's relief, depending on what downstream multiplies by.

## Consequences

- Sea level means metres above the model datum at *every* pipeline
  position; the Lab's future sea-level slider (RFC-014) becomes
  physically meaningful mid-program.
- `height_scale` threading collapses to derived uniform values computed
  at upload from the calibration.
- Transform reordering becomes safe-by-construction where it is
  meaningful and a type error where it is not.
- Honest units unlock honest physics constants elsewhere.

## Risks and alternatives

- Breadth, not depth: many call sites, each mechanical.  Migrate
  view-first (introduce calibrated `TerrainView` everywhere, keep raw
  accessors marked legacy), then chase the shader-uniform boundary.
- Bit-exactness: pure refactor stages must not change stored floats;
  the `RescaleReliefToRange` split must reproduce the historical
  normalize exactly for existing programs (golden-tested).
- Alternative "store metres directly" (drop normalized storage): more
  radical, breaks texture range assumptions (R32F is fine, but shaders
  and caches assume [0,1]); the calibration approach gets the semantics
  without the migration cliff, and leaves that door open.

## Implementation sketch

1. Introduce datum types + `HeightCalibration`; extend `TerrainGrid` /
   `TerrainView` (`height_scale` becomes one field of the calibration).
2. Port the hydrology stack (flood, census, drainage, watercourse) to
   typed thresholds -- this is where latent unit bugs would surface,
   so port it first and test against current outputs.
3. Split `NormalizeHeights`; update `docs/terrain-expressions.md`'s
   transform table and the Lab stage rows.
4. Derive shader uniforms from the calibration at `set_terrain` time.
