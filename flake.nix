{
  description = "AI Office Writer — Qt 6 / C++20";

  # Pinned. Floating `nixos-unstable` made local Qt (6.11) drift from CI (6.7)
  # and hid APIs like QImage::flipped() until CircleCI compiled them.
  # Bump this rev + flake.lock together; shell.nix reads the same lock.
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/2c423e03bbafcff28bfadc6781a4a8257f205cb5";

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
              cmake ninja gcc pkg-config ccache qt6.wrapQtAppsHook
            ];
            buildInputs = with pkgs; [
              qt6.qtbase qt6.qtdeclarative qt6.qtwayland zlib
              hunspell hunspellDicts.pt_BR hunspellDicts.en_US
              ibm-plex source-serif jetbrains-mono
            ];
            shellHook = ''
              echo "AI Office dev shell — cmake --preset linux && cmake --build --preset linux" >&2
              export DICPATH="${pkgs.hunspellDicts.pt_BR}/share/hunspell:${pkgs.hunspellDicts.en_US}/share/hunspell''${DICPATH:+:}$DICPATH"
            '';
          };
        });
    };
}
