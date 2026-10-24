# Lições com evidência (harnessing entre ondas)

> Mantido pelo nó `lessons` no início de cada Fleet (ver `docs/ORCHESTRATION.md`). Inspirado no
> "harnessing-level recursion" do Harness² (google-research/harness2): o que falhou na onda N vira regra
> verificável para a onda N+1. **Só entra lição com evidência** (thread de review, commit ou incidente).
> O checklist curto que executores e revisores aplicam está em `.cursor/rules/lessons.mdc`.

Última PR processada: **#7** (wave 19 — PDF/A-1b, PDF/UA-1 e tagging completo). CodeRabbit abriu 12 threads:
duas eram novas instâncias de L1 (DoS por `rows`/`cols` sem checar `type == "table"`) e L12 (`lessons` fora do
`needs` de `build-1`; `review-adjudication` sem `success_output`); três eram instância de L7 (doc fora de sync
com o código — `copies` ausente, frase de tagging desatualizada, decisão D3 sobre `dc:language` invertida); e
duas abriram lições novas: L15 (parágrafo multilíngue perdia formatação de run/drop cap/ruby por redesenhar
substring em vez de reusar os glyph runs já shapeados) e L16 (`qa/check_pdf.py` e `qa/check_wave19_pdf_tags.py`
detectavam `verapdf` ausente por substring de stderr, e não falhavam quando veraPDF reportava não-conformidade).
Uma thread (L17) é específica deste nó `lessons`: procurar a entrada arquivada em `AI_HANDOFF_ARCHIVE.md`
antes de avançar este marcador — já corrigido em `.cockpit/fleets/task.yaml` (commit `1ae66dc`) e replicado
nesta própria instrução de tarefa.

Formato: `L<n> — regra` · onde falhou (evidência) · ocorrências.

## Código

