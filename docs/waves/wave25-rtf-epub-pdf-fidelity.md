# Onda 25 — Fidelidade RTF, EPUB e PDF (spec arquitetural)

> Autor: Opus (Chief Architect), 2026-10-28.
> Origem: auditoria 2026-10 (`docs/audit/fix-plan.md`, onda 25).
> Fleet: `.cockpit/fleets/wave25-rtf-epub-pdf.yaml`.
> Escopo: Fase IA (core + QA), sem `src/app/`.
> Aprovação: push/PR pré-aprovados; o merge só sai com `scripts/pr-merge-gate.py` limpo e com as threads resolvidas.

## Diagnóstico

Os itens abaixo estão provados em `docs/audit/module-sweep.md` (ids A-rtf-*, A-epub-*, A-pdf-*) e em `docs/audit/coderabbit-pr11.json`.

**RTF**
- O `pt-BR` sai como LCID de `pt-PT`.
- Os grupos `\upr`/`\ud` perdem o texto.
- O espaçamento explícito de títulos é sobrescrito.
- O `\ilvl` não é escrito, e listas de checkbox viram listas numeradas.
- Imagem em célula some.
- Os campos `REF`/`SEQ`/`MERGEFIELD`/`DOCVARIABLE`/`FILLIN`/`NUMWORDS` viram texto fixo, e os argumentos de campo não são escapados.
- Texto oculto é publicado sem aviso.
- Notas perdem a formatação e endnotes voltam como footnotes.
- Comentários em tabela ou quebra somem sem aviso.
- O `\column` não é lido.
- O bookmark ignora o offset.
- As colunas de tabela saem sempre com a mesma largura.

**EPUB**
- Endnote órfã some.
- O `aside` sai sem `lang`/`dir`.
- Estilos de caractere são ignorados.
- Campos de página aparecem como página 1 de 1.
- Uma noteref oculta continua visível.
- `dir:auto` vira `ltr`.
- Classes CSS colidem.
- O `list_bullet` customizado é ignorado.

**PDF**
- Strings UTF-16 não escapam CR/LF/TAB/BS/FF.
- Com `copies>1` a estrutura de cada cópia é fundida.
- Listas aninhadas viram uma `/L` plana.
- Células mescladas saem sem `/ColSpan`/`/RowSpan`.

## Decisões (fechadas)

