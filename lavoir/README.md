# Lavoir

The wash-house by the water: a fresh workshop in the Atelier's
tradition, founded to carry the field, ledger, and domain ideas from
`ideas/` into running code. Metal on top, Arrow at the bottom,
borrowing in between.

## Charter

- C++ with a thin Cocoa shim, metal-cpp, Metal 4. The renderer opens
  a window or captures headlessly (`lavoir --capture out.png`).
- Types are `snake_case`, in the manner of the standard library.
  CamelCase is reserved for concepts and template parameters.
- Concepts before base classes: `Domain` and `Bundle` are concepts;
  anything with the right shape belongs.
- Storage is tenure. Buffers are owned high (page-aligned Arrow
  allocations, wrappable by Metal without copying, streamable to IPC)
  and leased downward as spans. Fields own nothing.
- Comments are micro-abstracts: a few durable `///` sentences saying
  what a thing is and why, no banners, no rules of hyphens.
- Functions stay short enough to read as sentences.
- Tests ride in `tests/lavoir/`, in the moppe suite.

## Build

    cmake -B build -G Ninja
    ninja -C build lavoir
    ./build/lavoir.app/Contents/MacOS/lavoir
