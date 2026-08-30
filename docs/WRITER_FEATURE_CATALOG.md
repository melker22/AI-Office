# Catálogo Writer — ideias do Guide, código nosso

Espelho git do board **Roadmap para Produção** no Notion ([hub](https://app.notion.com/p/3cb46a79156b819daef5ea9d7c41385a)). Sync: **2026-08-30** (Fase IA **onda 10**: objectos, página, revisão). Totais: **175 Done**, **20 Partial**, **46 Todo**. Coluna **Fase** = `IA` | `Humano` | `Infra` | `Fora`.

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
| Colunas de página 2–3 (desiguais) | Writer | WG 26.2 cap. 6 | JSON+MCP |
| Capítulo no cabeçalho | Writer | WG 26.2 cap. 6 | JSON+MCP |
| Marca d'água | Writer | WG 26.2 cap. 5 | JSON+MCP |
| Variáveis de documento | Writer | WG 26.2 cap. 17 | JSON+MCP |
| Campo de input | Writer | WG 26.2 cap. 17 | JSON+MCP |
| Campo ficheiro | Writer | WG 26.2 cap. 17 | JSON+MCP |
| Campo contagem de palavras | Writer | WG 26.2 cap. 17 | JSON+MCP |
| Documento só de leitura | Writer | WG 26.2 cap. 2 | JSON+MCP |
| Propriedades do documento | Writer | WG 26.2 cap. 1 | JSON+MCP |
| Fórmulas em células | Writer | WG 26.2 cap. 13 | JSON+MCP |
| Estilos de tabela | Writer | WG 26.2 cap. 13 | JSON+MCP |
| Largura auto de tabela | Writer | WG 26.2 cap. 13 | JSON+MCP |
| Repetir cabeçalho de tabela | Writer | WG 26.2 cap. 13 | JSON+MCP |
| Converter nota rodapé/fim | Writer | WG 26.2 cap. 3 | JSON+MCP |
| TOC por níveis e estilos | Writer | WG 26.2 cap. 15 | JSON+MCP |
| Concordância / índice remissivo | Writer | WG 26.2 cap. 15 | JSON+MCP |
| Imagem no cabeçalho | Writer | WG 26.2 cap. 6 | JSON+MCP |
| Rodar imagem | Writer | WG 26.2 cap. 11 | JSON+MCP |
| Recorte, flip e sombra de imagem | Writer | WG 26.2 cap. 11 | JSON+MCP |
| Formas (rect/ellipse/line) | Writer | WG 26.2 cap. 11 | JSON+MCP |
| Gráfico de barras | Writer | WG 26.2 cap. 11 | JSON+MCP |
| QR no texto | Writer | WG 26.2 cap. 11 | JSON+MCP |
| Equação numerada | Writer | WG 26.2 cap. 17 | JSON+MCP |
| Formulário no texto | Writer | WG 26.2 cap. 13 | JSON+MCP |
| Numeração de capítulos e linhas | Writer | WG 26.2 cap. 6 | JSON+MCP |
| Tom de papel, grelha, tamanho mm | Writer | WG 26.2 cap. 5 | JSON+MCP |
| Livreto PDF 2-up | Formats | WG 26.2 cap. 7 | JSON+MCP |
| Citação e base bibliográfica (JSON) | Writer | WG 26.2 cap. 15 | JSON+MCP |
| Índice de ilustrações | Writer | WG 26.2 cap. 15 | JSON+MCP |
| AutoText, redact, repeat last | Writer | WG 26.2 cap. 2–3 | JSON+MCP |
| Estatísticas, comparar, a11y | Writer | WG 26.2 cap. 3 | JSON+MCP |
| Ruby, texto vertical, kashida | Writer | WG 26.2 cap. 4 | JSON+MCP |
| Tesauro e user.dic | Writer | WG 26.2 cap. 3 | JSON+MCP |
| Papéis extra (B4/B5/Executive/Folio/Statement) | Writer | WG 26.2 cap. 5 | JSON+MCP |
| Accept/reject all revisões | Writer | WG 26.2 cap. 3 | JSON+MCP |
| Caps upper/lower/title | Writer | WG 26.2 cap. 4 | JSON+MCP |
| Entrelinha e espaçamento | Writer | WG 26.2 cap. 4 | JSON+MCP |
| Quebra de coluna | Writer | WG 26.2 cap. 6 | JSON+MCP |
| Mail merge | Writer | WG 26.2 cap. 14 | JSON+MCP |
| Exportar páginas PNG | MCP | WG 26.2 cap. 7 | JSON+MCP |
| Partir e juntar parágrafos | MCP | WG 26.2 cap. 2 | JSON+MCP |
| Wrap de imagem | Writer | WG 26.2 cap. 11 | JSON+MCP |
| Separador entre colunas | Writer | WG 26.2 cap. 6 | JSON+MCP |
| Exportar HTML | Formats | WG 26.2 cap. 7 | JSON+MCP |

## Partial

| Name | Area | Fonte | O que falta |
|------|------|-------|-------------|
| Shell Tinta & Papel | Design | UX Pilot | Zero vazamento Qt |
| Linux packaging / PATH | Platform | infra | PATH global NixOS |
| Round-trip ODT/DOCX | Formats | WG 26.2 cap. 1, 7 | Fidelidade visual LO; nested notes |
| Autocorrect mínimo | Writer | WG 26.2 cap. 2 | GUI |
| Campos dinâmicos | Writer | WG 26.2 cap. 17 | Dezenas de tipos do Guide |
| Control de alterações (inserir/apagar) | Writer | WG 26.2 cap. 3 | UI / balloons |
| Estilos só parágrafo | Writer | WG 26.2 cap. 8–9 | GUI carácter |
| Hifenação naive | Writer | WG 26.2 cap. 4 | Dicionário de padrões |
| Hiperlink | Writer | WG 26.2 cap. 3 | Visita + export |
| Localizar com regex | Writer | WG 26.2 cap. 3 | Substituir com formatação |
| Marcadores e referências cruzadas | Writer | WG 26.2 cap. 17 | Mais tipos de xref |
| Ortografia Hunspell | Writer | WG 26.2 cap. 3 | Build sem Hunspell |
| Sumário automático (só headings) | Writer | WG 26.2 cap. 15 | Estilos TOC1–n nomeados |
| PDF/A e PDF etiquetado | Formats | WG 26.2 cap. 7 | PDF etiquetado / tagged |
| Citação CSL | Writer | WG 26.2 cap. 15 | Processador CSL completo; há cite+JSON |
| CTL/CJK completo | Writer | WG 26.2 cap. 4 | Ruby/vertical/kashida; sem shaping completo |
| Órfãs e viúvas | Writer | WG 26.2 cap. 4 | Parágrafo não parte entre páginas |
| Índice bibliográfico | Writer | WG 26.2 cap. 15 | Entradas JSON; sem lista formatada CSL |
| Notas: reinício por página | Writer | WG 26.2 cap. 3 | Flag no JSON; marcador ainda estático |
| Tools MCP para cada mutação | MCP | infra | Chrome Humano |

## Todo (Humano / Infra — IA continua na próxima onda se restar JSON+MCP)

Fase IA Writer **não** está encerrada: o board ainda tem Todos de chrome, packaging e limites honestos. O motor JSON+MCP+`page_preview` da onda 10 cobre formas, QR, gráficos, ruby, livreto, stats, etc.

- **Humano (chrome Tinta & Papel):** régua; stats GUI; zoom GUI; print preview; Navigator; paleta; atalhos; recents; status bar; modo leitura; split; foco/typewriter; inspector; balloons; DnD reorder; teclado só no canvas; Autocorrect lista GUI; underline ao escrever; impressão.
- **Infra:** backup `~`; layout lazy; watch file; Hunspell Nix; Flatpak; CRDT; DOC/RTF nativo; EPUB; PDF font embed; XMP; assinatura; senha; master document; crash recovery; autosave.
- **Limites honestos (Partial):** tagged PDF via QPainter; dicionário de padrões de hífen; processador CSL; round-trip visual ODT/DOCX.
- **Testing (o utilizador pede à parte):** round-trip ctest; golden PNG; fuzz.

Plataforma já no board: Windows v1, macOS, Linux PATH Partial. Product: Planilha / PPT — **não começar**.

## Fora deste catálogo

StarBasic, Gallery, ícones e UI do LibreOffice; WPS; FreeOffice; Calc; Impress; Draw como app; Base; OLE Math.
