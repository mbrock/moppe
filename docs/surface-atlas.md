# The current-engine surface atlas

This is the small atlas for the part of Moppe that has adopted the Atelier
earth vocabulary. It describes the current engine, not the proposed second
engine. The purpose is to make its topology, intrinsic readings, and rendering
bridge enumerable without pretending that all terrain generation already has
this shape.

## Storeys

| Storey | Current object | Responsibility |
| --- | --- | --- |
| Combinatorial | `map::SurfaceDomain` | The finite vertex lattice, index/offset correspondence, horizontal spacing, boundary topology, and bilinear reconstruction stencil. |
| Intrinsic | `map::SurfaceSections`, `map::WaterSurfaceSections` | Typed 0-cochains sharing the lattice but kept in bundles belonging to the ground and water respectively. `map::Surface` owns the ground's materialization barriers. |
| Extrinsic | `game::SurfacePresentation`, `game::WaterPresentation` | Convert typed columns to plain scalar texture payloads and upload them through `render::Renderer`. These are the quantity-to-number bridges. |

The authoritative `RandomHeightMap` and the terrain-generation pipeline still
precede these storeys. `Surface::refresh` is the explicit point where their
finished geometry becomes intrinsic section data. Hydrology, trails, and
ecology add readings at later, named barriers. They do not mutate the domain.
`Surface::has_section<quantity_spec>()` distinguishes a section that has not
crossed its barrier from one whose legitimate value happens to be zero.

## Domain

| Domain | Sites | Boundary | Reconstruction | Defined in |
| --- | --- | --- | --- | --- |
| `SurfaceDomain` | Heightmap vertices, including Moppe's existing duplicated periodic seam | `terrain::Topology::Bounded` clamps; `terrain::Topology::Torus` wraps | Four-site bilinear stencil owned by the domain | `moppe/map/surface_domain.hh` |

The duplicated torus seam is an inherited storage fact of the current engine.
Moving that seam entirely into presentation remains Atelier work; this refactor
does not disguise the current heightmap as the proposal's seam-free topology.

## Intrinsic sections

All current sections are vertex 0-cochains in `SurfaceSections`.

| Quantity specification | Value | Meaning | Materialized by |
| --- | --- | --- | --- |
| `surface_elevation` | elevation point in metres | Height in the current default elevation frame | `Surface::refresh` |
| `surface_normal` | dimensionless vector | Detailed lighting and contact normal | `Surface::refresh` |
| `snow_support` | dimensionless scalar | Up component of the broad support plane used by snow | `Surface::refresh` |
| `channel_flux` | dimensionless planar vector | Channel tangent scaled by visible fluvial activity | drainage analysis in world setup |
| `surface_moisture` | dimensionless scalar | Ground wetness synthesized from standing water and drainage | moisture analysis in world setup |
| `waterline_distance` | length in metres | Horizontal distance to the extracted wet/dry curve | waterline analysis in world setup |
| `erosion_exposure` | dimensionless scalar | Removed-material signal normalized against its robust datum | `derive_geology_materials` |
| `deposition_cover` | dimensionless scalar | Deposited-material signal normalized against its robust datum | `derive_geology_materials` |
| `tree_habitat` | dimensionless scalar | Ecological support from water, elevation, and slope | `derive_tree_habitat` |
| `forest_cover` | dimensionless scalar | Recruited canopy after habitat, trails, and settlement | `derive_forest_cover` |
| `trail_influence` | dimensionless scalar | Shoulder-blended membership in formed trails | trail analysis in world setup |
| `home_base_influence` | dimensionless scalar | Membership in the inhabited clearing | trail analysis in world setup |

`moppe/map/surface_sections.hh` is the ontology page in code. Geometry
materialization lives in `surface.cc`; ecological rules live in
`surface_ecology.cc`. Consumers sample quantities from `map::Surface` or read a
named column through `surface.section<quantity_spec>()`.

## Presentation mappings

`game::SurfacePresentation` is deliberately mechanical. It performs no
terrain or ecology policy.

| Intrinsic section | Renderer payload |
| --- | --- |
| `forest_cover` | one float per terrain texel |
| `snow_support` | one float per terrain texel |
| `channel_flux` | interleaved world-plane x/z floats |
| `surface_moisture` | one float per terrain texel |
| `waterline_distance` | one metre-valued float per terrain texel |
| `erosion_exposure`, `deposition_cover` | two scalar texture lanes |
| `trail_influence`, `home_base_influence` | two scalar texture lanes |

## Water surface

`WaterSurfaceSections` uses the same `SurfaceDomain` because the renderer's
water sheet is sampled at the same sites, but it is not part of the ground
bundle. Matching texture dimensions are a presentation fact, not an identity.

| Quantity specification | Value | Meaning |
| --- | --- | --- |
| `surface_elevation` | elevation point in metres | Standing or running water height in the same affine elevation frame as the ground |
| `wave_amplitude` | dimensionless scalar | Local multiplier for visible surface motion |
| `water_velocity` | planar vector in metres per second | Horizontal movement of water detail through the sheet |

`WaterPresentation` is where physical elevation is divided by the terrain
height scale and where velocity loses its unit and y component for the Metal
texture contract. `game.cc` now owns a `WaterSurface` rather than anonymous
interleaved level and flow vectors.

Water-body identity, wet/dry membership, the extracted waterline complex, and
river-network incidence still live in their established terrain-analysis
objects. They are not falsely represented as more vertex columns merely to
make the atlas look complete.

## Frames and projections

The current surface has one implicit elevation origin and one world-space
horizontal chart. They are represented by existing `position_t`, `meters_t`,
and `SurfaceDomain` spacing, but not yet by registered frame projections. Sea
level and the home site remain runtime values rather than typed origins. Those
are explicit gaps relative to the Atelier earth proposal.
