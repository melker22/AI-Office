# Onda 23 — Integridade, entrada hostil e transporte (spec arquitetural)

> Autor: Opus (Chief Architect), 2026-10-24.
>
> - **Origem:** auditoria 2026-10 (`docs/audit/fix-plan.md`).
> - **Fleet:** `.cockpit/fleets/wave23-integrity.yaml`.
> - **Escopo:** Fase IA (core + MCP + CLI + QA). Sem `src/app/`; os achados da GUI ficam no backlog da Fase Humano.
> - **Aprovação:** o utilizador aprovou as ondas 23–26, com push e PR, e merge só com `scripts/pr-merge-gate.py` limpo (ask_05e099babed7).

## Diagnóstico

Os defeitos estão provados em `docs/audit/module-sweep.md`, `docs/audit/coderabbit-history.md` e `docs/audit/coderabbit-pr11.json`. Os ids abaixo (A-*, CR-*) são os desses arquivos. Os itens mais graves são:

- O MCP stdio responde com framing `Content-Length`. Clientes MCP padrão esperam JSON por linha.
- Uma falha de IPC faz o CLI/MCP editar um documento em branco.
- O undo depois de abrir outro documento faz o próximo save sobrescrever o arquivo errado.
- Falhas de escrita são reportadas como `ok:true`.
- Um zip bomb causa OOM.
- Um XML truncado abre como sucesso.

## Decisões (fechadas)

