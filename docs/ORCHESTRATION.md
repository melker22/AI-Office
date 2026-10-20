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

## Notion sempre sincronizado (regra do utilizador)

A documentação no Notion nunca pode ficar atrás do repositório, para outros agentes e serviços não
lerem informação velha. Toda Fleet tem um nó `notion-sync` (Sonnet, effort low, só tools do Notion,
sem código) depois do `handoff` e antes da aprovação humana: alinha o board "Roadmap para Produção" ao
`docs/WRITER_FEATURE_CATALOG.md`, atualiza o callout "Última sync" do hub e o snapshot. Trabalho feito
fora de Fleet (correções avulsas, merges) também termina com uma sincronização do Notion.

## Limites de uso

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
