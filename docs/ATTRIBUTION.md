# Atribuição — ideias de processador de texto

O **AI Office Writer** é uma implementação **independente** (GPL-3.0-or-later). O código, o modelo `.aio.json` e a UI Tinta & Papel são nossos.

As *ideias* de funcionalidade de documento de texto (sumário, notas, campos, control de alterações, colunas, ortografia, etc.) seguem o que um Writer livre já documenta. A referência citável é:

- The Document Foundation — [LibreOffice Writer Guide 26.2](https://books.libreoffice.org/en/WG262/WG262.html)

Não copiamos código-fonte, ícones, Gallery, macros StarBasic nem a interface do LibreOffice. O LibreOffice, se o usuário o tiver instalado, continua um programa **MPL-2.0** separado (`soffice` em runtime).

**WPS Office** (Kingsoft) e **SoftMaker FreeOffice** são software proprietário. Não são fonte deste app: a EULA deles não permite copiar código nem a UI. Funcionalidades genéricas de processador de texto (sumário, notas, …) entram pela documentação livre acima, reimplementadas no nosso modelo com IDs estáveis (MCP + GUI).
