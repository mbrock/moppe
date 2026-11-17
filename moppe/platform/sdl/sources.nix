{ pkgs }:
let
  inherit (pkgs) lib fetchurl runCommand;
  # The same mp-units revision CMakeLists.txt fetches; the Nix sandbox has no
  # network, so FetchContent is pointed at this unpacked copy instead.
  mp-units = runCommand "mp-units-src" { } ''
    mkdir -p $out
    tar -xzf ${
      fetchurl {
        url = "https://github.com/mpusz/mp-units/archive/f9016a280dcd62a040fa96df762241d6c8d9f633.tar.gz";
        sha256 = "84127fb457846c4e8391fb485084326b8b91671bab9b2ce377c52c3bb2dafc71";
      }
    } --strip-components=1 -C $out
  '';
  root = ../../..;
in
{
  inherit mp-units;
  src = lib.fileset.toSource {
    inherit root;
    fileset = lib.fileset.unions [
      (root + "/CMakeLists.txt")
      (root + "/moppe")
      (root + "/atelier/space.hh")
      (root + "/atelier/tree.hh")
      (root + "/atelier/tree.cc")
      (root + "/third_party/box3d")
      (root + "/third_party/nanoarrow")
      (root + "/third_party/core-math")
      (root + "/third_party/zstd")
      (root + "/tools/pack-assets")
      (root + "/data")
      (root + "/fonts")
      (root + "/textures")
    ];
  };
}
