{ pkgs, luv-shaderc, terrain }:
let
  sources = import ../moppe/platform/sdl/sources.nix { inherit pkgs; };
  sdl = pkgs.fetchzip {
    url = "https://github.com/libsdl-org/SDL/archive/release-3.4.2.zip";
    hash = "sha256-ev0QiKyj0O6gtk7cK/V0X5Noft0Zo/fMS+oM6emwynE=";
  };
  dawn = pkgs.fetchzip {
    url = "https://github.com/google/dawn/releases/download/v20260423.175430/emdawnwebgpu_pkg-v20260423.175430.zip";
    hash = "sha256-EGfO1eM1yh3BQWabkVdjR/6kAUtt6IYpICA0Fx4N3hw=";
  };
in pkgs.stdenvNoCC.mkDerivation {
  pname = "moppe-web";
  version = "1";
  inherit (sources) src;
  nativeBuildInputs = [ pkgs.emscripten pkgs.cmake pkgs.ninja pkgs.python3 luv-shaderc ];
  dontFixup = true;
  configurePhase = ''
    export HOME=$TMPDIR/home EM_CACHE=$TMPDIR/em-cache EMCC_CORES=$NIX_BUILD_CORES
    mkdir -p "$HOME" "$EM_CACHE"
    cp -r ${pkgs.emscripten}/share/emscripten/cache/. "$EM_CACHE/"
    chmod -R u+w "$EM_CACHE"
    export EMCC_LOCAL_PORTS="sdl3=${sdl}"
    emcmake cmake -S . -B build-web -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
      -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
      -DFETCHCONTENT_SOURCE_DIR_MP-UNITS=${sources.mp-units} \
      -DMOPPE_WEBGPU_PORT=${dawn}/emdawnwebgpu.port.py
  '';
  buildPhase = ''
    cmake --build build-web --target moppe -j $NIX_BUILD_CORES
  '';
  installPhase = ''
    mkdir -p $out
    cp build-web/moppe.{js,wasm,data} $out/
    cp build-web/moppe.html $out/index.html
    # The dependency is a separate component at a sibling immutable URL.
    test -s ${terrain}/terrain.json
    substituteInPlace $out/index.html \
      --replace-fail 'var Module = {' 'var Module = { terrainManifest: "../terrain/terrain.json",'
  '';
}
