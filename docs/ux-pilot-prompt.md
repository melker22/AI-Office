# UX Pilot prompt — AI Office Writer

Copy everything below the line into UX Pilot (desktop product, not a website). Generate **light and dark** for each screen. Target **1440×900** as the default frame and **1920×1080** as a second breakpoint. Do **not** imitate Microsoft Word’s ribbon, Fluent icons, or LibreOffice’s classic toolbars. This is an original desktop product.

---

## Product

**AI Office** is a native desktop word processor (Linux + Windows v1) whose real product is not “another Word”: it is a Writer whose **entire feature set is usable with precision by the user’s own AI** (Cursor, Claude Code, Grok Build, Copilot, Gemini, ChatGPT, Kimi, etc.). The app **does not sell AI credits, does not bundle a model, and has no API-key settings**. The user already has an AI. They ask that AI to integrate with AI Office. The editor is a tool.

**This screen set is Writer only** (spreadsheet and presentation come later — do not design them).

### Tech stack (for visual language, not for drawing IDE chrome)

- Language: **C++20**
- UI framework: **Qt 6.8+ / Qt Quick (QML)** — native desktop window, not a browser
- Document engine (invisible): LibreOfficeKit / LibreOffice for DOC, DOCX, ODT, PDF
- Agent surface (invisible): MCP + CLI `aioffice`
- Platforms: Linux and Windows (desktop window with shadow, not a mobile web mock)

Design for **QML Controls + custom components**: crisp vectors, 8px grid, no CSS-only tricks that Qt cannot do (no backdrop-filter stacks, no CSS grid masonry). Prefer solid fills, 1px hairline strokes, modest shadows, and readable type.

## Design principles

1. **Original identity.** Do not copy Office 365, Google Docs, Apple Pages, or LibreOffice. Invent a distinct layout grammar.
2. **The page is the hero.** The A4 (or Letter) sheet sits on a calm workspace. Chrome is secondary.
3. **AI is a capability, not a chatbot with tokens.** There is **no** credit counter, **no** “Ask Copilot” prompt box that sends to a vendor, **no** model picker. Integration is a **status + instruction panel**.
4. **Precision typography.** The canvas must look like a real document: margins, running styles, heading hierarchy, justified body text.
5. **Dense but calm.** Desktop productivity density (toolbars, ruler, status) without visual noise.
6. **Bilingual-ready layout.** UI copy in **Brazilian Portuguese** as primary; leave ~20% extra width in buttons vs English so localization does not break.

## Brand (create a complete original palette)

Do **not** use Microsoft blue (`#2B579A`, `#185ABD`) or LibreOffice green (`#18A303`, `#00A500`). Propose an original palette named in the file.

Direction for the designer (you may refine, not ignore):

- **Ink / paper** metaphor: warm paper in light mode, deep ink in dark mode.
- **Accent:** a single non-blue, non-green accent (suggested family: copper, amber, or terracotta) used for primary actions, the insertion caret, and “AI connected” state.
- **Neutrals:** 7-step scale for surfaces, hairlines, and muted text.
- **Semantic:** success, warning, destructive — used rarely (save failure, unsaved, disconnect).
- **Type:** a distinctive UI sans for chrome; a classical serif **only on the document page** (the page preview), never in the toolbar. Suggest pairings that exist as free fonts (e.g. UI: something like Inter / IBM Plex Sans / Source Sans; page: Liberation Serif / Source Serif / Fraunces). Qt will load them.

Radius: 8–12px on panels and buttons; **0px on the document page** (paper is rectangular). Hairlines 1px. Toolbar icon size 18–20px optical, hit target 32px.

## Window chrome

Desktop application window:

- Custom or native title bar is OK; if custom, include window controls on the **right** (Windows convention) and still look correct on Linux.
- App mark + document title + unsaved dot in the title area.
- No browser URL bar. No tabs of websites. **Document tabs** are allowed (one tab per open file) — design 1 tab and 3 tabs.
- Light / dark toggle lives in the status bar or a quiet header control — not a huge switch.

## Information architecture

```
[ Title bar: mark · filename · window controls ]
[ Compact command bar — NOT a Microsoft ribbon ]
[ Horizontal ruler ]
[ Left optional outline rail (collapsible) |  Page workspace (scroll)  |  Right inspector ]
[ Status bar: page n/m · words · language · AI status · zoom ]
```

The **right inspector** has two stacked regions:

1. **Documento** — outline / styles / page setup (tabs or segmented control).
2. **Integração** — the AI integration panel (always visible, never a hidden “AI chat”).

## Screens to generate

Generate each as a full desktop window. Light + dark.

### 1. Empty / first launch — “Novo documento”

No file open (or a blank untitled page). Workspace shows a pale desk and a faint A4 sheet with a blinking caret.

Center-left of the empty sheet: a quiet welcome (not a modal):