| # | Decisão |
|---|---|
| D1 | **MCP stdio = JSON delimitado por linha** (spec MCP). Cada mensagem é escrita como JSON compacto numa linha, terminada em `\n`, sem `\n` embutido e sem cabeçalho. A leitura aceita JSON por linha. Um cabeçalho `Content-Length` legado continua sendo aceito na entrada, mas nunca é escrito. Teste de conformidade: `qa/check_mcp_stdio.py` sobe o servidor como subprocess (como um cliente faz), envia `initialize`, `notifications/initialized`, `tools/list` e `tools/call`, e faz parse estrito linha a linha. O teste entra no ctest (label `playbook`). |
| D2 | **IPC nunca cai em silêncio numa sessão local.** O CLI e o MCP só usam a sessão headless local quando `AIOFFICE_HEADLESS=1` ou quando **não há GUI**, ou seja, o socket não existe ou a conexão é recusada no `connect`. Qualquer falha depois de conectar (timeout, frame parcial, resposta inválida) devolve `{ok:false, error:"IPC failure: …"}` e não executa a tool localmente. O servidor MCP escolhe o modo (GUI ou headless) **uma vez**, no primeiro `tools/call`, e o mantém durante o processo. `aioffice new` também respeita `AIOFFICE_HEADLESS` (A-mcp-6). |
| D3 | **Socket IPC por usuário e restrito.** O nome inclui o uid (`aioffice-<uid>`) e usa `QLocalServer::UserAccessOption`. Se um servidor vivo responde ao `connect`, uma segunda GUI **não** remove o socket: ela roda sem IPC e registra um aviso. `removeServer` só é chamado para socket órfão (A-mcp-8). Um frame parcial fica no buffer até chegar o `\n` (A-mcp-4). |
| D4 | **Entrada de undo/redo = {documento, path, dirty}.** Restaurar devolve os três (A-session-1). `checkpoint`/`restore` seguem a mesma regra. |
| D5 | **`batch` não aceita tools com efeito fora da memória:** `document_save`, `document_open`, `document_new`, exports e `set_read_only`. Elas devolvem o erro `"<tool> is not allowed inside batch"` antes de qualquer passo rodar (A-session-2). |
| D6 | **Ids de revisão** = `rev<N>`, com N = (maior número já usado) + 1. Nunca `size+1` (A-session-3). Ids importados de arquivos antigos são preservados. |
| D7 | **Edições estruturais mantêm as revisões.** `split_block` move para o bloco novo as revisões que ficam depois do corte. `join_blocks` e o backspace-join rebaseiam as revisões do bloco absorvido. `find_replace` chama `shiftRevisionOffsets` a cada troca. Se uma troca cortaria uma revisão ao meio, a operação inteira falha com erro claro, sem corromper nada (A-session-4/5). |
| D8 | **`set_selection` não suja o documento.** A seleção não faz parte do conteúdo: não chama `markChanged` e devolve `dirty` real (A-session-6). `insert_table` aplica o mesmo limite 200×64 da shorthand (A-session-7). `document_include` copia revisões e bookmarks do documento incluído, com ids renumerados para não colidir (A-session-8). |
| D9 | **Export nunca altera o documento de trabalho.** `saveDocument` atualiza os campos numa **cópia** usada só para o export. Isso vale para todo export e para read-only (CR-10-113). |
| D10 | **Campos: um ponto único, em ponto fixo.** Ordem: (1) campos estruturais e `if`; (2) layout; (3) `stat` com o total real de páginas; (4) se o passo 3 mudou o texto, repete-se o layout uma vez. Campos `if` recebem o número real de páginas do layout, não `1` (CR-10-114, A-document-6). Um `if` cujo texto muda a contagem de palavras não pode oscilar: dois `document_get` seguidos sem edição devolvem o mesmo texto (A-document-5). |
| D11 | **Toda escrita de arquivo usa `QSaveFile`** (escrita atômica): JSON, DOCX, ODT, RTF, EPUB, MD, TXT, HTML, PDF, e também o caminho da conversão `.doc`. Se `write`/`commit` falha, o resultado é `{ok:false,error}` e `path`/`dirty` não mudam (A-docx-3, A-odf-9, A-rtf-3, PR #11). |
| D12 | **Limites de entrada:** cada entrada de zip pode ter no máximo 256 MiB descomprimidos e o arquivo inteiro 1 GiB; acima disso o erro é `"archive entry too large"` (A-docx-1). `text:s text:c` é limitado a 10 000 (A-odf-3). Imagens de EPUB só são lidas de arquivo regular, até 32 MiB (A-epub-1). `insert_table` já é coberto pela D8. |
| D13 | **XML com erro = arquivo inválido.** `loadDocx`/`loadOdt` verificam `hasError()` e falham com mensagem (`"document.xml: <erro> at line N"`), nunca abrem parcial (A-docx-2, A-odf-2). |
| D14 | **JSON nativo:** um item de `body` que não é objeto é erro no open (A-document-7). Ids ausentes são atribuídos **depois** de carregar o corpo inteiro, sem colidir com ids explícitos. Ids explícitos duplicados são erro no open (PR #11). |
| D15 | **Legenda e cabeçalho/rodapé:** `caption`/`caption_runs` são serializados para todo bloco com legenda, não só imagem (A-document-1). Um cabeçalho/rodapé só com `chapter` ou `image` é escrito (A-document-2). `plainText()` inclui a legenda (A-document-3). Ids sanitizados no EPUB recebem sufixo quando colidem (A-epub-6). |
| D16 | **LibreOffice:** `convertTo` converte num diretório temporário privado e só depois move o arquivo para o destino (A-mcp-3). Falha ao iniciar o `soffice` gera erro imediato, não um timeout de 120 s (A-mcp-10). |
| D17 | **CLI:** argumento JSON inválido gera erro e exit ≠ 0, e a tool não roda (A-mcp-7). `aioffice play` **não é atômico**: roda os passos em sequência e para no primeiro `expect` que falha. A documentação passa a dizer isso (A-mcp-5). O schema de `page_preview` aceita `page` inteiro ou `"all"` (A-mcp-9). |
| D18 | **QA e Fleet (T4):** os itens do bloco QA/CI/Fleet de `docs/audit/fix-plan.md`. Um checker endurecido que revela um defeito agendado para as ondas 24–26 é registrado no ctest com `DISABLED TRUE` e um comentário com o id da auditoria. A onda que corrige o defeito reabilita o teste. Nenhum checker é afrouxado. |
| D19 | **Regressão:** cada defeito corrigido ganha um playbook que falha antes da correção, com prefixo `qa/wave23-`. Os 10 playbooks de `qa/audit/` desta onda já têm o `expect` do comportamento certo e falham hoje (`scripts/check-audit-playbooks.sh`). Eles passam a `qa/wave23-*` e entram no ctest; o ambiente (`/dev/full`, fixtures) é preparado por um teste de setup do CMake, não por `qa/audit/setup.sh`. |
| D20 | Não regredir as ondas 12–22. |

## Tarefas e roteamento

Todas seguem o contrato de executor e as regras 3–5 de `docs/ORCHESTRATION.md`. A Cursor está em 73% da cota mensal; por isso o executor principal desta onda é o **Grok Build**.

| Nó | Executor | Itens | Custo | Risco | Depende |
|---|---|---|---|---|---|
| `lessons` | Sonnet | PRs #10–#12 | Baixo | — | — |
| `t1-mcp` | Grok Build 4.7 (alto) | D1–D3, D16, D17; `src/mcp/`, `src/cli/`, `src/lok/`, `qa/check_mcp_stdio.py` | Médio | **Alto** | lessons |
| `t2-session` | Grok Build 4.7 (alto) | D4–D10; `Session.{h,cpp}` (exceto escrita em `saveTo`), `FieldExpr.cpp`, refresh de campos | Alto | **Alto** | lessons |
| `t3-io` | Grok Build 4.7 (médio) | D11–D15; `Docx.cpp`, `Odf.cpp`, `RtfWriter.cpp`, `Epub.cpp`, `Zip.cpp`, `Document.cpp` (`fromJson`/`toJson`/`plainText`), escrita de `Session::saveTo` | Médio | Médio | lessons |
| `t4-qa` | Composer 2.5 | D18; `qa/`, `scripts/`, `CMakeLists.txt`, `CMakePresets.json`, `.circleci/`, `.cockpit/fleets/` antigos | Baixo | Baixo | lessons |
| `integrate-1` | Composer 2.5 | merge dos 4 hashes | Baixo | Médio | t1–t4 |
| gate `build-1` | comando | `fleet-verify-merged.sh 5 && qa-all.sh --gui --formats` | 0 | — | — |
| `t5-regress` | Grok Build 4.7 | todos os `qa/wave23-*` verdes, nenhum `qa/audit/*` desta onda sobrando, suíte completa | Médio | Médio | build-1 |
| gate `qa-full` → loop `review` (Sonnet revisa; Grok Build corrige) → `adjudicate` → `qa-final` → `handoff` → `notion-sync` → `open-pr` | — | — | — | — | — |

Depois do `open-pr`, o Opus roda o gate de merge, aplica `cr-ack` aos achados de corpo já corrigidos e faz o merge.

## Critério de pronto

- `qa/check_mcp_stdio.py` passa com o servidor real.
- Os 10 playbooks de reprodução da auditoria desta onda (A-session-1/2/3/6/7/8, A-document-1, A-docx-3, A-odf-9, A-rtf-3) estão em `qa/wave23-*`, passam e entram no ctest.
- Cada decisão D1–D17 tem pelo menos um `expect` que falharia sem ela.
- `scripts/qa-all.sh --gui --interop --pdf --formats` verde no local e na CI.
