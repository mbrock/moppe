# Forge artifacts

The review forge reads `forge.x86_64-linux` from this flake and builds its
derivations. Each is also an ordinary package output:

```
nix build .#terrain
nix build .#web
nix build .#steam-deck
nix build .#linux-portable
nix build .#demo-video
```

`terrain` builds the native land baker, then runs Play/seed 123 at 1024 samples
(browser) and 2048 samples (native). No GitHub release upload is involved.
`web` pins its Emscripten toolchain and SDL3/Dawn ports and fetches the
`../terrain/terrain.json` manifest before starting. Serve these outputs as
sibling directories `web/` and `terrain/`, with CORS when using an opaque-origin
sandbox. Failed terrain downloads leave an error onscreen and do not start
local erosion. Plain `make web` remains available and carries terrain as before.

`steam-deck` bundles terrain and uses the portable recipe documented in
`steam-deck.md`. `linux-portable` uses the same recipe with an x86-64-v2 baseline.
Both keep the host's Vulkan/display/audio drivers dynamic, use glibc 2.31 as
the maximum ABI baseline, and run the same ELF verification at build time.
The original `moppe` and `moppe-steamdeck` packages remain available.

`demo-video` reuses `tools/record` and its river camera scene, which shares the
recording facilities used by `tools/trailer`. It records 20 seconds at 1280x720,
30 fps. Xvfb supplies the window surface, while host RADV renders the frames.
It requires a Nix builder advertising `gpu` with `/dev/dri` and
`/run/opengl-driver` exposed to its sandbox. Video rendering depends on the
host driver; byte-for-byte reproducibility is not claimed.

The output attrset can later include a signed macOS app requiring a Mac runner.
No signing credentials or Mac derivation are included now.
