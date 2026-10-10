# Dev shell used by the README (`nix-shell`) and by CircleCI.
# Nixpkgs is the revision locked in flake.lock — not `<nixpkgs>` — so local
# and CI resolve the same Qt instead of drifting (CI used to compile 6.7
# against APIs that only existed on the contributor's 6.11 channel).
let
  lock = builtins.fromJSON (builtins.readFile ./flake.lock);
  nixpkgs = lock.nodes.nixpkgs.locked;
  pinned = fetchTarball {
    url = "https://github.com/${nixpkgs.owner}/${nixpkgs.repo}/archive/${nixpkgs.rev}.tar.gz";
    sha256 = nixpkgs.narHash;
  };
in
{ pkgs ? import pinned {} }:
pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    cmake
    ninja
    gcc
    pkg-config
    ccache
    qt6.wrapQtAppsHook
  ];
  buildInputs = with pkgs; [
    qt6.qtbase
    qt6.qtdeclarative
    qt6.qtwayland
    zlib
    hunspell
    hunspellDicts.pt_BR
    hunspellDicts.en_US
    ibm-plex
    source-serif
    jetbrains-mono
    noto-fonts
    noto-fonts-cjk-sans
  ];
  shellHook = ''
    echo "AI Office dev shell — cmake --preset linux && cmake --build --preset linux" >&2
    export DICPATH="${pkgs.hunspellDicts.pt_BR}/share/hunspell:${pkgs.hunspellDicts.en_US}/share/hunspell''${DICPATH:+:}$DICPATH"
    export FONTCONFIG_FILE=${pkgs.makeFontsConf {
      fontDirectories = [
        "${pkgs.ibm-plex}/share/fonts"
        "${pkgs.source-serif}/share/fonts"
        "${pkgs.jetbrains-mono}/share/fonts"
        "${pkgs.noto-fonts}/share/fonts"
        "${pkgs.noto-fonts-cjk-sans}/share/fonts"
      ];
    }}
  '';
}
