# AI Office Writer — documentação completa

Writer nativo (Linux + Windows v1). A IA do usuário integra-se via MCP/CLI. Este app **não** vende créditos e **não** pede API key.

> Peça para a IA se integrar ao software para aproveitar todo o poder dele.

Fonte operacional entre sessões: [`AI_HANDOFF.md`](AI_HANDOFF.md). Design: [`DESIGN_CHANGES.md`](DESIGN_CHANGES.md). Contrato de tools: [`mcp-tools.md`](mcp-tools.md).

---

## 1. Produto

- Documento semântico JSON com **IDs estáveis** (`p1`, `h1`, `t1`, `l1`, `img1`, `br1`)
- Formato nativo: `.aio.json`
- ODT/DOCX nativos (zip+xml); DOC/PDF via LibreOffice **opcional**
- GUI Qt Quick — sistema **Tinta & Papel** (UX Pilot)
- Socket vivo: `$XDG_RUNTIME_DIR/aioffice.sock`

Planilha e apresentação: **apps separados** da suite (não entram neste Writer). Este repo só edita documento de texto.

## 2. Stack

| Camada | Tech |
|--------|------|
| Linguagem | C++20 |
| UI | Qt 6.5+ Quick / QML, estilo `Basic` |
| Core | Document + Session, Zip, ODT/DOCX |
| IPC | JSON-RPC MCP stdio + Unix socket |
| CLI | `aioffice` |
| Build | CMake 3.22+, presets `linux` / `windows` |
| Nix | `shell.nix` / `flake.nix` (ibm-plex, source-serif, jetbrains-mono) |

## 3. Estrutura do repositório

| Pasta | Função |
|-------|--------|
| `src/core/` | JSON, Session, ODT, DOCX, Preview/Layout, Zip |
| `src/app/` | GUI, `AppController`, `DocumentCanvas` |
| `src/mcp/` | MCP + IPC |
| `src/cli/` | CLI |
| `src/lok/` | LibreOffice opcional |
| `skills/` | Skill + `mcp.json` para agentes |
| `qa/` | Relatórios e artefatos de regressão |
| `docs/design/html/` | Export UX Pilot |

## 4. Build (NixOS)

```bash
nix-shell
cmake --preset linux
cmake --build --preset linux
./build/linux/aioffice
```

Hyprland: classe `aioffice`; eDP-1 = workspace 1. Log Qt: `QT_FORCE_STDERR_LOGGING=1`.

## 5. Roadmap e produção

| Item | Status | Produção |
|------|--------|----------|
| JSON nativo + IDs | Done | Manter source of truth |
| MCP + socket vivo | Done | Wrapper `scripts/aioffice` |
| Shell Tinta & Papel | Partial | Frames novos portados; FileDialog só no picker do SO |
| Canvas digitável | Done | Caret/seleção no frame Tinta & Papel |
| Round-trip ODT/DOCX | Partial | table/list/image/break/page + T-styles + numbering.xml |
| Imagens embutidas | Done | `image_b64` / Pictures / word/media |
| Listas numeradas | Done | numbering.xml + text:list contínuo |
| page_preview estável | Done | Layout único, zoom 1.15, IPC até newline |
| PDF nativo | Done | QPdfWriter |
| Header/footer | Done | Inspector + folha |
| Comentários | Done | Marcas/lista + tools |
| Tabs multi-doc | Done | Snapshots por aba |
| Linux PATH | Partial | `scripts/aioffice` (sem gravar PATH global do NixOS) |
| Windows v1 | Todo | |
| macOS | Todo | Depois |
| Planilha / PPT | Todo | **Não começar** |

## 6. Referência rápida para IA

| Tarefa | Onde |
|--------|------|
| Nova tool MCP | `Session.cpp` + `ToolDispatch.cpp` + `docs/mcp-tools.md` |
| Layout / preview / PDF | `src/core/Preview.cpp` / `Layout` |
| ODT | `src/core/Odf.cpp` |
| DOCX | `src/core/Docx.cpp` |
| Chrome | `src/app/qml/` + `Ui*` |
| Tokens | `Theme.qml` |

### Glossário

| Termo | Significado |
|-------|-------------|
| **AIO JSON** | Formato nativo `.aio.json` |
| **Tinta & Papel** | Sistema visual UX Pilot |
| **LOK** | LibreOfficeKit / soffice opcional |
| **after_id** | Âncora de inserção (`start` / `end` / id) |

---

*AI Office Writer · C++20 · Qt 6 · MPL-2.0 (LO opcional, também MPL)*
