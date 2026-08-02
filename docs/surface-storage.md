# Surface storage and presentation

Moppe stores ground geometry, derived ground readings, and water as three
typed bundles over one periodic `terrain::TerrainDomain`.

| Bundle | Owner | Columns |
| --- | --- | --- |
| `map::SurfaceGeometry` | `GeneratedWorld` | surface elevation, terrain normal, eroded material, deposited material, snow support |
| `map::SurfaceReadings` | `GeneratedWorld` | channel flux, moisture, waterline distance, erosion exposure, deposition cover, tree habitat, forest cover, trail influence, home-base influence |
| `terrain::WaterSheets` | `GeneratedWorld` | surface elevation, wave amplitude, water velocity |

No `SurfaceDomain`, `SurfaceAtlas`, `map::Surface`, or `map::WaterSurface`
wrapper remains. Algorithms exchange the domain and bundles they actually
consume.

## Geometry

Surface elevation is an affine quantity point in the world's vertical frame,
stored directly as one `float` in metres. Ground and water use the same
elevation specification, so their difference is a physical length. Normals
are vector quantities with the native `Vec3` representation.

Generation mutates `SurfaceGeometry` directly. `map::rebuild_geometry`
reconstructs normals and broad snow support after either generation or cache
loading. Continuous reads use `TerrainDomain`'s periodic interpolation
stencil; there is no copied refresh surface.

## Derived readings

Each analysis produces the narrowest useful bundle:

```text
fractional drainage -> channel flux
standing water + drainage -> moisture
painted water -> waterline distance
material history -> erosion exposure + deposition cover
geometry + moisture -> tree habitat
habitat + trail use -> forest cover
trail network -> trail influence + home-base influence
```

`game::analyze_surface` joins those bundles into one `SurfaceReadings` value
when it assembles the completed world. Domain equality is checked at the join,
and duplicate quantity specifications are compile-time errors.

## Water

`terrain::WaterSheets` retains water separately from the ground while sharing
its domain. Elevation and amplitude drive the continuous horizontal water
surface; the horizontal components of velocity drive flow-aligned detail.
Seas, lakes, traversed pools, river reaches, confluences, and mouths therefore
share one clipped field. Only vertical waterfall curtains are geometry derived
from `RiverNetwork`, because a single elevation at each x/z cannot represent a
falling sheet.

Water and ground readings use borrowed `TexturePixels` descriptions that
write their final format directly into backend staging memory. Physical water
elevation and amplitude write `RG32F`; planar velocity narrows once into
`RG16F`.

## Persistence

`spatial::write_bundle` stores a typed bundle as one Arrow IPC stream record
batch. Scalar representations become Arrow numeric arrays; vector
representations become fixed-size lists; unusual trivial representations use
fixed-size binary.

Field metadata records quantity specification, kind, unit, dimension, and
storage form. Schema metadata records bundle version and serialized domain
identity. A reader rejects a stream whose domain, column set, units,
dimensions, or representations do not match the requested C++ bundle type.

The terrain cache therefore stores the expensive `SurfaceGeometry` value
without inventing a parallel cache schema. Later derived readings are rebuilt
from the geometry and current code.

## Presentation boundary

Presentation owns the conversion from typed columns to renderer resources:

- `game::Terrain` uploads authoritative elevation and normal columns;
- ground-reading upload creates format-specific `TexturePixels` rules;
- water presentation supplies ocean setup and typed water texture rules; and
- waterfall presentation builds only vertical nickpoint curtains.

A rule borrows its bundle for the duration of the renderer call and writes
once into storage supplied by the backend. Units and semantic quantity types
do not leak into Metal or WebGPU APIs, and numeric texture-lane policy does
not leak back into terrain analysis.
