# RFC-012: Elevation datums and calibrated rasters

- Status: Draft
- Area: semantic foundations (heights, units, calibration)
- Interacts with: RFC-011, RFC-013; touches shaders, physics, hydrology
  thresholds

## Problem

A height in Moppe is a bare `float`, and what it *means* depends on
where you are standing in world construction. The geological field is a
dimensionless relative pattern; `TerrainEvaluator` maps its continent around
sea level using physical land and bathymetric relief divided by the world's
vertical extent; orogeny then evolves those stored samples. Multiplying a
stored value by `height_scale` yields metres above the model datum. Sea level
still exists simultaneously as metres (`WorldParams::water_level = 50 m`) and
as a normalized fraction compared directly against stored samples
(`OrogenyEvolution::evolution.sea_level`, `analyze_standing_water`'s
`sea_level`). The codebase that types row indices as `quantity_point`s
(`moppe/terrain/discretization.hh`) leaves its single most important scalar
untyped.

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
- `measure_height_range` is already the reusable min/max reading needed by
  inspection code.

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
travels with the view. `TerrainView::elevation_at` becomes the *only*
crossing, and every consumer (flood, trails, watercourse, orogeny,
shader-uniform upload) asks the view instead of multiplying by a smuggled
`height_scale`.

### 3. Make source calibration explicit

The continent-to-relief mapping in `TerrainEvaluator::begin` becomes an
explicit source-calibration value. It maps relative continent samples to
typed elevations around a typed sea datum. Orogeny, checkpoints, hydrology,
and rendering then share that calibration instead of assuming that one stored
unit always means the world's full vertical extent.

## Consequences

- Sea level means metres above the model datum at *every* pipeline
  position; the Lab's future sea-level slider (RFC-014) becomes
  physically meaningful mid-program.
- `height_scale` threading collapses to derived uniform values computed
  at upload from the calibration.
- Source construction and the two canonical stages become
  safe-by-construction at their typed boundaries.
- Honest units unlock honest physics constants elsewhere.

## Risks and alternatives

- Breadth, not depth: many call sites, each mechanical.  Migrate
  view-first (introduce calibrated `TerrainView` everywhere, keep raw
  accessors marked legacy), then chase the shader-uniform boundary.
- Bit-exactness: pure refactor stages must not change stored floats; the
  current orogeny source and fixed-seed world outputs remain golden-tested.
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
3. Move the orogeny source mapping into a typed calibration operation and
   update Terrain Lab readings to show that calibration.
4. Derive shader uniforms from the calibration at `set_terrain` time.
