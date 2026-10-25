# zstd's decoder (vendored)

Zstandard's decompressor as its single-file amalgamation, BSD licensed (see
`LICENSE`). It unpacks the assets and land a release builds into the game
(CMake's `MOPPE_EMBED_ASSETS`, `tools/pack-assets`).

- Upstream: https://github.com/facebook/zstd, release v1.5.7
- Contents: `zstddeclib.c`, made by upstream's
  `build/single_file_libs/combine.py -r ../../lib -x legacy/zstd_legacy.h
  -o zstddeclib.c zstddeclib-in.c`, and `lib/zstd.h` and
  `lib/zstd_errors.h` to call it by. No local changes.

To update: run the same in a newer release's `build/single_file_libs/` and
copy the three files over these.
