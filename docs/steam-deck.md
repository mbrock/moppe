# Moppe on a Steam Deck

The recommended Deck build is `packages.x86_64-linux.moppe-steamdeck`.
Build it with Nix on an x86_64 Linux machine, copy one tarball to the Deck,
and unpack it anywhere in your home directory. **The Deck needs no Nix,
root access, or system changes.** It uses SteamOS's Vulkan loader and RADV.
The original Nix package remains available as an alternative below.

## Build and install the portable version

On the build machine, from this repository:

```sh
nix build .#moppe-steamdeck
ls -lh result/moppe-steamdeck.tar.gz
cat result/verification.txt
scp result/moppe-steamdeck.tar.gz deck@steamdeck:~/Downloads/
```

SSH is just one way to transfer the archive; a USB drive works too. In
Desktop Mode on the Deck, open Konsole and unpack it:

```sh
mkdir -p ~/Games
tar -xzf ~/Downloads/moppe-steamdeck.tar.gz -C ~/Games
~/Games/moppe/moppe
```

Run once in Desktop Mode and let the world finish loading before adding
it to Steam. The executable carries fonts, textures, opening data,
SPIR-V shaders, and the published default land from `lands.json` inside
it, unpacked into `~/.cache/moppe/assets/` on first use. Derived
worlds are cached under `~/.cache/moppe/`; a world without published land
is generated on first launch. Keep the entire unpacked directory together.
No shader compiler or other build tools are needed on the Deck.

In Steam, choose **Games → Add a Non-Steam Game → Browse**, select
`/home/deck/Games/moppe/moppe`, and name it Moppe. Set launch options to:

```
%command% --fullscreen
```

Run it as a native Linux game (do not force Proton), then return to Game
Mode. The launcher works from any working directory and with spaces in
its installation path. It preserves Steam's overlay, library search path,
and Vulkan driver selection. If choosing a Steam Linux Runtime manually,
use **Steam Linux Runtime 3.0 (sniper)**.

To update, quit the game and replace the whole `~/Games/moppe` directory
with a freshly unpacked archive. The cache in your home directory survives;
changes to the land version cause the corresponding world to be rebuilt.

## How the portable build works

`moppe/platform/sdl/steamdeck.nix` uses Zig 0.16's Clang 21 C/C++ drivers
with target `x86_64-linux-gnu.2.31` and the x86-64-v3 CPU baseline (supported
by both Deck models). Clang 21 supports this game's C++23 and `#embed` shader
sources. Luv runs natively during the Nix build and its generated SPIR-V
is embedded in the executable.

