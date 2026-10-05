# Box3D (vendored)

Erin Catto's 3D physics engine, MIT licensed (see `LICENSE`).

- Upstream: https://github.com/erincatto/box3d
- Commit: e77352cd606dc1a34209094076199549a52ea0a1 (2026-10-04,
  "Add support for AVX2 and runtime feature detection")
- Contents: `include/` and the library sources of `src/` only. The upstream
  CMake files, samples, tests, benchmarks, and `extern/` are not vendored; the
  `box3d` target is defined in moppe's top-level `CMakeLists.txt`.

To update: copy `include/` and `src/*.{c,h,inl}` from a newer upstream
checkout over these files and record the new commit here.

Local patches (reapply after an update unless upstream has fixed them):

- `src/wheel_joint.c`, `b3WarmStartWheelJoint`: a wheel joint without
  steering added the spin-motor impulse to an angular impulse that already
  held it, so every substep warm-started the motor twice and a driven wheel
  received up to double its maximum motor torque. The non-steering branch now
  assigns, as the steering branch does.
