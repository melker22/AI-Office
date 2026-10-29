# Orquestração multi-IA (Cockpit Fleet)

O trabalho no AI Office roda como **Fleets** do AGI Cockpit (`.cockpit/fleets/*.yaml`): cada nó é
uma task real de um agente, com dependências, gates de QA e aprovação humana antes de qualquer push.

## Camadas e quando usar

| Camada | Agente / modelo na Fleet | Usar para | Nunca para |
|---|---|---|---|
| Estratégica | `claude` / `claude-opus-5-5` | Arquitetura, decisões críticas, spec da onda, auditoria final opcional | Escrever código, docs longas, testes |
| Tech Lead | `claude` / `claude-sonnet-5` | Plano de tarefa, revisão, QA, adjudicação, PR | Implementação que um executor faz |
| Executor principal | `cursor` / `grok-4.7-high` | Implementação, refatoração, debugging no core | Tarefas triviais (desperdício) |
| Executor secundário | `grok` / `grok-4.7` | Correções pós-revisão, continuidade quando o Cursor acabar | — |
| Executor barato | `cursor` / `composer-2.5` | Campos/tools MCP, round-trip de atributos, playbooks, merges, docs | Refatoração central, decisões |

Ordem de decisão: arquitetura? → Opus · análise/revisão? → Sonnet · implementação simples? → Composer ·
implementação não trivial? → Cursor Grok · Cursor sem limite? → Grok Build.

Gates de comando (`scripts/qa-all.sh`, o espelho local da CircleCI) custam **zero tokens** — sempre
preferir um gate a pedir "verifique se compila" num prompt.

## Fluxo de uma onda

1. **Opus** escreve `docs/waves/waveNN-<tema>.md`: diagnóstico, decisões fechadas (D1…), tabela de
   tarefas com executor/custo/risco/dependência, critério de pronto.
2. **Opus** escreve `.cockpit/fleets/waveNN-<tema>.yaml` a partir da tabela e valida.
3. A Fleet roda; o utilizador acompanha no side panel e só aprova o push/PR.
4. Tarefas avulsas usam a Fleet genérica `task`:

```bash
cockpit fleet run task --directory "$PWD" --arg task="Adicionar zoom na status bar" 
cockpit fleet run task --directory "$PWD" --arg task="..." --set 'implement.model=composer-2.5'   # tarefa simples
```

## Contrato de executor (obrigatório em toda Fleet)

Origem: na onda 17, (1) um executor caiu por erro de rede e a Fleet o marcou "completed" sem nenhum
commit, e (2) um nó de integração juntou uma ponta velha do branch de um executor. Nenhum gate pegou
isso porque o código antigo também passava nos testes. Regras:

1. **Todo nó executor** (implementação, correção, integração) declara `output_contract` com
   `commit` (sha de 40 hex = `git rev-parse HEAD` após o último commit), `qa_green` (bool) e
   `summary`, e `success_output: qa_green`. Executor que não submete a saída é cobrado pela Fleet e
   **falha** depois de 2 turnos corretivos — nunca vira "completed" em silêncio. `qa_green: false`
   falha o nó com o `blocker` como erro.
2. **Integração junta o hash, não o branch:** o prompt do nó de integração usa
   `{{needs.<id>.output.commit}}` (`git merge <sha>`), nunca `{{needs.<id>.branch}}`.
3. **Todo gate depois de executores verifica a entrega:** `scripts/fleet-verify-merged.sh <n> &&
   scripts/qa-all.sh …`, com os executores listados **diretamente** em `needs` do gate (para receber
   `FLEET_NEEDS_<ID>_OUTPUT`). O script falha se um commit reportado não existe, não está no HEAD
   testado, ou se vieram menos de `<n>` commits.
4. **Executores dentro de loop** (ex.: `fix-pass`): a verificação vai no `until` do loop
   (`test … -eq 0 && scripts/fleet-verify-merged.sh 1`), que recebe a saída dos nós do corpo.
   Nó opcional que pode ser pulado (ex.: adjudicação) também reporta `commit`, mas não entra no
   mínimo `<n>` — o script verifica todo commit presente.