| # | Decisão |
|---|---|
| D1 | **Tabela de LCID** única, usada pelo writer e pelo reader. Entradas: pt-BR 1046, pt-PT 2070, en-US 1033, en-GB 2057, es-ES 3082, es-MX 2058, fr-FR 1036, de-DE 1031, it-IT 1040, ar 1025, he 1037, ru 1049, zh-CN 2052, zh-TW 1028, ja 1041, ko 1042. A tag sem região usa a variante principal (`pt` → 1046, `en` → 1033). Uma língua desconhecida sai sem `\lang` e gera um warning. A ida e volta preserva as tags da tabela; uma tag fora dela (por exemplo bg-BG) sai sem `\lang`, com warning (A-rtf-1). |
| D2 | **`\upr`**: o reader usa o conteúdo do `{\*\ud …}` (Unicode) e descarta o fallback ANSI. Sem `\ud`, usa o ANSI (A-rtf-2). |
| D3 | **Títulos**: o espaçamento padrão de título (`\sb240\sa120`) só é escrito quando o bloco não tem espaçamento explícito. O valor explícito, inclusive 0, vence (A-rtf-4). |
| D4 | **Listas RTF**: o writer escreve `\ilvl`. Checkbox/task saem como lista com marcador ☐/☑ (que outros leitores veem) e com os destinos privados `{\*\aiolistkind checkbox}` e `{\*\aiocheck 0\|1}` por item. O reader honra esses destinos e restaura o kind e o estado (A-rtf-5, A-rtf-6). |
| D5 | **Imagem em célula**: o modelo de célula não tem imagem. O reader troca a imagem por `[imagem: <alt ou nome>]` na célula e gera um warning; nunca a descarta em silêncio. O writer faz o mesmo se receber uma (A-rtf-7). |
| D6 | **Campos RTF de ida e volta**: `REF`, `SEQ`, `MERGEFIELD`, `DOCVARIABLE`, `FILLIN` e `NUMWORDS` (para `word_count`) são escritos e lidos como campos. Os argumentos do `\fldinst` vão entre aspas, com `\`, `{`, `}` e `"` escapados. Caracteres de controle são removidos (A-rtf-8, PR #11). |
| D7 | **`hide_if`/`section_hidden` em RTF**: o texto sai com `\v` (oculto), como o `w:vanish` do DOCX, e o export gera um warning com o id. Isso não é redação (A-rtf-9). |
| D8 | **Notas RTF**: o corpo da nota mantém a formatação de caractere. Endnotes saem com `\ftnalt`, e o reader mapeia `\ftnalt` para `endnote` (A-rtf-10, PR #11). |
| D9 | **Comentários em blocos de tabela ou quebra** geram o mesmo warning dos outros tipos (A-rtf-11). |
| D10 | **`\column`**: o reader cria uma quebra de coluna, sem parágrafo vazio extra (A-rtf-12). |
| D11 | **Bookmark RTF no offset**: `\bkmkstart`/`\bkmkend` ficam na posição `Bookmark::offset` do parágrafo (A-rtf-13). |
| D12 | **Larguras de coluna RTF** vêm de `colWidthsMm` (convertidas em twips). Ficam iguais só quando não há larguras (A-rtf-14). |
| D13 | **Notas EPUB**: uma nota sem nenhuma referência (órfã) é emitida na seção de notas, com warning. Uma nota cujas referências estão todas ocultas é omitida, junto com a noteref (A-epub-2, A-epub-7). |
| D14 | O **`aside`** de nota recebe `lang`/`xml:lang`/`dir` do bloco da nota (A-epub-3). |
| D15 | **Estilos de caractere EPUB** viram classes CSS geradas a partir do `charStyle`, como os estilos de parágrafo (A-epub-4). |
| D16 | **Campos de página no EPUB** (sem páginas): `page_number`/`page_count` saem vazios. `xref` do tipo `page` com alvo no pacote vira um link para o alvo, com o texto do alvo (título/legenda); se o alvo falta ou está omitido, sai como texto simples, sem link. As duas substituições entram no warning (A-epub-5). |
| D17 | **`dir:auto`** usa a mesma detecção do primeiro caractere forte que o layout usa (`resolvedParagraphDirection`) (A-epub-8). |
| D18 | **Classes CSS únicas**: uma colisão recebe um sufixo numérico estável (A-epub-9). |
| D19 | **`list_bullet` customizado** sai como `list-style-type: "<bullet> "` (A-epub-10). |
| D20 | **Strings PDF**: `pdfUtf16String` escapa `\r \n \t \b \f` (escapes nomeados ou octais), além de `( ) \` (A-pdf-1). |
| D21 | **`copies>1`**: os elementos de estrutura são por cópia de página (chave página + bloco) (A-pdf-2). |
| D22 | **Listas aninhadas no PDF**: uma `/L` interna dentro do `/LI` pai, por nível (A-pdf-3). |
| D23 | **Células mescladas no PDF**: `/A << /O /Table /ColSpan n /RowSpan m >>` em `TD`/`TH` (A-pdf-4). |
| D24 | **Juízes**: cada decisão tem um teste que falha antes da correção, com prefixo `qa/wave25-`. O epubcheck continua com 0 erros e 0 avisos, e o veraPDF PDF/UA-1 e o PDF/A-1b continuam conformes. O RTF faz ida e volta pelo nosso import e, com `--interop`, pelo LibreOffice. Testes desabilitados que citam a onda 25 são reabilitados. |
| D25 | Não regredir as ondas 12–24. |

## Regras de execução (lições L22, L25–L27)

- **CI:** a CI roda como root e com outra glibc. Nenhum teste depende de privilégio nem de `LD_PRELOAD` herdado.
- **Nós Claude:** não usam skills nem subagentes e nunca submetem uma saída provisória.
- **Build:** nenhum nó de agente roda build completo do zero. O QA longo fica nos gates de comando; o build do worktree integrado é feito pelo gate.
- **Git:** ninguém usa `git stash`, porque ele é compartilhado entre os worktrees.

## Tarefas e roteamento

O Grok Build voltou. A Cursor está a 94% (modelos) e não é usada nesta onda.

| Nó | Executor | Itens |
|---|---|---|
| `lessons` | Sonnet | PRs #13–#15 + lições L25–L27 |
| `t1-rtf-writer` | Grok Build 4.7 | D1 (writer), D3, D4 (writer), D5 (writer), D6 (writer), D7, D8 (writer), D9, D11, D12 |
| `t2-rtf-reader` | Grok Build 4.7 | D1 (reader), D2, D4 (reader), D5 (reader), D6 (reader), D8 (reader), D10 |
| `t3-epub` | Grok Build 4.7 | D13–D19 |
| `t4-pdf` | Grok Build 4.7 | D20–D23 |

Depois das tarefas, a sequência é:
1. `integrate-1` (Grok Build, effort medium);
2. gate de build e QA;
3. `t5-regress`;
4. gate;
5. loop de revisão: Sonnet revisa e Grok Build corrige;
6. adjudicação;
7. `qa-final`;
8. handoff;
9. `notion-sync`;
10. `open-pr`.

## Critério de pronto

- Cada decisão D1–D23 tem um teste que falharia sem ela.
- O epubcheck continua com 0 erros e 0 avisos, e o veraPDF UA-1 e o A-1b continuam conformes.
- A ida e volta RTF preserva língua, listas e checkbox, campos, notas e endnotes, bookmarks e larguras de coluna.
- `scripts/qa-all.sh --gui --interop --pdf --formats` passa no local e na CI.
