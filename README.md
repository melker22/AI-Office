# AI Office

Editor de texto (Writer) com UI original em **Qt 6 / C++20**. O LibreOffice entra só como motor opcional (DOC/PDF). A IA é **a sua** — Cursor, Claude Code, Grok Build, Copilot, Gemini, ChatGPT, Kimi. Este app não vende créditos e não pede API key.

> Peça para a IA se integrar ao software para aproveitar todo o poder dele.

## O que já existe (v0.1)

- Documento semântico em JSON com **IDs estáveis** (`p1`, `h1`, `t1`…)
- Abrir/salvar **ODT**, **DOCX**, **AIO JSON**
- DOC via `soffice` se o LibreOffice estiver instalado; **PDF nativo** (QPdfWriter)
- GUI Qt Quick (placeholder até o design do UX Pilot)
- MCP (`aioffice mcp`) + CLI (`aioffice call …`)
- Socket local: com a GUI aberta, as tools editam o documento vivo

Este repositório é só o **Writer**. Planilha e apresentação serão **apps separados** (suite no estilo Microsoft Office / LibreOffice), não módulos deste binário. Reescrita em Rust: só após o C++ do Writer estar em produção.

## Design (UX Pilot)

Sistema **Tinta & Papel**. Cole [`docs/ux-pilot-prompt.md`](docs/ux-pilot-prompt.md) no UX Pilot. Página: [uxpilot.ai/a/ui-design?page=bOAKXwabCJcNcRA1DigB](https://uxpilot.ai/a/ui-design?page=bOAKXwabCJcNcRA1DigB). Qt só aplica o frame — ver [`docs/DESIGN_CHANGES.md`](docs/DESIGN_CHANGES.md).

## Continuidade (agentes)

Antes de alterar: ler [`docs/AI_HANDOFF.md`](docs/AI_HANDOFF.md). Wiki: [`docs/DOCUMENTACAO_COMPLETA.md`](docs/DOCUMENTACAO_COMPLETA.md). QA: [`docs/QA_REPORT.md`](docs/QA_REPORT.md). Notion (irmão do Rose Faxina, sem misturar): [AI Office — Documentação Técnica](https://app.notion.com/p/3cb46a79156b819daef5ea9d7c41385a).

## Build (Linux)

Precisa de Qt 6.5+, CMake 3.22+, Ninja, zlib, compilador C++20.

NixOS / flake:

```bash
nix-shell
cmake --preset linux
cmake --build --preset linux
./build/linux/aioffice
```

Outras distros: instale `qt6-base`, `qt6-declarative`, `cmake`, `ninja`, `zlib` e o mesmo preset.

## Build (Windows)

1. Instale Qt 6 (MSVC ou MinGW) e CMake + Ninja + zlib (vcpkg: `zlib`).
2. Abra o *Qt command prompt* e:

```bat
cmake --preset windows
cmake --build --preset windows
```

Defina `CMAKE_PREFIX_PATH` para a instalação do Qt se o preset não achar.

## Uso

```bash
aioffice                          # GUI
aioffice mcp                      # servidor MCP (stdio)
aioffice tools                    # lista as tools
aioffice call document_get '{}' 
aioffice new /tmp/nota.odt
aioffice get /tmp/nota.odt
```

Integração MCP: use [`scripts/aioffice`](scripts/aioffice) (wrapper para `build/linux/aioffice`) ou copie [`skills/mcp.json`](skills/mcp.json). Não dá para gravar no PATH global do NixOS daqui — no flake, aponte `command` para o wrapper.

Skill para agentes: [`skills/SKILL.md`](skills/SKILL.md). Contrato: [`docs/mcp-tools.md`](docs/mcp-tools.md). Arquitetura: [`docs/architecture.md`](docs/architecture.md).

## Licença

[Mozilla Public License 2.0](LICENSE) — a mesma licença do LibreOffice. LibreOffice, se instalado, permanece um programa MPL-2.0 separado, invocado em runtime (não há link estático).
