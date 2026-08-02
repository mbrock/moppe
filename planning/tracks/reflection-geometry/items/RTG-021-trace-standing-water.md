+++
id = "RTG-021"
title = "Trace the raw standing-water reflection signal"
rfc = "RFC-0004"
track = "reflection-geometry"
status = "done"
depends_on = ["RTG-020"]
order = 60
areas = ["rendering", "water", "metal", "shaders"]
+++
# Trace the raw standing-water reflection signal

## Outcome

Actual clipped water geometry can provide sparse reflection origins and
optical normals to one Metal 4 closest-hit query without entering scene color.

## Scope

Rasterize the ordinary coarse and lattice standing-water surfaces into one
single-sample input per in-flight slot. Share ripple-normal math with ordinary
water, reject running water, validate visibility against terrain, and trace
one reflected terrain ray. Keep every output in a separate texture.

## Acceptance

- Origin depth and normals are continuous over visible standing water.
- Hit normal, distance, and validity agree spatially.
- Sky misses remain coherent and no output is sampled by ordinary water.

## Evidence

The final six-panel capture shows coherent inputs, sky radiance, and matching
terrain-hit auxiliaries. Metal validation reports no error.
