# Determinism across platforms

A seed makes the same world, bit for bit, on every platform the game builds
for. That lets a finished world be shared as a cache: whatever machine baked
it, any other would have computed exactly the same thing.

Verified on 2026-10-09 with `world-fingerprint 2048 play 123` (the default
world) on an Apple M2 Pro (macOS, AppleClang, libc++) and an Intel i7-10700K
(NixOS, Clang 21, libstdc++), and again at 257 and 1025 samples, with x86-64
and x86-64-v3 code generation: every stage, every geological step, and every
file of the finished world agree.

## What the rules are

Three things differed between platforms, and each has one rule:

1. **No contraction.** Clang fuses `a * b + c` into one fused multiply-add
   where the hardware has it: every arm64 machine, but not baseline x86-64.
   A fused operation rounds once instead of twice, so the bits differ. The
   whole project builds with `-ffp-contract=off` (`CMakeLists.txt`); write
   `std::fma` where a fused operation is wanted, which is exact everywhere.

2. **Correctly rounded transcendentals.** Apple's libm, glibc, and Microsoft's
   CRT each compute `sin`, `cos`, `tan`, `atan2`, `exp`, `log`, `log2`,
   `pow`, and `hypot` to within an ulp or so, differently. Generation code
   calls `moppe::cr::` (`moppe/correct_math.hh`) instead, CORE-MATH's
   correctly rounded implementations (`third_party/core-math`): each returns
   the true result rounded once, which is unique. `sqrt`, `floor`, `ceil`,
   `round`, `fma`, `fmod`, and `remainder` are exact in every library
   already, so `std::` is fine for them.

3. **One vector arithmetic.** `Vec3` (`moppe/gfx/math.hh`) is a Clang
   three-lane vector on every platform. Lanewise operations are exact IEEE;
   its dot product, length, normalization, and cross product are written out
   in a fixed order rather than taken from Apple's simd or the standard
   library's three-argument `hypot`, which round differently.

The standard library's algorithms are a fourth hazard that has not bitten
yet: `std::sort` and `std::nth_element` order equal elements differently in
libc++ and libstdc++, `std::priority_queue` breaks ties differently, the
`std::*_distribution` classes are implementation-defined, and unordered
containers iterate in different orders. Generation code already avoids them
where order matters (`make_perlin_permutation` draws its own bounded
integers from `std::mt19937`, which is fully specified); keep comparisons
total -- break ties by cell index -- when adding a sort or a heap.

## Checking it

`world-fingerprint [RESOLUTION [PROFILE [SEED]]]` (a developer tool:
configure with `-DMOPPE_BUILD_DEVELOPER_TOOLS=ON`) prints a hash after every
generation stage, after each geological step, and of each file of the
finished world. Run it on two machines and `diff` the output; the first
differing line says where they part.

```bash
cmake --build build --target world-fingerprint
./build/world-fingerprint 257 play 123 > mac.txt
```

`tests/terrain/geological_test.cc` pins the hash of the initial geological
field, which therefore holds on every platform. A change that alters the
world on purpose updates the pinned hashes and bumps `CACHE_VERSION` in
`moppe/game/world_cache.cc`.

## Threads

Generation runs on every hardware thread and computes the same bits on any
number of them. `moppe::parallel_for` (`moppe/parallel.hh`) hands each
thread a contiguous range, and the rule is that a body writes only its own
range's elements: a cell's route, a face's flux, a row's partial sum.
Totals are added by row and then the rows in order, never per thread, since
float addition does not associate. `MOPPE_THREADS=N` overrides the count;
the default 2048 land hashes the same with 10 threads on the M2 and 16 on
the i7.

## Cost

Correct rounding cost about 15% of generation on the M2 at first (23.8 s to
27.4 s for a 1025 world): CORE-MATH's `hypotf`, `cosf`, `atan2f`, and `sinf`
are slower than Apple's, and the loops lose their fused operations. The
speed work that followed more than repaid it (docs/orogeny-performance.md):
most of those calls were algebra that plain arithmetic does exactly. x86
builds target x86-64-v3 so CORE-MATH's exact arithmetic uses hardware FMA
(7% faster on the i7; the same bits).

## Not covered

GPU computation. Metal, Vulkan, and Direct3D do not promise IEEE-exact
division, fusion, or transcendentals, so a GPU stage cannot be part of the
canonical world; `MOPPE_METAL_OROGENY=1` (docs/orogeny-performance.md) is
such a stage and makes a world that only the same GPU reproduces.
