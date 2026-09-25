# AI Office

**Writer nativo.** A IA é a tua — Cursor, Claude Code, Grok, Copilot, Gemini, ChatGPT, Kimi. Este programa não vende créditos e não pede API key.

[![GPL-3.0](https://img.shields.io/badge/licença-GPL--3.0--or--later-B5651D?style=flat-square)](LICENSE)
[![C++20](https://img.shields.io/badge/C++-20-2A2420?style=flat-square)](#build)
[![Qt 6](https://img.shields.io/badge/Qt-6-B5651D?style=flat-square)](#build)
[![MCP](https://img.shields.io/badge/MCP-nativo-2A2420?style=flat-square)](#a-tua-ia)
[![Linux + Windows](https://img.shields.io/badge/plataforma-Linux%20%2B%20Windows-B5651D?style=flat-square)](#build)

![AI Office — Tinta e Papel](docs/github/banner.svg)

> Peça para a IA se integrar ao software para aproveitar todo o poder dele.

A fonte da verdade é um documento `.aio.json` com **IDs estáveis** (`p1`, `h1`, `t1`…). A IA aponta para esses IDs. Não há chat Copilot dentro da app, nem ribbon Word, nem botão Compartilhar.

Este repositório é **só o Writer**. Planilha e apresentação serão programas à parte, quando o Writer round-tripar ODT/DOCX com qualidade.

---

## O que já faz

- Abrir e gravar **ODT**, **DOCX** e JSON nativo
- **PDF** nativo (QPdfWriter); DOC antigo via LibreOffice se existir `soffice`
- Layout de página no sítio: estilos, tabelas, notas, campos, sumário, imagens, formas
- **MCP** (`aioffice mcp`) e **CLI** (`aioffice call …`) com o mesmo contrato `{ok:false,error}`
- Com a GUI aberta, as tools editam o documento **vivo** (socket local)

Ainda é v0.1: o chrome Tinta & Papel está a ser pintado por cima do Qt; o Writer ainda não está “pronto a mostrar ao mundo”. O motor JSON + MCP já é o sítio onde a tua IA trabalha.

## Design

Sistema **Tinta & Papel** (UX Pilot). Cobre `#B5651D`, papel `#FBF7F0`, tinta `#2A2420`. Qt `Basic` e componentes `Ui*` — a cara padrão do Qt não deve vazar.

Frame e tokens: [`docs/DESIGN_CHANGES.md`](docs/DESIGN_CHANGES.md). Prompt UX Pilot: [`docs/ux-pilot-prompt.md`](docs/ux-pilot-prompt.md).

## A tua IA

1. Preferir a GUI aberta: as tools mexem no documento actual.
2. MCP:

```json
{
  "mcpServers": {
    "ai-office": {
      "command": "aioffice",
      "args": ["mcp"]
    }
  }
}
```

Se `aioffice` não estiver no `PATH`, aponta o `command` para `./build/linux/aioffice` ou para o wrapper [`scripts/aioffice`](scripts/aioffice).

3. CLI: `aioffice call document_get '{}'` (as mesmas tools).
4. Fluxo: `integrate_help` → `document_get` → editar por **id** → `page_preview` → `document_save`.

Skill: [`skills/SKILL.md`](skills/SKILL.md). Contrato: [`docs/mcp-tools.md`](docs/mcp-tools.md).

## Build

Qt 6.5+, CMake 3.22+, Ninja, zlib, C++20.

**Linux (NixOS):**

```bash
nix-shell
cmake --preset linux
cmake --build --preset linux
./build/linux/aioffice
```

Noutras distros: `qt6-base`, `qt6-declarative`, `cmake`, `ninja`, `zlib`, o mesmo preset.

**CI:** [`.circleci/config.yml`](.circleci/config.yml) — `nix-shell` pinado em `flake.lock` (o mesmo Qt local e na CI), builda `aioffice-cli` (sem QML) e corre os `qa/wave*.json` excepto o template `qa/wave1-foundation.json`. [CircleCI](https://app.circleci.com/pipelines/github/melker22/AI-Office).

**Windows:** Qt 6 (MSVC ou MinGW), CMake, Ninja, zlib (`vcpkg`). No *Qt command prompt*:

```bat
cmake --preset windows
cmake --build --preset windows
```

## Uso rápido

```bash
aioffice                          # GUI
aioffice mcp                      # servidor MCP (stdio)
aioffice tools                    # lista as tools
aioffice call document_get '{}'
```

## Documentação

| | |
|---|---|
| Continuidade entre agentes | [`docs/AI_HANDOFF.md`](docs/AI_HANDOFF.md) |
| Arquitectura | [`docs/architecture.md`](docs/architecture.md) |
| Wiki técnica | [`docs/DOCUMENTACAO_COMPLETA.md`](docs/DOCUMENTACAO_COMPLETA.md) |
| Ideias de Writer (não código) | [`docs/ATTRIBUTION.md`](docs/ATTRIBUTION.md) |

Hub Notion (interno): [AI Office — Documentação Técnica](https://app.notion.com/p/3cb46a79156b819daef5ea9d7c41385a).

## Licença

Copyright (C) 2026 Melker Halberd Pereira Alves

[GNU GPL v3](LICENSE) (ou, à tua escolha, qualquer versão posterior). Software livre: podes copiar, modificar e usar noutros projectos, desde que esses também sejam livres sob a GPL. O aviso de copyright (este nome) tem de ficar.

O LibreOffice, se estiver instalado, continua um programa **MPL-2.0** à parte (`soffice` em runtime — sem link estático).
