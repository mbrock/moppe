{
  description = "moppe builds that need Nix: NHAL on Xbox and Linux, and Luv's shader compiler";

  inputs.nixbox.url = "github:mbrock/nixbox";
  inputs.luv.url = "github:mbrock/luv";
  inputs.nixpkgs.follows = "luv/nixpkgs";

  outputs =
    { self, nixbox, luv, nixpkgs }:
    let
      forEachSystem = f: builtins.mapAttrs f nixbox.lib;
      # A finished world baked on the host, shipped in the Xbox package so
      # the console need not generate one: tools/deploy-xbox bakes it and
      # names it here, which needs --impure. Pure evaluation ships none.
      xboxBakedWorld =
        let
          directory = builtins.getEnv "MOPPE_XBOX_BAKED_WORLD";
        in
        if directory == "" then
          null
        else
          builtins.path {
            path = directory;
            name = "moppe-baked-world";
          };
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
          # The game's world generation and simulation, headless on Xbox.
          moppe-core-xbox = import ./moppe/platform/uwp/xbox.nix { inherit xbox; };
          # The game, drawn by the NHAL renderer through Direct3D 12.
          moppe-xbox = import ./moppe/platform/uwp/game.nix {
            inherit xbox;
            bakedWorld = xboxBakedWorld;
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
          # Generates a world on the console and writes LocalState/report.txt.
          deploy-moppe-core-xbox = xbox.mkDeploy self.packages.${system}.moppe-core-xbox;
          # Plays the game on the console.
          deploy-moppe-xbox = xbox.mkDeploy self.packages.${system}.moppe-xbox;
        }
      );
      devShells = forEachSystem (
        system: _:
        {
          nhal-xbox = self.packages.${system}.nhal-xbox.devShell;
          moppe-core-xbox = self.packages.${system}.moppe-core-xbox.devShell;
          moppe-xbox = self.packages.${system}.moppe-xbox.devShell;
        }
        // nixpkgs.lib.optionalAttrs (nixpkgs.lib.hasSuffix "-linux" system) {
          # The game on Linux: NHAL on Vulkan, in an SDL3 window
          # (`nix develop`, then the usual cmake configure and build).
          default = import ./moppe/platform/linux/shell.nix {
            pkgs = nixpkgs.legacyPackages.${system};
            luv-shaderc = self.packages.${system}.luv-shaderc;
          };
        }
      );
    };
}
