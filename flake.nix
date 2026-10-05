{
  description = "moppe builds that need Nix: NHAL on Xbox, and Luv's shader compiler";

  inputs.nixbox.url = "github:mbrock/nixbox";
  inputs.luv.url = "github:mbrock/luv";

  outputs =
    { self, nixbox, luv }:
    let
      forEachSystem = f: builtins.mapAttrs f nixbox.lib;
    in
    {
      packages = forEachSystem (
        system: xbox: {
          # Lowers moppe/nhal's Lisp shaders to MSL, HLSL, and reflection.
          luv-shaderc = luv.packages.${system}.luv-shaderc;
          nhal-xbox = import ./moppe/nhal/xbox.nix {
            inherit xbox;
            luv-shaderc = self.packages.${system}.luv-shaderc;
          };
        }
      );
      apps = forEachSystem (
        system: xbox: {
          luv-shaderc = {
            type = "app";
            program = "${self.packages.${system}.luv-shaderc}/bin/luv-shaderc";
          };
          # nix run .#deploy-nhal-xbox: sign, install, launch, and screenshot.
          deploy-nhal-xbox = xbox.mkDeploy self.packages.${system}.nhal-xbox;
        }
      );
      devShells = forEachSystem (
        system: _: { nhal-xbox = self.packages.${system}.nhal-xbox.devShell; }
      );
    };
}
