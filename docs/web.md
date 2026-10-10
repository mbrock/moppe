# The game in the browser

Moppe runs in a browser as WebAssembly drawing through WebGPU. It is the
whole game and the one renderer: the SDL host (`moppe/platform/sdl/`) built
by Emscripten, whose window is the page's canvas, and the NHAL renderer on
its WebGPU device (`moppe/nhal/webgpu/`, docs/nhal.md), with `world.lisp`
lowered to WGSL by luv-shaderc. Nothing in the game or its shaders is
written again for the web.

## Build and run

Emscripten and Bun, once (`brew install emscripten bun` on the Mac), and
luv-shaderc on `PATH` or Nix to fetch the pinned one. Then:

```sh
make web-serve
```

and open <http://localhost:8080>. `make web` only builds: it bakes the
page's land with the native baker (`tools/bake-land 1024 play 123`, into
`build-web/lands/`), configures `build-web/` with `emcmake cmake`, and
builds the `moppe` target into `build-web/moppe.html` with its `.js`,
`.wasm`, and `.data` (the textures, fonts, opening, and land, 35 MB).

The server (`tools/serve-web.ts`) sends the `Cross-Origin-Opener-Policy`
and `Cross-Origin-Embedder-Policy` headers that make the page cross-origin
isolated, which WebAssembly threads need; the files do not work opened from
disk or served without them.

The page's address carries the command line and the development switches:

```text
http://localhost:8080/?args=--graphics-quality+low&MOPPE_WEATHER=mist
```

`args` is split at spaces into the program's arguments, and every
`MOPPE_...` parameter becomes an environment variable, so
`MOPPE_FPS_REPORT=1`, `MOPPE_NHAL_TIMINGS=1`, `MOPPE_HUD=debug`,
`MOPPE_DEMO=1`, and the rest work as on the desktop, reporting to the
browser's console.

## What the browser needs

- WebGPU with the `float32-filterable` feature, which the terrain's height
  and water textures are filtered through. The page says so when it is
  missing. `timestamp-query` is used for pass timings where offered.
- WebAssembly threads (shared memory), exceptions, and SIMD.

It has been run in Chromium on macOS (Dawn on Metal) and in Chrome 151 on
Linux (Dawn on Vulkan, started with `--enable-unsafe-webgpu
--enable-features=Vulkan`); Safari and Firefox have not been tried. On an
RX 6600 under Linux, in a 950x1045 window, the default world holds the
display's 60 frames a second with about 7 ms of GPU time a frame, and is
ready to play eight or nine seconds after loading begins.

## The page

`moppe/platform/web/shell.html` is the page around the canvas, and
`pre.js` runs before the program:

- It acquires the WebGPU adapter and device, asking for the features above
  and the adapter's own limits in place of WebGPU's defaults (the
  renderer's buffers and programs are sized for desktop GPUs), and hands
  the device to the program as `Module.preinitializedWebGPUDevice`.
  Acquiring a device is asynchronous, and the host wants one when it makes
  its window, so the program starts only once the device is there.
- It turns the address into arguments and environment, and reports a lost
  device or a failure to start over the canvas (`Module.moppeFail`).
  WebGPU's validation errors go to the console, the first sixteen.

The host is the desktop's. A browser's loop is the page's, so
`platform::run` arranges a turn of the host's loop at each animation frame
and returns, and the game lives on after `main` (`moppe/game/main.cc`).
SDL's window fills the document and follows the browser window's size, at
the display's pixel density; SDL maps the keyboard by key position, as on
the desktop, and captures the pointer once the page has been clicked or a
key pressed. Its gamepad support is compiled in but has not been tried.
Frames are stepped by the document timeline's time, which the browser
advances once per displayed frame.

World generation runs on worker threads, as everywhere, from a pool the
page starts with (one per core and two over), so none has to be started
while the page's thread is busy.

## The world

A page keeps nothing between visits, so it does not save or look for a
finished-world cache, and its default world is the 1024-sample Play world,
seed 123, whose land (the geology's output, docs/land.md) is in the page's
package: land is the same on every platform (docs/determinism.md), so the
Mac bakes what the browser would have generated. From it the page plants
its forests and finishes the world in under ten seconds. Another seed or
resolution (`?args=--seed+7`) is generated in the page from nothing, which
for a 1024-sample world takes about a minute, within the half gigabyte of
memory the page starts with; the memory can grow to WebAssembly's four.

The cache folder is in memory. Screenshots (`P`) are written into the
page's in-memory file system and go no further yet.

## Publishing

`make web-deploy` (`tools/deploy-web`) builds, uploads the four files to a
new folder under `/releases/` on the web host, and then replaces the small
homepage (`tools/web-index.html`) that redirects to the latest release, so
a visit to the stable address finds the newest build while a loaded one
never mixes files from two. `tools/moppe.Caddyfile` is the site: it
compresses, sends the isolation headers, caches releases forever, and never
caches the homepage. `MOPPE_WEB_HOST` and `MOPPE_WEB_ROOT` override the
host (`igloo`) and its folder.

## The build's particulars

- Every object is compiled with `-pthread` and `-fwasm-exceptions`.
  WebAssembly's own exceptions unwind C++ frames even for the JavaScript
  exception Emscripten's "infinite loop" emulation throws, which would
  destroy the game on `main`'s stack; hence `run` returning instead.
- The page keeps function names (`--profiling-funcs`) but no DWARF, which
  Binaryen 6.0.3 cannot rewrite for this program and which would hold back
  its optimizer.
- Emscripten 6.0.3's clang crashes compiling Box3D's `shape.c` with SIMD at
  `-O2`; that file is compiled at `-O1`. CORE-MATH is given the
  floating-point exception names Emscripten's `<fenv.h>` lacks, as zero:
  WebAssembly has no such flags to raise.
- SDL3 and Dawn's `webgpu.h` bindings are Emscripten's ports (`--use-port`),
  fetched and built on first use.
