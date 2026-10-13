{ pkgs, luv-shaderc }:
let
  sources = import ../moppe/platform/sdl/sources.nix { inherit pkgs; };
  baker = pkgs.llvmPackages_21.stdenv.mkDerivation {
    pname = "moppe-land-baker";
    version = "1";
    inherit (sources) src;
    nativeBuildInputs = [ pkgs.cmake pkgs.ninja pkgs.pkg-config luv-shaderc ];
    buildInputs = [ pkgs.sdl3 pkgs.vulkan-headers pkgs.vulkan-loader ];
    cmakeFlags = [ "-DCMAKE_BUILD_TYPE=Release" "-DBUILD_TESTING=OFF"
      "-DMOPPE_BUILD_DEVELOPER_TOOLS=ON" "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
      "-DFETCHCONTENT_SOURCE_DIR_MP-UNITS=${sources.mp-units}"
      "-DCMAKE_C_FLAGS=-march=x86-64-v3" "-DCMAKE_CXX_FLAGS=-march=x86-64-v3" ];
    buildPhase = "cmake --build . --target land-bake -j $NIX_BUILD_CORES";
    installPhase = "install -Dm755 land-bake $out/bin/land-bake";
  };
in pkgs.runCommand "moppe-terrain" { nativeBuildInputs = [ baker pkgs.python3 ]; } ''
  mkdir -p $out
  land-bake $out 1024 play 123
  land-bake $out 2048 play 123
  python3 - "$out" <<'PY'
import json, pathlib, sys
root = pathlib.Path(sys.argv[1])
files = [p.name for p in root.glob('land-*-1024-*.arrows')]
assert len(files) == 1
(root / 'terrain.json').write_text(json.dumps({'files': files}))
PY
''
