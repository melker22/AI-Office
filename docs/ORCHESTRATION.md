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
