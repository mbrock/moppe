{ pkgs }:
let
  inherit (pkgs) lib fetchurl runCommand;
  # The same mp-units revision CMakeLists.txt fetches; the Nix sandbox has no
  # network, so FetchContent is pointed at this unpacked copy instead.
  mp-units = runCommand "mp-units-src" { } ''
    mkdir -p $out
    tar -xzf ${
      fetchurl {
        url = "https://github.com/mpusz/mp-units/archive/bb86582ec2561419bc461a5ce7df01328ba08ea1.tar.gz";
        sha256 = "eb78dbc19c65d5954d31b78149936ca46f2eafb5e5f9c2b1e58f583fcf6705ea";
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
      (root + "/data")
      (root + "/fonts")
      (root + "/textures")
    ];
  };
}