- **L1 — Toda entrada externa é hostil.** Arquivo importado (DOCX/ODT/.aio.json), argumento MCP e
  expressão de usuário têm limite de tamanho/profundidade, tipo validado e erro tratado; nunca crash nem
  aceitação silenciosa. · PR #6 parser sem limite de recursão; PR #2 `deleted_runs` não-objeto; PR #4
  `<w:delText>` importado como texto vivo; PR #7 `Block::fromJson` aplicava o atalho numérico de
  `rows`/`cols` a qualquer bloco (até sem `type`), permitindo alocar ~10¹⁰ células a partir de
  `document_open`/`insert_blocks`/`replace_range`
  (https://github.com/melker22/AI-Office/pull/7#discussion_r4331347217). · 4
- **L2 — Mapear por identidade, nunca por posição, entre export e import.** Índice de bloco/run muda
  quando o formato expande ou funde elementos (TOC, notas). Identidade por fingerprint sozinha ainda não
  basta: inclua tipo de bloco e número de ocorrência, senão uma entrada de TOC com o mesmo texto do
  heading (ou um parágrafo repetido) rouba o campo. · PR #6 `conditionals` restaurados no bloco errado
  (commit `98194e3`); PR #6 2ª rodada CodeRabbit — fingerprint sem tipo/ocorrência ainda confundia TOC
  com heading (https://github.com/melker22/AI-Office/pull/6#discussion_r4329241338, corrigido em
  `0f4bcae`). · 2
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
- **L15 — Caminho especial de desenho reusa o layout já shapeado, nunca redesenha substring plano.**
  Quando um requisito extra (span de idioma, marcação) exige tratar um trecho do texto à parte, desenhe
  os glyph runs que o `QTextLayout`/`QTextLine` já calculou para aquele trecho; reshapear um substring
  com fonte/pen do bloco descarta formatação de run (negrito, cor, tamanho, sublinhado), espaçamento de
  justificação e a ordem bidi, e pula caminhos que fazem `return` antecipado (drop cap, ruby). · PR #7
  parágrafo multilíngue perdia negrito/cor/drop cap/ruby no span de idioma estrangeiro, corrigido
  desenhando `QTextLine::glyphRuns` em vez de `pdfDrawText` num substring
  (https://github.com/melker22/AI-Office/pull/7#discussion_r4331347223, commit `36ea315`). · 1
- **L7 — Schema, docs e código dizem a mesma coisa.** Isso inclui prosa descritiva, não só tipos: uma
  frase ou decisão "fechada" que descreve um comportamento antigo (já substituído) induz a próxima onda
  a reintroduzir o bug. · PR #6 `insert_field.arg` string no schema, objeto na doc e no código; PR #7
  `docs/mcp-tools.md` sem `copies` em `document_export_pdf`
  (https://github.com/melker22/AI-Office/pull/7#discussion_r4331347155); PR #7 frase de tagging ainda
  descrevia o fallback da wave 15 (`/Part`), omitindo `/Table`/`/Figure`/`/Note`/`/TOC` da wave 19
  (https://github.com/melker22/AI-Office/pull/7#discussion_r4331347162); PR #7 `docs/waves/wave19-pdf.md`
  D3 dizia que o idioma ia para o XMP quando na verdade só vai para `/Lang` do catálogo — PDF/A-1 proíbe
  `dc:language` (https://github.com/melker22/AI-Office/pull/7#discussion_r4331347176). · 4
- **L8 — Portabilidade de compilador.** Forward declaration com a mesma tag (`struct`/`class`) da
  definição. · PR #6 (MSVC C4099). · 1

## Testes

- **L9 — Teste cobre o critério inteiro, inclusive a ordem que quebra.** Ex.: inserir no meio DEPOIS de
  já existir xref, não só antes. · Onda 18 review: playbook de `seq` só testava xrefs novos. · 1
- **L10 — `expect` precisa poder falhar.** Expect vazio/nulo é erro; falha de transporte/IPC nunca
  casa com `{"ok":false}`. · PR #6. · 1
- **L11 — Ambiente hermético.** Tudo que a CI usa vem do Nix (fontes, Python, soffice opcional); nada
  do sistema do desenvolvedor. Teste com `nix-shell --pure`. · PR #3 fontes; PR #4 `python3`. · 2
- **L16 — Script de QA que chama ferramenta externa propaga a falha dela, não só imprime.** Passe
  `check=False` explícito em todo `subprocess.run` (nunca dependa do default) e trate o `returncode`/saída
  à mão; detecte "ferramenta ausente" com `shutil.which`/`FileNotFoundError`, nunca procurando "not found"
  no stderr (combina com diagnóstico normal da ferramenta); e se o resultado reportar não-conformidade,
  o script levanta/sai não-zero — nunca imprime e segue com `exit 0`. · PR #7 `qa/check_pdf.py` confundia
  "verapdf ausente" com qualquer stderr contendo "not found"
  (https://github.com/melker22/AI-Office/pull/7#discussion_r4331347189); PR #7
  `qa/check_wave19_pdf_tags.py` imprimia o resultado do veraPDF UA-1 e saía `ok`/0 mesmo não-conforme
  (https://github.com/melker22/AI-Office/pull/7#discussion_r4331347201). · 2
- **L19 — Autoteste exercita o ponto de entrada real, não só as funções puras.** Um `--self-test` que
  chama `evaluate()` com dados falsos passa mesmo com `main()` quebrado; inclua pelo menos um caso de
  ponta a ponta pelo `main` com a rede/IO falsos. · PR #10: o refactor do `scripts/pr-merge-gate.py`
  passou no autoteste e no QA 75/75, mas o caminho real lançava `TypeError` (tupla não desempacotada) e
  só apareceu no primeiro uso de verdade. · 1

## Processo (Fleets)

- **L12 — Executor entrega `commit`; integração junta o hash; gate verifica.** Todo nó que comita —
  inclusive opcional/auditoria — entra em `needs` do gate seguinte e tem `output_contract`/
  `success_output`; senão o gate não enxerga o commit e um nó que para sem commitar ainda passa por
  "completed". · Onda 17: nó caído marcado "completed"; merge de ponta velha → PR #5; PR #6 `qa-final`
  não listava `review-adjudication`/`opus-audit` em `needs` e `opus-audit` não tinha `output_contract`
  (https://github.com/melker22/AI-Office/pull/6#discussion_r4328866979); PR #7 `build-1` não listava o nó
  `lessons` em `needs` apesar de ele comitar
  (https://github.com/melker22/AI-Office/pull/7#discussion_r4331347135); PR #7 `review-adjudication`
  comita e pode retornar `verdict: failed`, mas não tinha `success_output`, então um veredito falho não
  parava `qa-final`/`handoff`/`notion-sync`
  (https://github.com/melker22/AI-Office/pull/7#discussion_r4331347151). · 5
- **L13 — Nó isolado de fase posterior começa com `git merge` da integração anterior.** · Onda 18. · 1
- **L14 — Revisor procura defeitos da lista acima antes de estilo.** O CodeRabbit achou defeitos que
  duas/três passadas do Sonnet deixaram passar em três ondas seguidas (PR #3, #4, #6). · 3
- **L18 — O gate de merge lê o corpo da revisão, não só os comentários inline.** O CodeRabbit põe no
  corpo ("Outside diff range comments") o que o GitHub não deixa postar inline; contar só inline deixou
  passar 5 achados reais. Usar `scripts/pr-merge-gate.py`. · PR #9 (2 achados: undo do título,
  exportação read-only), PR #9 1ª rodada (contagem dupla de notas no check_epub), PR #6
  (`document_stats` com blocos ocultos), PR #4 (catálogo). PR #10: o gate lia só as revisões do HEAD e
  um push logo após a revisão de `343e3c5` escondeu 2 achados de corpo (export read-only altera o
  documento; `stat pages` calculado antes de atualizar campos `if`) — todo achado de corpo, de qualquer
  commit, agora exige `cr-ack`. · 7
- **L17 — O nó `lessons` lê handoff atual E arquivo antes de avançar o marcador.** Se a entrada de
  handoff de um PR ainda não processado já foi movida para `AI_HANDOFF_ARCHIVE.md`, ler só
  `AI_HANDOFF.md` perde o registro de intervenção e o Passo 4 marca esse PR como processado sem revisitar
  essa evidência. · PR #7 `.cockpit/fleets/task.yaml` não instruía a busca no arquivo
  (https://github.com/melker22/AI-Office/pull/7#discussion_r4331347131, corrigido em `1ae66dc` e
  replicado na instrução deste nó). · 1
