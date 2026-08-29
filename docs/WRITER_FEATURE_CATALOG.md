# Catálogo Writer — ideias do Guide, código nosso

Espelho git do board **Roadmap para Produção** no Notion ([hub](https://app.notion.com/p/3cb46a79156b819daef5ea9d7c41385a)). Sync: **2026-08-29** (Fase IA wave 3: tabs, drop cap, nested table, mala direta, bordas, PDF/A). Totais: **51 Done**, **19 Partial**, **171 Todo**. Coluna **Fase** = `IA` | `Humano` | `Infra` | `Fora`.

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
| Validar schema e IDs | MCP | infra | JSON+MCP |
| Operações em lote por IDs | MCP | infra | JSON+MCP |
| Transacções MCP atómicas | MCP | infra | JSON+MCP |
| Checkpoint e rollback via MCP | MCP | infra | JSON+MCP |
| Playbook de calls MCP | MCP | infra | JSON+MCP |
| JSON canónico para git | MCP | infra | JSON+MCP |
| page_preview por intervalo | MCP | infra | JSON+MCP |
| Inserir Markdown em IDs | MCP | infra | JSON+MCP |
| Árvore do documento via MCP | MCP | infra | JSON+MCP |
| Duplicar bloco | MCP | infra | JSON+MCP |
| Mover bloco por ID | MCP | infra | JSON+MCP |
| Diff estruturado via MCP | MCP | infra | JSON+MCP |
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
| Notas de rodapé e de fim | Writer | WG 26.2 cap. 3 | JSON+MCP |
| Tachado, superescrito, subscrito | Writer | WG 26.2 cap. 4 | JSON+MCP |
| Tabulações | Writer | WG 26.2 cap. 4 | JSON+MCP |
| Letra capitular | Writer | WG 26.2 cap. 4 | JSON+MCP |
| Tabelas aninhadas | Writer | WG 26.2 cap. 13 | JSON+MCP |
| Mala direta | Writer | WG 26.2 cap. 14 | JSON+MCP |
| Bordas de parágrafo | Writer | WG 26.2 cap. 4 | JSON+MCP |
| Bordas da página | Writer | WG 26.2 cap. 5 | JSON+MCP |
| Quebra de linha no parágrafo | Writer | WG 26.2 cap. 2 | JSON+MCP |

## Partial

| Name | Area | Fonte | O que falta |
|------|------|-------|-------------|
| Shell Tinta & Papel | Design | UX Pilot | Zero vazamento Qt |
| Linux packaging / PATH | Platform | infra | PATH global NixOS |
| Round-trip ODT/DOCX | Formats | WG 26.2 cap. 1, 7 | Fidelidade visual LO; nested notes |
| Autocorrect mínimo | Writer | WG 26.2 cap. 2 | AutoFormat; GUI |
| Campos dinâmicos | Writer | WG 26.2 cap. 17 | Dezenas de tipos do Guide |
| Colunas de página 2–3 | Writer | WG 26.2 cap. 6 | Colunas desiguais |
| Control de alterações (inserir/apagar) | Writer | WG 26.2 cap. 3 | UI / balloons |
| Estilos só parágrafo | Writer | WG 26.2 cap. 8–9 | Page, list, frame; GUI carácter |
| Hifenação naive | Writer | WG 26.2 cap. 4 | Dicionário de padrões |
| Hiperlink | Writer | WG 26.2 cap. 3 | Visita + export |
| Localizar com regex | Writer | WG 26.2 cap. 3 | Substituir com formatação |
| Marcadores e referências cruzadas | Writer | WG 26.2 cap. 17 | Mais tipos de xref |
| Ortografia Hunspell | Writer | WG 26.2 cap. 3 | Build sem Hunspell |
| Sumário automático (só headings) | Writer | WG 26.2 cap. 15 | Índices extra, estilos TOC |
| PDF/A e PDF etiquetado | Formats | WG 26.2 cap. 7 | PDF etiquetado / tagged |

## Todo (backlog longo — um agente pode implementar no nosso modelo)

Critério: se cabe em JSON+IDs + MCP/CLI + canvas Tinta & Papel, entra mesmo que demore anos. Macros = playbook MCP, não StarBasic. Gráficos/fórmulas = renderer **nosso**, não Calc/Math OLE.

- **Runs / texto:** fonte/tamanho/cor; realce; versalete; kerning; idioma; maiúsculas; carácter especial; NBSP/hífen suave; limpar formato; pintar formato; substituir; find por formato; goto página/ID; colar plain/HTML; texto oculto; aspas por locale; zona de hifenação.
- **Página:** ímpar/par; formato/reinício do nº; fundo; gutter; papéis extra; livreto; capítulo no header; colunas desiguais; separador; imagem no header; break-before; keep-with-next; tons de papel.
- **Chrome:** régua; stats; zoom GUI; print preview; Navigator rico; paleta; atalhos; recents; status bar; modo leitura; split; foco/typewriter; inspector por tipo.
- **Tabela:** estilos; sort; texto↔tabela; repeat header; bordas de célula; v_align; split; width auto; padding.
- **Lista:** estilos; bullet custom; restart; checkbox.
- **Objectos:** rotate/flip; ajustes; opacity; lib de imagens (não Gallery); chart nosso; align; z-index; conectores.
- **Campos / notas / TOC:** filename; data formatada; word_count; user vars; input; xref heading/lista/caption; link interno; restart notas; endnotes por seção; convert note; separador; TOC níveis/outros estilos; concordância.
- **Revisão:** accept/reject all; vista original/final; track formato; snapshots; balloons; fios; senha; read-only; redact.
- **Export:** Markdown; txt; PDF etiquetado; página-como-imagem; copy MD; embed fontes; comments/XMP no PDF; print range/duplex.
- **Língua:** user.dic; underline ao escrever; vários Hunspell; padrões de hífen; Autocorrect lista; AutoFormat; CJK vertical; ruby; kashida.
- **Estilos:** carácter; moldura; hierarquia/next; update from selection; load from template; select by style.
- **MCP (o que a IA usa):** tool por mutação restante; promote heading; split/join; wrap_list. **Feito nesta leva:** validate, batch, ids[], checkpoint, play, JSON canónico, page_preview from/to, insert_markdown, outline rico.
- **Infra:** crash recovery; layout lazy; backup ~; MIME; Flatpak; Hunspell no Nix; watch file; CRDT local (anos).
- **A11y:** alt; reading order; teclado só no canvas.
- **Citações:** CSL ABNT/APA; merge e-mail local (envelopes).
- **GUI extra:** DnD reorder; selecção múltipla; repeat last.
- **Testing:** round-trip ctest; golden PNG; fuzz parsers.

Plataforma já no board: Windows v1, macOS, Linux PATH Partial. Product: Planilha / PPT — **não começar**.

## Fora deste catálogo

StarBasic, Gallery, ícones e UI do LibreOffice; WPS; FreeOffice; Calc; Impress; Draw como app; Base; OLE Math.
