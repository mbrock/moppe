# The website

The game's landing page, which is meant to replace the bare game at
GitHub Pages (`.github/workflows/pages.yml` still publishes the game
alone). The page is ordinary HTML, written for a phone first: its text,
links, and layout are the DOM's. Behind them, filling the screen, the
game's own C++ grows the world's land and draws it.

- The geology (`moppe/site/geology.*`) grows the default world's land
  (seed 123, play profile, 5 km) on a 256 lattice, an eighth as fine as
  the game's, through the 40 steps of its stream-power evolution: two
  million years. Built by Emscripten as `moppe-site-geology`, it runs in a
  Web Worker (`moppe/site/geology-worker.js`) and posts the heights after
  each step. It links only the terrain code and `map/surface.cc`: 275 KB of
  WebAssembly, 100 KB compressed; in an orb's Chromium the whole history
  takes about seven seconds.
- The genesis view (`moppe/site/genesis.*`, shaders in
  `moppe/site/shaders/genesis.lisp`) draws those heights over NHAL in four
  looks it blends: the map sheet (the inks of `game/relief_map.cc`, hill
  shade, contours, the sea by its depth), a clay relief model, the lattice
  as a wire drawing, and the land under a sky with clouds and their
  shadows. The lattice is periodic, so its tiles surround the view to the
  horizon. It mixes each step's heights into the next, so the land moves
  rather than jumps. `genesis_choreography` turns the page's scroll into
  the view: straight down at the map at the top, tipping to the relief
  model, then low over the land. Built as `moppe-site-genesis`, it runs
  on the page's thread on WebGPU (`moppe/site/genesis_web.cc`); the page
  acquires the device itself and needs no optional feature.
- Without WebGPU the worker also paints each step as the map sheet's
  relief, and the page shows that instead.

The page (`moppe/site/index.html`) plays the steps at four a second as
they arrive, and the corner says which look the land is in and how old it
is.

## Build and run

With Emscripten as for the game (docs/web.md):

```sh
make site
bun run tools/serve-web.ts
```

and open <http://localhost:8080/site/>. `make site` builds both modules
in `build-web/` and lays the page out in `build-web/site/`.

Natively, `moppe-site-geology` times the steps and writes each as a
picture, and on Linux `moppe-site-genesis-capture` renders the genesis
view through Vulkan without a window (lavapipe in an orb), which judges
the looks and the choreography without a browser:

```sh
cmake --build build --target moppe-site-geology moppe-site-genesis-capture
./build/moppe-site-geology 256 123 /tmp/geology      # /tmp/geology-STEP.ppm
./build/moppe-site-genesis-capture /tmp/genesis 780x1688 0 0.5 1
                                                     # /tmp/genesis-0.50.ppm
```

The orb's headless Chromium offers no WebGPU adapter, so there the page
shows its painted fallback; the WGSL can still be checked with naga.
