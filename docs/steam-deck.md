# Moppe on a Steam Deck

The Steam Deck is an x86_64 Linux machine (SteamOS, Arch-based, read-only
root) with an AMD RDNA2 GPU driven by Mesa's RADV Vulkan driver, a
1280x800 screen (60 Hz LCD, 90 Hz OLED), and a gamepad. Moppe already runs
on Linux through Vulkan (docs/nhal.md), so the Deck needs no port: it
needs the game installed in a form that SteamOS's read-only system cannot
get in the way of, and a Steam shortcut for Game Mode.

That form is the flake's `packages.x86_64-linux.moppe`
(`moppe/platform/sdl/linux.nix`), installed with Nix. The package carries
everything the game loads, the Vulkan driver included: on a host without
NixOS's `/run/opengl-driver` its wrapper points the Vulkan loader at the
closure's own Mesa, so nothing from SteamOS but the kernel's `amdgpu`
driver and the display is involved. The wrapper also replaces the
`LD_LIBRARY_PATH` and drops the `LD_PRELOAD` that Steam gives a shortcut,
which would otherwise put Steam runtime libraries ahead of the game's own.

These instructions are written for whoever sets this up on the Deck, a
person or an agent. They have not yet been run on a real Deck; see "What
is verified" at the end, and correct this file with what you find.

## Requirements

- A Steam Deck (LCD or OLED) on SteamOS 3.5 or newer, in Desktop Mode for
  the setup (Steam button, Power, Switch to Desktop), with the internet.
- A password for the `deck` user (`passwd` in Konsole if none is set yet),
  since installing Nix needs `sudo`.
- About 3 GB free on the internal storage: the game's closure is 1.7 GB,
  most of it Mesa and LLVM, plus the build's intermediate store paths.
- Time: no binary cache serves moppe yet, so the first install builds the
  shader compiler and the game from source on the Deck (expect tens of
  minutes), and the first launch generates the world (about 3.3 minutes
  on eight threads of a 5 GHz desktop i7; expect 5-8 minutes on the
  Deck), which is then cached.

## Setup

1. In Desktop Mode, open Konsole and install Nix with the Determinate
   Systems installer, which supports SteamOS (it keeps `/nix` on the home
   partition, where SteamOS updates leave it alone):

   ```bash
   curl -fsSL https://install.determinate.systems/nix | sh -s -- install
   ```

   Open a new Konsole window afterwards so `nix` is on the PATH.

2. Install the game from the public repository:

   ```bash
   nix profile install github:mbrock/moppe#moppe
   ```

   This puts `moppe` at `~/.nix-profile/bin/moppe`.

3. Run it once from Konsole, in a window, and let it generate the world.
   Doing this in Desktop Mode keeps Game Mode from giving up on a game
   that shows a loading screen for minutes:

   ```bash
   moppe
   ```

   The world is cached under `~/.cache/moppe/`. When the valley appears,
   quit (Escape).