- Title: **AI Office**
- Subtitle: **Writer**
- Primary button: **Novo documento**
- Secondary: **Abrir arquivo**
- Tiny text: `ODT · DOCX · DOC · PDF`

Right inspector **Integração** in the **disconnected** state (see component below). This is the most important empty state in the product.

### 2. Editor — document in progress (primary screen)

A real multi-page article on screen (Portuguese dummy content is fine), e.g. a short essay with:

- Title (large)
- Heading 1, Heading 2
- Body justified, a **bold** phrase, an *italic* phrase
- A bulleted list (3 items)
- A 3×3 table with header row
- An image placeholder with caption

Command bar shows active styles (Heading 1 highlighted when caret is in a heading). Ruler shows margins. Status: `Página 1 de 2 · 428 palavras · pt-BR · IA desconectada · 110%`.

Right inspector: Outline of the current headings; Styles list (Normal, Título, Heading 1–3, Citação); Integração disconnected.

### 3. Editor — AI connected

Same as screen 2, but Integração is **connected**:

- Status pill: **IA conectada** (accent)
- Short text: the agent can use every Writer function through MCP/CLI
- No chat transcript. No token meter. Optional: last tool name in muted type, e.g. `insert_blocks · há 2s` — this is telemetry of *the user’s AI*, not our chatbot.

The document should look slightly “just written” (caret at end of a new paragraph) to imply the AI acted.

### 4. Open file

A native-feeling file dialog is **not** required as a separate artboard if it would look like the OS. Instead: an in-window **Abrir** overlay/panel with recent files (3 items: `.docx`, `.odt`, `.doc`) and a drop zone “Solte o arquivo aqui”. Formats listed: ODT, DOCX, DOC, RTF, AIO (JSON nativo).

### 5. Save / Export

Compact modal or side sheet: destination, format chips (`ODT`, `DOCX`, `DOC`, `PDF`, `AIO JSON`), primary **Salvar**. PDF is an export, visually equal but labeled **Exportar PDF**.

### 6. Styles & page setup (inspector focused)

Right inspector tab **Página**: paper size A4/Letter, orientation, margins (4 fields, mm). Tab **Estilos**: list with type preview (Aa) and a **Novo estilo** action. Do not design a 1998 style dialog with 12 tabs.

### 7. Table selected

The 3×3 table is selected. A floating or inline **table mini-bar** appears near the table (rows/cols, header row, cell align) — original, not Excel-like. Command bar does not turn into a second ribbon.

### 8. Find / replace

A slim find bar docked under the command bar (not a giant dialog): field, next/prev, replace, replace all. Keyboard-hint `Ctrl+F`.

## Components (design system sheet)

Create one **components** artboard with:

- App mark / wordmark **AI Office** (Writer)
- Window buttons
- Command bar icon buttons: New, Open, Save, Undo, Redo, Bold, Italic, Underline, lists, align L/C/R/J, Heading dropdown, font size stepper, insert table, insert image, insert break
- Segmented control (inspector tabs)
- Buttons: primary, secondary, ghost, destructive
- Inputs, dropdown, numeric (mm)
- Document tab (active/inactive/unsaved)
- Page thumbnail / A4 sheet with drop shadow
- Horizontal ruler with margin handles
- Outline row (H1/H2/H3)
- Style row with live preview
- Status bar parts
- **Integração panel — disconnected**
- **Integração panel — connected**
- Toasts: saved, opened, error opening `.doc` without LibreOffice
- Empty drop zone

### Integração panel (copy — use verbatim)

**Disconnected**

- Title: **Integração com IA**
- Body: **Peça para a IA se integrar ao software para aproveitar todo o poder dele.**
- Helper: `Cursor · Claude Code · Grok Build · Copilot · Gemini · ChatGPT · Kimi`
- Secondary control: **Copiar instrução** (copies a short prompt for the user’s AI). Do **not** add API key fields, vendor logos as login, or a prompt textarea that “sends” anywhere.

**Connected**

- Title: **IA conectada**
- Body: A IA do usuário pode ler e editar este documento com IDs, estilos e layout — sem créditos deste aplicativo.
- Muted: `MCP · cli aioffice`

## Do not

- Do not draw a Copilot-like chat column with suggested prompts and a credit pack.
- Do not clone the Microsoft ribbon (Home / Insert / Layout tabs with huge icon grids).
- Do not use LibreOffice’s 2000s toolbar soup or galaxy-style start center.
- Do not design spreadsheet, slides, mail merge, or a web landing page.
- Do not add onboarding that asks for OpenAI/Anthropic/xAI keys.
- Do not use stock “AI sparkle on every button”. At most **one** small mark on the Integração panel.

## Deliverables

1. Light + dark for screens 1–8
2. Components sheet
3. A simple 8px spacing and type scale note (Qt-friendly: 11/13/14/16/20/28)
4. Named color tokens

Export frames that a developer can implement in **Qt Quick QML** (auto layouts ≈ Column/Row/anchors, not CSS Grid).
