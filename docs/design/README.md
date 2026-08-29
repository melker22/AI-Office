# AI Office — pacote de design (UX Pilot)

**Fonte de verdade visual:** [página Tinta & Papel](https://uxpilot.ai/a/ui-design?page=bOAKXwabCJcNcRA1DigB) (`pageId`: `bOAKXwabCJcNcRA1DigB`).

O HTML exportado aqui manda sobre Fusion/Material/Kirigami. Qt Quick só aplica esse desenho. Prompt: [`docs/ux-pilot-prompt.md`](../ux-pilot-prompt.md). Implementação: [`docs/DESIGN_CHANGES.md`](../DESIGN_CHANGES.md).

## Tokens

| Token | Hex | Uso |
|-------|-----|-----|
| Accent / cobre | `#B5651D` | CTA, caret, IA conectada |
| Accent dark | `#D68A44` | Escuro |
| Paper | `#FBF7F0` / `#FDFBF7` | Folha |
| Ink | `#2A2420` | Texto UI |
| Workspace | `#E4DDD2` | Mesa |
| Dark surface | `#1D1916` | Janela escura |
| UI font | IBM Plex Sans | Chrome |
| Page font | Source Serif 4 / Liberation Serif | Só na folha |

Raio 8–12px no chrome; **0px na página**. Hit de ícone 32px.

## HTML exportado

Colocar frames em `html/` (nome estável). O mapa frame → QML vive em `DESIGN_CHANGES.md`.
