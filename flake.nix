{
  description = "AI Office Writer — Qt 6 / C++20";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAll = nixpkgs.lib.genAttrs systems;
    in {
      devShells = forAll (system:
        let pkgs = nixpkgs.legacyPackages.${system};
        in {
          default = pkgs.mkShell {
            nativeBuildInputs = with pkgs; [
              cmake ninja gcc pkg-config qt6.wrapQtAppsHook
            ];
            buildInputs = with pkgs; [
              qt6.qtbase qt6.qtdeclarative qt6.qtwayland zlib
              ibm-plex source-serif jetbrains-mono
            ];
          };
        });
    };
}