5. **Nós isolados de fases posteriores** (ex.: `t3`/`t4` depois de `integrate-1`) nascem no commit
   base da Run, não no branch já integrado. O prompt deles começa com
   `git merge {{needs.integrate-1.output.commit}}` (e o nó lista `integrate-1` em `needs`), para
   trabalharem sobre o código integrado e reportarem um commit que já o contém. Origem: onda 18, em que
   T3/T4 só perceberam por conta própria que estavam sem os tipos de campo da fase 1.

Esqueleto para nós paralelos isolados:

```yaml
  t1:
    workspace: isolated
    prompt: |
      … Ao terminar, submeta: commit = `git rev-parse HEAD`; qa_green; summary.
    success_output: qa_green
    output_contract:
      type: object
      required: [commit, qa_green, summary]
      properties:
        commit: { type: string, pattern: "^[0-9a-f]{40}$" }
        qa_green: { type: boolean }
        blocker: { type: string }
        summary: { type: string }
  integrate-1:
    needs: [t1, t2]
    prompt: |
      git merge {{needs.t1.output.commit}} e {{needs.t2.output.commit}} …
      Submeta commit = HEAD após o merge, qa_green, summary.
    success_output: qa_green
    output_contract: { … mesmo contrato … }
  build-1:
    type: gate
    gate: command
    needs: [integrate-1, t1, t2]
    run: scripts/fleet-verify-merged.sh 3 && scripts/qa-all.sh --gui
```

## Gate de merge (obrigatório antes de todo merge)

`scripts/pr-merge-gate.py <pr> [--since <push ISO8601>]` — exit 0 = limpo, 1 = triar, 2 = pendente
(status em andamento, mesclabilidade não calculada, limite da API ou rede fora). Confere: CI e CodeRabbit em
`success` no HEAD; nenhum comentário inline novo do CodeRabbit desde o push; `Actionable comments posted: 0`
nas revisões do HEAD; `mergeable_state == clean` (relido antes do CLEAN). E **todo** achado bloqueante do corpo —
"Outside diff range", "Duplicate", "Critical", "Major" ou "Minor" (Nitpick, Trivial e Additional não
bloqueiam) — do corpo de **qualquer** revisão do PR — de qualquer commit, não só do HEAD — precisa de
um comentário humano no PR com `cr-ack <id> <commit da correção ou motivo>` (o id completo do marcador, que o gate imprime; prefixo não vale). Depois,
as threads não resolvidas são conferidas com a tool MCP do GitHub (`pull_request_read get_review_comments`).
Só então merge. Token opcional: `GITHUB_TOKEN`/`GH_TOKEN` ou `~/.config/aio-pr-watch/token`. Quando o CodeRabbit revisa sem deixar status no commit, a única prova aceita é uma revisão publicada pelo próprio bot (`coderabbitai[bot]`) no SHA do HEAD. As respostas "Review finished"/"Already reviewed the last commit" não valem: podem sair sem revisão (ex.: limite de taxa) e não dizem qual SHA foi revisado — nesse caso confira à mão.

**Esperar a CI e o CodeRabbit:** depois de um push ou de um `@coderabbitai review`, rode em segundo plano
`scripts/pr-watch.py <pr> [--since <ISO do pedido>] [--until cr|all] [--notify-task <id>]`. O processo
termina assim que há algo a fazer, e o término acorda o agente: revisão do bot no SHA do HEAD
(`cr=reviewed`), resposta "Review finished"/"Already reviewed" sem revisão (`cr=no_review` → peça
`@coderabbitai full review`), "rate limited" (`cr=rate_limited` → peça de novo depois) ou CI vermelha
(`ci=failure`). Com `--until all`, um resultado do CodeRabbit ainda espera a CI terminar. Cada consulta
manda o ETag da anterior (`If-None-Match`); com token, um 304 não gasta a cota. Sem token, o 304 conta e cada consulta faz pelo menos 6 requisições
(mais com paginação), então o intervalo é 420 s (cerca de 51 de 60 req/h por IP); com um token de leitura em `~/.config/aio-pr-watch/token` (modo 600, também usado pelo
gate), 30 s (5000 req/h). Um 403/429 de limite espera o `X-RateLimit-Reset`. Não substitui o gate: só diz
quando rodá-lo.

