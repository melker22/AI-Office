# Catálogo Writer — ideias do Guide, código nosso

Espelho git do board **Roadmap para Produção** no Notion ([hub](https://app.notion.com/p/3cb46a79156b819daef5ea9d7c41385a)). Sync: **2026-08-30** (Fase IA wave 8: células, quebras, recuos, gutter). Totais: **100 Done**, **19 Partial**, **122 Todo**. Coluna **Fase** = `IA` | `Humano` | `Infra` | `Fora`.

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
| Família, tamanho e cor da fonte | Writer | WG 26.2 cap. 4 | JSON+MCP |
| Realce de texto | Writer | WG 26.2 cap. 4 | JSON+MCP |
| Versalete e capitalização | Writer | WG 26.2 cap. 4 | JSON+MCP |
| Lista de tarefas (checkbox) | Writer | WG 26.2 cap. 12 | JSON+MCP |
| Estilos de carácter | Writer | WG 26.2 cap. 8 | JSON+MCP |
| Fundo da página | Writer | WG 26.2 cap. 5 | JSON+MCP |
| Texto oculto (não seção) | Writer | WG 26.2 cap. 4 | JSON+MCP |
| Limpar formatação | Writer | WG 26.2 cap. 4 | JSON+MCP |
| Pintar formato | Writer | WG 26.2 cap. 4 | JSON+MCP |
| Espaço rígido e hífen opcional | Writer | WG 26.2 cap. 2 | JSON+MCP |
| Inserir carácter especial | Writer | WG 26.2 cap. 2 | JSON+MCP |
| Kerning, tracking e ligaduras | Writer | WG 26.2 cap. 4 | JSON+MCP |
| Idioma por parágrafo ou run | Writer | WG 26.2 cap. 4 | JSON+MCP |
| Localizar por formatação | Writer | WG 26.2 cap. 3 | JSON+MCP |
| Ir para página ou ID | Writer | WG 26.2 cap. 3 | JSON+MCP |
| Seleccionar pelo estilo | Writer | WG 26.2 cap. 8 | JSON+MCP |
| Reiniciar numeração de lista | Writer | WG 26.2 cap. 12 | JSON+MCP |
| Marcador personalizado | Writer | WG 26.2 cap. 12 | JSON+MCP |
| Estilos de lista | Writer | WG 26.2 cap. 12 | JSON+MCP |
| Estilos de página | Writer | WG 26.2 cap. 8 | JSON+MCP |
| Formato e reinício do nº de página | Writer | WG 26.2 cap. 5 | JSON+MCP |
| Hierarquia de estilos e estilo seguinte | Writer | WG 26.2 cap. 8 | JSON+MCP |
| Actualizar estilo a partir da selecção | Writer | WG 26.2 cap. 8 | JSON+MCP |
| Campo data/hora com formato | Writer | WG 26.2 cap. 17 | JSON+MCP |
| Envelopes e etiquetas | Writer | WG 26.2 cap. 14 | JSON+MCP |
| Cabeçalho e rodapé ímpar/par | Writer | WG 26.2 cap. 6 | JSON+MCP |
| Primeira página diferente | Writer | WG 26.2 cap. 6 | JSON+MCP |
| Colar HTML sanitizado | Writer | WG 26.2 cap. 3 | JSON+MCP |
| Colar sem formatação | Writer | WG 26.2 cap. 3 | JSON+MCP |
| AutoFormat (aspas, fracções) | Writer | WG 26.2 cap. 2 | JSON+MCP |
| Aspas tipográficas por locale | Writer | WG 26.2 cap. 2 | JSON+MCP |
| Moldura / caixa de texto | Writer | WG 26.2 cap. 11 | JSON+MCP |
| Estilos de moldura | Writer | WG 26.2 cap. 8 | JSON+MCP |
| Carregar estilos de um template | Writer | WG 26.2 cap. 8 | JSON+MCP |
| Promover e despromover heading | MCP | WG 26.2 cap. 8 | JSON+MCP |
| Envolver e desenvolver lista | MCP | WG 26.2 cap. 12 | JSON+MCP |
| Converter texto e tabela | Writer | WG 26.2 cap. 13 | JSON+MCP |
| Dividir tabela | Writer | WG 26.2 cap. 13 | JSON+MCP |
| Ordenar tabela | Writer | WG 26.2 cap. 13 | JSON+MCP |
| Exportar Markdown | Formats | WG 26.2 cap. 7 | JSON+MCP |
| Exportar texto plano | Formats | WG 26.2 cap. 7 | JSON+MCP |
| Copiar seleção como Markdown | Formats | infra | JSON+MCP |
| Manter com o seguinte | Writer | WG 26.2 cap. 4 | JSON+MCP |
| Quebra de página antes do parágrafo | Writer | WG 26.2 cap. 5 | JSON+MCP |
| Bordas e sombreado de célula | Writer | WG 26.2 cap. 13 | JSON+MCP |
| Alinhamento vertical na célula | Writer | WG 26.2 cap. 13 | JSON+MCP |
| Padding de célula | Writer | WG 26.2 cap. 13 | JSON+MCP |
| Recuos de parágrafo | Writer | WG 26.2 cap. 4 | JSON+MCP |
| Margem de encadernação (gutter) | Writer | WG 26.2 cap. 5 | JSON+MCP |

## Partial

| Name | Area | Fonte | O que falta |
|------|------|-------|-------------|
| Shell Tinta & Papel | Design | UX Pilot | Zero vazamento Qt |
| Linux packaging / PATH | Platform | infra | PATH global NixOS |
| Round-trip ODT/DOCX | Formats | WG 26.2 cap. 1, 7 | Fidelidade visual LO; nested notes |
| Autocorrect mínimo | Writer | WG 26.2 cap. 2 | GUI |
| Campos dinâmicos | Writer | WG 26.2 cap. 17 | Dezenas de tipos do Guide |
| Colunas de página 2–3 | Writer | WG 26.2 cap. 6 | Colunas desiguais |
| Control de alterações (inserir/apagar) | Writer | WG 26.2 cap. 3 | UI / balloons |
| Estilos só parágrafo | Writer | WG 26.2 cap. 8–9 | GUI carácter |
| Hifenação naive | Writer | WG 26.2 cap. 4 | Dicionário de padrões |
| Hiperlink | Writer | WG 26.2 cap. 3 | Visita + export |
| Localizar com regex | Writer | WG 26.2 cap. 3 | Substituir com formatação |
| Marcadores e referências cruzadas | Writer | WG 26.2 cap. 17 | Mais tipos de xref |
| Ortografia Hunspell | Writer | WG 26.2 cap. 3 | Build sem Hunspell |
| Sumário automático (só headings) | Writer | WG 26.2 cap. 15 | Índices extra, estilos TOC |
| PDF/A e PDF etiquetado | Formats | WG 26.2 cap. 7 | PDF etiquetado / tagged |

## Todo (backlog longo — um agente pode implementar no nosso modelo)

Critério: se cabe em JSON+IDs + MCP/CLI + canvas Tinta & Papel, entra mesmo que demore anos. Macros = playbook MCP, não StarBasic. Gráficos/fórmulas = renderer **nosso**, não Calc/Math OLE.

- **Runs / texto:** maiúsculas (GUI); carácter especial extra; limpar formato GUI; zona de hifenação.
- **Página:** ímpar/par (secções); papéis extra; livreto; capítulo no header; colunas desiguais; separador; imagem no header; tons de papel.
- **Chrome:** régua; stats; zoom GUI; print preview; Navigator rico; paleta; atalhos; recents; status bar; modo leitura; split; foco/typewriter; inspector por tipo.
- **Tabela:** estilos; repeat header; width auto.
- **Lista:** GUI de estilos/marcador/reinício.
- **Objectos:** rotate/flip; ajustes; opacity; lib de imagens (não Gallery); chart nosso; align; z-index; conectores.
- **Campos / notas / TOC:** filename; word_count; user vars; input; xref heading/lista/caption; link interno; restart notas; endnotes por seção; convert note; separador; TOC níveis/outros estilos; concordância.
- **Revisão:** accept/reject all; vista original/final; track formato; snapshots; balloons; fios; senha; read-only; redact.
- **Export:** PDF etiquetado; página-como-imagem; embed fontes; comments/XMP no PDF; print range/duplex.
- **Língua:** user.dic; underline ao escrever; vários Hunspell; padrões de hífen; Autocorrect lista; CJK vertical; ruby; kashida.
- **Estilos:** load from template GUI.
- **MCP (o que a IA usa):** tool por mutação restante. **Feito nesta leva:** promote_heading, wrap_list, text_to_table, split_table, export_markdown/text, copy_markdown.
- **Infra:** crash recovery; layout lazy; backup ~; MIME; Flatpak; Hunspell no Nix; watch file; CRDT local (anos).
- **A11y:** alt; reading order; teclado só no canvas.
- **Citações:** CSL ABNT/APA; etiquetas.
- **GUI extra:** DnD reorder; selecção múltipla; repeat last.
- **Testing:** round-trip ctest; golden PNG; fuzz parsers.

Plataforma já no board: Windows v1, macOS, Linux PATH Partial. Product: Planilha / PPT — **não começar**.

## Fora deste catálogo

StarBasic, Gallery, ícones e UI do LibreOffice; WPS; FreeOffice; Calc; Impress; Draw como app; Base; OLE Math.
