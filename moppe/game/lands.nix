# The published land (lands.json) for the source's LAND_VERSION
# (moppe/game/land.hh), unpacked into one folder of land files that a package
# ships in its worlds/ folder (CMake's MOPPE_LANDS_DIR). Each is fetched by
# its recorded hash, and each is exactly what the game would generate for its
# recipe on any platform (docs/determinism.md), so a package with none still
# works: it generates on first launch.
{ pkgs }:
let
  inherit (pkgs) lib;
  root = ../..;
  version = lib.toInt (
    builtins.head (
      builtins.match ".*LAND_VERSION = ([0-9]+);.*" (
        builtins.readFile (root + "/moppe/game/land.hh")
      )
    )
  );
  manifest = builtins.fromJSON (builtins.readFile (root + "/lands.json"));
  current = builtins.filter (land: land.version == version) manifest.lands;
in
pkgs.runCommand "moppe-lands-v${toString version}" { nativeBuildInputs = [ pkgs.zstd ]; } (
  ''
    mkdir -p $out
  ''
  + lib.concatMapStrings (land: ''
    zstd -dc ${pkgs.fetchurl { inherit (land) url sha256; }} | tar -xf - -C "$TMPDIR"
    mv "$TMPDIR/worlds/${land.file}" $out/
  '') current
)