**Cota do CodeRabbit (plano Essentials):** o limite de revisões de PR é por hora, com janela móvel, e cai
conforme as revisões de PR dos últimos 7 dias (0–29: 5/h; 30–39: 4/h; 40–49: 3/h; 50–59: 2/h; 60 ou mais:
1/h, uma de cada vez). Cada push revisado, cada `@coderabbitai review` e cada `@coderabbitai full review`
contam 1; um push que bate no limite conta 0. Regras:

1. **Revisão local antes do push:** `coderabbit review --agent --base <base>` (CLI autenticado com a chave
   agentic; a cota do CLI é separada da de PR). Corrija os achados reais antes de empurrar.
2. **Uma revisão de PR por rodada:** junte todas as correções de uma rodada num push e peça revisão uma vez.
   Não peça `review` logo depois de um push que já dispara a revisão automática.
3. **`full review` só quando o bot respondeu sem revisar** (`cr=no_review`) e nada mais resolve.
4. **Antes de pedir revisão,** comente `@coderabbitai rate limit` (não gasta revisão; o bot diz se há
   revisão disponível e quando tentar de novo). Com `cr=rate_limited`, use essa resposta, não um palpite.
5. **Um PR em revisão por vez:** ondas e PRs de ferramenta não disputam a mesma janela.
6. **Consumo do período:** `coderabbit usage` (exige o login OAuth; com a chave agentic o relatório falha).

Origem: no PR #9 o gate antigo contava só comentários inline e o merge saiu com 2 achados do corpo da
revisão ("Outside diff range" — o GitHub não deixa postá-los inline). No PR #10 o gate já lia o corpo, mas
só das revisões do HEAD: um push logo depois de uma revisão escondeu 2 achados, porque a revisão
incremental do commit novo não os repete. Desde então todo achado de corpo bloqueante (Outside diff range,
Duplicate, Critical, Major, Minor) exige `cr-ack` com commit ou motivo — um `cr-ack <id>` sem texto não vale.

## Notion sempre sincronizado (regra do utilizador)

A documentação no Notion nunca pode ficar atrás do repositório, para outros agentes e serviços não
lerem informação velha. Toda Fleet tem um nó `notion-sync` (Sonnet, effort low, só tools do Notion,
sem código) depois do `handoff` e antes da aprovação humana: alinha o board "Roadmap para Produção" ao
`docs/WRITER_FEATURE_CATALOG.md`, atualiza o callout "Última sync" do hub e o snapshot. Trabalho feito
fora de Fleet (correções avulsas, merges) também termina com uma sincronização do Notion.

## Fallback quando o limite do Claude acaba (autorizado pelo utilizador)

Regra do utilizador: o Sonnet da Cursor é **só reserva** — cota mensal e pequena (menor que a do Grok e
a do Composer). Use-o **apenas enquanto o Claude estiver esgotado** e devolva os nós ao Claude assim que
a janela renovar (inclusive interrompendo um nó que esteja rodando na Cursor: o gasto dali em diante é o
que importa). Com o Claude disponível, nenhum nó roda no Sonnet da Cursor.

Quando a janela do Claude (5 h ou semanal) esgota, os nós `agent: claude` **não esperam o reset**:
são movidos para outro provedor até o limite voltar, sem editar o YAML:

| Nó (Claude) | Fallback | Motivo |
|---|---|---|
| revisão, adjudicação, plano, `open-pr`, `lessons` | `cursor` / `claude-sonnet-5-thinking-high` | Mesmo Sonnet 5, cota da Cursor; Cursor tem o MCP do GitHub |
| `notion-sync` | `grok` / `grok-4.7` (effort low) | A Cursor não tem o MCP do Notion; o Grok tem a credencial |
| auditoria Opus | `cursor` / `claude-opus-5-5-high` | Mesmo Opus via Cursor |

Como aplicar numa Run em andamento (o nó ativo continua; só os próximos mudam). Inclua **todo** nó
`agent: claude` que ainda não rodou — o exemplo abaixo cobre os nós das Fleets de onda:

