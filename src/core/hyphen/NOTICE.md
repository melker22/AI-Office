# Hyphenation pattern dictionaries — provenance and license

Vendored for the Knuth-Liang hyphenation algorithm in `../Hyphenation.cpp`
(wave 12). These are **data files** (syllable-break pattern tables), not
code — distributed unmodified.

## `hyph_en_US.dic`

- Source: `hyphen-2.8.8.tar.gz` (SourceForge `hunspell/Hyphen`), via
  `nixpkgs.hyphenDicts.en_US`.
- License: tri-licensed **GPL-2.0 / LGPL-2.1 / MPL-1.1** (standard libhyphen
  dictionary licensing). Used here under the **LGPL-2.1** option, which
  combines freely into a GPL-3.0-or-later work.

## `hyph_pt_BR.dic`

- Source: LibreOffice `DivSilab` (Brazilian Portuguese hyphenator) by
  Raimundo Santos Moura, via `nixpkgs.hyphenDicts.pt_BR`
  (`hyphen-dict-pt-br-libreoffice-24.8`).
- License: dual **LGPLv3 / MPL** per the file's own header (see the top of
  `hyph_pt_BR.dic` and nixpkgs' `meta.license = MPL-2.0`). Both terms are
  GPL-3.0-or-later compatible.

`hyph_pt_PT.dic` (LibreOffice pt-PT hyphenator) was evaluated and **not**
vendored: its own header states a plain "GNU GPL license" with no version or
"or later" clause, which is more ambiguous to combine safely than the
pt_BR file's explicit LGPLv3/MPL dual license. `pt_BR` patterns are used for
Portuguese; European Portuguese words hyphenate correctly in the large
majority of cases since syllabification rules are largely shared.
