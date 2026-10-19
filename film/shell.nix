# The film's development shell (`nix develop .#film`): Rust, and what
# fframes builds and links against -- libclang for its ffmpeg bindings,
# the assemblers and codecs of the ffmpeg it builds, and Vulkan for its
# Skia renderer and preview window.
{ pkgs }:

let
  # The ffmpeg fframes downloads prebuilt was linked against x264's ABI
  # 163, the stable release of June 2021; nixpkgs has moved on.
  x264-163 = pkgs.x264.overrideAttrs (old: {
    version = "0-unstable-2021-06-13";
    src = pkgs.fetchFromGitLab {
      domain = "code.videolan.org";
      owner = "videolan";
      repo = "x264";
      rev = "5db6aa6cab1b146e07b60cc1736a01f21da01154";
      hash = "sha256-0M7dc1s9tVdmqB0d3u+Wmrksgb8I5Lz7jlJta/7Mnms=";
    };
    patches = [ ];
  });
in
pkgs.mkShell {
  name = "moppe-film";
  nativeBuildInputs = with pkgs; [
    cargo
    rustc
    clang
    pkg-config
    nasm
    yasm
    ninja
    cmake
    perl
    python3
    ffmpeg
  ];
  buildInputs = with pkgs; [
    x264-163
    x265
    libopus
    libvpx
    alsa-lib
    fontconfig
    freetype
    vulkan-loader
    vulkan-headers
    libGL
    wayland
    libxkbcommon
    libx11
    libxcursor
    libxrandr
    libxi
  ];
  LIBCLANG_PATH = "${pkgs.llvmPackages.libclang.lib}/lib";
  shellHook = ''
    export LD_LIBRARY_PATH=${
      pkgs.lib.makeLibraryPath (
        with pkgs;
        [
          x264-163
          vulkan-loader
          libGL
          wayland
          libxkbcommon
          fontconfig
          freetype
          alsa-lib
          libx11
          libxcursor
          libxrandr
          libxi
        ]
      )
    }''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
    export PATH=$HOME/.cargo/bin:$PATH
  '';
}
