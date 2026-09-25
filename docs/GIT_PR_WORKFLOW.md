# Fluxo Git: branch + pull request (não push direto no `main`)

Guia curto para copiar para outros repositórios. Vale mesmo trabalhando **sozinho**, se o repo tem CI (CircleCI, GitHub Actions) ou review automático (CodeRabbit).

## Por que não push direto no `main`

Push no `main` faz a CI e o review rodarem **depois** que o código já é a verdade do repositório. Se quebrar, o `main` já está quebrado.

Uma pull request (PR) é um pedido: “esta branch, se estiver boa, entra no `main`”. A CI e o CodeRabbit comentam o **diff** antes do merge.

Isso não exige time. O “revisor” pode ser só o bot + o job de CI.

## O fluxo

```
main  (só recebe merge)
  └── feat/minha-coisa     ← você trabalha aqui
         └── PR → main     ← CI + CodeRabbit olham
                └── merge  ← agora sim o main anda
```

1. Atualize o `main` local (`git checkout main && git pull`).
2. Crie uma branch: `git checkout -b feat/nome-curto` (ou `fix/`, `ci/`, `docs/`).
3. Commits **nessa** branch. Mensagens em inglês, imperativo (`fix: …`, `feat: …`).
4. `git push -u origin HEAD`.
5. Abra a PR no GitHub: **base = `main`**, **compare = a sua branch**.
6. Espere o check da CI. Leia o CodeRabbit. Mais commits na **mesma** branch atualizam a PR sozinhos.
7. Merge quando estiver verde. Apague a branch se quiser.

## O que **não** fazer

- `git push origin main` com feature, CI, ou qualquer coisa que compile.
- Abrir PR do `main` para uma branch antiga só para “o bot olhar o passado”. Isso não protege o `main`.
- Criar uma branch nova a cada correção da mesma PR — continue na branch da PR.

## Exceção

Push direto no `main` **somente** se o usuário pedir explicitamente, e só para typo trivial em docs. Fora isso, PR.

## Como copiar para outro projeto

1. Copie este arquivo para `docs/GIT_PR_WORKFLOW.md` (ou equivalente).
2. Se o editor tiver regras de agente (Cursor `.cursor/rules/*.mdc`, Grok, Claude), acrescente uma regra `alwaysApply`: nunca `git push origin main`; sempre branch + PR para `main`.
3. No GitHub: Settings → Branches → proteja `main` se quiser impedir push direto (opcional, mas fecha a porta).

## Checklist para o agente / para você

- [ ] Trabalho está numa branch, não em `main`
- [ ] `git push` foi da branch
- [ ] PR aponta **para** `main`
- [ ] CI rodou nessa PR
- [ ] Merge só depois disso
