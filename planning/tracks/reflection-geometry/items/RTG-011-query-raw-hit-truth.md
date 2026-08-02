+++
id = "RTG-011"
title = "Query the terrain proxy and expose raw hit truth"
rfc = "RFC-0004"
track = "reflection-geometry"
status = "done"
depends_on = ["RTG-010"]
order = 30
areas = ["rendering", "metal", "shaders", "diagnostics"]
+++
# Query the terrain proxy and expose raw hit truth

## Outcome

One Metal 4 compute pass can traverse the retained terrain acceleration
structure and make its result inspectable without changing scene color.

## Scope

Trace camera-primary rays for the deterministic capture and write four
quadrants: hit normal, logarithmic distance, primitive/barycentric topology,
and hit mask. Bind the acceleration structure, uniforms, output, and triangle
buffer through one private Metal 4 argument table.

## Acceptance

- A terrain skyline separates hits from sky misses.
- Facet normals, distance, primitive boundaries, and proxy extent are visible.
- The diagnostic is encoded after ordinary scene capture and never sampled by
  water or presentation.

## Evidence

The validated 2560x1600 diagnostic under
`/tmp/moppe-reflection-geometry-goal0` shows all four coherent views. The
compute shader compiles for macOS, iOS, and tvOS Metal 4 SDK targets.
