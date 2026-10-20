# Lições com evidência (harnessing entre ondas)

> Mantido pelo nó `lessons` no início de cada Fleet (ver `docs/ORCHESTRATION.md`). Inspirado no
> "harnessing-level recursion" do Harness² (google-research/harness2): o que falhou na onda N vira regra
> verificável para a onda N+1. **Só entra lição com evidência** (thread de review, commit ou incidente).
> O checklist curto que executores e revisores aplicam está em `.cursor/rules/lessons.mdc`.

Última PR processada: **#6** (onda 18; lições semeadas manualmente pelo arquiteto a partir dos PRs #2–#6).

Formato: `L<n> — regra` · onde falhou (evidência) · ocorrências.

## Código

- **L1 — Toda entrada externa é hostil.** Arquivo importado (DOCX/ODT/.aio.json), argumento MCP e
  expressão de usuário têm limite de tamanho/profundidade, tipo validado e erro tratado; nunca crash nem
  aceitação silenciosa. · PR #6 parser sem limite de recursão; PR #2 `deleted_runs` não-objeto; PR #4
  `<w:delText>` importado como texto vivo. · 3
- **L2 — Mapear por identidade, nunca por posição, entre export e import.** Índice de bloco/run muda
  quando o formato expande ou funde elementos (TOC, notas). · PR #6 `conditionals` restaurados no bloco
  errado. · 1
- **L3 — Um contrato, todos os caminhos.** Ao mudar um invariante (ex.: delete = ponto), migre TODOS os
  criadores, o reject/accept, o import e os arquivos antigos (normalizar ao carregar). · PR #4 Backspace
  ainda criava delete com largura; reject usava `end - start`. · 1
- **L4 — Correção completa, não pontual.** Valor derivado (stat, seq, xref) é recalculado num ponto
  único (leitura/salvamento), não em gatilhos por ferramenta. · Onda 18 review: `stat` só atualizava
  com `hide_if`/vars; `seq` congelado na inserção. · 2
- **L5 — Operações em vários blocos geram dados por bloco.** · PR #4 `priorRuns` só do primeiro bloco. · 1
- **L6 — Sintaxe do formato-alvo, não a nossa.** Exporte construção nativa só quando a semântica é
  idêntica no Word/LibreOffice; senão exporte o resultado avaliado. · PR #6 `IF` com identificador e
  aspas simples; PR #3 ordem `w:bidi`/`w:jc` fora do schema. · 2
- **L7 — Schema, docs e código dizem a mesma coisa.** · PR #6 `insert_field.arg` string no schema,
  objeto na doc e no código. · 1
- **L8 — Portabilidade de compilador.** Forward declaration com a mesma tag (`struct`/`class`) da
  definição. · PR #6 (MSVC C4099). · 1

## Testes

- **L9 — Teste cobre o critério inteiro, inclusive a ordem que quebra.** Ex.: inserir no meio DEPOIS de
  já existir xref, não só antes. · Onda 18 review: playbook de `seq` só testava xrefs novos. · 1
- **L10 — `expect` precisa poder falhar.** Expect vazio/nulo é erro; falha de transporte/IPC nunca
  casa com `{"ok":false}`. · PR #6. · 1
- **L11 — Ambiente hermético.** Tudo que a CI usa vem do Nix (fontes, Python, soffice opcional); nada
  do sistema do desenvolvedor. Teste com `nix-shell --pure`. · PR #3 fontes; PR #4 `python3`. · 2

## Processo (Fleets)

- **L12 — Executor entrega `commit`; integração junta o hash; gate verifica.** · Onda 17: nó caído
  marcado "completed"; merge de ponta velha. → PR #5. · 2
- **L13 — Nó isolado de fase posterior começa com `git merge` da integração anterior.** · Onda 18. · 1
- **L14 — Revisor procura defeitos da lista acima antes de estilo.** O CodeRabbit achou defeitos que
  duas/três passadas do Sonnet deixaram passar em três ondas seguidas (PR #3, #4, #6). · 3
