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
let
  # makeFontsConf is not hermetic: it includes /etc/fonts/conf.d, XDG and
  # FHS font dirs, and DejaVu. On NixOS those dirs exist (Liberation, a
  # second Noto, user fonts); in the CircleCI nixos/nix image they do not,
  # so the same shell measured different glyph advances. This file lists
  # only the Nix font directories the layout asks for.
  fontsConf = pkgs.writeText "aioffice-fonts.conf" ''
    <?xml version="1.0"?>
    <!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
    <fontconfig>
      <dir>${pkgs.liberation_ttf}/share/fonts</dir>
      <dir>${pkgs.ibm-plex}/share/fonts</dir>
      <dir>${pkgs.source-serif}/share/fonts</dir>
      <dir>${pkgs.jetbrains-mono}/share/fonts</dir>
      <dir>${pkgs.noto-fonts}/share/fonts</dir>
      <dir>${pkgs.noto-fonts-cjk-sans}/share/fonts</dir>
      <cachedir>/tmp/aioffice-fontconfig-cache</cachedir>

      <!-- Document default is Liberation Serif. Arabic/Hebrew/CJK must resolve
           to Noto, not to IBM Plex's script faces or Noto Nastaliq Urdu. -->
      <alias binding="strong">
        <family>serif</family>
        <prefer><family>Liberation Serif</family></prefer>
      </alias>
      <alias binding="strong">
        <family>sans-serif</family>
        <prefer><family>IBM Plex Sans</family></prefer>
      </alias>
      <alias binding="strong">
        <family>monospace</family>
        <prefer><family>JetBrains Mono</family></prefer>
      </alias>
      <match target="pattern">
        <test name="lang" compare="contains"><string>ar</string></test>
        <edit name="family" mode="prepend" binding="strong">
          <string>Noto Naskh Arabic</string>
        </edit>
      </match>
      <match target="pattern">
        <test name="lang" compare="contains"><string>he</string></test>
        <edit name="family" mode="prepend" binding="strong">
          <string>Noto Serif Hebrew</string>
        </edit>
      </match>
      <match target="pattern">
        <test name="lang" compare="contains"><string>ja</string></test>
        <edit name="family" mode="prepend" binding="strong">
          <string>Noto Sans CJK JP</string>
        </edit>
      </match>
      <match target="pattern">
        <test name="lang" compare="contains"><string>zh</string></test>
        <edit name="family" mode="prepend" binding="strong">
          <string>Noto Sans CJK SC</string>
        </edit>
      </match>
      <match target="pattern">
        <test name="lang" compare="contains"><string>ko</string></test>
        <edit name="family" mode="prepend" binding="strong">
          <string>Noto Sans CJK KR</string>
        </edit>
      </match>
    </fontconfig>
  '';
in
pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    cmake
    ninja
    gcc
    pkg-config
    ccache
    python3
    git
    qt6.wrapQtAppsHook
  ];
  buildInputs = with pkgs; [
    verapdf
    epubcheck
    qpdf
    poppler-utils
    zbar
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
    liberation_ttf
    noto-fonts
    noto-fonts-cjk-sans
  ];
  shellHook = ''
    echo "AI Office dev shell — cmake --preset linux && cmake --build --preset linux" >&2
    export DICPATH="${pkgs.hunspellDicts.pt_BR}/share/hunspell:${pkgs.hunspellDicts.en_US}/share/hunspell''${DICPATH:+:}$DICPATH"
    mkdir -p /tmp/aioffice-fontconfig-cache
    # Override any FONTCONFIG_FILE inherited from the host (NixOS sets one).
    export FONTCONFIG_FILE=${fontsConf}
    unset FONTCONFIG_PATH
  '';
}
