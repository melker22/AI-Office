# Catálogo Writer — ideias do Guide, código nosso

Espelho git do board **Roadmap para Produção** no Notion ([hub](https://app.notion.com/p/3cb46a79156b819daef5ea9d7c41385a)). Sync: **2026-08-29**. Totais: **22 Done**, **16 Partial**, **53 Todo** (91 linhas).

Fonte citável: The Document Foundation — [LibreOffice Writer Guide 26.2](https://books.libreoffice.org/en/WG262/WG262.html). Atribuição e limites: [`ATTRIBUTION.md`](ATTRIBUTION.md).

## Regras

- Só **ideias** de processador de texto. Não copiar código, ícones, Gallery, notebookbar nem StarBasic do LibreOffice.
- WPS Office e SoftMaker FreeOffice **não** são fonte.
- Calc, Impress, Draw-app, Base e Math-as-OLE: **fora** deste Writer (linha Product *Planilha / PPT* = não começar). Formas *dentro* do documento de texto = cap. 11, no nosso canvas.
- Equivalente a macros: **MCP/CLI**, não gravador.

Camadas: `JSON+MCP+GUI` · `JSON+MCP` · `só JSON` · `infra`.

## Done

| Name | Area | Fonte | Camada |
|------|------|-------|--------|
| Imagens embutidas | Formats | WG 26.2 cap. 11 | JSON+MCP+GUI |
| Listas numeradas reais | Formats | WG 26.2 cap. 12 | JSON+MCP+GUI |
| Markdown básico | Formats | WG 26.2 cap. 2 | JSON+MCP |
| PDF nativo | Formats | WG 26.2 cap. 7 | JSON+MCP+GUI |
| JSON nativo + IDs estáveis | MCP | infra | JSON+MCP |
| MCP + socket GUI viva | MCP | infra (não StarBasic) | JSON+MCP |
| page_preview estável | MCP | infra | JSON+MCP |
| Alinhamento de parágrafo | Writer | WG 26.2 cap. 4 | JSON+MCP+GUI |
| Canvas digitável | Writer | WG 26.2 cap. 2 | JSON+MCP+GUI |
| Comentários no bloco | Writer | WG 26.2 cap. 3 | JSON+MCP+GUI |
| Digitação e undo/redo | Writer | WG 26.2 cap. 2 | JSON+MCP+GUI |
| Estilos de parágrafo Heading/Title/Quote | Writer | WG 26.2 cap. 8 | JSON+MCP+GUI |
| Header/footer + nº página | Writer | WG 26.2 cap. 6 | JSON+MCP+GUI |
| Listas com marcadores aninhadas | Writer | WG 26.2 cap. 12 | JSON+MCP+GUI |
| Localizar texto simples | Writer | WG 26.2 cap. 3 | JSON+MCP+GUI |
| Negrito, itálico, sublinhado | Writer | WG 26.2 cap. 4 | JSON+MCP+GUI |
| Outline de headings | Writer | WG 26.2 cap. 8 | JSON+MCP+GUI |
| Papel A4/Letter, margens e orientação | Writer | WG 26.2 cap. 5 | JSON+MCP+GUI |
| Quebra de página | Writer | WG 26.2 cap. 5 | JSON+MCP+GUI |
| Tabelas: inserir, linhas, colunas, cabeçalho | Writer | WG 26.2 cap. 13 | JSON+MCP+GUI |
| Tabs multi-documento | Writer | infra | JSON+MCP+GUI |
| Templates de documento | Writer | WG 26.2 cap. 10 | JSON+MCP+GUI |

## Partial

| Name | Area | Fonte | O que falta |
|------|------|-------|-------------|
| Shell Tinta & Papel | Design | UX Pilot | Zero vazamento Qt |
| Round-trip ODT/DOCX | Formats | WG 26.2 cap. 1, 7 | Notas, campos, TOC, revisões |
| Linux packaging / PATH | Platform | infra | PATH global NixOS |
| Autocorrect mínimo | Writer | WG 26.2 cap. 2 | Lista editável / AutoFormat |
| Campos dinâmicos | Writer | WG 26.2 cap. 17 | Dezenas de tipos do Guide |
| Colunas de página 2–3 | Writer | WG 26.2 cap. 6 | Quebra de coluna, separador |
| Control de alterações (inserir/apagar) | Writer | WG 26.2 cap. 3 | Formato, UI rica, round-trip |
| Estilos só parágrafo | Writer | WG 26.2 cap. 8–9 | Char, page, list, frame |
| Hifenação naive | Writer | WG 26.2 cap. 4 | Dicionário de padrões |
| Hiperlink | Writer | WG 26.2 cap. 3 | Visita + export |
| Localizar com regex | Writer | WG 26.2 cap. 3 | Substituir com formatação |
| Marcadores e referências cruzadas | Writer | WG 26.2 cap. 17 | Mais tipos de xref |
| Notas de rodapé e de fim | Writer | WG 26.2 cap. 3 | Layout + ODT/DOCX |
| Ortografia Hunspell | Writer | WG 26.2 cap. 3 | Build sem Hunspell |
| Sumário automático (só headings) | Writer | WG 26.2 cap. 15 | Índices extra, estilos TOC |
| Tachado, superescrito, subscrito | Writer | WG 26.2 cap. 4 | Round-trip ODT/DOCX |

## Todo (Writer / Formats)

Um Name por feature. **Não** começar Planilha/PPT.

- **Formato da página:** estilos de página; primeira página diferente; seções; seções ocultas; quebra de coluna; marca d’água; órfãs/viúvas; impressão; envelopes/etiquetas.
- **Parágrafo:** recuos; espaçamento; entrelinha; bordas; tabulações; letra capitular.
- **Imagem / objectos no texto:** âncora; wrap; crop; legenda; formas no canvas (cap. 11, não Draw); QR; grelha/snap; moldura/caixa de texto; numeração de linhas.
- **Índices / longo prazo:** índice alfabético; bibliográfico; ilustrações; documento mestre; mala direta; formulários (sem Base); numeração de capítulos; texto condicional; AutoText; tesauro; gramática; comparar documentos; base bibliográfica; acessibilidade; autosave; propriedades do doc; CTL/CJK.
- **Tabela:** mesclar/dividir células (`colSpan` já no JSON); fórmulas de célula (não Calc); tabelas aninhadas.
- **Fórmulas no texto:** renderer **nosso** (não LO Math).
- **Formatos:** EPUB; HTML nativo; RTF nativo; DOC nativo (hoje soffice opcional); assinatura digital.

Plataforma (já no board, não Writer Guide): Windows v1, macOS. Product: Planilha / PPT — **não começar**.

## Fora deste catálogo

StarBasic, Gallery, ícones e UI do LibreOffice; WPS; FreeOffice; Calc; Impress; Draw como app; Base; OLE Math.
