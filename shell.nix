{ pkgs ? import <nixpkgs> {} }:
pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    cmake
    ninja
    gcc
    pkg-config
    qt6.wrapQtAppsHook
  ];
  buildInputs = with pkgs; [
    qt6.qtbase
    qt6.qtdeclarative
    qt6.qtwayland
    zlib
    ibm-plex
    source-serif
    jetbrains-mono
  ];
  shellHook = ''
    echo "AI Office dev shell — cmake --preset linux && cmake --build --preset linux"
  '';
}
