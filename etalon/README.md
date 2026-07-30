# Étalon

Étalon is a small Zig workshop in the Atelier tradition. Its name is the
French word for a reference standard: an object against which a measurement
is compared.

The experiment asks how much of Moppe's quantity discipline needs C++
template and operator machinery, and how much can instead be expressed as
small, explicit compile-time transformations:

- a **dimension** is a canonical exponent vector;
- a **quantity specification** adds semantic identity and tensor order;
- a **quantity** is a generated runtime type carrying only its representation;
- a **unit** converts a representation to and from coherent storage;
- a **field operation** computes its result specification at compile time;
- an **interpretation** gives a mechanically derived result a domain meaning.

The distinction between dimension and meaning is deliberate. `airspeed` and
`rate_of_climb_speed` have the same dimension but different specifications.
Likewise, an elevation point cannot be offset by a water depth merely because
both ultimately involve metres.

The first calculus operation is a periodic one-dimensional Laplacian. Applying
it to surface elevation automatically produces inverse-length dimensions.
Calling that result `terrain_curvature` is a separate, explicit operation whose
claimed dimensions Zig checks at compile time.

## Build

Étalon tracks Zig nightly rather than a stable release. This checkpoint was
built with:

    0.17.0-dev.1503+1f1bee62e

Run it from the repository root:

    make etalon-test
    make etalon
    make etalon-watch

Or work directly:

    cd etalon
    zig build test
    zig build run
    zig build -fincremental --watch unit

Build products stay in Zig's normal `.zig-cache/` and `zig-out/` directories
and are ignored by Git.

The full `test` step also compiles deliberately invalid examples and expects their
diagnostics. These establish that equal dimensions do not permit adding
`airspeed` to `rate_of_climb_speed`, and that a scalar Laplacian cannot be
silently interpreted as vector-valued `water_velocity`. The `unit` step omits
these expected failures because nightly watch mode keeps failed compiler
processes alive; it is the target used for the incremental edit loop.
