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
| Intrinsic | `map::SurfaceSections` | Typed 0-cochains sharing that domain. `map::Surface` owns their materialization barriers and sampling interface. |
| Extrinsic | `game::SurfacePresentation` | Converts typed columns to plain scalar texture payloads and uploads them through `render::Renderer`. This is the quantity-to-number bridge. |

The authoritative `RandomHeightMap` and the terrain-generation pipeline still
precede these storeys. `Surface::refresh` is the explicit point where their
finished geometry becomes intrinsic section data. Hydrology, trails, and
ecology add readings at later, named barriers. They do not mutate the domain.

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
| `trail_influence`, `home_base_influence` | two scalar texture lanes |

Moisture, water surface/flow, shoreline distance, and the sediment ledger are
still staged as untyped renderer data in `game.cc`. They are the next honest
candidates for section adoption; this atlas lists the boundary instead of
claiming the migration is complete.

## Frames and projections

The current surface has one implicit elevation origin and one world-space
horizontal chart. They are represented by existing `position_t`, `meters_t`,
and `SurfaceDomain` spacing, but not yet by registered frame projections. Sea
level and the home site remain runtime values rather than typed origins. Those
are explicit gaps relative to the Atelier earth proposal.
