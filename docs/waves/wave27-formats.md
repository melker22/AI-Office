# Onda 27 — Formatos: importar EPUB, HTML pelo escritor XHTML e notas com vários parágrafos (spec arquitetural)

> Autor: Opus (Chief Architect), 2026-11-02.
>
> - **Origem:** `docs/WRITER_FEATURE_CATALOG.md`, Todo IA (Writer): "Importar EPUB", "`export_html` no escritor XHTML" e
>   "Notas com vários parágrafos (rodapé/fim) — round-trip ODT/DOCX fiel".
> - **Fleet:** `.cockpit/fleets/wave27-formats.yaml`.
> - **Escopo:** Fase IA (core + MCP + QA). Não mexe em `src/app/`.
> - **Executores:** DeepSeek V4.1 Flash pelo OpenRouter (agente `cockpit`) nas tarefas, porque o Grok Build está sem cota;
>   Composer na integração e no handoff; Sonnet na revisão. Primeira onda com executor do OpenRouter: o custo de cada nó
>   entra no handoff.
> - **Aprovação:** push e PR pré-aprovados. O merge só sai com `scripts/pr-merge-gate.py` limpo e as threads resolvidas.

## Diagnóstico

- **EPUB:** `document_save` escreve EPUB 3 (onda 20, fidelidade na onda 25), mas `document_open` não lê `.epub`.
- **HTML:** `export_html` usa um gerador simples em `Session::exportHtml` (`runsToHtml`): sem estilos, notas, campos
  avaliados, `lang`/`dir`, legendas nem imagens reais. O EPUB já tem um gerador XHTML completo (`src/core/Epub.cpp` sobre
  `XhtmlWriter`), que o HTML não reaproveita.
- **Notas:** um bloco `footnote`/`endnote` guarda só `runs`, ou seja, um parágrafo. Uma nota de Word ou LibreOffice com
  vários parágrafos é achatada no import, e o export nunca escreve mais de um parágrafo.

## Decisões (fechadas)

| # | Decisão |
|---|---|
| D1 | **Parágrafos de nota = U+2029.** Dentro dos `runs` de um bloco `footnote`/`endnote`, o caractere U+2029 (PARAGRAPH SEPARATOR) separa parágrafos. Não muda o schema JSON e mantém os offsets de revisões, comentários e bookmarks. `\n` continua sendo quebra de linha dentro do mesmo parágrafo. Em outros tipos de bloco, U+2029 vindo de entrada externa vira `\n`. Contagem de palavras e `plainText()` tratam U+2029 como espaço. |
| D2 | **Notas com vários parágrafos, em todos os formatos.** Import: DOCX `w:footnote`/`w:endnote` com vários `w:p`, ODT `text:note-body` com vários `text:p`, RTF `\footnote` com `\par` interno e EPUB `aside` com vários `p` viram runs separados por U+2029. Export: cada parágrafo vira o elemento próprio do formato (`w:p`, `text:p`, `\par` dentro do grupo da nota, `<p>` dentro do `aside`). O preview e o PDF pintam cada parágrafo numa linha nova. A ida e volta pelo nosso import e pelo LibreOffice (`--interop`) preserva o número de parágrafos e a formatação de cada um. |
| D3 | **MCP das notas:** `insert_footnote`/`insert_endnote` aceitam `paragraphs: [string]` (um item por parágrafo) além de `text`. `document_get` devolve a nota com o U+2029 nos runs, e `docs/mcp-tools.md` documenta isso. |
| D4 | **Um gerador XHTML para EPUB e HTML.** A renderização de blocos do EPUB sai do `Epub.cpp` para uma função compartilhada (ex.: `renderBlocksXhtml`), usada pelos capítulos do EPUB e pelo `export_html`. O EPUB não muda de comportamento: epubcheck continua com 0 erros e 0 avisos e os juízes `qa/wave20-epub*`/`qa/wave25-*` continuam verdes. |
| D5 | **`export_html` = um arquivo HTML5 autossuficiente** pelo gerador da D4: `<!DOCTYPE html>`, `<html lang>`, `<meta charset="utf-8">`, `<title>` dos metadados, o CSS dos estilos num `<style>`, estilos de parágrafo e de caractere como classes, campos avaliados, `hide_if`/`section_hidden` omitidos, `lang`/`dir`, legendas, tabelas com `colspan`/`rowspan`, listas aninhadas e notas numa `<section>` no fim, com links de ida e volta. Imagens vão embutidas como `data:` URI (o arquivo não depende de pasta ao lado). O documento é XML bem formado (HTML polyglot), para os juízes conseguirem parsear. O resultado continua `{ok, path}` (mais `warnings` quando houver), com escrita atômica (onda 23 D11). |
| D6 | **`document_open` lê `.epub`** (`EpubReader`, arquivo novo). Ordem: `container.xml` → OPF → `spine`. Cada documento de conteúdo XHTML vira blocos: `h1`–`h6` → `heading` (nível); `p` → `paragraph`; `ul`/`ol`/`li` → `list-item` (nível pelo aninhamento, `list_kind` bullet ou decimal); `table`/`tr`/`td`/`th` → `table` (`colspan`/`rowspan`, primeira linha de `th` = cabeçalho); `img` → `image` com os bytes do pacote; `figure`/`figcaption` → legenda; `strong`/`b`, `em`/`i`, `u`, `s`/`del`, `sub`, `sup`, `a href` → formatação e link do run; `lang`/`xml:lang`/`dir` → bloco; `a epub:type="noteref"` + `aside epub:type="footnote"/"endnote"` → nota com `note_id` (D1 para vários parágrafos). Metadados: `dc:title`, `dc:creator`, `dc:language`. O `nav` não vira conteúdo. Um EPUB exportado por nós volta com a mesma estrutura de blocos (texto, títulos, listas, tabelas, imagens, notas e legendas). Estilos de parágrafo vêm das classes que o nosso export gera, quando reconhecidas; CSS arbitrário de EPUBs de terceiros não é interpretado (só `font-weight`, `font-style` e `text-decoration` inline). |
| D7 | **Entrada hostil no EPUB:** valem os limites da onda 23 D12 (entrada de zip ≤ 256 MiB, total ≤ 1 GiB, imagem ≤ 32 MiB). XML com erro = arquivo inválido com mensagem (`"<arquivo>: <erro> at line N"`). `href` com `..`, caminho absoluto ou esquema (`http:`, `file:`) é ignorado com aviso. Nada é buscado na rede; `script`, `iframe`, `object` e `form` são descartados. EPUB com DRM (`META-INF/encryption.xml` cifrando conteúdo) é recusado com erro claro. |
| D8 | **Juízes:** cada decisão tem um teste que falha antes da correção, com prefixo `qa/wave27-`. EPUB: abrir um EPUB nosso e um EPUB de terceiro escrito à mão em `qa/fixtures/wave27-*.epub` (gerado por script no teste, sem binário opaco), checando a árvore de blocos. HTML: parsear o resultado como XML e conferir os elementos, as classes, as notas e a imagem `data:`. Notas: ida e volta DOCX, ODT, RTF e EPUB com 3 parágrafos e formatação diferente em cada um; com `--interop`, o LibreOffice abre o nosso DOCX e o ODT com os 3 parágrafos. A CI pode rodar como root e com outra glibc: nenhum teste depende disso. |
| D9 | Não regredir as ondas 12–26. |

