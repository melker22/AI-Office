# Onda 19 — PDF completo (PDF/A-1b real, PDF/UA-1, tagging total, XMP) — spec arquitetural

> Autor: Opus (Chief Architect), 2026-10-20. Fleet: `.cockpit/fleets/wave19-pdf.yaml` (contrato de
> executor, nó `lessons` no início, `notion-sync` antes da aprovação). Fase IA: core + MCP, sem `src/app/`.

## Diagnóstico (medido, `main` @ a680799)

Documento de teste (heading, parágrafos, tabela) exportado e validado com **veraPDF 1.28.2** (validador
de referência, disponível no nixpkgs fixado), `qpdf` e `pdffonts`:

- **`pdfa: true` NÃO é PDF/A-1b** — veraPDF reprova: (6.1.3-1) o trailer do nosso update incremental
  (pós-processador da onda 13/15) não tem `/ID`; (6.7.9-2/3) o XMP que o Qt grava tem `pdf:Trapped`,
  fora do schema predefinido do PDF/A-1. O catálogo vendia isso como Partial "PDF/A".
- **PDF/UA-1 reprova**: sem `/Lang` (catálogo e spans — 50 ocorrências), sem
  `/ViewerPreferences/DisplayDocTitle`, sem a extensão `pdfuaid` no XMP, conteúdo não marcado nem como
  tag nem como `/Artifact` (cabeçalho/rodapé/nº de página/marca d'água).
- **Tagging**: só `/Document /Part /H1 /P`. Tabela cai no fallback; não há `/Table /TR /TH /TD`,
  `/Figure` (com `/Alt`), `/Note`, `/TOC /TOCI`, `/L /LI /Lbl /LBody`.
- **Fontes**: já embutidas (`pdffonts`: `emb yes`), sem subconjunto. O Todo "Incorporar fontes" estava
  desatualizado.
- **XMP**: o Qt já grava, incompleto (`dc:creator` vazio; sem assunto/palavras-chave).
- `Document.readingOrder` é gravado e validado, mas a estrutura do PDF segue a ordem de pintura.

## Decisões (fechadas)

| # | Decisão | Motivo |
|---|---|---|
| D1 | **veraPDF é o juiz.** `verapdf` entra no `shell.nix` (nixpkgs fixado; L11). Novo `qa/check_pdf.py` roda veraPDF (`--flavour 1b` e `ua1`), `qpdf --json=1` (árvore de estrutura) e `pdffonts`; ctest com `LABELS pdf`; `scripts/qa-all.sh --pdf` liga esse label; a CI roda `ctest -L "playbook\|roundtrip\|pdf"` | "Passou no qpdf --check" não prova conformidade; veraPDF prova |
| D2 | **PDF/A-1b de verdade**: o pós-processador copia o `/ID` do trailer original para todo trailer incremental; exportamos nosso próprio XMP (`QPdfWriter::setDocumentXmpMetadata`) só com schemas permitidos: `dc:title/creator/description`, `pdf:Keywords/Producer`, `xmp:CreateDate/ModifyDate/CreatorTool`, `pdfaid:part=1/conformance=B` — sem `pdf:Trapped`. Transparência é proibida no PDF/A-1: com `pdfa`, opacidade/sombra/marca d'água translúcida são pintadas opacas e a tool devolve `warnings[]` dizendo o que foi achatado | Corrige a promessa falsa |
| D3 | **XMP completo em todo export** (não só `pdfa`): autor, assunto, palavras-chave, datas e idioma vindos das propriedades do documento; mesmo conteúdo no dicionário `/Info` | Fecha o Todo "Metadados XMP" |
| D4 | **Um marcador por elemento etiquetável.** Generaliza a técnica da onda 15: TODO elemento etiquetável (parágrafo, heading, item de lista, célula de tabela, imagem/forma/gráfico, nota, entrada de TOC) é pintado dentro de exatamente um `setClipRect` próprio, e todo conteúdo de página que não é documento (cabeçalho, rodapé, nº de página, marca d'água, fundo, bordas de página) dentro de um clip de **artifact**. A lista ordenada de elementos sai de UMA função compartilhada por pintura e pós-processador (sucessora de `orderedBlocksForPage`). O fallback de página inteira continua como rede de segurança, mas os testes exigem **zero fallback** nos documentos da suíte | Mesma técnica já validada, estendida em vez de reescrever o gerador de PDF |
| D5 | **Estrutura**: `/L > /LI > (/Lbl, /LBody)`; `/Table > /TR > (/TH \| /TD)` (cabeçalho = TH); `/Figure` com `/Alt` = `image.alt` (sem alt → `validate`/export avisa); `/Note` para notas de rodapé/fim; `/TOC > /TOCI`; artifacts como `/Artifact BMC … EMC` fora da árvore. `/Lang` no catálogo = idioma do documento; spans com outro `lang` ganham `/Lang` próprio | O que o PDF/UA-1 exige e os leitores de tela usam |
| D6 | **Ordem de leitura**: a ordem dos filhos de `/Document` segue `doc.readingOrder` quando definido (molduras/formas), senão a ordem do documento | Usa o campo que já existe |
| D7 | **Modo `ua`**: `document_export_pdf {ua: true}` exige PDF/UA-1 — inclui `pdfuaid:part=1`, `DisplayDocTitle`; falta de alt em figura vira erro `{ok:false}` em vez de aviso. `pdfa` e `ua` combináveis | Contrato explícito para quem precisa de acessibilidade |
| D8 | **Fora da onda**: subconjunto de fontes (só tamanho; vira Todo novo "Subconjunto de fontes no PDF"); PDF/A-2/3 (Qt só gera A-1b); assinatura digital | Escopo honesto |
| D9 | Não regredir: ondas 13 (anotações de comentário), 15 (tagging), 16 (RTL no PDF), 17–18 (round-trip) | — |

## Tarefas e roteamento (contrato de executor; regras 3–5 do ORCHESTRATION)

| Nó | Executor | Por quê | Custo | Risco | Depende |
|---|---|---|---|---|---|
| `lessons` | Sonnet (medium) | Lições do PR #6 (L2/L9 recaíram) | Baixo | — | — |
| `t1-tagging` | Cursor Grok 4.7 | D4–D6 + `/ID` do trailer: Preview.cpp (clips) + PdfPostProcess.cpp | Alto | **Alto** | lessons |
| `t2-metadata` | Composer 2.5 | D2 (XMP, transparência, warnings), D3, D7 (args MCP), docs | Baixo | Médio | lessons |
| `t3-pdf-qa` | Composer 2.5 | D1: verapdf no Nix, `qa/check_pdf.py`, ctest `pdf`, `--pdf`, CI, documentos de teste | Baixo | Baixo | lessons |
| `integrate-1` | Composer 2.5 | Merge pelos 3 hashes | Baixo | Médio | t1, t2, t3 |
| gate `build-1` | comando | `fleet-verify-merged.sh 4 && qa-all.sh --gui` | 0 | — | integrate-1, t1–t3 |
| `t4-compliance` | Cursor Grok 4.7 | Leva `qa-all.sh --pdf` a verde: corrige o que o veraPDF ainda acusar | Médio | Médio | build-1 |
| gate `qa-full` | comando | `fleet-verify-merged.sh 1 && qa-all.sh --gui --interop --pdf` | 0 | — | t4 |
| loop `review` | Sonnet revisa (checklist de lições primeiro) · Grok Build corrige (≤3) | — | Médio | — | qa-full |
| gate `qa-final` → `handoff` → `notion-sync` → humano → `open-pr` | — | — | Baixo | — | — |

## Critério de pronto

- veraPDF: `--flavour 1b` **compliant** para todo documento da suíte exportado com `pdfa: true`;
  `--flavour ua1` **compliant** para todo documento exportado com `ua: true`.
- `qpdf --json`: árvore com `/Table/TR/TH/TD`, `/L/LI/Lbl/LBody`, `/Figure` com `/Alt`, `/Note`,
  `/TOC/TOCI`, `/Lang` no catálogo e em span de outro idioma; ordem de `/Document` = `reading_order`
  quando definido; **nenhum** `/Part` de fallback.
- `pdffonts`: toda fonte `emb yes`, inclusive árabe/hebraico/CJK (onda 16).
- PDF/A com marca d'água translúcida: compliant e `warnings` lista o achatamento.
- `scripts/qa-all.sh --gui --interop --pdf` verde local; CI verde com o label `pdf`.
