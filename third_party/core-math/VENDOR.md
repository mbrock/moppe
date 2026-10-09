# CORE-MATH (vendored)

Correctly rounded mathematical functions from INRIA's CORE-MATH project, MIT
licensed (see `LICENSE`). moppe uses them so a world generates bit for bit the
same on every platform (docs/determinism.md); the C++ entry points are
`moppe::cr::*` in `moppe/correct_math.hh`.

- Upstream: https://gitlab.inria.fr/core-math/core-math
- Commit: 040ee482a8caefe2afe71a3a59d43923d2f0c7e6 (2026-10-09)
- Contents: for sin, cos, tan, atan2, exp, log, log2, pow, and hypot, the
  binary32 (`src/binary32/F/Ff.c`) and binary64 (`src/binary64/F/F.c`)
  implementations and the private headers beside them, one directory per
  function as upstream lays them out (`log` and `pow` each carry their own
  `dint.h`). Upstream's test drivers, MPFR references, and
  `function_under_test.h` are not vendored; the `core-math` target is
  defined in moppe's top-level `CMakeLists.txt`.

To update: copy the same files from a newer checkout over these and record
the new commit here; to add a function, copy its directory the same way and
add it to `CORE_MATH_FUNCTIONS` and `moppe/correct_math.hh`.

Local patch (reapply after an update): `binary64/hypot/hypot.c`,
`binary32/pow/powf.c`, and `binary64/pow/pow.c` save the floating-point
exception flags before computing and restore them after, so an exact result
raises no spurious inexact. moppe never reads the flags, and the save and
restore cost a C library call on arm64 and a serializing MXCSR write on
x86-64 per call, so their `get_flag(s)` and `set_flag(s)` do nothing when
`CORE_MATH_IGNORE_FLAGS` is defined (the `core-math` target defines it).
Only flags change; no result does.

The implementations call `__builtin_fma` for their double-double arithmetic.
That is exact everywhere, but on an x86-64 target without FMA it becomes a
call into the C library's software `fma`; x86 builds should target
x86-64-v3 (Haswell and later, every Steam Deck and Xbox Series).
