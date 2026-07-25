# Terrain generation and analysis

Moppe has one finite terrain lattice and one authoritative ground bundle.
Generation creates typed columns over that lattice; ordered transforms mutate
the elevation and material-history columns; analyses return named finite
products. There is no runtime field-expression language or general
materialization backend.

## The common domain

`terrain::TerrainDomain` owns:

- periodic width and height;
- physical X/Z sample spacing;
- cell area and world periods;
- checked index/offset conversion;
- the bilinear stencil used for continuous sampling.

An NxN domain is N periodic cells. It stores no duplicated seam. CPU
neighbours and GPU texel reads wrap at N.

Terrain generation, surface geometry, flood, drainage, merge trees, trails,
waterlines, and water surfaces exchange this domain directly rather than
translating between grid descriptions.

## Typed finite bundles

`spatial::Bundle<Domain, Quantities...>` stores one native vector per quantity
specification over a finite domain. A row is one site; a focus is a site plus
its domain position for local rules. `spatial::get<QS>` selects a column by
meaning, and `spatial::sample<QS>` reconstructs a continuous reading.

The value category determines reconstruction:

- quantities use an ordinary weighted sum;
- quantity points use one anchor plus weighted point differences.

Surface elevation is an affine point in the world vertical frame. It occupies
exactly one `float`, measured in metres. A normal occupies exactly one `Vec3`.
The types add compile-time meaning without boxed runtime storage.

`TerrainElevations` is the analysis constraint for any `TerrainDomain` bundle
containing `surface_elevation`. The minimal `ElevationMap` is useful for tests
and numerical transform steps. The full surface geometry bundle satisfies the
same concept without copying or creating a view.

## Authoritative surface

`Surface::geometry()` is the mandatory ground bundle. It contains:

- surface elevation;
- terrain normal;
- eroded and deposited material history;
- broad snow support.

One optional `SurfaceReadings` bundle holds the later hydrology, geology,
ecology, and land-use columns. Completed-world construction fills it before
handoff. `map::Surface` also owns interpolation, geometry rebuilding, cache
I/O, material-history updates, and derivation of those readings.

Ground and water use separate bundles over the same domain.
`WaterSurfaceSections` reuses the ground elevation specification and affine
frame, so water elevation minus ground elevation is a physical depth. Wave
amplitude and water velocity remain water-specific columns.

All CPU and GPU terrain heights are metre-valued. Renderer height textures and
water-sheet lanes receive the native metre floats unchanged; the shader
vertical scale is one. The world's vertical extent is only a camera and shadow
bound.

## Direct geology

`generate_geology(domain, seed)` directly fills a
`GeologicalSections` bundle containing:

- `continent_shape`;
- `uplift_weight`.

The noise composition is the generator, not a public graph-shaped parameter
object. Its constants live beside the arithmetic they control. The
implementation preserves seeded periodicity, deterministic row evaluation,
and progress reporting without an expression DAG or generic evaluator.

The deleted expression system included `ScalarField`, typed phantom fields,
`CpuEvaluator`, the Metal function-stitching backend, and
`TerrainDiscretization`. Its only production job was this fixed geology
recipe, so direct finite generation is both smaller and more honest.

## Direct world construction

The game has one terrain sequence, spelled literally in world loading:

```text
initialize_terrain(surface, seed, water datum)
  -> evolve_terrain(surface, uplift, evolution parameters)
  -> form_terrain_trails(surface, trail parameters)
```

These are free operations in `map/terrain_generation.*`, not methods on a
stateful evaluator. There is no transform variant, legal-order validator,
checkpoint ledger, editor schema, or second execution model.

`WorldRecipe` binds physical extent, resolution, water datum, root seed, and
generation profile. It carries the two algorithm values that genuinely vary:
`StreamPowerEvolution` and `TrailFormation`. The profile selects evolution
duration; it does not construct a runtime program. The `smoke` profile runs
one 50,000-year erosion step so instrumented smoke and coverage sessions still
exercise evolution without simulating the full `fast` profile.

Stream-power evolution is a numerical kernel over physical metre floats.
Each step publishes its current values as an `ElevationMap` when invoking
bundle-constrained flood analysis. D-infinity routing consumes the resulting
filled surface and lake census; it does not need a redundant ground view.

## Hydrology readings

Standing water consumes an elevation bundle and a sea level in metres:

```text
elevation bundle + sea level
  -> FloodField
       water level and depth in metres
       ocean membership
       spill receivers
  -> LakeCensus
       physical area, depth, volume, level, and spill
```

On the torus there is no exterior boundary. The largest connected component
at or below sea level is the global ocean, with scan order breaking ties. An
all-land world uses its global minimum as the explicit endorheic fallback.

Wet drainage uses the filled flood surface and census:

```text
FloodField + LakeCensus
  -> DrainageGraph
       receiver topology
       slope
       contributing area
  -> FractionalDrainage
       D-infinity routes
       fractional contributing area
       channel tangent and area flux
```

The single-receiver graph owns water-body and reach topology. Fractional
drainage refines continuous direction and accumulation; D8 is not a second
supported routing mode.

`RiverNetwork`, `WaterSheets`, and `Waterline` are later readings from these
products. Water sheets remain physical in metres. Presentation only packs
heterogeneous typed columns into the renderer's homogeneous numeric lanes.

## Trails

Trail planning consumes an elevation bundle, a drainage graph, and a flood
field. A roughly 16-metre planning lattice is a deliberate route-search
resolution, not another description of the terrain domain. Every coarse edge
is checked against full-resolution elevations.

The plan chooses a buildable home base and a scenic focus, then joins two
terrain-costed rides into one directed circuit. A periodic Hermite alignment
refines the route; bounded cut and fill produce physical earthwork deltas.
Trail and home-base influence are one optional typed `TrailUseMap`; the two
readings are generated and become valid together.

See [Trail system](trails.md) for routing and grading details, and
[Lattice harmonization](lattice-harmonization.md) for the deletion history.
