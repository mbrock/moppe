# moppe on Xbox: the game, drawn by the NHAL renderer through Direct3D 12,
# built by nixbox from the repository's CMakeLists.txt with MOPPE_XBOX_GAME.
# `xbox` is nixbox.lib.<system>. `bakedWorld`, a finished-world cache baked
# on the host (tools/bake-world), ships in the package so the console starts
# in it instead of generating a world; without one the console generates.
{
  xbox,
  luv-shaderc,
  bakedWorld ? null,
}:
let
  inherit (xbox.pkgs) lib fetchurl runCommand;
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
xbox.mkXboxApp {
  pname = "moppe-xbox";
  version = "0.1.0";
  identity = "Moppe.game";
  displayName = "moppe";
  description = "Ride a motocross bike through a generated mountain valley";
  backgroundColor = "#0E1116";
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
      (root + "/fonts")
      (root + "/textures")
    ];
  };
  nativeBuildInputs = with xbox.pkgs; [
    cmake
    ninja
    directx-shader-compiler
    luv-shaderc
  ];
  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
    "-DBUILD_TESTING=OFF"
    "-DMOPPE_XBOX_GAME=ON"
    "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
    "-DFETCHCONTENT_SOURCE_DIR_MP-UNITS=${mp-units}"
  ]
  ++ lib.optional (bakedWorld != null) "-DMOPPE_XBOX_BAKED_WORLD=${bakedWorld}";
}