4. Add it to Steam. Either run `steamos-add-to-steam
   ~/.nix-profile/bin/moppe` (SteamOS's helper), or in the Steam client:
   Games, Add a Non-Steam Game to My Library, Browse, and pick
   `/home/deck/.nix-profile/bin/moppe` (right-click in the file dialog to
   show hidden files). Then, in the shortcut's Properties, name it
   `Moppe` and set its launch options to:

   ```
   %command% --fullscreen
   ```

5. Return to Game Mode and start Moppe from the library.

## Playing

The Deck's controls arrive through Steam Input as a standard gamepad,
which moppe's SDL3 host reads (`moppe/platform/input.cc`): the left stick
drives and steers (and walks), the right trigger boosts, `A` deploys the
glider or restarts, `B` mounts and dismounts the bike, `X` cycles the
camera, `Y` boosts, flares, or skips the opening, and the D-pad navigates
Terrain Lab. The right stick does nothing yet. On foot the game looks
around with the mouse, which Steam's default layout for a non-Steam game
puts on the right trackpad. The keyboard controls are in CLAUDE.md
(Steam+X brings up the on-screen keyboard).

## Graphics and performance

Measured on chapel, an RX 6600 XT (RDNA2 like the Deck, through the same
RADV), at 1280x800 inside gamescope with presentation uncapped: about
330 fps with the default settings and 650 with `--graphics-quality low`.
The Deck's GPU has roughly a sixth or seventh of that card's throughput,
so expect about 45-60 fps by default and a steady 60 on `low`; measure on
the Deck with `MOPPE_FPS_REPORT=1` (see Troubleshooting for the log). If it
falls short, try these launch options in turn:

```
%command% --fullscreen --graphics-quality low
%command% --fullscreen --render-scale 0.4
```

The scene already renders at half the drawable by default and is
temporally upscaled.

A seed makes the same valley on the Deck as on the Mac
(docs/determinism.md).

## Updating

```bash
nix profile upgrade moppe
```

The world cache is keyed by the world's recipe, so an update keeps the
generated world unless the terrain code changed; then the next launch
generates again (do it from Desktop Mode, as in step 3).

## Troubleshooting

- See the game's log by changing the launch options to
  `MOPPE_FPS_REPORT=1 %command% --fullscreen > /home/deck/moppe.log 2>&1`
  (Steam runs them through a shell), then read the file in Desktop Mode.
- `ERROR: ld.so: object ... gameoverlayrenderer.so ... cannot be
  preloaded` lines are harmless: the wrapper's own shell sees Steam's
  preload before it clears it for the game.
- No Vulkan device, or the wrong one: run
  `VK_LOADER_DEBUG=driver moppe` in Konsole. It should find
  `radeon_icd.x86_64.json` in a `/nix/store/...-mesa-*` directory. Setting
  `VK_DRIVER_FILES` yourself overrides the wrapper's choice (for instance
  `/usr/share/vulkan/icd.d/radeon_icd.x86_64.json`, SteamOS's own RADV, as
  an experiment; mixing it with the game's libraries may not load).
- A black screen or no window in Game Mode: gamescope runs games under
  Xwayland; try `SDL_VIDEO_DRIVER=x11 %command% --fullscreen`.
- A world that never finishes in Game Mode: generate it from Desktop Mode
  first (step 3).
- After a SteamOS update, if `nix` is gone, rerun the installer; it
  repairs the installation and the store survives on the home partition.

## Testing without a Deck

chapel (NixOS, RX 6600 XT) stands in for the Deck: gamescope, the
compositor of the Deck's Game Mode, runs headless at the Deck's size, and
bubblewrap hides NixOS's `/run/opengl-driver` so the wrapper takes the
closure's Mesa exactly as it does on SteamOS:

```bash
P=$(nix build .#moppe --print-out-paths)
G=$(nix build --inputs-from . nixpkgs#gamescope --print-out-paths)
B=$(nix build --inputs-from . nixpkgs#bubblewrap --print-out-paths)
$G/bin/gamescope -W 1280 -H 800 -w 1280 -h 800 --backend headless -- \
  $B/bin/bwrap --dev-bind / / --tmpfs /run --bind /run/user /run/user \
    --bind /run/current-system /run/current-system -- \
  /run/current-system/sw/bin/env MOPPE_DEMO=1 MOPPE_FPS_REPORT=1 \
    MOPPE_RIDE_CAPTURE_DIR=/tmp/deck MOPPE_RIDE_CAPTURE_FRAMES=3 \
    $P/bin/moppe --fullscreen
```

`gamescope -r 1000` with `MOPPE_VULKAN_PRESENT=immediate
MOPPE_FRAME_CLOCK=host` uncaps the frame rate for the measurements above;
`taskset -c 0-7` and `--no-world-cache` time a first launch's generation.

## What is verified

On chapel, with the package built from this flake: it builds; it runs
full screen at 1280x800 under gamescope 3.16 (SteamOS's) on RADV from the
closure's Mesa with `/run/opengl-driver` hidden and a Steam-like
`LD_PRELOAD` and `LD_LIBRARY_PATH` set; it generates a fresh world in
3.3 minutes on eight threads at 1.46 GB peak resident memory; and it
draws the frame rates above.

Not yet verified, because nobody has run it on a Deck: the Determinate
installer on current SteamOS; building from source on the Deck; the
Deck's real frame rate and generation time; Steam Input's mapping of the
Deck's controls and trackpad; how Game Mode's gamescope session presents
the window (Xwayland or Wayland); and the 90 Hz OLED.
