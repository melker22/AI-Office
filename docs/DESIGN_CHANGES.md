# AI Office — Tinta & Papel (UX Pilot → QML)

> **Fonte:** [UX Pilot AI Office](https://uxpilot.ai/a/ui-design?page=bOAKXwabCJcNcRA1DigB)  
> **Regra:** o frame HTML manda. Qt `Basic` + componentes `Ui*` pintam o desenho. Sem Fusion, Material, ribbon Word, chat Copilot.

## Tokens (`Theme.qml`)

Cobre `#B5651D`, papel `#FBF7F0`/`#FDFBF7`, tinta `#2A2420`, dark `#D68A44`/`#1D1916`. UI IBM Plex Sans; serif só na folha.

## Componentes QML

| QML | Papel |
|-----|--------|
| `UiButton` | Primary / secondary / ghost / destructive |
| `UiField` | Campo (sem TextField Fusion) |
| `UiCombo` | Dropdown de estilo |
| `UiSpin` | Margens mm |
| `UiTip` | Tooltip |
| `IconBtn` | Hit 32px |
| `Theme.qml` | Singleton de tokens (`accentInk`, nunca `onAccent`) |

## Mapa de telas

| Tela UX Pilot | QML |
|---------------|-----|
| Novo documento | `EmptyWelcome.qml` |
| Editor | `Main.qml` + `DocumentCanvas` |
| IA conectada | `AiIntegratePanel.qml` |
| Abrir | `OpenPanel.qml` |
| Salvar / exportar | `SaveSheet.qml` |
| Página e estilos | `Sidebar.qml` (colunas, hífen, hiperlink, campos, TOC, alterações) |
| Tabela selecionada | `TableMiniBar.qml` |
| Find / replace | `FindBar.qml` (toggle regex `.*`) |
| Componentes | `Theme.qml` + `Ui*` |

Novos frames (2026-08-28, job `wIOu49yS2pyteKKuI9cH`) em [`docs/design/html/`](design/html/README.md):

| Tela UX Pilot | QML |
|---------------|-----|
| Caret e seleção | `DocumentCanvas` (caret `#B5651D`, highlight run, I-beam) + `TableMiniBar` só com tabela do caret |
| Workspace multi-página | `StatusBarBar` prev/next; `OutlineRail`/`Sidebar` `jumpToBlock` |
| Comentários da IA | `Sidebar` lista + `comment_add`/`comment_resolve` |
| Cabeçalho / rodapé | Folha via `paintPage`; inspector Página (`headerText`/`footerText`/`pageNumbers`) |
| Tabs 3 documentos | `DocTabs.qml` ativo / inativo / ponto sujo |
| Documento não salvo | `UnsavedSheet.qml` (não `MessageDialog`) |
| Aviso LibreOffice | `LibreOfficeSheet.qml` |
| Salvar PDF nativo | `SaveSheet.qml` chip “nativo”; `.doc` “LibreOffice” |

## Copy de Integração (literal)

**Peça para a IA se integrar ao software para aproveitar todo o poder dele.**
