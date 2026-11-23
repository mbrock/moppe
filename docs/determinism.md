# Determinism across platforms

The CPU reference computes identical land for a given recipe on supported
platforms. `tools/bake-world` uses that reference and packages the finished
world, including river beds, roads and forests. `tools/bake-land` packages the
four land fields. Both bakers and `world-fingerprint` select CPU evolution
independently of the local game setting. To play a shared bake on desktop,
set `MOPPE_BAKED_WORLD` to the directory printed by `tools/bake-world`. The
loader reads its extent, seed, profile and resolution from its header.

Local GPU generation may produce different heights and stream trees. A shared
session should load the same baked asset on every peer. The multiplayer world
tag includes the finished terrain heights. Local Metal caches carry the backend
and algorithm version. A bundled finished world, bundled land, or downloaded
reference land takes precedence when starting the matching recipe.

## CPU reference

Verified on 2026-10-09 with `world-fingerprint 2048 play 123` (the former 5 km default) on an Apple M2 Pro (macOS, AppleClang, libc++) and an Intel i7-10700K
(NixOS, Clang 21, libstdc++), and again at 257 and 1025 samples, with x86-64
and x86-64-v3 code generation: every stage, every geological step, and every
file of the finished world agree. Verified again on 2026-10-12, with the
rivers' beds in the land (v3), on an Apple M5, an AMD Ryzen 9 7950X3D, and
the i7: all 76 lines the same, and the same as the files the game itself
had cached on the Mac.

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

`world-fingerprint [RESOLUTION [PROFILE [SEED [KM]]]]` (a developer tool:
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
the original 2048 land hashes the same with 10 threads on the M2 and 16 on
the i7.

## Cost

Correct rounding cost about 15% of generation on the M2 at first (23.8 s to
27.4 s for a 1025 world): CORE-MATH's `hypotf`, `cosf`, `atan2f`, and `sinf`
are slower than Apple's, and the loops lose their fused operations. The
speed work that followed more than repaid it (docs/orogeny-performance.md):
most of those calls were algebra that plain arithmetic does exactly. x86
builds target x86-64-v3 so CORE-MATH's exact arithmetic uses hardware FMA
(7% faster on the i7; the same bits).

## GPU acceptance

`MOPPE_OROGENY=cpu|metal|metal-multigrid` selects the local solver on macOS.
Metal uses single-receiver flow with saddle carving, while the CPU uses
fractional D-infinity flow. Near-tie decisions, floating-point evaluation,
channel width and the positions of individual tributaries may vary.

The Metal tests compare two seeded landscapes with the CPU at the same extent,
resolution and geological age. They check the elevation distribution, exposed
land fraction, largest coastal catchment and number of substantial river
mouths using the CPU hydrology analyzer. Independent budgets compare final
surface volume plus exported sediment with the initial surface and uplift.
The contributing areas at outlets must sum to the entire periodic domain,
including in the all-land regression. The full-size comparison and hillshades
are in [Orogeny performance](orogeny-performance.md).
