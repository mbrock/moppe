# Surface storage and presentation

Moppe has one periodic `terrain::TerrainDomain` and two ground bundles over
it. `map::Surface` owns both:

- `SurfaceGeometry` always exists. It contains elevation, normal, eroded and
  deposited material history, and broad snow support.
- `SurfaceReadings` is created when world analysis begins. It contains channel
  flux, moisture, waterline distance, erosion and deposition presentation
  signals, tree habitat, forest cover, trail influence, and home-base
  influence.

There is no separate surface-domain type or atlas object. Terrain algorithms,
ground storage, hydrology products, and water sheets exchange the same domain
value directly.

## Geometry

Surface elevation is an affine point in the world's vertical frame. It is
stored in metres with the native representation of one `float`. Normal and
snow-support reconstruction use the interpolation stencil owned by
`TerrainDomain`.

Generation mutates the geometry bundle directly. Rebuilding geometry readings
recomputes normals and snow support and clears derived readings, because their
inputs may have changed.

## Derived readings

Completed-world construction fills `SurfaceReadings` in one explicit sequence:

```text
fractional drainage -> channel flux
standing water + drainage -> moisture
painted water sheet -> waterline distance
moisture + geometry -> tree habitat
trail analysis -> trail and home-base influence
habitat + use -> forest cover
material history -> erosion exposure and deposition cover
```

The active world is handed to the main thread only after this sequence. The
single optional bundle supports focused construction tests without imposing
an option or repeated domain copy on every column group.

## Water

`terrain::WaterSheets` is a distinct typed bundle over the same domain. It
contains water elevation, wave amplitude, and planar velocity. Ground and
water share an elevation specification and affine frame, so their difference
is a physical depth.

`map::WaterSurface` owns the completed water-sheet bundle and offers continuous
sampling. Matching ground and water dimensions are a world invariant, not a
reason to combine them into one object.

## Presentation

`game::SurfacePresentation` and `game::WaterPresentation` are the deliberate
quantity-to-number bridges. They pack typed columns into the homogeneous float
lanes expected by the renderer. Terrain and ecology policy remains on the
world side of this boundary.
