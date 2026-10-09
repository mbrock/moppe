# The game on x86_64 Linux as a package: NHAL on Vulkan in an SDL3 window,
# installed as bin/moppe with its assets in share/moppe.
#
# Everything it loads comes from Nix, the Vulkan driver too: on a system
# without NixOS's /run/opengl-driver (SteamOS, say) the wrapper points the
# loader at this nixpkgs' Mesa (RADV for AMD, ANV for Intel), so the game
# needs nothing from the host but its kernel's GPU driver and a display.
# docs/steam-deck.md runs it on a Steam Deck.
{ pkgs, luv-shaderc }:
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
  drivers = lib.concatMapStringsSep ":" (name: "${pkgs.mesa}/share/vulkan/icd.d/${name}_icd.x86_64.json") [
    "radeon"
    "intel"
  ];
in
pkgs.llvmPackages_21.stdenv.mkDerivation {
  pname = "moppe";
  version = "0.1.0";
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
      (root + "/data")
      (root + "/fonts")
      (root + "/textures")
    ];
  };
  nativeBuildInputs = [
    pkgs.cmake
    pkgs.ninja
    pkgs.pkg-config
    pkgs.makeWrapper
    luv-shaderc
  ];
  buildInputs = [
    pkgs.sdl3
    pkgs.vulkan-headers
    pkgs.vulkan-loader
  ];
  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
    "-DBUILD_TESTING=OFF"
    "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
    "-DFETCHCONTENT_SOURCE_DIR_MP-UNITS=${mp-units}"
  ];
  # mp-units installs its headers whatever MP_UNITS_BUILD_INSTALL says.
  postInstall = ''
    rm -rf $out/include $out/lib/cmake
  '';
  # Steam launches a shortcut with its runtime's LD_LIBRARY_PATH, which
  # outranks a Nix binary's own library paths, and preloads its overlay;
  # the game takes neither. NixOS provides its own driver; elsewhere,
  # unless the caller chose one, the loader takes Mesa's from this closure.
  postFixup = ''
    wrapProgram $out/bin/moppe \
      --set LD_LIBRARY_PATH ${lib.makeLibraryPath [ pkgs.vulkan-loader ]} \
      --unset LD_PRELOAD \
      --run 'if [ ! -e /run/opengl-driver ] && [ -z "''${VK_DRIVER_FILES:-}''${VK_ICD_FILENAMES:-}" ]; then export VK_DRIVER_FILES=${drivers}; fi'
  '';
  meta = {
    description = "Ride a motocross bike through a generated mountain valley";
    mainProgram = "moppe";
    platforms = [ "x86_64-linux" ];
  };
}
