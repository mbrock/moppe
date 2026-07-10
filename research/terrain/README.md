# Terrain generation research

This folder keeps the primary papers guiding Moppe's terrain laboratory.
The PDFs are archived locally so implementation work can refer to stable
copies rather than depending on browser bookmarks.

## Papers

- `tzathas-2024-analytical-erosion.pdf` — Petros Tzathas, Boris
  Gailleton, Philippe Steer, and Guillaume Cordonnier, "Physically-based
  Analytical Erosion for fast Terrain Generation," *Computer Graphics
  Forum* 43(2), 2024.
  DOI: [10.1111/cgf.15033](https://doi.org/10.1111/cgf.15033)
- `jain-2024-fastflow.pdf` — Aryamaan Jain, Bernhard Kerbl, James Gain,
  Brandon Finley, and Guillaume Cordonnier, "FastFlow: GPU Acceleration
  of Flow and Depression Routing for Landscape Simulation," *Computer
  Graphics Forum* 43(7), 2024.
  DOI: [10.1111/cgf.15243](https://doi.org/10.1111/cgf.15243)

## How they relate

These are complementary 2024 approaches, not a sequence where FastFlow
implements the analytical paper.

The analytical-erosion paper replaces geological time stepping with
one-dimensional analytical stream-power solutions evaluated along river
trees. Because the terrain determines its drainage network and the drainage
network changes the terrain, it still performs a fixed-point iteration;
multigrid acceleration lets basin boundaries move across the grid quickly.
Its reference implementation is CPU-oriented and also describes hillslope
and thermal terms needed near ridges.

FastFlow accelerates the graph operations underneath landscape simulation:
flow accumulation is expressed as a parallel scan over drainage trees, and
depression routing as a parallel minimum-spanning-tree problem. It then uses
those operations for an implicit, slope-linear stream-power update and a
semi-implicit sediment-deposition pass on the GPU.

Both descend from the same stream-power landscape-evolution family and cite
earlier implicit work. FastFlow cites the analytical paper for its treatment
of hillslope effects, but does not build its erosion solver from the latter's
method of characteristics or multigrid fixed-point solution.

For Moppe, they suggest two separable modules:

1. A drainage backend: receivers, depressions, accumulated area, river trees.
2. An evolution operator: analytical aging, implicit stream-power stepping,
   droplet erosion, sediment deposition, or combinations of them.

Keeping those pieces separate will let the terrain lab compare methods while
feeding every result into the same heightmap renderer.
