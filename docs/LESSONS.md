# Lições com evidência (harnessing entre ondas)

> Mantido pelo nó `lessons` no início de cada Fleet (ver `docs/ORCHESTRATION.md`). Inspirado no
> "harnessing-level recursion" do Harness² (google-research/harness2): o que falhou na onda N vira regra
> verificável para a onda N+1. **Só entra lição com evidência** (thread de review, commit ou incidente).
> O checklist curto que executores e revisores aplicam está em `.cursor/rules/lessons.mdc`.

Última PR processada: **#15** (onda 24 — fidelidade de ida-e-volta DOCX/ODT e QR decodificável). 7
threads inline em duas rodadas de CodeRabbit (todas endereçadas), mais 2 achados fora do diff no corpo
da 2ª revisão — ambos nitpick de precisão de comentário/doc, nenhum corrigido: comentário do CMake dizia
que os testes `wave17` ficavam fora do `qa-all` padrão por causa do label `wave24`, quando o filtro
`-L 'playbook|roundtrip|wave24'` já casa pelo label `roundtrip`
(https://github.com/melker22/AI-Office/pull/15#pullrequestreview-5681108253, não corrigido); e a nota de
auditoria de `qa/check_qr.py` citava o pacote Nix `zbar` em vez do executável `zbarimg` que o script
chama (mesmo review, não corrigido). Reforçou L7 (três arquivos de doc — `docs/AI_HANDOFF.md`,
`docs/audit/fix-plan.md`, `docs/waves/wave24-docx-odt-fidelity.md` — ainda diziam nível de ECC QR "L" com
o código entregando nível "M", um regex `hex6` inexistente em vez do laço de `docxHexColor`, e `bidi` só
RTL para `"1"`/`"true"` quando `bidiIsOn` também aceita vazio e `"on"`, mais o D10 da wave 24 omitindo a
dedução por contenção de intervalo além do match exato — todos corrigidos nos commits `c83b9c8`..`ac25f6e`:
https://github.com/melker22/AI-Office/pull/15#discussion_r4396808595,
https://github.com/melker22/AI-Office/pull/15#discussion_r4396808602,
https://github.com/melker22/AI-Office/pull/15#discussion_r4396808609) e abriu a lição nova L24 (geração de
id novo a partir do tamanho de uma coleção que dedup pode encolher). Também registradas, por incidente de
Fleet (não de PR): L25 (nó Claude não usa skills/subagentes/tarefas em segundo plano e não submete saída
"provisória" — Fleet wave24 Run `4712f7db`, Fleet audit Run `6fda0b72`), L26 (nó de agente não roda build
completo do zero — Fleet wave24-finish Run `a41c009a`) e L27 (ninguém usa `git stash`, compartilhada entre
worktrees — pop no checkout principal pegou a stash de um agente no worktree do PR #15). Formato:
`L<n> — regra` · onde falhou (evidência) · ocorrências.

Antes disso, **PR #13** (onda 23 — integridade, I/O hostil e transporte stdio do MCP). 15 threads
inline em três rodadas de CodeRabbit, mais 8 achados fora do diff no corpo das revisões (PR #13
pullrequestreview-5642604443/5642650466/5642889711/5643127751/5643779721) e um nitpick trivial não corrigido
(canvas.xhtml, pullrequestreview-5643827857). Reforçou L3 (`priorRuns` de revisão de formatação e remapeamento
de bookmark/xref incompletos em múltiplos caminhos de código, não só um), L4 (estatística usando `plainText()`
em vez de `statText()` para legendas), L7 (decisões "fechadas" do spec da onda 23 descreviam código já
substituído), L10/L18/L21 (gate e self-test com evidência incompleta) e abriu a lição nova L23 (checagem de
papel de documento EPUB por heurística de nome de arquivo em vez de metadado canônico do manifesto/OPF, errada
em três rodadas sucessivas). PR #11 foi revisão-only (base `audit/coderabbit-base` vs código completo) e foi
fechada sem merge — não conta como PR processado, mas os 29 achados do seu `coderabbit-pr11.json` já
alimentaram o `fix-plan.md` e as lições abaixo quando tinham evidência própria de thread. Entre #7 e #12,
quatro PRs mergearam:
- **PR #8** (fix trivial de encoding UTF-8 do banner): 1 thread menor (exemplo de XML malformado em
  `docs/AI_HANDOFF_ARCHIVE.md`), sem padrão novo.
- **PR #9** (wave 20 — EPUB 3 export e RTF nativo): 24 threads em duas rodadas. A maioria já documentada em
  `docs/AI_HANDOFF.md`; abriu/reforçou L4 (rescan de notas ainda quadrático na 2ª rodada), L6 (seis desvios
  de sintaxe RTF real) e a lição nova L20 (mutar `noteUses` durante a iteração sobre ele, ordem de nota
  dependente de hash).
- **PR #10** (correções outside-diff e gate que lê o corpo da revisão): 12 threads em duas rodadas. Reforçou
  L18 (paginação, autenticação, `mergeable` nulo, `--since` como texto, HEAD obsoleta) e L19 (tupla não
  desempacotada só aparecia no `main` real).
- **PR #12** (auditoria + gate mais rígido): 19 threads. Reforçou L10 (CLI ausente, playbook sem `expect`,
  diretório vazio, parar no primeiro `ok:true` — todos "passavam" o gate obrigatório sem testar nada) e L18
  (regex de seção/prompt casava em qualquer lugar do corpo, não só no título — achado citado dentro de outro
  achado virava limite falso); abriu a lição nova L21 (diretório de auditoria compartilhado precisa de lock
  estável e controlado pelo dono, não de caminho fixo em `/tmp` nem de variável por processo).

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
- **L3 — Um contrato, todos os caminhos.** Ao mudar um invariante (ex.: delete = ponto, escrita atômica,
  remapeamento de id), migre TODOS os criadores, o reject/accept, o import e os arquivos antigos (normalizar
  ao carregar) — e lembre que uma correção pode *destruir* o invariante em vez de só esquecê-lo num caminho:
  apagar o destino antes do rename não é "escrita atômica parcial", é nenhuma. · PR #4 Backspace ainda criava
  delete com largura; reject usava `end - start`. PR #13 (onda 23, três rodadas de CodeRabbit): `split_block`
  não dividia `priorRuns` da revisão de formatação no ponto de corte, perdendo a metade direita no reject
  (https://github.com/melker22/AI-Office/pull/13#discussion_r4370398850, corrigido em `498c96d`);
  `find_replace` dentro de uma revisão de formatação não atualizava `priorRuns`, perdendo a substituição no
  reject (https://github.com/melker22/AI-Office/pull/13#pullrequestreview-5642650466, corrigido em `5bd2d35`);
  limpar o intervalo inteiro de uma revisão de formatação deixava `priorRuns` vazio em vez de um `TextRun`
  vazio, e o reject falhava com "no prior formatting"
  (https://github.com/melker22/AI-Office/pull/13#pullrequestreview-5643127751, corrigido em `b401bfe`); o
  `expandRegexReplacement` (`\0`, `\&`, `\\`) só era aplicado numa das duas fases de `find_replace`
  (https://github.com/melker22/AI-Office/pull/13#discussion_r4370637444, corrigido em `851f607`);
  `document_include` remapeava bookmark ids nos runs mas não nas células de tabela nem em células aninhadas
  (https://github.com/melker22/AI-Office/pull/13#discussion_r4371177039, corrigido em `f5caace`/`1bec219`,
  CodeRabbit confirmou a lacuna de novo em
  https://github.com/melker22/AI-Office/pull/13#pullrequestreview-5643670952 antes do fix final); e um nome de
  bookmark importado que colide com um do host ficava sem remapear
  (https://github.com/melker22/AI-Office/pull/13#pullrequestreview-5643127751, corrigido em `7135558`);
  `QFile::rename` nunca sobrescreve destino existente, então `placeConverted` apagava o destino antes do
  rename — se o rename falhasse depois, o destino ficava sem arquivo nenhum, pior que antes da "correção
  atômica" (https://github.com/melker22/AI-Office/pull/13#discussion_r4370398863, corrigido em `53f2369`); e
  `export_text`/`export_markdown`/`export_html` com `path` ainda usavam `QFile::open(WriteOnly|Truncate)` sem
  checar o retorno de `write()`, só `saveTo` usava o helper atômico compartilhado
  (https://github.com/melker22/AI-Office/pull/13#pullrequestreview-5642604443, corrigido em `83f0b12`). · 9
- **L4 — Correção completa, não pontual.** Valor derivado (stat, seq, xref) é recalculado num ponto
  único (leitura/salvamento), não em gatilhos por ferramenta; e uma correção que só estreita o caso comum
  ainda precisa cobrir o caso em lote. · Onda 18 review: `stat` só atualizava com `hide_if`/vars; `seq`
  congelado na inserção. PR #9 1ª rodada: `resolveNotes`/`finishNote` escaneava o documento inteiro por
  nota resolvida; a correção limitou o rescan a partir do último índice resolvido, mas isso ainda é O(n²)
  quando várias notas chegam pendentes no mesmo flush — CodeRabbit reabriu o mesmo defeito na 2ª rodada
  (https://github.com/melker22/AI-Office/pull/9#discussion_r4338510187,
  https://github.com/melker22/AI-Office/pull/9#discussion_r4339255897); corrigido coletando todos os ids
  pendentes de uma vez em `resolveNotes` antes de reescrevê-los (commit `6dcc356`). PR #13: `Block::plainText()`
  era a fonte de palavras/caracteres em `document_stats`, mas também servia para outros usos que não devem
  contar legenda; a correção manteve `plainText()` só com texto do corpo e passou a estatística a usar
  `statText()`, que inclui legenda (decisão D15)
  (https://github.com/melker22/AI-Office/pull/13#discussion_r4370398847, corrigido em `c63bf75`). · 4
- **L5 — Operações em vários blocos geram dados por bloco.** · PR #4 `priorRuns` só do primeiro bloco. · 1
- **L20 — Não mute uma coleção enquanto itera sobre ela; ordem de saída que importa é construída
  explicitamente, nunca herdada da ordem de iteração de um hash.** `QHash`/`QMap` não garantem ordem
  estável, e inserir/remover durante um loop sobre o próprio container invalida iteradores. · PR #9
  `Epub.cpp::writeChapter` chamava `writeNotes(x, ctx, *ctx.noteUses)` segurando iteradores de `QHash`
  sobre o mesmo mapa que a escrita modificava, e a ordem das notas no capítulo vinha dessa iteração em vez
  da ordem de primeira referência no texto
  (https://github.com/melker22/AI-Office/pull/9#discussion_r4338510140); corrigido copiando cada vetor de
  uso antes de escrever e emitindo as notas na ordem de primeira referência (commit `90c3f5f`). · 1
- **L6 — Sintaxe do formato-alvo, não a nossa.** Exporte construção nativa só quando a semântica é
  idêntica no Word/LibreOffice; senão exporte o resultado avaliado. · PR #6 `IF` com identificador e
  aspas simples; PR #3 ordem `w:bidi`/`w:jc` fora do schema; PR #9 `RtfWriter` tratava `\u` como valor
  sem sinal (o leitor nativo exige inteiro de 16 bits com sinal), escrevia `\pngblip` para toda imagem
  (JPEG precisa de `\jpegblip`), `\picw`/`\pich` como twips em vez de pixels, não reiniciava propriedades
  de parágrafo com `\pard` em cada célula de tabela, fechava a tabela com `\pard\par` (parágrafo vazio a
  mais no reimport) e perdia a formatação de caractere do resultado de campo/hyperlink por não aplicar
  `charProps` dentro de `\fldrslt` — seis desvios da sintaxe RTF real, dois deles ainda presentes na 2ª
  rodada (https://github.com/melker22/AI-Office/pull/9#discussion_r4338510203,
  https://github.com/melker22/AI-Office/pull/9#discussion_r4338510227,
  https://github.com/melker22/AI-Office/pull/9#discussion_r4339255905); corrigidos em `90c3f5f`/`6dcc356`.
  · 3
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
  `dc:language` (https://github.com/melker22/AI-Office/pull/7#discussion_r4331347176). PR #13:
  `docs/waves/wave23-integrity.md` D7 ainda descrevia `find_replace` chamando `shiftRevisionOffsets` por
  substituição, quando o código novo planeja todos os hits primeiro e usa `revisionAcceptsReplacement`; D10
  descrevia uma realimentação de layout que o código não faz; D5 omitia `undo`/`redo`/`checkpoint`/`restore`
  da lista que `toolForbiddenInBatch` já proibia
  (https://github.com/melker22/AI-Office/pull/13#pullrequestreview-5642889711, corrigido em `fecf1fc`). PR
  #15: `docs/AI_HANDOFF.md`, `docs/audit/fix-plan.md` e `docs/waves/wave24-docx-odt-fidelity.md` diziam
  que o encoder QR usa nível de ECC "L" quando o código entrega nível "M" e reserva os padrões de função
  antes de escrever os módulos de dado
  (https://github.com/melker22/AI-Office/pull/15#discussion_r4396808595); `fix-plan.md` também descrevia a
  validação de `w:color` como um regex `hex6` inexistente em vez do laço de `docxHexColor`, e `bidi` como
  RTL só para `"1"`/`"true"` quando `bidiIsOn` também aceita vazio e `"on"`
  (https://github.com/melker22/AI-Office/pull/15#discussion_r4396808602); e o D10 da wave 24 descrevia só a
  dedução por match exato `(block_id, kind, start, end)`, omitindo que `Docx.cpp` também descarta uma
  revisão nativa cujo intervalo está contido numa revisão JSON do mesmo bloco/tipo
  (https://github.com/melker22/AI-Office/pull/15#discussion_r4396808609) — todos corrigidos nos commits
  `c83b9c8`..`ac25f6e`; e um nitpick trivial não corrigido na mesma PR: o comentário do CMake sobre os
  ctests `wave17` e a nota de auditoria citando `zbar` em vez de `zbarimg`
  (https://github.com/melker22/AI-Office/pull/15#pullrequestreview-5681108253). · 6
- **L8 — Portabilidade de compilador.** Forward declaration com a mesma tag (`struct`/`class`) da
  definição. · PR #6 (MSVC C4099). · 1
- **L24 — Id novo nunca vem do tamanho atual de uma coleção; aloque acima do maior sufixo numérico
  em uso, inclusive ids vindos de JSON.** `tamanho+1` (ou qualquer derivação de `size()`) como gerador de id colide assim que a
  coleção encolhe por dedup/remoção entre a alocação de dois ids do mesmo tipo — e colide de novo com um id
  importado de fonte externa (JSON) que já ocupava um número alto. · PR #15: `loadDocx` dava a toda marca
  nativa de revisão (`w:ins`/`w:del`/`w:rPrChange`) o id
  `QStringLiteral("rev%1").arg(doc.revisions.size() + 1)` em três pontos idênticos (delete, format, insert);
  como `flushP` removia revisões deduplicadas com `removeAt` entre uma marca e a próxima, `doc.revisions.size()`
  diminuía e o próximo id gerado colidia com um id de revisão nativa ou de JSON (ex.: `rev7`) já existente,
  fazendo accept/reject por id agir na revisão errada ou nas duas, e o re-export escrever o mesmo `w:id` em
  dois `w:ins`/`w:del` diferentes
  (https://github.com/melker22/AI-Office/pull/15#discussion_r4396808630); corrigido com `nextPrefixedId`
  (via `nextRevisionId`), que aloca acima do maior sufixo numérico já em uso, ids de JSON inclusos (commit `a718d3e`), com teste
  prévio cobrindo o caso (`qa/wave24-docx-d10-revision-ids.json`, commit `c83b9c8`, ids `rev1`/`rev2`/`rev7`
  mais marcas nativas parcialmente deduplicadas). · 1

## Testes

- **L9 — Teste cobre o critério inteiro, inclusive a ordem que quebra.** Ex.: inserir no meio DEPOIS de
  já existir xref, não só antes. · Onda 18 review: playbook de `seq` só testava xrefs novos. · 1
- **L10 — Todo `expect`/gate obrigatório precisa poder falhar de verdade.** Expect vazio/nulo é erro;
  falha de transporte/IPC nunca casa com `{"ok":false}`; e um gate de CI não "passa" quando a ferramenta
  está ausente, quando um playbook não tem `expect` nenhum, quando o primeiro resultado bom interrompe a
  checagem dos demais, ou quando o diretório de playbooks está vazio — distinga sempre falha esperada real
  de "nada rodou". · PR #6 (`expect` vazio). PR #12: o loop de `aioffice-cli play` em
  `.cockpit/fleets/audit-codebase.yaml` aceitava qualquer falha (inclusive binário ausente) como
  reprodução confirmada
  (https://github.com/melker22/AI-Office/pull/12#discussion_r4368948993, corrigido em `a3870cd`);
  `qa/audit/A-session-1-undo-path.json` e os demais playbooks de auditoria não tinham `expect`
  (https://github.com/melker22/AI-Office/pull/12#discussion_r4368965483, corrigido em `f8d2701`);
  `scripts/check-audit-playbooks.sh` saía `0` com "0 audit playbook(s) checked" quando o diretório estava
  vazio (corrigido em `af6d8c9`); e `A-docx-3-odf-9-rtf-3-write-failure-ignored.json` testava DOCX, ODT e
  RTF num só playbook, parando no primeiro `ok:true` e nunca checando os outros dois formatos
  (https://github.com/melker22/AI-Office/pull/12#discussion_r4368989107, corrigido em `af6d8c9`). PR #13:
  `qa/check_wave23_lok.py` passava quando `document_export_pdf` falhava antes de chamar o LibreOffice (destino
  inalterado + CLI com saída não-zero), sem exigir evidência de que a publicação foi de fato tentada —
  corrigido exigindo que o interposer registre uma publicação no destino
  (https://github.com/melker22/AI-Office/pull/13#discussion_r4370637434, corrigido em `9e74fdc`). · 4
- **L11 — Ambiente hermético.** Tudo que a CI usa vem do Nix (fontes, Python, soffice opcional); nada
  do sistema do desenvolvedor. Teste com `nix-shell --pure`. · PR #3 fontes; PR #4 `python3`. · 2
- **L21 — Diretório de trabalho compartilhado entre execuções usa lock com caminho estável e controlado
  pelo dono, nunca um caminho fixo em `/tmp` nem uma variável de ambiente que varia por processo.** Um
  caminho fixo em `/tmp` é alvo de symlink/TOCTOU por outro usuário local; um lock derivado de
  `XDG_RUNTIME_DIR` muda entre processos, então duas execuções tomam locks diferentes e corrompem as
  fixtures uma da outra. · PR #12: `qa/audit/setup.sh` apagava e recriava `/tmp/aio-audit` sem exclusão
  nenhuma com os playbooks que o liam (https://github.com/melker22/AI-Office/pull/12#discussion_r4368989111,
  corrigido em `af6d8c9`); o lock subsequente usava `exec 9>/tmp/aio-audit.lock`, um caminho previsível e
  symlinkável (https://github.com/melker22/AI-Office/pull/12#discussion_r4369326580, corrigido em
  `eb2d611`); e mesmo corrigido para um diretório do dono, a chave ainda vinha de `XDG_RUNTIME_DIR`, que
  varia por invocação (https://github.com/melker22/AI-Office/pull/12#discussion_r4369354620); corrigido com
  lock único em `$HOME/.cache` cobrindo `setup.sh` e todos os playbooks, mais diretório de trabalho privado
  por execução (commit `a8870a7`). PR #13: `scripts/fleet-verify-merged.sh` escrevia a saída do self-test em
  caminhos fixos `/tmp/fleet-verify-self-*.txt`; outro usuário local que criasse esses caminhos como symlink
  antes podia truncar um arquivo seu, arbitrário, pelo redirecionamento do self-test
  (https://github.com/melker22/AI-Office/pull/13#discussion_r4370398843, corrigido com `mktemp` em
  `061ea50`). · 2
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
- **L22 — Teste não depende de privilégio: a CI roda como root.** `chmod` de diretório, permissão de
  arquivo e "sem acesso" não bloqueiam root (imagem `nixos/nix` da CircleCI). Provoque a falha por um
  meio que vale para qualquer usuário — `/dev/full`, FIFO, nome acima de `NAME_MAX`, entrada malformada —
  e nunca faça o teste "recusar root" (vira CI vermelha, não proteção). · PR #13: `wave23_io_hostile`
  passava local e falhava na CI com "refusing to run as root". Idem para a glibc: um `LD_PRELOAD` de
  teste herdado por processos filhos quebrou na CI (`GLIBC_ABI_DT_X86_64_PLT`), onde `/usr/bin/env`
  usa outra glibc — o interposer remove o `LD_PRELOAD` ao carregar e o teste confere que não vazou. O mesmo
  interposer (`qa/place_convert_interpose.c`) tinha um defeito funcional independente de privilégio: ao
  interceptar `open`/`open64`/`openat`/`openat64` com `O_TMPFILE`, não repassava o argumento `mode`
  variádico ao `open` real, então todo arquivo `O_TMPFILE` criado pelo processo sob teste saía com a
  permissão errada (https://github.com/melker22/AI-Office/pull/13#discussion_r4370637431, corrigido com
  `needs_mode` em `95f66fa`). · 3
- **L23 — Classifique pelo metadado canônico, nunca por heurística de nome/substring sobre texto
  concatenado; metadado ilegível falha a checagem, não passa por omissão.** Um checker de papel de
  documento (navegação vs. conteúdo num EPUB, cabeçalho vs. corpo) que decide por regex solto sobre um bundle
  de vários XML concatenados, ou por substring no nome do arquivo ("contém `nav`"), aceita qualquer entrada
  real porque o fallback nunca é exercitado (self-test só usa fragmentos de raiz única) ou produz falso
  positivo/negativo (um conteúdo chamado `canvas.xhtml`, um TOC com sub-capítulos cujo `<ol>` aninhado é
  legítimo). Decida pelo manifesto/OPF (`properties="nav"`), com href percent-decoded e sem fragmento/query, e
  se o OPF não parsear, falhe a checagem — nunca trate "não sei" como "não é navegação". · PR #13, três
  rodadas de CodeRabbit sobre `qa/check_epub.py`: `html_has_nested_list` usava `ET.fromstring` no bundle
  inteiro do EPUB (sempre `ParseError`, sempre regex) e a regex casava `<li>` e `<ol>` através de arquivos
  diferentes, aceitando listas lineares como aninhadas
  (https://github.com/melker22/AI-Office/pull/13#discussion_r4370398840, corrigido parseando cada XHTML
  isoladamente em `e11c988`); a correção seguinte ainda incluía `nav.xhtml` na busca, cujo TOC aninhado
  mascarava a ausência de lista aninhada nos capítulos
  (https://github.com/melker22/AI-Office/pull/13#discussion_r4370637426, corrigido excluindo documentos de
  navegação em `75fc7c3`); a exclusão usava substring `"nav"` no nome do arquivo, então um capítulo de
  conteúdo chamado algo como `navigator.xhtml` seria ignorado por engano, e o self-test com `canvas.xhtml`
  não cobria essa lacuna (nitpick trivial, não corrigido:
  https://github.com/melker22/AI-Office/pull/13#pullrequestreview-5643827857); os hrefs do manifesto OPF
  precisavam de percent-decoding e remoção de fragmento/query antes de comparar com os nomes do ZIP
  (https://github.com/melker22/AI-Office/pull/13#discussion_r4370742453, corrigido em `82f8385`); e por fim,
  quando o OPF é malformado, `_nav_document_names` pulava seu manifesto e tratava a ausência de metadado como
  "nenhum documento de navegação", deixando `toc.xhtml` contar como conteúdo
  (https://github.com/melker22/AI-Office/pull/13#pullrequestreview-5643779721, corrigido falhando a checagem
  quando o OPF não parseia, em `c545057`). · 5

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
  commit (Outside diff range, Duplicate, Critical, Major, Minor), agora exige `cr-ack <id> <commit ou motivo>`.
  PR #10 também: o gate não paginava `comments`/`reviews` — um achado além da página 100 passaria
  despercebido (https://github.com/melker22/AI-Office/pull/10#discussion_r4343191716); não autenticava, e
  o limite de 60 req/h sem token é suficiente para derrubar checagens repetidas
  (https://github.com/melker22/AI-Office/pull/10#discussion_r4343191711); tratava `mergeable: null`
  (ainda calculando) como problema em vez de pendente
  (https://github.com/melker22/AI-Office/pull/10#discussion_r4343191724); comparava `--since` como texto
  em vez de instante, então um timestamp com offset diferente de `Z` ordenava errado
  (https://github.com/melker22/AI-Office/pull/10#discussion_r4343723124); e podia reportar `CLEAN` com a
  HEAD já obsoleta por não reler o PR antes do `exit 0`
  (https://github.com/melker22/AI-Office/pull/10#discussion_r4343723129) — corrigidos em
  `ee54b67`/`71d16d6`. PR #12: o regex de início de seção (`_SECTION_RE`) e o de limite do prompt
  (`_PROMPT_HEADING_RE`) casavam com qualquer trecho do corpo da revisão, não só com o título na posição
  de cabeçalho — um achado bloqueante cujo próprio texto citava `Nitpick comments (1)` ou `Prompt for all
  review comments` virava, ele mesmo, um limite de seção falso e escondia esse achado e todos os
  seguintes, em três rodadas sucessivas de revisão
  (https://github.com/melker22/AI-Office/pull/12#discussion_r4369491712,
  https://github.com/melker22/AI-Office/pull/12#discussion_r4369617528,
  https://github.com/melker22/AI-Office/pull/12#discussion_r4369645293; corrigidos ancorando o casamento
  à posição estrutural do cabeçalho, commits `bf03420`/`cf5ce76`/`cc094c4`); e `cr-ack <id>` sem commit
  nem motivo era aceito como reconhecimento válido (corrigido no mesmo PR). Regra: um gate que decide
  "limpo" só pode fazê-lo com evidência completa, autenticada, corrente (reler antes do `exit 0`) e
  ancorada em posição estrutural — nunca em substring de texto arbitrário. PR #13: `pr-merge-gate.py` tratava
  qualquer PR que listasse o commit HEAD como "revisão completa" mesmo quando esse PR era uma branch
  empilhada que só compartilha o commit, não a própria revisão
  (https://github.com/melker22/AI-Office/pull/13#discussion_r4370398845); e `head_pr_count` contava PRs
  abertos cujo branch contém o commit HEAD mas cujo próprio head é outro commit — um PR irmão inflava a
  contagem de "revisões pendentes" do PR correto
  (https://github.com/melker22/AI-Office/pull/13#discussion_r4370637439); corrigidos juntos, escopando o
  caminho "sem revisão" a reviews cujo `commit_id` é exatamente o head deste PR (`b67e915`/`fecf1fc`). · 11
- **L17 — O nó `lessons` lê handoff atual E arquivo antes de avançar o marcador.** Se a entrada de
  handoff de um PR ainda não processado já foi movida para `AI_HANDOFF_ARCHIVE.md`, ler só
  `AI_HANDOFF.md` perde o registro de intervenção e o Passo 4 marca esse PR como processado sem revisitar
  essa evidência. · PR #7 `.cockpit/fleets/task.yaml` não instruía a busca no arquivo
  (https://github.com/melker22/AI-Office/pull/7#discussion_r4331347131, corrigido em `1ae66dc` e
  replicado na instrução deste nó). · 1
- **L25 — Nó Claude não usa skills, subagentes nem tarefas em segundo plano, e nunca submete saída
  "provisória".** Um nó que roda uma etapa incompleta e ainda assim preenche o `output_contract` como se a
  etapa tivesse terminado (ex.: `issues: 0` sem ter revisado nada, `qa_green: false` como resposta
  definitiva em vez de rodar a correção e tentar de novo) corrompe a decisão dos nós seguintes do DAG, que
  não têm como distinguir "revisado e limpo" de "não revisado". · Fleet wave24 (Run `4712f7db`):
  `review-pass` submeteu `issues: 0` "provisório" e `fix-pass` submeteu `qa_green: false` "provisório";
  Fleet audit (Run `6fda0b72`). · 2
- **L26 — Nó de agente não roda build completo do zero.** O tempo de QA longo é do gate de comando
  (timeout do nó) ou de um build já aquecido (cache/`ccache`/árvore de build reaproveitada); um nó que
  decide recompilar tudo do zero gasta o próprio orçamento de tempo só na compilação e nunca chega a
  rodar o QA que deveria reportar. · Fleet wave24-finish (Run `a41c009a`): o nó `stabilize` estourou o
  tempo de um comando com um build completo do zero. · 1
- **L27 — Ninguém usa `git stash`.** A stash é compartilhada entre `main` e todos os worktrees; `stash
  pop`/`stash apply` sem filtro explícito pode restaurar o WIP de outro agente no checkout errado,
  destruindo ou misturando trabalho que não é seu. Use um commit WIP na própria branch/worktree, nunca a
  stash global. · Um `pop` no checkout principal recolheu a stash de um agente que trabalhava no worktree
  do PR #15. · 1
