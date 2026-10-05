# The NHAL demo for Xbox, built by nixbox; `xbox` is nixbox.lib.<system>.
{ xbox, luv-shaderc }:
xbox.mkXboxApp {
  pname = "nhal-demo";
  version = "0.1.0";
  displayName = "moppe NHAL demo";
  backgroundColor = "#0E1116";
  src = xbox.pkgs.lib.fileset.toSource {
    root = ../..;
    fileset = xbox.pkgs.lib.fileset.unions [
      ./meson.build
      ./nhal.hh
      ./reflection.hh
      ./table.hh
      ./d3d12
      ./demo
    ];
  };
  sourceRoot = "source/moppe/nhal";
  nativeBuildInputs = with xbox.pkgs; [
    meson
    ninja
    directx-shader-compiler
    luv-shaderc
  ];
}
