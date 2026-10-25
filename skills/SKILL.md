# AI Office — skill for agents

Use this when the user asks you to write, format, or edit a document in **AI Office Writer**, or says “integra no AI Office”.

## What this software is

A local Writer (ODT/DOCX/DOC). It does **not** ship a model and does **not** take API keys. You (Cursor, Claude Code, Grok Build, Copilot, Gemini, ChatGPT, Kimi, …) are the AI. The app is a tool.

## How to attach

1. Prefer the GUI if the user has it open: tools edit the **live** document.
2. Start or configure MCP:

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

If `aioffice` is not on `PATH`, use the built binary (e.g. `build/linux/aioffice`).

3. CLI fallback: `aioffice call TOOL '<json>'` (same tools). The live GUI is used only when the per-user socket accepts a connection and `AIOFFICE_HEADLESS` is unset. If the connection drops after connect, the call is an IPC failure and does not edit a blank document. MCP stdio is one JSON object per line.

4. Call `integrate_help` once, then `document_get`.

## How to edit (precision)

- Address content by **block id** (`p1`, `h1`, `t1`, …), never by mouse or pixels.
- Workflow: `document_get` → mutate (`insert_blocks`, `apply_style`, `apply_character_format`, `set_cell`, `set_page_layout`, …) → `page_preview` to verify appearance → `document_save`.
- Unknown `after_id` fails; do not assume append.
- `apply_character_format` accepts `start`/`end` on the block text.
- Omit `id` on new blocks; the editor assigns them.
- Paper: `set_page_layout` with `size` A4|Letter, `orientation`, `margins_mm` [top, right, bottom, left].

Full contract: `docs/mcp-tools.md`.

## Formats

- Save `.odt` / `.docx` / `.aio.json` / PDF nativo without LibreOffice.
- `.doc` open still needs LibreOffice (`soffice`).

## Copy for the user

If they ask how to connect: tell them to keep AI Office open and ask you to integrate. Do not ask them for API keys or credits.
