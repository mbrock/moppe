{ pkgs, luv-shaderc }:
let
  terrain = import ./terrain.nix { inherit pkgs luv-shaderc; };
  native = import ../moppe/platform/sdl/linux.nix { inherit pkgs luv-shaderc terrain; };
  portable = args: import ../moppe/platform/sdl/steamdeck.nix ({ inherit pkgs luv-shaderc terrain; } // args);
in {
  inherit terrain;
  steam-deck = portable {};
  linux-portable = portable { cpu = "x86_64_v2"; archiveName = "moppe-linux"; };
  web = import ./web.nix { inherit pkgs luv-shaderc terrain; };
  demo-video = pkgs.runCommand "moppe-demo-video" {
    requiredSystemFeatures = [ "gpu" ];
    nativeBuildInputs = [ pkgs.python3 pkgs.ffmpeg pkgs.gamescope pkgs.vulkan-tools ];
  } ''
    export HOME=$TMPDIR/home XDG_CACHE_HOME=$TMPDIR/cache XDG_RUNTIME_DIR=$TMPDIR/runtime
    mkdir -p "$HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" $out build
    chmod 700 "$XDG_RUNTIME_DIR"
    export VK_DRIVER_FILES=/run/opengl-driver/share/vulkan/icd.d/radeon_icd.x86_64.json
    vulkaninfo --summary
    install -Dm755 ${../tools/record} tools/record
    patchShebangs tools/record
    ln -s ${native}/bin/moppe build/moppe
    export MOPPE_BUILD_DIR=$PWD/build MOPPE_FFMPEG=${pkgs.ffmpeg}/bin/ffmpeg
    # The game's deterministic recorder uses the same scenes and camera system
    # as tools/trailer; headless gamescope presents on the real GPU.
    ${pkgs.bashInteractive}/bin/bash tools/record "$out/demo.mp4" river \
      --seconds 20 --fps 30 --size 1280x720 -- --seed 123 --terrain-quality play
    ffprobe -v error -show_streams -of json "$out/demo.mp4" > "$out/video.json"
    python3 - "$out/video.json" <<'PYCODE'
import json, sys
streams = json.load(open(sys.argv[1]))['streams']
video = next(s for s in streams if s['codec_type'] == 'video')
assert (video['width'], video['height']) == (1280, 720)
assert float(video['duration']) >= 18
PYCODE
  '';
}
