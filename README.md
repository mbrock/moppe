# Moppe

**A small open world to walk, ride, fly, fish, and camp in, grown from its geology.**

![A meadow of wildflowers and golden autumn birches in morning light](screenshots/birch-meadow.png)

Moppe grows a landscape from a seed: two million years of tectonic uplift,
stream-power erosion, sediment transport, hillslope creep, graded river beds,
and earthwork roads. Over the resulting ground, trees and field-layer plants
compete for light, moisture, and soil; homesteads and mountain sanctuaries
graze their pastures; lightning fires leave snags and fireweed; and autumn
(*ruska*) turns the birches gold from the fells down.

Play begins at the trailhead on an autumn morning at 60°N, on foot beside a
parked 1972 Honda CT90 Trail 90.

- **Play in your browser:** [moppe.swa.sh](https://moppe.swa.sh) (WebAssembly + WebGPU; mirror at [mbrock.github.io/moppe](https://mbrock.github.io/moppe/))
- **Download native builds:** [GitHub Releases](https://github.com/mbrock/moppe/releases/latest) for macOS (Apple Silicon), Linux / Steam Deck, and Windows

---

## The land

<p align="center">
  <img src="screenshots/highland-vista.png" width="49%"
       alt="High alpine fell with early snow, boulders, bedded rock strata, and a forested valley">
  <img src="screenshots/sanctuary.png" width="49%"
       alt="A stone sanctuary enclosure and footpath on a high ridge overlooking autumn forests and lakes">
</p>
<p align="center">
  <img src="screenshots/river-confluence.png" width="49%"
       alt="Two streams meeting in a graded channel with gravel bars and sunlit ripples">
  <img src="screenshots/waterfall.png" width="49%"
       alt="A waterfall curtain tumbling over a bedrock step between white birch trunks">
</p>

Every world begins with a seed and a `WorldRecipe` over a periodic 5×5 km
torus (up to 10×10 km with `--world-size`). Because terrain generation uses
correctly rounded `moppe::cr::` math (`CORE-MATH`) with contraction disabled,
a seed grows bit-for-bit the same land on every platform and architecture.

- **Geology and rivers:** Tectonic forcing lifts the crust for the first
  500,000 years while fluvial erosion, sediment transport, and hillslope creep
  run for two million. Rivers cut parabolic beds along their drainage graphs,
  laying gravel point bars and strands on the inside of bends and steep cut
  banks on the outside. Discharge scales with catchment area; water pools
  behind bedrock sills, accelerates over riffles and waterfalls, deposits
  boulders and cobbles by Shields competence, and is crossed by stepping-stone
  fords where streams run shallow and narrow.
- **Ecology and disturbance:** Vegetation is analysed from site fields rather
  than painted zones. Soil water, soil depth, peat, cold, wind exposure, and
  bedrock — a limestone sheet over crystalline basement, scoured bare into
  granite whalebacks (*hällmark*) on ridges and seeping into bright green
  spring fens at the limestone's foot — meet each species' tolerance curve.
  Spruce, Scots pine, birch, and low multi-stemmed fell birch compete for the
  canopy; lichen, heather (*ljung*), bilberry, sphagnum, dwarf birch, sedge,
  and alvar turf share the ground beneath. Lightning fires sweep dry pine
  country, leaving standing grey snags and wine-red fireweed that recover
  through birch and pine back to spruce.
- **Roads, places, and fingerposts:** Cart roads and footpaths graded into the
  slopes link places with Swedish names rolled from the seed: the trailhead
  (*-säter*), a coastal landing (*-vik*) with a plank jetty, lake-outlet
  farms (*-kvarn* or *-fors*) with turning undershot watermills, upland peak
  sanctuaries (*-berget* or *-varden*) with stone walls and fire platforms,
  and Falu-red farmsteads across the lower country. At every junction,
  Swedish *vägvisare* fingerposts name the ways and their distances along the
  network.
- **Daylight and weather:** At 60°N on the autumn equinox, the sun rises due
  east at 06:00 and sets due west at 18:00. By night a waxing gibbous moon
  shades and shadows the world while the stars turn about the pole and the
  Milky Way crosses the sky. Weather moves through spells of clear, fair,
  overcast, valley mist, drizzle, rain, and night thunderstorms.

---

## Getting around

<p align="center">
  <img src="screenshots/fingerpost.png" width="49%"
       alt="Standing beside the 1972 Honda CT90 Trail 90 at a Swedish fingerpost and road junction">
  <img src="screenshots/hang-glider.png" width="49%"
       alt="Flying a hang glider with the Trail 90 tethered beneath the wing">
</p>
<p align="center">
  <img src="screenshots/skiff.png" width="49%"
       alt="Planing across the bay in a birch-plywood skiff with a long-tail engine off the jetty">
  <img src="screenshots/helicopter-sling.png" width="49%"
       alt="A helicopter airlifting the army jeep on a cable sling over a stream and autumn forest">
</p>

You start on foot and can step onto or into whatever machine is within reach
(`F`, or `B` on a controller):

- **On foot:** Walk, run, jump, wade streams against the current, or cross dry
  on stepping stones. The rigged hiker turns head and chest toward your gaze
  before stepping round on the spot, leans into turns at a run, and carries a
  wicker basket in hand.
- **1972 Honda CT90 Trail 90:** Parked at the trailhead. A Box3D rigid-body
  chassis on sprung wheel joints over a streamed heightfield and tree-trunk
  colliders, complete with jump jets (`Space`), rear-brake skids (`Shift`)
  that swing the tail round in a fan of dirt, and balanced wheelies (`R`).
- **Mountain bike and Long John:** An early-nineties hardtail mountain bike
  waits at the highest lookout above the trailhead (`W` pedals through the
  gears, `Space` bunny-hops), and a homemade red-and-raw-steel Long John cargo
  bike with hub-motor assist stands outside the nearest farmhouse.
- **Hang glider:** Boost the Trail 90 into the air and press `E` to deploy the
  wing with the motorcycle tethered below it. Dive to buy airspeed, pull up to
  spend it on height, hold `Space` to flare, or press `E` again to drop the
  bike and reduce wing loading; touching down on the wheels folds the wing and
  rides on.
- **Jeep, tractor, and VW Caddy:** An army jeep (seating two in multiplayer)
  waits on a road verge, a little red farm tractor stands near the trailhead,
  and a first-series VW Caddy pickup under a fibreglass hardtop is parked
  above the coastal landing. Any machine driven into a tight spot can heave
  itself free, be bumped by another vehicle, or be hauled out on foot with a
  tow rope (`F`).
- **Plywood skiff:** Moored at the landing's plank jetty (or the nearest open
  shore). A 3.93 m film-faced birch skiff with a 4.1 kW Honda GX200 long-tail
  on the transom (`Space` ties the governor back for full speed), simulated
  panel-by-panel over Gerstner sea waves and river currents with a Kelvin
  wake, rooster tail, and bow spray.
- **Helicopter:** Parked on a level pad near the trailhead. Spool up the
  rotor, lift off (`Space` climbs, `Shift` descends), hover over any bike,
  jeep, tractor, Caddy, or skiff, and press `F` to hook it on a spring-loaded
  sling line and carry it across the country.

---

## Being outdoors

<p align="center">
  <img src="screenshots/farmstead.png" width="49%"
       alt="A Falu-red farmhouse and barn beside a stream, with the Long John cargo bike parked outside">
  <img src="screenshots/barn-interior.png" width="49%"
       alt="Looking through the open doors of a Falu-red barn at cows and a horse in their stalls">
</p>
<p align="center">
  <img src="screenshots/camp-night.png" width="49%"
       alt="A burning campfire and a hammock strung between two trees under a starry night sky">
  <img src="screenshots/relief-map.png" width="49%"
       alt="The unfolded 5x5 km topographic relief map with 10 m contours, rivers, roads, and markers">
</p>

- **Chart, map, and scan:** A round compass chart in the corner turns with the
  camera. Pressing `M` unfolds a full-world topographic relief sheet (shaded
  relief, 10 m brown contours, rivers, roads, spot heights, and markers for
  every machine and place); `M` again zooms to the 1.8 km around you. On foot,
  `Q` sends a 60 m scan ring over the ground that labels nearby mushroom groups
  and pairs of trees suited for a hammock.
- **Foraging, fishing, and supper:** Pick chanterelles, funnel chanterelles,
  and porcini under spruce and birch with `E` (poisonous red fly agarics stay
  in the moss), or gather eggs from the nest boxes inside a lived-in barn.
  Facing open water on foot — or drifting in the skiff with the engine idle —
  press `E` to cast a float, wait for a nibble, and strike with `E` when it
  goes under to land perch, roach, pike, arctic char, brown trout, grayling,
  herring, or cod. Light a campfire (`T`) and press `E` beside it to set a
  pan of fish, eggs, and mushrooms on the hearth stones to fry and eat.
- **Camp and beacons:** Stand between two bare-trunked trees 2.6–5.2 m apart
  and press `R` to hang a hammock, then `E` to lie down, rock it with `A`/`D`,
  and watch the crowns and stars while the clock runs forty times as fast
  until morning (hold `S` to keep ordinary time). At a peak sanctuary's stone
  platform, `T` lights a beacon that sends up a column of smoke by day and
  glows across sightlines by night.
- **Animals and homesteads:** North Swedish horses, fjällkor, and Gotland
  sheep graze the pastures; moose browse young forest by the mires, roe deer
  keep to forest edges, and reindeer roam the high fells. Lived-in farmsteads
  have Falu-red houses and barns you can walk inside, furnished with stalls,
  hay racks, tack, tools, a lantern, tied horses and cows, Linderöd pigs in a
  pen, and Skånsk blommehöns (roosters, hens, and chicks) pecking about the
  yard and roosting on perches at dusk.
- **Two players:** Launch with `--multiplayer` on a local network (or
  `--join <host[:port]>`) to share the world peer-to-peer over UDP port 27960.
  Picked mushrooms, lit beacons, and the clock are shared, and you can take
  the passenger seat in the other player's jeep or the bow seat of their
  skiff.

---

## Controls

`W`/`A`/`S`/`D`, `E`, and `F` use physical QWERTY key positions on every
keyboard layout (so Dvorak and Colemak work without rebinding); other letter
keys follow the character they type. Xbox, PlayStation, and MFi controllers
are supported natively.

| Action | Keyboard & Mouse | Controller |
| --- | --- | --- |
| Move / drive / steer / bank | `W` `A` `S` `D` | Left stick |
| Look around / steer on foot | Mouse (`L` releases cursor) | Right stick |
| Run (on foot) / rear-brake skid (bikes) / descend (helicopter) | `Shift` | Left trigger |
| Jump / boost / brake (cars) / flare (glider) / climb (helicopter) / tie governor (boat) | `Space` | Right trigger / `A` / `Y` |
| Mount or dismount vehicle / haul stuck vehicle / hook sling load | `F` | `B` |
| Pick mushroom or egg / cast or strike fishing line / cook & eat / lie in hammock / deploy glider | `E` | `A` |
| Hang or take down hammock (on foot) / lean back for wheelie (Trail 90) | `R` | Left shoulder |
| Light or douse campfire / light sanctuary beacon | `T` | Right shoulder |
| Scan nearby mushrooms and hammock trees | `Q` | — |
| Cycle camera (chase, front, first-person) | `Tab` | `X` |
| Unfold map (whole world → near 1.8 km → close) | `M` / `Esc` | — |
| Toggle diagnostic overlay | `H` | — |
| Save screenshot (`screenshots/run-<timestamp>/`) | `P` | — |
| Toggle gameplay video recording (`clip-NNN.mp4`) | `V` | — |

---

## Build and run

### Desktop (macOS and Linux)

On **macOS**, install Xcode's command-line tools, CMake 3.24+, and Ninja. On
**Linux**, enter the Nix flake's development shell (`nix develop`), which
provides Clang 21, CMake, Ninja, SDL3, Vulkan headers/loader, `luv-shaderc`,
and `ffmpeg`.

Configure and build with `RelWithDebInfo` (without a build type CMake defaults
to `Debug`, where world generation is several times slower):

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target moppe
```

Fetch the pre-baked default land (optional; otherwise the first launch
generates and caches it in `~/Library/Caches/Moppe` or `~/.cache/moppe`):

```sh
./tools/fetch-land
```

Run the game:

```sh
# macOS
./build/moppe.app/Contents/MacOS/moppe

# Linux
./build/moppe
```

Useful launch flags and environment switches:

```sh
# Windowed mode at a specific logical size and seed
./build/moppe --windowed --window-size 1600x900 --seed 123

# Graphics preset, scene render scale, and dynamic-resolution target FPS
./build/moppe --graphics-quality high --render-scale 0.5 --target-fps 60

# Generate a 10x10 km world at 4096x4096 terrain resolution
./build/moppe --world-size 10 --terrain-resolution 4096

# Play with another machine on the local network or by address
./build/moppe --multiplayer
./build/moppe --join 192.168.1.42

# Start at a chosen hour (0..24) or weather spell (clear|fair|overcast|mist|drizzle|rain|storm)
MOPPE_CLOCK=21.5 MOPPE_WEATHER=clear ./build/moppe --windowed

# Record deterministic H.264 gameplay video of any built-in scene
./tools/record /tmp/camp.mp4 camp
./tools/record /tmp/boat.mp4 boat --seconds 15 --size 1280x720
```

### Browser (WebAssembly + WebGPU)

With Emscripten and Bun installed (`brew install emscripten bun` on macOS, or
via the Nix shell):

```sh
make web-serve
```

Then open <http://localhost:8080>. URL query parameters forward CLI flags and
environment variables (for example
`?args=--graphics-quality+low&MOPPE_WEATHER=mist`). `make site` builds the
interactive landing page (`moppe/site/`) into `build-web/site/`.

### Steam Deck, Windows, iOS, tvOS, and Xbox

- **Steam Deck / portable Linux tarball:** `nix build .#moppe-steamdeck`
  produces a self-contained archive that runs on SteamOS without Nix or root
  access ([docs/steam-deck.md](docs/steam-deck.md)).
- **Windows (cross-built on Linux):** `nix build .#moppe-windows` builds a
  standalone `moppe.exe` on Vulkan with embedded assets and default land.
- **iPhone and Apple TV:** `make phone` and `make tv` build, sign, install,
  and launch on a paired iOS or tvOS device.
- **Xbox Series X|S (Developer Mode):** `make xbox` (or
  `nix run .#deploy-moppe-xbox`) bakes the default world and deploys the
  Direct3D 12 UWP package ([docs/nhal.md](docs/nhal.md)).

### Tests and formatting

```sh
make test          # build moppe-tests and run ctest
make etalon-test   # run the Zig quantity-specification workshop tests
make check-format  # verify clang-format 21 style across all sources
```

---

## Architecture and documentation

Moppe is written in C++23 with physical quantities checked at compile time via
`mp-units` (`moppe/quantities.hh`) and shaders written in Luv's Lisp shader
language (`moppe/nhal/renderer/shaders/world.lisp`), lowered ahead of time by
`luv-shaderc` to Metal Shading Language, HLSL, SPIR-V, and WGSL.

- `moppe/terrain/`, `moppe/spatial/`, and `moppe/map/` — finite periodic
  domains, typed structure-of-arrays quantity bundles, Arrow IPC persistence,
  tectonic uplift and stream-power erosion, lake and river network analysis,
  road grading, and vegetation competition.
- `moppe/mov/` — Box3D rigid-body vehicle assemblages (Trail 90, mountain
  bike, Long John, jeep, tractor, Caddy), character capsule movement, hang
  glider flight dynamics, and panel-by-panel boat hydrodynamics.
- `moppe/game/` — `GeneratedWorld` construction and caching, `GameSession`
  simulation, `FrameView` composition, places, animals, poultry, foraging,
  fishing, camp, relief map painting, and UDP companion sync.
- `moppe/render/` and `moppe/nhal/` — the game-shaped renderer interface and
  its NHAL implementation across Metal 4, Vulkan 1.3, Direct3D 12, and WebGPU:
  vertex-pulled terrain, compute-culled instanced trunk forest, flow-mapped
  water with screen-space reflections, temporal upscaling, dynamic resolution,
  and GPU-evaluated Slug quadratic text in the HUD and the 3D world.

Further reading in [`docs/`](docs/README.md):

- [NHAL and the renderer](docs/nhal.md) — passes, shaders, terrain surfacing,
  forest LOD, water, and platform backends.
- [Land cache and river channels](docs/land.md) — fixed-point Arrow bundles,
  river bed grading, and deterministic world sharing.
- [Determinism](docs/determinism.md) — cross-platform bit-identical world
  generation and stage fingerprinting.
- [Browser build](docs/web.md) and [Landing page](docs/site.md) — Emscripten,
  WebGPU, single-threaded world assembly, and the live genesis view.
- [Steam Deck guide](docs/steam-deck.md) and [macOS releases](docs/mac-releases.md)
  — packaging, Developer ID signing, notarization, and Sparkle updates.
- [Engine atlas](docs/engine-atlas.md), [Units](docs/units.md), and
  [Ontology](docs/ontology.md) — domain ownership, physical units, and world
  representation.
- [Development guidelines](AGENTS.md) — complete build, capture, video
  recording, and inspection workflows.
