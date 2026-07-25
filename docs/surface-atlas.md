# The current-engine surface atlas

This is the small atlas for Moppe's adopted Atelier earth vocabulary. It
describes the current engine, not the proposed second engine. The purpose is
to make its topology, intrinsic readings, and rendering bridge enumerable.

For the wider ownership, state, presentation, and target map, start with the
[engine atlas](engine-atlas.md).

## Storeys

| Storey | Current object | Responsibility |
| --- | --- | --- |
| Combinatorial | `map::SurfaceDomain` | The finite toroidal vertex lattice, index/offset correspondence, horizontal spacing, and bilinear reconstruction stencil. |
| Intrinsic | `map::SurfaceAtlas`, `map::WaterSurfaceSections` | Typed 0-cochains sharing the lattice but kept in named ground groups and a distinct water bundle. `map::Surface` owns the authoritative ground geometry and its later analyses. |
| Extrinsic | `game::SurfacePresentation`, `game::WaterPresentation` | Convert typed columns to plain scalar texture payloads and upload them through `render::Renderer`. These are the quantity-to-number bridges. |

Terrain generation writes the mandatory geometry bundle directly. Hydrology,
geology, ecology, and use add readings at later, named barriers. They do not
mutate the domain.
`Surface::atlas()` exposes those views: a null section pointer means it has not
crossed its barrier, while a present section can legitimately contain zeroes.

## Domain

| Domain | Sites | Boundary | Reconstruction | Defined in |
| --- | --- | --- | --- | --- |
| `SurfaceDomain` | One site per terrain sample | Always wraps over the full lattice extent | Four-site bilinear stencil owned by the domain | `moppe/map/surface_domain.hh` |

The domain stores no duplicated seam. Presentation repeats the world by
translated images; it does not add lattice sites.

## Intrinsic sections

All current sections are vertex 0-cochains over the atlas's one
`SurfaceDomain`. `geometry()` exists for the whole lifetime of `Surface`.
The other views expose optional named sections so their existing barriers
remain visible without a parallel Boolean availability ledger. Trail and
home-base readings share one use bundle because they are one analysis product.

| Group and section | Value | Meaning | Becomes valid |
| --- | --- | --- | --- |
| `geometry`: `surface_elevation` | affine elevation point in metres | Authoritative position in the world's vertical frame | terrain evaluation or cache load |
| `geometry`: `terrain_normal` | dimensionless vector | Detailed lighting and contact normal | `rebuild_geometry_readings` |
| `geometry`: `eroded_surface_material`, `deposited_surface_material` | semantic dimensionless scalars | Lifetime cut/fill history in terrain storage units | terrain evolution |
| `geometry`: `snow_support` | dimensionless scalar | Up component of the broad support plane used by snow | `rebuild_geometry_readings` |
| `hydrology`: `channel_flux` | dimensionless planar vector | Channel tangent scaled by visible fluvial activity | drainage analysis in world setup |
| `hydrology`: `surface_moisture` | dimensionless scalar | Ground wetness synthesized from standing water and drainage | moisture analysis in world setup |
| `hydrology`: `waterline_distance` | length in metres | Horizontal distance to the extracted wet/dry curve | waterline analysis in world setup |
| `geology`: `erosion_exposure`, `deposition_cover` | dimensionless scalars | Normalized removed- and deposited-material signals | `derive_geology_materials` |
| `ecology`: `tree_habitat` | dimensionless scalar | Ecological support from water, elevation, and slope | `derive_tree_habitat` |
| `ecology`: `forest_cover` | dimensionless scalar | Recruited canopy after habitat, trails, and settlement | `derive_forest_cover` |
| `use`: `trail_influence` | dimensionless scalar | Shoulder-blended membership in formed trails | trail analysis in world setup |
| `use`: `home_base_influence` | dimensionless scalar | Membership in the inhabited clearing | trail analysis in world setup |

`moppe/map/surface_sections.hh` is the ontology page in code. Geometry
storage and reconstruction live in `surface_geometry.cc`; ecological rules live in
`surface_ecology.cc`. Consumers sample quantities from `map::Surface` or read
the appropriate named atlas view, such as
`surface.atlas().ecology().forest_cover()`.

## Presentation mappings

`game::SurfacePresentation` is deliberately mechanical. It performs no
terrain or ecology policy. The Terrain Lab uses its narrow path-payload route
too: a rebuilt `TrailNetwork` becomes the same terrain-path texture lanes only
at this bridge, while a pristine Lab view reuses its already materialized
payload.

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

`WaterPresentation` keeps elevation in metres while packing elevation,
amplitude, and the x/z components of velocity into the renderer's homogeneous
numeric lanes. It also turns the metre-valued water datum and typed world
extent into the renderer's numeric ocean setup. World assembly owns a
`WaterSurface` rather than anonymous interleaved level and flow vectors.

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
