# The game in the browser

Moppe runs in a browser as WebAssembly drawing through WebGPU. It is the
whole game and the one renderer: the SDL host (`moppe/platform/sdl/`) built
by Emscripten, whose window is the page's canvas, and the NHAL renderer on
its WebGPU device (`moppe/nhal/webgpu/`, docs/nhal.md), with `world.lisp`
lowered to WGSL by luv-shaderc. Nothing in the game or its shaders is
written again for the web.

It is published at <https://moppe.swa.sh> and on GitHub Pages, both built
from `master` as it moves (below).

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
`.wasm`, and `.data` (the textures, fonts, opening, and land, 35 MB). The
four files are the whole page, and any server of static files serves them.

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

WebGPU with the `float32-filterable` feature, which the terrain's height
and water textures are filtered through; the page says so when it is
missing. `timestamp-query` is used for pass timings where offered. Beyond
that, WebAssembly with exceptions and SIMD.

It has been run in Chromium on macOS (Dawn on Metal) and in Chrome 151 on
Linux (Dawn on Vulkan, started with `--enable-unsafe-webgpu
--enable-features=Vulkan`); Safari and Firefox have not been tried. On an
RX 6600 under Linux, in a 950x1045 window, the default world holds the
display's 60 frames a second with about 7 ms of GPU time a frame, and is
ready to play about ten seconds after loading begins.

## One thread

The page has no threads. Threads in a browser need shared memory, which
needs a cross-origin isolated page, which needs a server that sends two
particular headers; GitHub Pages, for one, cannot. And the game uses
threads only to make a world: for the parallel loops of the geology, and to
keep the loading screen moving meanwhile. Rendering and simulation have one
thread everywhere, and assembling the world from baked land takes one
thread as long as it takes sixteen (3.0 s either way on an M5).

So a page built without threads loses little. `platform::async` there
(`moppe/platform/sdl/services.cc`) does its work on the page's own thread,
a few frames after it is asked, so the loading screen is up first; the
screen then stands still for the nine seconds the world takes, and the
browser may call the page unresponsive if it is clicked meanwhile.
`parallel_for` and the generators find one hardware thread and start none.

## The world

A page keeps nothing between visits, so it does not save or look for a
finished-world cache, and its default world is the 1024-sample Play world,
seed 123, whose land (the geology's output, docs/land.md) is in the page's
package: land is the same on every platform (docs/determinism.md), so the
build bakes natively what the browser would have generated. Land is a
fifth the download a finished world would be (14 MB against 83 MB,
compressed), and finishing it is most of those nine seconds.

Another seed or resolution (`?args=--seed+7`) is generated in the page from
nothing, on its one thread: minutes with the page standing still, a way to
try something rather than a way to play. The memory starts at half a
gigabyte, which the default world stays within, and can grow to
WebAssembly's four.

The cache folder is in memory. Screenshots (`P`) are written into the
page's in-memory file system and go no further yet.

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

## Publishing

Two places build and publish `master` as it moves, each on its own.

**moppe.swa.sh.** On the web host a timer (`tools/web-host/moppe-web.timer`
and `.service`) runs `tools/publish-web` from a checkout every minute. When
origin's master has moved, the script checks it out, bakes the land in the
flake's shell, builds with the host's Emscripten, copies the four files
into a new folder under `/var/www/moppe/releases/`, and then replaces the
small homepage (`tools/web-index.html`) that redirects to the latest
release, so a visit finds the newest build while a loaded one never mixes
two. It keeps five releases, and tries a commit that fails to build only
once. `tools/web-host/Caddyfile` is the site's block of the host's
Caddyfile: compressed, releases cached forever, the homepage never.
`journalctl -u moppe-web` says what it did, and `tools/publish-web --force`
in the checkout builds and publishes at once.

**GitHub Pages.** `.github/workflows/pages.yml` does the same on GitHub's
runners at each push to master: Nix for luv-shaderc and the land's
compiler, a pinned emsdk, `make web`, and the four files as the site, the
page as `index.html`. The baked land is kept between runs until the
terrain's code changes.

## The build's particulars

- Every object is compiled with `-fwasm-exceptions`. WebAssembly's own
  exceptions unwind C++ frames even for the JavaScript exception
  Emscripten's "infinite loop" emulation throws, which would destroy the
  game on `main`'s stack; hence `run` returning instead.
- The page keeps function names (`--profiling-funcs`) but no DWARF, which
  Binaryen 6.0.3 cannot rewrite for this program and which would hold back
  its optimizer.
- Emscripten 6.0.3's clang crashes compiling Box3D's `shape.c` with SIMD at
  `-O2`; that file is compiled at `-O1`. CORE-MATH is given the
  floating-point exception names Emscripten's `<fenv.h>` lacks, as zero:
  WebAssembly has no such flags to raise.
- SDL3 and Dawn's `webgpu.h` bindings are Emscripten's ports (`--use-port`),
  fetched and built on first use.
