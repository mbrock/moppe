# Blender bundled with pyarrow and moppe_export, the small nanobind C++ module
# that constructs Moppe's typed spatial::Bundles and hands them straight to
# pyarrow over the Arrow C Data Interface for ZSTD-compressed .arrows output.
{ pkgs }:
let
  inherit (pkgs) lib;
  sources = import ../../moppe/platform/sdl/sources.nix { inherit pkgs; };
  root = ../..;
  python = pkgs.blender.python;
  stdenv =
    if pkgs.stdenv.hostPlatform.isLinux then pkgs.llvmPackages_21.stdenv else pkgs.stdenv;

  moppe-export = python.pkgs.toPythonModule (
    stdenv.mkDerivation {
      pname = "moppe-export";
      version = "0.1.0";
      src = lib.fileset.toSource {
        inherit root;
        fileset = lib.fileset.unions [
          (root + "/tools/figure/export-ext")
          (root + "/moppe/correct_math.hh")
          (root + "/moppe/quantities.hh")
          (root + "/moppe/gfx/vector.hh")
          (root + "/moppe/spatial/bundle.hh")
          (root + "/moppe/spatial/bundle_storage.hh")
          (root + "/moppe/game/model_bundle.hh")
          (root + "/third_party/nanoarrow")
        ];
      };
      sourceRoot = "source/tools/figure/export-ext";
      nativeBuildInputs = [
        pkgs.cmake
        pkgs.ninja
        python
        python.pkgs.nanobind
      ];
      buildInputs = [ python ];
      cmakeFlags = [
        "-DCMAKE_BUILD_TYPE=Release"
        "-DMP_UNITS_SOURCE_DIR=${sources.mp-units}"
        "-DPython_EXECUTABLE=${python}/bin/python3"
      ];
      installPhase = ''
        runHook preInstall
        mkdir -p "$out/${python.sitePackages}"
        cmake --install . --prefix "$out/${python.sitePackages}"
        runHook postInstall
      '';
    }
  );
  pythonEnv = python.withPackages (ps: [
    ps.pyarrow
    moppe-export
  ]);
in
# Upstream blender.withPackages copies $out/share on Linux, which does not
# exist on macOS's .app layout; wrapping the executable with PYTHONPATH and
# --python-use-system-env works uniformly on both macOS and Linux.
pkgs.runCommand "moppe-blender-${pkgs.blender.version}"
  {
    nativeBuildInputs = [ pkgs.makeWrapper ];
    meta.mainProgram = "blender";
  }
  ''
    mkdir -p "$out/bin"
    makeWrapper "${lib.getExe pkgs.blender}" "$out/bin/blender" \
      --prefix PYTHONPATH : "${pythonEnv}/${python.sitePackages}" \
      --add-flags "--python-use-system-env"
  ''