## Regras de execução

- **Nós de agente:** fazem tudo no turno, sem skills nem subagentes, e nunca submetem saída provisória.
- **Build:** só o alvo do CLI no worktree isolado (o primeiro build desse alvo é inevitável); o QA completo é do gate.
- **Git:** ninguém usa `git stash`.
- **CI:** nenhum teste depende de privilégio nem de `LD_PRELOAD` herdado.

## Tarefas e roteamento

| Nó | Executor | Itens | Arquivos |
|---|---|---|---|
| `lessons` | Sonnet | PRs #16–#19 | `docs/LESSONS.md`, `.cursor/rules/lessons.mdc` |
| `t1-epub-in` | DeepSeek V4.1 Flash | D6, D7, D2 (import EPUB) | `src/core/EpubReader.{h,cpp}` (novo), abertura de `.epub` em `Session.cpp`, `CMakeLists.txt`, `docs/mcp-tools.md`, `qa/` |
| `t2-html` | DeepSeek V4.1 Flash | D4, D5, D2 (export EPUB/HTML) | `src/core/Epub.{h,cpp}`, `src/core/Xhtml.{h,cpp}`, `Session::exportHtml`, `docs/mcp-tools.md`, `qa/` |
| `t3-notes` | DeepSeek V4.1 Flash | D1, D2 (DOCX, ODT, RTF, preview/PDF), D3 | `Document.{h,cpp}` (contagem/`plainText`), `Docx.cpp`, `Odf.cpp`, `RtfReader.cpp`, `RtfWriter.cpp`, pintura de nota em `Preview.cpp`, `insert_footnote`/`insert_endnote` em `Session.cpp` e `ToolDispatch.cpp`, `docs/mcp-tools.md`, `qa/` |

A T1 e a T2 tratam o U+2029 no EPUB e no HTML com a convenção da D1; a T3 não mexe no `Epub.cpp`. A integração junta as
três (conflitos esperados em `Session.cpp`, `CMakeLists.txt` e `docs/mcp-tools.md`).

Depois das tarefas: `integrate-1` (Composer) → gate de build e QA → `t4-regress` (DeepSeek) → gate → loop de revisão
(Sonnet revisa, DeepSeek corrige) → adjudicação (Sonnet) → `qa-final` → handoff (Composer) → `notion-sync` → `open-pr`.

## Critério de pronto

- Cada decisão D1–D7 tem um teste `qa/wave27-*` que falharia sem ela.
- Um EPUB exportado por nós e um EPUB de terceiro abrem com a estrutura certa; um EPUB hostil falha com erro claro.
- `export_html` gera um HTML5 bem formado com estilos, notas, campos e imagens embutidas.
- Uma nota com 3 parágrafos faz ida e volta em DOCX, ODT, RTF e EPUB, e passa pelo LibreOffice com `--interop`.
- epubcheck 0/0; veraPDF PDF/UA-1 e PDF/A-1b conformes.
- `scripts/qa-all.sh --gui --interop --pdf --formats` passa no local e na CI.
