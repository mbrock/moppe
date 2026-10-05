{
  description = "moppe builds that need Nix: NHAL on Xbox through nixbox";

  inputs.nixbox.url = "github:mbrock/nixbox";

  outputs =
    { self, nixbox }:
    let
      forEachSystem = f: builtins.mapAttrs f nixbox.lib;
    in
    {
      packages = forEachSystem (
        _: xbox: { nhal-xbox = import ./moppe/nhal/xbox.nix xbox; }
      );
      # nix run .#deploy-nhal-xbox: sign, install, launch, and screenshot.
      apps = forEachSystem (
        system: xbox: {
          deploy-nhal-xbox = xbox.mkDeploy self.packages.${system}.nhal-xbox;
        }
      );
      devShells = forEachSystem (
        system: _: { nhal-xbox = self.packages.${system}.nhal-xbox.devShell; }
      );
    };
}
