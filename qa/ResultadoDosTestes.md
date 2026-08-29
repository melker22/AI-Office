# Resultado dos testes — AI Office Writer 0.1.0

**Data:** 29 de agosto de 2026 (noite)  
**Antes:** QA da manhã (28–29/08)  
**Como:** GUI viva + `aioffice call` (42 tools). Integração primeiro, depois hunt adversarial.  
**Artefatos:** `qa/artifacts-2026-08-29-hunt/` · `qa/hunt-2026-08-29.json`

Integração: documento **Integração Grok Build** (tabela com cabeçalho certo, search `socket local` em `p2` 82–94). Preview 115 KB. Screenshot: `artifacts-2026-08-29-hunt/`.

Hunt: 79 checagens → **75 OK, 4 falhas duras**, mais vários “aceita lixo” que o harness marcou OK mas são bugs.

Planilha e PPT **não** fazem parte deste app (suite em apps separados).

---

## Veredito

O Writer **passou na integração** e as correções da tarde **seguram** no caminho feliz:

- `header_texts` na linha 0 + `cells` a partir da linha 1  
- Markdown `**bold**` / `*italic*`  
- Template `name` ou `template`, `force`, título `Carta`, `fields` (`Mineiros`, `Sterfanny`)  
- `insert_at_caret` **substitui** o range (`abc DEF ghi` → `abc XYZ ghi`)  
- `apply_character_format` start/end só no meio (`CDE` bold)  
- `document_diff` vs save (`added: [p2]`)  
- Round-trip JSON/ODT/DOCX com tabela+lista+imagem+quebra+Letter paisagem+B/I/U  
- PDF nativo, unicode no filename, JPEG mime  

Ainda dá para quebrar: **imagem fantasma**, **tabela 0×N**, **preview em branco** em texto longo, **largura de imagem no DOCX**, validação frouxa, highlight da seleção no lugar errado.

---

## Integração (ok)

Tools pedidas: `document_get`, `insert_blocks`, `apply_style`, `set_page_layout`, `page_preview`, `set_cell`/`insert_table`, `search`, `selection_get`. Sem API key. Painel **IA conectada**.

Tabela viva: row0 `Tool | Papel`, row1 `document_get | …`.

---

## Regressões que **fechou** (não reabrir)

| Bug de ontem | Hoje |
| --- | --- |
| `cells` comia `header_texts` | row0 = cabeçalho |
| `**bold**` literal | parse ok + nested list level 2 |
| template só `template`, sem force, título `carta` | `name` funciona, force, título humano, fields |
| caret furava o título | range substituído |
| format range pintava o bloco | só `CDE` |
| diff vazio | `added: ['p2']` |
| width_mm ODT 50→80 | ODT/JSON guardam 40 |
| listas 1.1.1 | 1.2.3 (já ontem à noite) |

---

## Bugs novos / que ainda pegam

### P1

#### 1. `insert_image` de arquivo inexistente **não erra**

`path: /no/such/file.png` → `{ok:true, ids:['img1']}`. Bloco `image` com `image_b64` vazio. Preview/PDF mostram caixa vazia. Tem que ser `{ok:false}`.

#### 2. `delete_col` zera a tabela

Dá para apagar **todas** as colunas. Resultado: `"rows": [[], []]`. Preview ainda `ok` (PNG ~10 KB). `delete_row` até 0 linhas também passa. Falta um mínimo 1×1.

#### 3. `page_preview` em branco de novo (texto longo)

Parágrafo unicode longo (`ação 中文 🎉` × 400): `pages=2` mas PNG **7370 B** — página creme. Mesmo sintoma de 28/08. Tabela 40×12: PNG 9828 B, suspeito. Caminho feliz (integração) rasteriza; **carga grande falha calado**.

#### 4. DOCX perde `width_mm`

JSON/ODT: 40. DOCX reaberto: **80** (default).

#### 5. Highlight da seleção no glyph errado

`search "socket local"` + `set_selection` p2 82–94. A GUI pinta **“GUI”** de roxo, não “socket local”. Offsets da tool certos; o canvas mapeia mal.

### P2 — aceita lixo / UX

| ID | O quê |
| --- | --- |
| 6 | `set_page_layout` aceita `size=Legal`, `orientation=upside`, `margins_mm=[-10,0,5000,3]` |
| 7 | `apply_style` estilo inexistente (`NaoExiste`) grava no bloco |
| 8 | `create_style` com `size_pt=-3` e `color=not-a-color` |
| 9 | `insert_list kind=square` → `ok` (virou `bullet` numa chamada; noutra o hunt não recusou) |
| 10 | `page_preview` page 0/99 **clampeia** para 1 sem aviso; zoom 0, −1 e 20 aceitos (zoom 20 → PNG 1.9 MB) |
| 11 | `search` é **case-insensitive** (`aaa` acha `Aaa` e `AAA`) e não tem flag `case` |
| 12 | `find_replace` all `aa`→`a` em `aaaa` → `count=3` texto `aa` (não é replace-all estável) |
| 13 | Comentários: resolve duas vezes `ok`; nada no papel (só painel Documento) |
| 14 | Mini-barra de tabela: botão H sem ação; dois ⌫ parecidos |
| 15 | `tableSelected` = “existe tabela”, não célula ativa |
| 16 | DOCX margens 12 → `11.9944…` |
| 17 | `insert_blocks` array vazio = sucesso silencioso |
| 18 | Undo além da história: o último de 15 undos ainda `ok=true` (esvaziou até o `document_new`); redo vazio erra certo |
| 19 | Digitação humana no canvas: código existe; **não** retestei clicar-e-escrever nesta rodada |

---

## Melhorar (ordem)

1. Recusar imagem sem bytes; recusar tabela 0×N.  
2. Preview: mesmo painter do canvas; se paginar texto longo, PNG não pode ser 7 KB.  
3. Mapear seleção a glyphs (bug do “GUI”).  
4. Validar page size (`A4`/`Letter`), orientation, margens ≥ 0, zoom (0.25–4), `kind` de lista.  
5. `search` com `case` / `word`; replace-all sem recuar no match.  
6. Persistir `width_mm` no DOCX (`wp:extent` / drawing).  
7. Comentário no papel.  
8. `apply_style` desconhecido = erro.

---

## Adicionar (Writer only)

- Merge / largura de coluna **usada no layout** (o campo `col_widths_mm` já entra no JSON)  
- Confirmar descarte no botão Novo da GUI  
- Track changes  
- Nota de rodapé, sumário  
- `search` próximo/anterior  
- Teste manual de IME/acentos no canvas  

Não: planilha, PPT, neste binário.

---

## Reprodução rápida

```bash
# imagem fantasma
aioffice call insert_image '{"path":"/no/such/file.png"}'
# → ok true, bloco vazio

# tabela sem coluna
aioffice call insert_table '{"rows":2,"cols":2}'
aioffice call delete_col '{"id":"t1","col":0}'
aioffice call delete_col '{"id":"t1","col":0}'
# → rows: [[],[]]

# preview morto
# inserir ~4000 chars unicode e page_preview → ~7 KB PNG
```

---

## Conclusão

Integração ok. O caminho feliz do Writer está **muito** melhor que 28/08. Os bugs que restam são de **borda e honestidade da API** (não mentir `ok`, não aceitar página Legal de cabeça para baixo, não pintar o highlight em “GUI”). Prioridade: imagem/tabela inválidas + preview de documento grande + seleção visual.
