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

## Experiments

- `erosion-lifetime-experiment.md` records the fixed-seed droplet-lifetime,
  sediment-ledger, and erosion-footprint sweeps. It explains why the default
  now uses natural water termination and explicit settlement, and why the
  remaining sink problem led to the standing-water/flood-field reading now
  exposed in the Terrain Lab.
- `stream-power-experiment.md` records the first finite-time analytical,
  droplet, hillslope, and combined comparison on one fixed source.

## Moppe implementation direction

The analytical method needs one adaptation before it can operate on Moppe's
periodic world.  Its river trees are rooted at fixed boundary cells, and the
paper requires at least one such cell for a well-posed solution.  A torus has
no geometric boundary.  Moppe should instead use an explicit outlet mask:

- submerged ocean cells are fixed outlets in the normal random-world recipe;
- an all-land recipe must provide explicit sinks, or deliberately select basin
  minima as fixed sinks;
- depression routing must ensure every other cell reaches one of those roots,
  without creating a receiver cycle across a periodic seam.

The first implementation should preserve the paper's module boundaries:

1. Build a tested `DrainageGraph` containing one receiver per cell, a
   topological ordering, accumulated drainage area, downstream distance, and
   outlet/basin identity.
2. [done, first slice] Implement the paper's finite-time characteristic
   solution for `n = 1` on a fixed drainage graph.
3. [partial] Add relaxed elevation/drainage fixed-point iteration, then its
   coarse-to-fine V-cycle. Keep fixed outlets unchanged at every level.
4. [partial] Expose this as a separate pipeline stage beside droplet erosion.
   Time, uplift, `k`, `m`, sea level, relaxation, and iteration count are now
   first-class parameters; outlet policy and multigrid levels remain.
5. Treat FastFlow as an optional GPU drainage backend for the same graph
   product, not as a different meaning for the analytical stage.

The historical 2049-square profile took about 10.78 seconds for 1.5 million
droplets capped at 64 steps. The conservation experiment reallocates a similar
step budget to 300,000 droplets that naturally die around step 305. MSL field
lowering accelerates recipe previews but does not accelerate this CPU erosion
pass; drainage and erosion remain the important compute-backend boundary.
