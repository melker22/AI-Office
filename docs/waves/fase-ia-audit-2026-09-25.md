# Auditoria da Fase IA — 2026-09-25 (Opus)

Pergunta: a Fase IA (tudo o que a IA faz via JSON+MCP, sem chrome) está completa?
**Resposta: não.** O board diz 147 Done / 8 Partial / 0 Todo, mas o código mostra itens "Done" que não
existem ou são mínimos, e 11 Todos de camada JSON+MCP estão sem Fase atribuída.

Método: board Notion "Roadmap para Produção" (SQL por Fase/Status) × código (`src/`, 133 tools MCP
registradas em `ToolDispatch.cpp`) × `docs/WRITER_FEATURE_CATALOG.md`.

## 1. Marcados Done no Notion, mas ausentes ou mínimos no código

| Item (Notion) | Realidade no código | Proposta |
|---|---|---|
| Kashida / justificação árabe | Zero ocorrências em `src/` | Onda 16 verifica o que o Qt faz ao justificar árabe → Partial ou Done honesto |
| Texto condicional | Nada (`conditional` não existe) | Onda com campos IF |
| Impressão: duplex / páginas opostas | Só intervalo, cópias e livreto 2-up; sem duplex/facing | Rebaixar para Partial; duplex é Humano (QPrinter) |
| Texto vertical CJK | Bloco rodado 90° (glifos deitados) | Rebaixar para Partial (D6 da onda 16) |
| Verificação gramatical | `grammar_check` = espaço duplo + palavra repetida | Rebaixar para Partial; motor real é decisão (LanguageTool = Java/servidor) |
| Ordem de leitura | `reading_order` é gravado mas o PDF etiquetado usa a ordem de pintura | Ligar ao tagging de PDF |

O texto "Production Action" de ~20 itens Done ainda diz "Falta …" (ex.: Âncora de imagem, Índice
alfabético, Órfãs e viúvas, Seções). Nesses o código **existe** — só o texto do Notion ficou velho.

## 2. Partial da Fase IA (8)

| Item | O que falta de verdade |
|---|---|
| CTL e CJK | Onda 16 (agendada) |
| Round-trip ODT/DOCX | `w:del`/`text:change` real, revisões de formato, notas aninhadas |
| Campos dinâmicos | IF/condicional, sender, estatísticas do documento |
| PDF/A e etiquetado | Tagging de tabelas/imagens/notas/TOC; usar `reading_order` |
| Citação CSL | Processador `.csl` genérico (sem lib C/C++ compatível) |
| Autocorrect mínimo | Parte IA feita; lista editável = Humano → **reclassificar** |
| Tools MCP para cada mutação | Notion já diz "Fase IA fechada, chrome = Humano" → **marcar Done** |
| Ortografia Hunspell | Build liga Hunspell quando existe (shell.nix tem) → só packaging → **Infra** |

## 3. Todos sem Fase, mas implementáveis 100% por IA (camada JSON+MCP, headless)

Exportação EPUB · Metadados XMP no PDF · Incorporar fontes no PDF · RTF nativo · DOC nativo ·
Assinatura digital (PDF) · Suite de round-trip (ctest) · Testes visuais golden PNG · Fuzz dos parsers.
O catálogo do repo chama esses de "Infra", mas nenhum precisa de humano nem de GUI.

Sem Fase e claramente **Humano**: Zoom da folha na GUI, Vista dividida, "Estilos só parágrafo" (falta
GUI de carácter).

## Ondas IA sugeridas depois da 16

1. **17 — Round-trip fiel:** `w:del`/`text:change`, revisões de formato, notas aninhadas + suite ctest de round-trip.
2. **18 — Campos e texto condicional:** IF, sender, estatísticas; texto/parágrafo condicional.
3. **19 — PDF completo:** tagging de tabela/imagem/nota/TOC + `reading_order` + XMP + fontes embutidas.
4. **20 — Formatos novos:** EPUB, RTF nativo (DOC nativo é caro → decisão do utilizador).
5. Honestidade do board: reclassificar os itens das seções 1–2 (tarefa barata, Composer).