The glibc baseline is **2.31**, verified against Valve's [sniper package
sources](https://repo.steampowered.com/steamrt-images-sniper/snapshots/latest-container-runtime-depot/sources/)
(`glibc_2.31-13+deb11u14` when checked). Debian 11 uses the same ABI baseline.
The ELF interpreter is `/lib64/ld-linux-x86-64.so.2`, not a Nix store path.

SDL3, libc++, the C++ ABI/unwind support, and volk's Vulkan dispatch are
statically linked. The glibc family stays dynamic so host graphics drivers
share the host libc. The optional `MOPPE_VULKAN_DYNAMIC` CMake setting uses
volk to open `libvulkan.so.1` and resolve the renderer's Vulkan calls; SDL
also opens the host loader for window surfaces. The existing Nix package
retains its direct Vulkan linkage.

SDL is built from the unpatched upstream source: it loads X11/Wayland,
PulseAudio/ALSA, and udev by their library names, without Nix paths. Those
libraries are supplied by SteamOS or the Steam runtime. This is a portable
game bundle, not a replacement for the host desktop/graphics stack. Mesa,
LLVM, Vulkan loaders, glibc, and display/audio libraries are not bundled.
Optional video recording still requires a host `ffmpeg` executable.

Each tarball build runs `moppe/platform/sdl/verify-steamdeck.py` and fails
if the executable has an unexpected `NEEDED` entry, a Nix store reference,
an RPATH/RUNPATH, an incorrect interpreter, or GLIBC requirements above
2.31. The report is saved beside the archive as `verification.txt`.

## Playing and graphics

Steam Input presents the Deck's controls as a standard gamepad. Left
stick moves/steers, right stick looks around, right trigger boosts, `A`
deploys the glider or restarts, `B` mounts/dismounts, `X` cycles the camera,
and `Y` boosts, flares, or skips the opening. The D-pad navigates Terrain
Lab. Steam's trackpad mouse mapping also works. Keyboard controls are in
`AGENTS.md`; Steam+X opens the on-screen keyboard.

The Deck's display is 1280×800. If performance is insufficient, try:

```
%command% --fullscreen --graphics-quality low
%command% --fullscreen --render-scale 0.4
```

Deck frame rates have not been measured. Prior tests of the Nix package
on chapel's RX 6600 XT under headless gamescope reached about 330 fps at
1280×800 with defaults, and 650 with `low`; they are not Deck benchmarks.

## Troubleshooting

- Run the launcher from Konsole to see errors. For a Steam log, use
  `MOPPE_FPS_REPORT=1 %command% --fullscreen > /home/deck/moppe.log 2>&1`.
- No Vulkan device: run `VK_LOADER_DEBUG=driver ~/Games/moppe/moppe`.
  The portable build should find **the host's** RADV ICD, normally under
  `/usr/share/vulkan/icd.d/`. Remove stale `VK_DRIVER_FILES` or
  `VK_ICD_FILENAMES` overrides. Do not point it at a Nix Mesa driver.
- A black screen or no Game Mode window: try
  `SDL_VIDEO_DRIVER=x11 %command% --fullscreen` (gamescope has Xwayland).
- A slow first load: finish it once in Desktop Mode. Fresh generation can
  take minutes; subsequent launches use the cache.
- A missing `libvulkan.so.1` error means the host/runtime graphics stack
  is absent or hidden; installing Nix on the Deck is not the remedy.

## Alternative: install the original Nix package

This route bundles its own Mesa driver and a larger Nix closure. It can
be useful on hosts whose system Vulkan stack is unsuitable.

1. In Desktop Mode, set a `deck` password with `passwd` if needed. Install
   Nix using the [Determinate installer](https://install.determinate.systems/):

   ```sh
   curl -fsSL https://install.determinate.systems/nix | sh -s -- install
   ```

   Open a new Konsole window afterward. This needs `sudo`, several GB of
   disk space, and time to compile the game and shader compiler if uncached.

2. Install and run:

   ```sh
   nix profile install github:mbrock/moppe#moppe
   moppe
   ```

3. Add `/home/deck/.nix-profile/bin/moppe` to Steam, with
   `%command% --fullscreen` as above. Update with `nix profile upgrade moppe`.

`moppe/platform/sdl/linux.nix` packages this version. On systems without
NixOS's `/run/opengl-driver`, its wrapper chooses the closure's Mesa
(unless `VK_DRIVER_FILES`/`VK_ICD_FILENAMES` is already set), replaces
`LD_LIBRARY_PATH`, and clears `LD_PRELOAD`. This behavior belongs only to
the Nix alternative; the portable launcher preserves the host environment.

## Verification and remaining Deck checks

On igloo, the portable binary passes both `readelf` and `objdump` checks:
its only `NEEDED` libraries are `libm.so.6`, `libc.so.6`,
`ld-linux-x86-64.so.2`, `libpthread.so.0`, and `libdl.so.2`. Its newest
required GLIBC symbol is **2.29**, within the 2.31 target. There are no
RPATH/RUNPATH entries or embedded Nix store paths in the stripped binary.
The original `.#moppe` Nix package also builds successfully.

The unpacked portable game runs `--help` in a bare Debian 11 container
(glibc 2.31), with no Nix installed or mounted. On igloo's Ubuntu host,
with a clean environment and `/nix` hidden by bubblewrap, it opens an X11
window under Xvfb, loads the system Vulkan loader and Mesa llvmpipe,
reads the bundled default land, builds and caches the world, renders a
640×400 gameplay frame, saves a PNG, and exits successfully. The test
uses a fresh home directory and a bundle path containing a space.

To repeat the checks on a Linux host with Docker, Xvfb, bubblewrap, and
system Mesa software Vulkan installed:

```sh
nix build .#moppe-steamdeck
probe=$(mktemp -d)
mkdir -p "$probe/unpacked game" "$probe/home" "$probe/frames"
tar -xzf result/moppe-steamdeck.tar.gz -C "$probe/unpacked game"
python3 moppe/platform/sdl/verify-steamdeck.py \
  "$probe/unpacked game/moppe/bin/moppe"
docker run --rm --network none \
  -v "$probe/unpacked game:/game:ro" debian:bullseye-slim \
  /game/moppe/moppe --help
env -i PATH=/usr/bin:/bin HOME="$probe/home" \
  SDL_VIDEO_DRIVER=x11 SDL_AUDIO_DRIVER=dummy \
  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
  MOPPE_DEMO=1 MOPPE_OPENING=none MOPPE_RIDE_CAPTURE_START=0 \
  MOPPE_RIDE_CAPTURE_FRAMES=1 MOPPE_RIDE_CAPTURE_DIR="$probe/frames" \
  xvfb-run -a bwrap --ro-bind / / --dev-bind /dev /dev --tmpfs /nix \
  --bind "$probe" "$probe" --chdir /tmp -- \
  "$probe/unpacked game/moppe/moppe" \
  --seed 123 --window-size 640x400 --graphics-quality low
```

Use the software ICD filename installed by your distribution if it differs.
Igloo has no GPU device exposed, so this does **not** verify real Deck/RADV
rendering. A real Deck is still required to confirm SteamOS installation,
Game Mode and Wayland presentation, Steam overlay/Input, real audio,
suspend/resume, performance, and the OLED model's 90 Hz display. The Debian
check verifies the glibc baseline, not a full sniper/pressure-vessel session.
