# Moppe on Raspberry Pi 5

Experimental: the ARM64 build rendered the default world on a Pi 5 and
recognized its Xbox controller, but sustained play was only about 2–7 fps
at low settings. This is not yet a practical target for playing the game.
The final manual RG32F filtering and descriptor-pool budgeting changes
are a checkpoint and still need a fresh build and validation run.

The Nix package supports 64-bit Raspberry Pi OS (`aarch64-linux`). It
includes SDL3, the Vulkan loader, Mesa's Broadcom V3DV driver, the shaders,
and the published default land. Build and run from the graphical desktop:

```sh
nix build .#moppe
./result/bin/moppe --windowed --window-size 1280x720 \
  --graphics-quality low --target-fps 30
```

If a remote ARM64 Nix builder is configured, add `--max-jobs 0` to the
build command to compile there. The finished package runs locally on the
Pi; the builder does not need a GPU.

V3DV does not expose `VK_KHR_push_descriptor`. The Vulkan backend uses
ordinary descriptor sets on such devices, allocating them from pools
owned by each in-flight frame and reusing them after that frame completes.
Set `MOPPE_VULKAN_DESCRIPTOR_SETS=1` to exercise this path on another GPU.

For fullscreen play, set the desktop to 1280x720 first and replace the
window options with `--fullscreen`. Fullscreen uses the desktop's pixel
dimensions, so a 4K desktop remains expensive even at low scene quality.

On Wayland, a launch over SSH must use the graphical session's environment
(`XDG_RUNTIME_DIR` and `WAYLAND_DISPLAY`) and the same desktop user. Both
the GPU render node and the controller's input device must be accessible
to that user. The startup log should identify **V3D**, not llvmpipe, and
print `gamepad connected` with the controller's name.

The Xbox controls are the same as on other platforms: left stick moves or
steers, right stick looks, B mounts/dismounts, X changes camera, A deploys
the glider or restarts, and the right trigger boosts. Y skips the opening.
Set `MOPPE_OPENING=none` to start directly beside the bike.

`MOPPE_FPS_REPORT=1` logs frame rates every ten seconds. The first launch
also constructs and caches the derived world from the bundled land.
