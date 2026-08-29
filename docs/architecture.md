# Architecture — AI Office Writer

AI Office is a native **Writer**. Spreadsheet and presentation are **separate apps** in the same suite (like Microsoft Office / LibreOffice), not features of this binary. The GUI is original (Qt Quick). LibreOffice is a **runtime engine**, not a fork. The user’s AI talks to a **semantic document API** (stable block IDs), never to mouse coordinates.

## Process model

```
aioffice                 → Qt Quick GUI + IPC server (live Session)
aioffice mcp             → MCP stdio; proxies to GUI if live, else headless Session
aioffice call <tool>     → CLI; same tools as MCP
```

When the GUI is open, MCP/CLI attach via a local socket and mutate the **open document**. When it is not, they operate headless on a file path (or an in-memory document).

Linux socket: `$XDG_RUNTIME_DIR/aioffice.sock` (fallback `/tmp/aioffice-$UID.sock`).  
Windows: named pipe `\\.\pipe\aioffice`.

## Layers

| Layer | Role |
| --- | --- |
| `src/app` | QML chrome, `DocumentCanvas` (QPainter preview of the semantic model) |
| `src/core` | `Document` + `Session` ops; native `.aio.json`; ODT/DOCX zip+xml |
| `src/lok` | Discover LibreOffice; `soffice --headless` convert; optional LOK `dlopen` |
| `src/mcp` | JSON-RPC 2.0 (MCP stdio + IPC) |
| `src/cli` | `aioffice call` and friendly aliases |

## Source of truth

The in-memory `aioffice::Document` is canonical while editing. LibreOffice is used to:

- Open binary **DOC** (convert to ODT, then parse)
- Round-trip exotic DOCX/ODT features on import when `soffice` is present
- **Export PDF**
- Future: LOK tiled rendering (v1 paints the semantic model itself)

If LibreOffice is missing, the app still edits, opens, and saves **AIO JSON, ODT, and DOCX** via the built-in serializers. DOC and PDF export then return a structured error telling the user (or the AI) to install LibreOffice.

## Licenses

- Our code: **GPL-3.0-or-later** — Copyright (C) 2026 Melker Halberd Pereira Alves
- Writer feature *ideas* (not code) documented in [`docs/ATTRIBUTION.md`](ATTRIBUTION.md) (LibreOffice Writer Guide / TDF)
- LibreOffice (optional runtime): **MPL-2.0** — dynamically invoked, not statically linked
- The app never ships a model and never takes vendor API keys

## Rust

Out of scope until the C++ product is in production.

## Module map

- [`docs/mcp-tools.md`](mcp-tools.md) — tool contract (JSON IDs)
- [`docs/ux-pilot-prompt.md`](ux-pilot-prompt.md) — UI design prompt
- [`skills/SKILL.md`](../skills/SKILL.md) — what agents should read