```bash
cockpit fleet pause <runId>
S=claude-sonnet-5-thinking-high
cockpit fleet resume <runId> \
  --set "lessons.agent=cursor" --set "lessons.model=$S" \
  --set "review.review-pass.agent=cursor" --set "review.review-pass.model=$S" \
  --set "review-adjudication.agent=cursor" --set "review-adjudication.model=$S" \
  --set "open-pr.agent=cursor" --set "open-pr.model=$S" \
  --set "opus-audit.agent=cursor" --set "opus-audit.model=claude-opus-5-5-high" \
  --set "notion-sync.agent=grok" --set "notion-sync.model=grok-4.7" --set "notion-sync.effort=low"
# Fleet genérica `task`: troque/adicione `plan` e `adjudicate` no lugar de review-adjudication.
```

Mudar o agente de um nó `session: continue` cria uma task nova na próxima iteração (o prompt inicial
revisa o diff inteiro, então nada se perde). Quando o Claude voltar: `cockpit fleet pause`, `cockpit task
cancel` no nó que estiver na Cursor, e `cockpit fleet resume --unset '<nó>.agent' --unset '<nó>.model'`
para todos os nós movidos — eles voltam ao YAML (Claude).

## Limites de uso

**Guardião de uso (automático).** `scripts/usage-guard.py` roda como daemon. A instalação é feita por `scripts/usage-guard/install.sh`, que copia o script para `~/.local/share/aio-usage-guard/`, cria o serviço systemd do usuário `aio-usage-guard.service` (`Restart=always`, fora do grupo de processos do Cockpit, para um restart do Cockpit não derrubá-lo) e registra os dois hooks. O hook `app.ready` (`usage-guard-daemon`) só garante que o serviço esteja de pé; o `flock` mantém uma única instância. O que ele faz:

- Avisa uma vez quando a janela de 5 h ou a semanal do Claude, ou a cota mensal da Cursor, passa de 90%.
- O hook `task.waiting` com `waitingReason=usage_limit` (`usage-guard-record`) anota cada task parada por limite, junto com o horário de reset.
- Depois do reset, retoma o que parou por limite, inclusive o orquestrador: uma Fleet com `cockpit fleet resume`, e uma task com uma mensagem de "continue de onde parou".
- A cada 5 minutos procura Runs pausadas por limite que nenhum hook anotou.
- Também retoma uma Run cujos nós foram interrompidos só porque o Cockpit reiniciou ("did not survive the restart"), passado o período de carência.
- Nunca retoma uma Run pausada à mão nem uma task que espera por outro motivo. Uma Run com qualquer nó interrompido por outro motivo (Steer, cancelamento) também não é retomada, porque `fleet resume` religa todos os nós interrompidos; o guardião avisa e deixa a decisão para você.

Comandos:

- `python3 scripts/usage-guard.py status`: estado do daemon e o que está pendente.
- `… once --dry-run`: mostra o que faria, sem agir.
- `… --self-test`: autoteste.
- Log: `~/.local/state/aio-usage-guard/guard.log`.

Ao mudar o script no repositório, rode de novo `scripts/usage-guard/install.sh`.


Ver `cockpit usage --all`. Quando um provedor esgota, **não edite o YAML** — sobrescreva na hora:

```bash
# Cursor esgotado → executor secundário (Grok Build) para os nós do Cursor
cockpit fleet run wave16-ctl-cjk --directory "$PWD" \
  --set 't1-layout-core.agent=grok' --set 't1-layout-core.model=grok-4.7' --set 't1-layout-core.effort=high'
# Nó interrompido por limite → retomar depois do reset, ou mudar o agente
cockpit fleet resume <runId>
cockpit fleet retry <runId> --node <nó> --set '<nó>.agent=grok' --set '<nó>.model=grok-4.7'
```

Um nó parado por limite fica `interrupted` e a Run pausa — nada se perde; `resume` após o reset continua.

## Regras que as Fleets herdam

- `.cursor/rules/*.mdc` (handoff, commits em inglês, **nunca push no `main`**).
- Toda Fleet termina num gate humano antes de push/PR.
- `git diff main...HEAD` nas revisões assume o `main` local atualizado (`git pull` antes de rodar).
