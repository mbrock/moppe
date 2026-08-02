+++
id = "RTG-010"
title = "Build a bounded periodic terrain reflection proxy"
rfc = "RFC-0004"
track = "reflection-geometry"
status = "done"
depends_on = ["RTG-001"]
order = 20
areas = ["rendering", "terrain", "metal", "tests"]
+++
# Build a bounded periodic terrain reflection proxy

## Outcome

The Metal backend can derive a small ray-tracing representation from the
completed authoritative surface without becoming another terrain owner.

## Scope

Sample a camera-relevant periodic window at source stride 8, emit opaque
terrain triangles, quantify height and projected error, upload one private
vertex buffer, and retain one primitive acceleration structure with the Metal
terrain resources. Do not include water, trees, actors, instances, or refits.

## Acceptance

- Flat terrain reconstructs exactly.
- Discarded relief produces nonzero measured error.
- A window crossing the toroidal seam samples the correct source heights.
- Counts, error, retained memory, scratch, and construction time are reported.

## Evidence

The portable builder and three tests cover exactness, error, and seam
wrapping. The seed-123 proxy contains 22,050 triangles, retains 4,675,784 GPU
bytes, and reports the full error and construction record in the
[findings](../findings.md).
