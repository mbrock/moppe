# The website

The game's landing page, which is meant to replace the bare game at
GitHub Pages (`.github/workflows/pages.yml` still publishes the game
alone). The page is ordinary HTML: its text, links, and layout are the
DOM's. What it shows of the game comes from the game's own C++, compiled
by Emscripten into small modules that run in Web Workers and hand the page
pictures, so the page stays responsive and needs no WebGPU.

So far it has one exhibit, the geology (`moppe/site/geology.cc`): the
default world's land (seed 123, play profile, 5 km) grown on a 256 lattice,
an eighth as fine as the game's, through the 40 steps of its stream-power
evolution, two million years. After the first picture and after every step
the module paints the heights as a shaded relief in the map sheet's inks
(`game/relief_map.cc`) and posts the picture; the page
(`moppe/site/index.html`) plays them back at a steady pace and lets the
reader scrub through them. The module links only the terrain code and
`map/surface.cc`: 275 KB of WebAssembly, 100 KB compressed. In an orb's
Chromium the whole history takes about seven seconds; natively, on one
thread, a step takes some 75 ms.

## Build and run

With Emscripten as for the game (docs/web.md):

```sh
make site
bun run tools/serve-web.ts
```

and open <http://localhost:8080/site/>. `make site` builds the
`moppe-site-geology` target in `build-web/` and lays the page out in
`build-web/site/`. Natively the same target times the steps and writes
each picture, which judges a lattice or a palette without a browser:

```sh
cmake --build build --target moppe-site-geology
./build/moppe-site-geology 256 123 /tmp/geology   # /tmp/geology-STEP.ppm
```
