# Lições com evidência (harnessing entre ondas)

> Mantido pelo nó `lessons` no início de cada Fleet (ver `docs/ORCHESTRATION.md`). Inspirado no
> "harnessing-level recursion" do Harness² (google-research/harness2): o que falhou na onda N vira regra
> verificável para a onda N+1. **Só entra lição com evidência** (thread de review, commit ou incidente).
> O checklist curto que executores e revisores aplicam está em `.cursor/rules/lessons.mdc`.

Última PR processada: **#21** (fix: `pr-watch.py` reporta um auto-review do CodeRabbit pausado). 3
threads inline em 3 rodadas, todas corrigidas no mesmo commit `21a028a`. Reforçou L18 (gate/watcher só
decide com evidência completa e corrente): `cr_state` escolhia o evento mais novo entre respostas e
avisos de rate-limit por timestamp, mas checava o aviso de "auto-review pausado" só depois, num scan de
retorno antecipado separado — um aviso de pausa mais antigo que uma resposta/rate-limit mais nova podia
sobrepor essa resposta mais nova, ou o inverso, dependendo de qual checagem corria primeiro, violando a
própria regra "o evento mais novo decide" que o código já aplicava aos outros tipos de evento
(https://github.com/melker22/AI-Office/pull/21#discussion_r4461085755, corrigido tornando o aviso de
pausa um evento com timestamp na MESMA coleção, removido o scan separado; self-test cobre as duas
ordens). E o aviso de pausa usava `updated_at` do comentário inteiro como prova de "aviso novo", mas
esse campo também muda quando só um checkbox ou outro trecho do resumo é editado sem tocar no marcador
de pausa — um editor que resolvesse o checkbox depois de uma revisão já pedida reativava `paused` mesmo
sem pausa nova, podendo disparar outro pedido de revisão desnecessário
(https://github.com/melker22/AI-Office/pull/21#discussion_r4461214432, corrigido restringindo a detecção
de pausa a quando nenhuma revisão foi pedida para o head atual — `pause_ok=False` durante uma janela de
`--since` que já cobre o head). Regra: o "evento mais novo decide" vale para TODO tipo de sinal, inclusive
casos especiais tratados em código separado, e o timestamp de um objeto composto (comentário/resumo) não
prova que o campo específico que importa mudou — restrinja a janela em que esse sinal é válido em vez de
confiar no timestamp do contêiner.

Antes dela, **PR #20** (feat: onda 27 — import EPUB, HTML pelo writer XHTML, notas multi-parágrafo). 5
threads inline em 1 rodada (todas corrigidas) mais 2 achados fora do diff na mesma revisão (ambos
corrigidos) — e, só durante esta PR (não na revisão dela, no fluxo que a processa), o CodeRabbit mudou o
formato do corpo de revisão e quebrou o parser do nosso próprio gate/watcher. Reforçou L18: o corpo da
revisão da PR #20 trocou as seções antigas por blocos `<details>` com selo de imagem (`alt="Outside
diff"`/`"Nitpick"`/...) e severidade em negrito, abrindo com uma lista de status de 31 marcadores
`cr-comment`; o parser antigo só conhecia títulos de seção, então não achava NENHUM achado de corpo —
um achado de corpo real (outside-diff) teria sido mergeado sem `cr-ack` (identificado e corrigido antes
do merge desta mesma PR, commit `69ba6fae`, com self-test sobre o corpo real da PR #20: exatamente o
achado outside-diff conta, o nitpick trivial não, e a revisão conta como revisão do head). Logo depois,
um achado que cita um marcador `cr-comment` dentro de um bloco de código no próprio texto do achado podia
fazer o parser extrair esse marcador citado (dentro do fence) como o id real do achado, em vez do
marcador verdadeiro no fim do bloco — um achado forjado dentro de um trecho de código citado por outro
achado escondia o marcador real e podia colidir com um `cr-ack` de outro achado
(corrigido no commit `48b3ae7`, lendo o id do fim do próprio bloco do selo, com o conteúdo de blocos de
código apagado antes; self-test com um marcador forjado dentro de um fence). Reforçou L7 (schema MCP,
docs e código concordam em prosa, inclusive listas enumeradas completas): a descrição MCP de
`document_open`/`document_save` ainda não citava o import EPUB novo nem o array opcional `warnings`, e
`document_save` ainda dizia "EPUB import is not supported" (achado fora do diff,
pullrequestreview-5747575553, corrigido em `a01e538`); o schema de `paragraphs` em
`insert_footnote`/`insert_endnote` declarava só `type: array`, sem `items`, então um item não-string virava
parágrafo vazio em silêncio (mesmo achado fora do diff, corrigido em `a072074`, tipando `items` como
string e documentando precedência sobre `text`); `docs/mcp-tools.md` listava só 6 dos 10 contêineres que
`EpubReader::isSkippedContainer` descarta, faltando `style`/`embed`/`noscript`/`template`
(https://github.com/melker22/AI-Office/pull/20#discussion_r4454510119, corrigido em `714f100`); e
`docs/ORCHESTRATION.md` ainda descrevia os comandos `cockpit fleet pause`/`resume` sem o `<runId>`
obrigatório (https://github.com/melker22/AI-Office/pull/20#discussion_r4451178316, corrigido em
`5a843cb`). Reforçou L3 (um contrato, todos os caminhos): `loadRtfFile` chama
`Document::normalizeExternalParaSeps()` antes de aceitar o documento carregado (U+2029 externo a um
bloco de nota vira `\n`), mas o novo `loadEpubFile` não chamava — um XHTML de terceiros com U+2029 legítimo
dentro de um `<p>`/`<li>`/`<td>` entrava sem normalizar, e o resto do pipeline (export, preview,
`plainText()`) trata U+2029 como separador de parágrafo só válido em nota
(https://github.com/melker22/AI-Office/pull/20#discussion_r4451178351, corrigido em `7cbbdb4`, chamando a
mesma normalização após `resolveAll()` e antes do `std::move`). Reforçou L4 (correção completa, não
pontual — aqui, escopo de seleção aplicado só em parte do cálculo): `export_html` com `ids` restringia
`inFlowNotes` à seleção, mas o laço de notas órfãs ainda percorria `doc.body` inteiro, então uma nota só
referenciada por um bloco fora da seleção virava órfã e era exportada/avisada mesmo sem pertencer ao
recorte pedido (https://github.com/melker22/AI-Office/pull/20#discussion_r4451178344, corrigido em
`8d4e85a`, mantendo só as órfãs cujo bloco está na seleção, ou alcançáveis a partir delas). Reforçou L23
(classifique por metadado canônico, nunca por heurística de superfície — aqui, texto vazio como proxy de
"sem conteúdo"): `splitNoteParagraphs` descartava todo run com texto vazio ao quebrar por U+2029, mas um
run vazio pode carregar uma âncora de nota aninhada ou metadado de campo que o serializador ODF/DOCX
precisa; o texto vazio não significa run sem conteúdo
(https://github.com/melker22/AI-Office/pull/20#discussion_r4451178322, corrigido em `fedd273`, preservando
runs de texto vazio no parágrafo atual).

Antes dela, **PR #18** (tools: `pr-watch.py` espera CI/CodeRabbit com ETag). 11 threads inline em
seis rodadas de CodeRabbit, todos corrigidos, mais 8 achados fora do diff em três revisões — 6
corrigidos/reconhecidos com `cr-ack` (commits `cdf1b5a`/`f13df51`), mas os **2 últimos** (paginação de
CI e uma corrida de arquivo) ficaram sem correção nem `cr-ack` quando o autor mergeou 1 minuto após a
revisão final. Reforçou L18 (gate de merge só decide "limpo" com evidência completa): `reply_outcome`
aceitava o marcador de resposta do CodeRabbit (`<!-- This is an auto-generated reply by CodeRabbit -->`)
e o aviso de rate-limit como substring dentro de texto arbitrário, então um comentário comum que citasse
esse texto entre aspas virava prova de "sem revisão"/"rate limited"
(https://github.com/melker22/AI-Office/pull/18#pullrequestreview-5699977040,
https://github.com/melker22/AI-Office/pull/18#discussion_r4414256046, corrigidos em `cdf1b5a`/`658d648`
exigindo o marcador como linha inteira, não citação); uma resposta de bot dentro de uma thread de review
(corpo vazio) contava como revisão do head, mascarando um head na verdade sob rate-limit (commit
`d3f2080`); e os endpoints de status/check-run da CI eram buscados só na primeira página, então um
status de falha além do item 100 escaparia de `--until all`
(https://github.com/melker22/AI-Office/pull/18#pullrequestreview-5705791800, **não corrigido antes do
merge**). Abriu lição nova (dentro de L1, ver abaixo): corrigir uma corrida checagem-depois-ação
(`path.exists()` então `path.stat()`) retirando a checagem prévia só *desloca* a mesma corrida para o
próximo passo — o arquivo ainda podia desaparecer entre `stat()` e `read_text()`, sem handler
(https://github.com/melker22/AI-Office/pull/18#discussion_r4418069841, corrigido parcialmente em
`d2525024`; a corrida reaberta em
https://github.com/melker22/AI-Office/pull/18#pullrequestreview-5705791800, **também não corrigida antes
do merge**). Reforçou L1 (entrada externa hostil, aqui a resposta da API do GitHub): itens malformados em
`reviews`/`comments` (não-dict, timestamp não-ISO) derrubavam `cr_state` com exceção não tratada em vez de
`fetch_failed` (https://github.com/melker22/AI-Office/pull/18#discussion_r4418069857, corrigido em
`d2525024`); e `--interval`/`--timeout` aceitavam negativo/NaN do `argparse`, chegando a `sleep()` com
`ValueError` (https://github.com/melker22/AI-Office/pull/18#pullrequestreview-5704553222, corrigido em
`f13df51`, junto com os outros 4 achados fora do diff da mesma revisão — timestamp de review ausente,
validação de forma aninhada, releitura do head, erro de token). Reforçou L28 (condição sobre item com
dado ausente não libera por omissão): uma revisão sem `submitted_at` (ou em estado `PENDING`) virava
`reviewed` por padrão, em vez de pendente (mesma revisão/commit `f13df51`).

Antes dela, **PR #19** (onda 26 — layout e fidelidade de preview). 5 threads inline em duas rodadas de
CodeRabbit (todos corrigidos). Reforçou L4 (valor derivado recalculado num ponto único/consistente):
`seqNumberOfBlock` visitava os campos `seq` da tabela aninhada antes da legenda, numerando na ordem
contrária à do export DOCX (que escreve a tabela primeiro), então um `category_number` cross-reference
citava o número do campo aninhado em vez do número da tabela
(https://github.com/melker22/AI-Office/pull/19#discussion_r4413566282, corrigido em `fe8d5d6`, visitando
células e `cell.nested` antes de `captionRuns`). Reforçou L7 (schema/docs/código concordam em prosa): a
decisão D8 da onda 26 prometia travessia "em qualquer profundidade", quando `forEachFieldRun` entrega só
um nível de tabela aninhada (`NestedCell` guarda runs, não outra tabela)
(https://github.com/melker22/AI-Office/pull/19#discussion_r4413566258, corrigido em `e54ee6b`). Reforçou
L11 (ambiente hermético/portável, aqui versão mínima do Python): `qa/wave26-d8b-filepath.py` usava uma
barra invertida dentro de uma expressão de f-string — sintaxe só válida a partir do Python 3.12 — e o
CMake só exige Python 3, sem mínimo 3.12, então o script quebrava com `SyntaxError` num Python 3.10/3.11
(https://github.com/melker22/AI-Office/pull/19#discussion_r4413566267, corrigido em `e82d179`, calculando
a contagem fora da f-string). Outros 2 achados (seleção de TOC pintada só na primeira entrada; formatos
por-run omitidos na medição de `blockTextLayout`, descasando da pintura e cortando texto no clipe da
célula) foram corrigidos sem abrir padrão novo
(https://github.com/melker22/AI-Office/pull/19#discussion_r4413566330,
https://github.com/melker22/AI-Office/pull/19#discussion_r4413566287, corrigidos em
`059d9e1`/`84b1e86`).

Antes disso, **PR #17** (onda 25 — fidelidade RTF/EPUB/PDF). 33 threads inline, todos corrigidos,
mais 6 achados fora do diff em 5 revisões — as 39 ocorrências foram corrigidas, nenhum nitpick ficou
pendente nesta PR. Reforçou L6 (sintaxe RTF real): `HYPERLINK` perdia o alvo por não pular os switches
`\o`/`\t`/`\m`/`\n` antes dele
(https://github.com/melker22/AI-Office/pull/17#discussion_r4407576899, corrigido em `122179d`); a
correção seguinte ainda perdia o `\l` quando havia URL e `\l` juntos
(https://github.com/melker22/AI-Office/pull/17#discussion_r4407777966, corrigido em `fd3f6bf0`); e a
escrita de volta citava o nome crú do alvo local em vez do nome de bookmark normalizado por `bm()`
(https://github.com/melker22/AI-Office/pull/17#discussion_r4409259849, corrigido em `4377fc1`) — três
rodadas da mesma função de hyperlink. Mais no `RtfWriter`: coluna sem largura herdava 1 twip em vez de
um valor usável (https://github.com/melker22/AI-Office/pull/17#pullrequestreview-5691749401, corrigido
em `a4f5647`, commit que também cobriu o `col_widths_mm` zero/negativo da revisão seguinte,
https://github.com/melker22/AI-Office/pull/17#pullrequestreview-5694970421); e um item de lista oculto
por `\v` só escondia o texto, deixando o marcador `\listtext`/caixa de seleção visível
(https://github.com/melker22/AI-Office/pull/17#pullrequestreview-5693983450, corrigido em `07fcf6e`).
Reforçou L11 (ambiente hermético, sem caminho fixo de build do desenvolvedor): os autotestes de RTF,
EPUB e PDF da onda 25 chamavam a suíte sem repassar o `--cli` recebido, caindo num `build/linux`
hardcoded
(https://github.com/melker22/AI-Office/pull/17#discussion_r4407576885, corrigido em `d0d649f`;
https://github.com/melker22/AI-Office/pull/17#discussion_r4408490311, corrigido em `773dead`;
https://github.com/melker22/AI-Office/pull/17#discussion_r4408490304, corrigido em `9c6ae89`;
https://github.com/melker22/AI-Office/pull/17#discussion_r4409259834, corrigido em `3ef16ae`); e um
playbook que o CTest também roda direto, sem o rewrite de caminho temporário da suíte, dependia do mesmo
`build/linux` fixo e falhava num build `linux-debug`/Windows limpo
(https://github.com/melker22/AI-Office/pull/17#discussion_r4409010083, corrigido em `4df7899`). Reforçou
L24 (id derivado colide com id existente): com notas `fn1` e `fn1-c1`, o sufixo `-cN` de cópia do PDF
produzia o mesmo `/Note /ID` para a segunda cópia de `fn1` e a primeira de `fn1-c1`
(https://github.com/melker22/AI-Office/pull/17#discussion_r4408490330, corrigido em `66a2053`, reaberto
e corrigido de novo em `ef1517d` quando o primeiro fix ainda colidia com um id de bloco que já parecia um
id de cópia: https://github.com/melker22/AI-Office/pull/17#discussion_r4408864387). Outros ~19 achados
(link de página EPUB para fragmento inexistente, nota referenciada só por outra nota caindo como órfã,
performance de `cellCoveredBySpan`, ordem dos `/L` raiz do PDF, `SystemExit` do validador EPUB
interrompendo o laço de checagem, lacunas de prosa em `docs/mcp-tools.md`/
`docs/WRITER_FEATURE_CATALOG.md`/`.cursor/rules/lessons.mdc`) foram corrigidos sem abrir padrão novo.

Antes dela, **PR #16** (guarda de uso resiste a reinícios do Cockpit; gate aceita revisão do CodeRabbit
sem status) — 6 threads inline, todos corrigidos no commit `d178478`. Reforçou L18 (gate de merge só
decide "limpo" com evidência completa e corrente): a checagem de identidade do bot aceitava qualquer
login com prefixo `coderabbit` em vez do bot exato
(https://github.com/melker22/AI-Office/pull/16#discussion_r4406774015); uma resposta de comando ("Review
finished") sem status nem objeto de revisão virava prova de revisão completa, mesmo sob rate-limit
(https://github.com/melker22/AI-Office/pull/16#discussion_r4406774021); a frescor da resposta comparava
com a data do commit em vez do SHA revisado exato, então editar uma resposta antiga também passava
(https://github.com/melker22/AI-Office/pull/16#discussion_r4406774028); e o fallback de resposta se
aplicava mesmo quando o status já dizia "Review paused", mascarando esse pendente
(https://github.com/melker22/AI-Office/pull/16#discussion_r4406774035). Reforçou as lições novas L28 e
L29 (abertas na PR #14 anterior, abaixo), com evidência deste PR sobre o mesmo `usage-guard.py`: o scan
reatribuía um timestamp novo a cada verificação em vez de reusar o marcador pendente da mesma
interrupção, retentando uma falha lenta em todo ciclo
(https://github.com/melker22/AI-Office/pull/16#discussion_r4406774042, corrigido no mesmo commit); e o
aviso de revisão manual sempre citava "limite de uso renovado" mesmo quando a causa era um reinício do
Cockpit (https://github.com/melker22/AI-Office/pull/16#discussion_r4406774052, corrigido no mesmo
commit).

Antes dela, **PR #14** (guarda de uso espera limite e retoma trabalho parado, primeira versão do
`usage-guard.py`) — 24 threads inline em várias rodadas, mais 6 achados fora do diff em 4 revisões,
todos corrigidos. Abriu as lições novas L28 (condição agregada sobre um grupo exige todos os membros) e
L29 (pureza de `--dry-run` e reuso de marcador): `once --dry-run` marcava um marcador de retomada como
concluído mesmo sem retomar de fato, suprimindo o scan real por até 7 dias
(https://github.com/melker22/AI-Office/pull/14#pullrequestreview-5669475245, corrigido em `f92f9b0`); a
mesma revisão apontou que o resume disparava ao existir qualquer nó limitado por uso, não ao passar o
reset de todos os nós limitados do Run (mesmo link, mesmo commit); e a revisão seguinte apontou que um
nó reconhecido como limitado por uso mas com reset nulo/inválido escapava do `max(...)` e liberava o
resume mesmo assim
(https://github.com/melker22/AI-Office/pull/14#pullrequestreview-5677842045, corrigido em `04efa55`).

Antes disso, **PR #15** (onda 24 — fidelidade de ida-e-volta DOCX/ODT e QR decodificável). 7
threads inline em duas rodadas de CodeRabbit (todas endereçadas), mais 2 achados fora do diff no corpo
da 2ª revisão — ambos nitpick de precisão de comentário/doc, nenhum corrigido: comentário do CMake dizia
que os testes `wave17` ficavam fora do `qa-all` padrão por causa do label `wave24`, quando o filtro
`-L 'playbook|roundtrip|wave24'` já casa pelo label `roundtrip`
(https://github.com/melker22/AI-Office/pull/15#pullrequestreview-5681108253, não corrigido); e a nota de
auditoria de `qa/check_qr.py` citava o pacote Nix `zbar` em vez do executável `zbarimg` que o script
chama (mesmo review, não corrigido). Reforçou L7 (três arquivos de doc — `docs/AI_HANDOFF.md`,
`docs/audit/fix-plan.md`, `docs/waves/wave24-docx-odt-fidelity.md` — ainda diziam nível de ECC QR "L" com
o código entregando nível "M", um regex `hex6` inexistente em vez do laço de `docxHexColor`, e `bidi` só
RTL para `"1"`/`"true"` quando `bidiIsOn` também aceita vazio e `"on"`, mais o D10 da wave 24 omitindo a
dedução por contenção de intervalo além do match exato — todos corrigidos nos commits `c83b9c8`..`ac25f6e`:
https://github.com/melker22/AI-Office/pull/15#discussion_r4396808595,
https://github.com/melker22/AI-Office/pull/15#discussion_r4396808602,
https://github.com/melker22/AI-Office/pull/15#discussion_r4396808609) e abriu a lição nova L24 (geração de
id novo a partir do tamanho de uma coleção que dedup pode encolher). Também registradas, por incidente de
Fleet (não de PR): L25 (nó Claude não usa skills/subagentes/tarefas em segundo plano e não submete saída
"provisória" — Fleet wave24 Run `4712f7db`, Fleet audit Run `6fda0b72`), L26 (nó de agente não roda build
completo do zero — Fleet wave24-finish Run `a41c009a`) e L27 (ninguém usa `git stash`, compartilhada entre
worktrees — pop no checkout principal pegou a stash de um agente no worktree do PR #15). Formato:
`L<n> — regra` · onde falhou (evidência) · ocorrências.

Antes disso, **PR #13** (onda 23 — integridade, I/O hostil e transporte stdio do MCP). 15 threads
inline em três rodadas de CodeRabbit, mais 8 achados fora do diff no corpo das revisões (PR #13
pullrequestreview-5642604443/5642650466/5642889711/5643127751/5643779721) e um nitpick trivial não corrigido
(canvas.xhtml, pullrequestreview-5643827857). Reforçou L3 (`priorRuns` de revisão de formatação e remapeamento
de bookmark/xref incompletos em múltiplos caminhos de código, não só um), L4 (estatística usando `plainText()`
em vez de `statText()` para legendas), L7 (decisões "fechadas" do spec da onda 23 descreviam código já
substituído), L10/L18/L21 (gate e self-test com evidência incompleta) e abriu a lição nova L23 (checagem de
papel de documento EPUB por heurística de nome de arquivo em vez de metadado canônico do manifesto/OPF, errada
em três rodadas sucessivas). PR #11 foi revisão-only (base `audit/coderabbit-base` vs código completo) e foi
fechada sem merge — não conta como PR processado, mas os 29 achados do seu `coderabbit-pr11.json` já
alimentaram o `fix-plan.md` e as lições abaixo quando tinham evidência própria de thread. Entre #7 e #12,
quatro PRs mergearam:
- **PR #8** (fix trivial de encoding UTF-8 do banner): 1 thread menor (exemplo de XML malformado em
  `docs/AI_HANDOFF_ARCHIVE.md`), sem padrão novo.
- **PR #9** (wave 20 — EPUB 3 export e RTF nativo): 24 threads em duas rodadas. A maioria já documentada em
  `docs/AI_HANDOFF.md`; abriu/reforçou L4 (rescan de notas ainda quadrático na 2ª rodada), L6 (seis desvios
  de sintaxe RTF real) e a lição nova L20 (mutar `noteUses` durante a iteração sobre ele, ordem de nota
  dependente de hash).
- **PR #10** (correções outside-diff e gate que lê o corpo da revisão): 12 threads em duas rodadas. Reforçou
  L18 (paginação, autenticação, `mergeable` nulo, `--since` como texto, HEAD obsoleta) e L19 (tupla não
  desempacotada só aparecia no `main` real).
- **PR #12** (auditoria + gate mais rígido): 19 threads. Reforçou L10 (CLI ausente, playbook sem `expect`,
  diretório vazio, parar no primeiro `ok:true` — todos "passavam" o gate obrigatório sem testar nada) e L18
  (regex de seção/prompt casava em qualquer lugar do corpo, não só no título — achado citado dentro de outro
  achado virava limite falso); abriu a lição nova L21 (diretório de auditoria compartilhado precisa de lock
  estável e controlado pelo dono, não de caminho fixo em `/tmp` nem de variável por processo).

Formato: `L<n> — regra` · onde falhou (evidência) · ocorrências.

## Código

- **L1 — Toda entrada externa é hostil.** Arquivo importado (DOCX/ODT/.aio.json), argumento MCP e
  expressão de usuário têm limite de tamanho/profundidade, tipo validado e erro tratado; nunca crash nem
  aceitação silenciosa. · PR #6 parser sem limite de recursão; PR #2 `deleted_runs` não-objeto; PR #4
  `<w:delText>` importado como texto vivo; PR #7 `Block::fromJson` aplicava o atalho numérico de
  `rows`/`cols` a qualquer bloco (até sem `type`), permitindo alocar ~10¹⁰ células a partir de
  `document_open`/`insert_blocks`/`replace_range`
  (https://github.com/melker22/AI-Office/pull/7#discussion_r4331347217); PR #18 (`pr-watch.py`): a
  resposta da API do GitHub também é entrada hostil — um item não-dict ou um `submitted_at`/`updated_at`
  fora do formato ISO dentro de `reviews`/`comments` derrubava `cr_state` com exceção não tratada (exit
  code fora do contrato 0/2/3) em vez de virar `fetch_failed`
  (https://github.com/melker22/AI-Office/pull/18#discussion_r4418069857, corrigido em `d2525024`); e
  `--interval`/`--timeout` do `argparse` aceitavam negativo/NaN/infinito, chegando a `sleep()` com
  `ValueError` (https://github.com/melker22/AI-Office/pull/18#pullrequestreview-5704553222, corrigido em
  `f13df51`). **Corolário sobre corrida checagem-depois-ação:** corrigir uma checagem prévia (`exists()`
  antes de `stat()`) catching o erro no passo seguinte só desloca a mesma corrida — trate a falha no
  ponto da operação real que pode falhar (o último passo da sequência), nunca numa pré-condição isolada;
  PR #18 corrigiu `exists()`+`stat()` tratando `FileNotFoundError` em `stat()` (commit `d2525024`), mas a
  mesma corrida reabriu um passo depois, entre esse `stat()` e o `read_text()` seguinte — achado da
  revisão final, sem correção nem `cr-ack` antes do merge
  (https://github.com/melker22/AI-Office/pull/18#pullrequestreview-5705791800). · 6
- **L2 — Mapear por identidade, nunca por posição, entre export e import.** Índice de bloco/run muda
  quando o formato expande ou funde elementos (TOC, notas). Identidade por fingerprint sozinha ainda não
  basta: inclua tipo de bloco e número de ocorrência, senão uma entrada de TOC com o mesmo texto do
  heading (ou um parágrafo repetido) rouba o campo. · PR #6 `conditionals` restaurados no bloco errado
  (commit `98194e3`); PR #6 2ª rodada CodeRabbit — fingerprint sem tipo/ocorrência ainda confundia TOC
  com heading (https://github.com/melker22/AI-Office/pull/6#discussion_r4329241338, corrigido em
  `0f4bcae`). · 2
- **L3 — Um contrato, todos os caminhos.** Ao mudar um invariante (ex.: delete = ponto, escrita atômica,
  remapeamento de id), migre TODOS os criadores, o reject/accept, o import e os arquivos antigos (normalizar
  ao carregar) — e lembre que uma correção pode *destruir* o invariante em vez de só esquecê-lo num caminho:
  apagar o destino antes do rename não é "escrita atômica parcial", é nenhuma. · PR #4 Backspace ainda criava
  delete com largura; reject usava `end - start`. PR #13 (onda 23, três rodadas de CodeRabbit): `split_block`
  não dividia `priorRuns` da revisão de formatação no ponto de corte, perdendo a metade direita no reject
  (https://github.com/melker22/AI-Office/pull/13#discussion_r4370398850, corrigido em `498c96d`);
  `find_replace` dentro de uma revisão de formatação não atualizava `priorRuns`, perdendo a substituição no
  reject (https://github.com/melker22/AI-Office/pull/13#pullrequestreview-5642650466, corrigido em `5bd2d35`);
  limpar o intervalo inteiro de uma revisão de formatação deixava `priorRuns` vazio em vez de um `TextRun`
  vazio, e o reject falhava com "no prior formatting"
  (https://github.com/melker22/AI-Office/pull/13#pullrequestreview-5643127751, corrigido em `b401bfe`); o
  `expandRegexReplacement` (`\0`, `\&`, `\\`) só era aplicado numa das duas fases de `find_replace`
  (https://github.com/melker22/AI-Office/pull/13#discussion_r4370637444, corrigido em `851f607`);
  `document_include` remapeava bookmark ids nos runs mas não nas células de tabela nem em células aninhadas
  (https://github.com/melker22/AI-Office/pull/13#discussion_r4371177039, corrigido em `f5caace`/`1bec219`,
  CodeRabbit confirmou a lacuna de novo em
  https://github.com/melker22/AI-Office/pull/13#pullrequestreview-5643670952 antes do fix final); e um nome de
  bookmark importado que colide com um do host ficava sem remapear
  (https://github.com/melker22/AI-Office/pull/13#pullrequestreview-5643127751, corrigido em `7135558`);
  `QFile::rename` nunca sobrescreve destino existente, então `placeConverted` apagava o destino antes do
  rename — se o rename falhasse depois, o destino ficava sem arquivo nenhum, pior que antes da "correção
  atômica" (https://github.com/melker22/AI-Office/pull/13#discussion_r4370398863, corrigido em `53f2369`); e
  `export_text`/`export_markdown`/`export_html` com `path` ainda usavam `QFile::open(WriteOnly|Truncate)` sem
  checar o retorno de `write()`, só `saveTo` usava o helper atômico compartilhado
  (https://github.com/melker22/AI-Office/pull/13#pullrequestreview-5642604443, corrigido em `83f0b12`).
  PR #20 (onda 27): `loadRtfFile` chama `Document::normalizeExternalParaSeps()` antes de aceitar o
  documento carregado, mas o novo `loadEpubFile` não chamava — um `<p>`/`<li>`/`<td>` de terceiros com
  U+2029 legítimo entrava sem normalizar, e o resto do pipeline trata esse caractere como separador só
  válido dentro de nota
  (https://github.com/melker22/AI-Office/pull/20#discussion_r4451178351, corrigido em `7cbbdb4`, chamando
  a mesma normalização após `resolveAll()` e antes do `std::move`). · 10
- **L4 — Correção completa, não pontual.** Valor derivado (stat, seq, xref) é recalculado num ponto
  único (leitura/salvamento), não em gatilhos por ferramenta; e uma correção que só estreita o caso comum
  ainda precisa cobrir o caso em lote. · Onda 18 review: `stat` só atualizava com `hide_if`/vars; `seq`
  congelado na inserção. PR #9 1ª rodada: `resolveNotes`/`finishNote` escaneava o documento inteiro por
  nota resolvida; a correção limitou o rescan a partir do último índice resolvido, mas isso ainda é O(n²)
  quando várias notas chegam pendentes no mesmo flush — CodeRabbit reabriu o mesmo defeito na 2ª rodada
  (https://github.com/melker22/AI-Office/pull/9#discussion_r4338510187,
  https://github.com/melker22/AI-Office/pull/9#discussion_r4339255897); corrigido coletando todos os ids
  pendentes de uma vez em `resolveNotes` antes de reescrevê-los (commit `6dcc356`). PR #13: `Block::plainText()`
  era a fonte de palavras/caracteres em `document_stats`, mas também servia para outros usos que não devem
  contar legenda; a correção manteve `plainText()` só com texto do corpo e passou a estatística a usar
  `statText()`, que inclui legenda (decisão D15)
  (https://github.com/melker22/AI-Office/pull/13#discussion_r4370398847, corrigido em `c63bf75`). PR #19
  (onda 26): `seqNumberOfBlock` visitava os campos `seq` de `cell.nested` antes da legenda da tabela,
  numerando na ordem contrária à do export DOCX (que escreve a tabela antes da legenda), então um
  `category_number` cross-reference citava o número do campo aninhado em vez do da tabela
  (https://github.com/melker22/AI-Office/pull/19#discussion_r4413566282, corrigido em `fe8d5d6`,
  visitando células e `cell.nested` antes de `captionRuns`). PR #20 (onda 27): `export_html` com `ids`
  restringia `inFlowNotes` à seleção, mas o laço de notas órfãs ainda percorria `doc.body` inteiro —
  escopo de seleção aplicado só numa parte do cálculo, não na correção completa — então uma nota só
  referenciada por um bloco fora da seleção virava órfã e era exportada/avisada mesmo fora do recorte
  pedido (https://github.com/melker22/AI-Office/pull/20#discussion_r4451178344, corrigido em `8d4e85a`,
  mantendo só as órfãs cujo bloco está na seleção, ou alcançáveis a partir delas). · 6
- **L5 — Operações em vários blocos geram dados por bloco.** · PR #4 `priorRuns` só do primeiro bloco. · 1
- **L20 — Não mute uma coleção enquanto itera sobre ela; ordem de saída que importa é construída
  explicitamente, nunca herdada da ordem de iteração de um hash.** `QHash`/`QMap` não garantem ordem
  estável, e inserir/remover durante um loop sobre o próprio container invalida iteradores. · PR #9
  `Epub.cpp::writeChapter` chamava `writeNotes(x, ctx, *ctx.noteUses)` segurando iteradores de `QHash`
  sobre o mesmo mapa que a escrita modificava, e a ordem das notas no capítulo vinha dessa iteração em vez
  da ordem de primeira referência no texto
  (https://github.com/melker22/AI-Office/pull/9#discussion_r4338510140); corrigido copiando cada vetor de
  uso antes de escrever e emitindo as notas na ordem de primeira referência (commit `90c3f5f`). · 1
- **L6 — Sintaxe do formato-alvo, não a nossa.** Exporte construção nativa só quando a semântica é
  idêntica no Word/LibreOffice; senão exporte o resultado avaliado. · PR #6 `IF` com identificador e
  aspas simples; PR #3 ordem `w:bidi`/`w:jc` fora do schema; PR #9 `RtfWriter` tratava `\u` como valor
  sem sinal (o leitor nativo exige inteiro de 16 bits com sinal), escrevia `\pngblip` para toda imagem
  (JPEG precisa de `\jpegblip`), `\picw`/`\pich` como twips em vez de pixels, não reiniciava propriedades
  de parágrafo com `\pard` em cada célula de tabela, fechava a tabela com `\pard\par` (parágrafo vazio a
  mais no reimport) e perdia a formatação de caractere do resultado de campo/hyperlink por não aplicar
  `charProps` dentro de `\fldrslt` — seis desvios da sintaxe RTF real, dois deles ainda presentes na 2ª
  rodada (https://github.com/melker22/AI-Office/pull/9#discussion_r4338510203,
  https://github.com/melker22/AI-Office/pull/9#discussion_r4338510227,
  https://github.com/melker22/AI-Office/pull/9#discussion_r4339255905); corrigidos em `90c3f5f`/`6dcc356`.
  PR #17 (onda 25): `finishField()` do `RtfReader` perdia o alvo de `HYPERLINK \l "bookmark"` por não
  pular os switches `\o`/`\t`/`\m`/`\n` que o Word grava antes dele
  (https://github.com/melker22/AI-Office/pull/17#discussion_r4407576899, corrigido em `122179d`); a
  correção seguinte ainda perdia o `\l` quando a mesma `HYPERLINK` também tinha URL, escrevendo só a URL
  (https://github.com/melker22/AI-Office/pull/17#discussion_r4407777966, corrigido em `fd3f6bf0`); e o
  `RtfWriter` citava o nome crú do alvo local em vez do nome de bookmark normalizado por `bm()`, perdendo
  o link quando a normalização muda o nome
  (https://github.com/melker22/AI-Office/pull/17#discussion_r4409259849, corrigido em `4377fc1`) — três
  rodadas sucessivas da mesma função de hyperlink. Mais no `RtfWriter`: uma coluna sem largura declarada
  herdava 1 twip (ilegível) em vez do fallback uniforme já usado para colunas além da lista
  (https://github.com/melker22/AI-Office/pull/17#pullrequestreview-5691749401, corrigido em `a4f5647`,
  que também cobriu `col_widths_mm` zero/negativo apontado na revisão seguinte,
  https://github.com/melker22/AI-Office/pull/17#pullrequestreview-5694970421); e um item de lista oculto
  por `\v` só escondia o texto do item, deixando visível o marcador `\listtext`/caixa de seleção que vem
  antes do `\v`
  (https://github.com/melker22/AI-Office/pull/17#pullrequestreview-5693983450, corrigido em `07fcf6e`).
  · 4
- **L15 — Caminho especial de desenho reusa o layout já shapeado, nunca redesenha substring plano.**
  Quando um requisito extra (span de idioma, marcação) exige tratar um trecho do texto à parte, desenhe
  os glyph runs que o `QTextLayout`/`QTextLine` já calculou para aquele trecho; reshapear um substring
  com fonte/pen do bloco descarta formatação de run (negrito, cor, tamanho, sublinhado), espaçamento de
  justificação e a ordem bidi, e pula caminhos que fazem `return` antecipado (drop cap, ruby). · PR #7
  parágrafo multilíngue perdia negrito/cor/drop cap/ruby no span de idioma estrangeiro, corrigido
  desenhando `QTextLine::glyphRuns` em vez de `pdfDrawText` num substring
  (https://github.com/melker22/AI-Office/pull/7#discussion_r4331347223, commit `36ea315`). · 1
- **L7 — Schema, docs e código dizem a mesma coisa.** Isso inclui prosa descritiva, não só tipos: uma
  frase ou decisão "fechada" que descreve um comportamento antigo (já substituído) induz a próxima onda
  a reintroduzir o bug. · PR #6 `insert_field.arg` string no schema, objeto na doc e no código; PR #7
  `docs/mcp-tools.md` sem `copies` em `document_export_pdf`
  (https://github.com/melker22/AI-Office/pull/7#discussion_r4331347155); PR #7 frase de tagging ainda
  descrevia o fallback da wave 15 (`/Part`), omitindo `/Table`/`/Figure`/`/Note`/`/TOC` da wave 19
  (https://github.com/melker22/AI-Office/pull/7#discussion_r4331347162); PR #7 `docs/waves/wave19-pdf.md`
  D3 dizia que o idioma ia para o XMP quando na verdade só vai para `/Lang` do catálogo — PDF/A-1 proíbe
  `dc:language` (https://github.com/melker22/AI-Office/pull/7#discussion_r4331347176). PR #13:
  `docs/waves/wave23-integrity.md` D7 ainda descrevia `find_replace` chamando `shiftRevisionOffsets` por
  substituição, quando o código novo planeja todos os hits primeiro e usa `revisionAcceptsReplacement`; D10
  descrevia uma realimentação de layout que o código não faz; D5 omitia `undo`/`redo`/`checkpoint`/`restore`
  da lista que `toolForbiddenInBatch` já proibia
  (https://github.com/melker22/AI-Office/pull/13#pullrequestreview-5642889711, corrigido em `fecf1fc`). PR
  #15: `docs/AI_HANDOFF.md`, `docs/audit/fix-plan.md` e `docs/waves/wave24-docx-odt-fidelity.md` diziam
  que o encoder QR usa nível de ECC "L" quando o código entrega nível "M" e reserva os padrões de função
  antes de escrever os módulos de dado
  (https://github.com/melker22/AI-Office/pull/15#discussion_r4396808595); `fix-plan.md` também descrevia a
  validação de `w:color` como um regex `hex6` inexistente em vez do laço de `docxHexColor`, e `bidi` como
  RTL só para `"1"`/`"true"` quando `bidiIsOn` também aceita vazio e `"on"`
  (https://github.com/melker22/AI-Office/pull/15#discussion_r4396808602); e o D10 da wave 24 descrevia só a
  dedução por match exato `(block_id, kind, start, end)`, omitindo que `Docx.cpp` também descarta uma
  revisão nativa cujo intervalo está contido numa revisão JSON do mesmo bloco/tipo
  (https://github.com/melker22/AI-Office/pull/15#discussion_r4396808609) — todos corrigidos nos commits
  `c83b9c8`..`ac25f6e`; e um nitpick trivial não corrigido na mesma PR: o comentário do CMake sobre os
  ctests `wave17` e a nota de auditoria citando `zbar` em vez de `zbarimg`
  (https://github.com/melker22/AI-Office/pull/15#pullrequestreview-5681108253). PR #19 (onda 26): a
  decisão D8 prometia travessia de `forEachFieldRun` "em qualquer profundidade", quando o modelo entregue
  (`NestedCell` guarda runs, não outra tabela) só suporta um nível de tabela aninhada
  (https://github.com/melker22/AI-Office/pull/19#discussion_r4413566258, corrigido em `e54ee6b`,
  restringindo a prosa ao nível entregue). PR #20 (onda 27, achado fora do diff): a descrição MCP de
  `document_open`/`document_save` ainda não citava o import EPUB novo nem o array opcional `warnings`, e
  `document_save` ainda dizia que o import EPUB não é suportado (pullrequestreview-5747575553, corrigido
  em `a01e538`); o schema de `paragraphs` em `insert_footnote`/`insert_endnote` declarava só
  `type: array`, sem `items`, então um item não-string de `paragraphs` virava parágrafo vazio em silêncio
  (mesmo achado, corrigido em `a072074`, tipando `items` como string e documentando a precedência sobre
  `text`); `docs/mcp-tools.md` listava só 6 dos 10 contêineres que `EpubReader::isSkippedContainer`
  descarta, faltando `style`/`embed`/`noscript`/`template`
  (https://github.com/melker22/AI-Office/pull/20#discussion_r4454510119, corrigido em `714f100`); e
  `docs/ORCHESTRATION.md` descrevia os comandos `cockpit fleet pause`/`resume` sem o `<runId>`
  obrigatório (https://github.com/melker22/AI-Office/pull/20#discussion_r4451178316, corrigido em
  `5a843cb`). PR #21: a mesma `docs/ORCHESTRATION.md` dizia que o DeepSeek entra quando o Grok Build está
  sem cota, mas a regra nova da mesma linha intercala o Grok da Cursor (`grok-4.7-high`) antes do
  DeepSeek — duas instruções conflitantes na mesma tabela
  (https://github.com/melker22/AI-Office/pull/21#discussion_r4461119963, corrigido em `21a028a`). · 9
- **L8 — Portabilidade de compilador.** Forward declaration com a mesma tag (`struct`/`class`) da
  definição. · PR #6 (MSVC C4099). · 1
- **L24 — Id novo nunca vem do tamanho atual de uma coleção; aloque acima do maior sufixo numérico
  em uso, inclusive ids vindos de JSON.** `tamanho+1` (ou qualquer derivação de `size()`) como gerador de id colide assim que a
  coleção encolhe por dedup/remoção entre a alocação de dois ids do mesmo tipo — e colide de novo com um id
  importado de fonte externa (JSON) que já ocupava um número alto. · PR #15: `loadDocx` dava a toda marca
  nativa de revisão (`w:ins`/`w:del`/`w:rPrChange`) o id
  `QStringLiteral("rev%1").arg(doc.revisions.size() + 1)` em três pontos idênticos (delete, format, insert);
  como `flushP` removia revisões deduplicadas com `removeAt` entre uma marca e a próxima, `doc.revisions.size()`
  diminuía e o próximo id gerado colidia com um id de revisão nativa ou de JSON (ex.: `rev7`) já existente,
  fazendo accept/reject por id agir na revisão errada ou nas duas, e o re-export escrever o mesmo `w:id` em
  dois `w:ins`/`w:del` diferentes
  (https://github.com/melker22/AI-Office/pull/15#discussion_r4396808630); corrigido com `nextPrefixedId`
  (via `nextRevisionId`), que aloca acima do maior sufixo numérico já em uso, ids de JSON inclusos (commit `a718d3e`), com teste
  prévio cobrindo o caso (`qa/wave24-docx-d10-revision-ids.json`, commit `c83b9c8`, ids `rev1`/`rev2`/`rev7`
  mais marcas nativas parcialmente deduplicadas). PR #17 (onda 25): o id derivado por cópia do PDF
  (`-cN` anexado ao id de bloco para a nota de rodapé de cada cópia extra) colidia quando o documento já
  tinha notas `fn1` e `fn1-c1` — a segunda cópia de `fn1` e a primeira cópia de `fn1-c1` recebiam o mesmo
  `/Note /ID`
  (https://github.com/melker22/AI-Office/pull/17#discussion_r4408490330, corrigido em `66a2053`); o
  primeiro fix ainda colidia quando um id de bloco já tinha a forma de um id de cópia de outra nota,
  reaberto e corrigido exigindo que `note_ids` falhe também em duplicata
  (https://github.com/melker22/AI-Office/pull/17#discussion_r4408864387, corrigido em `ef1517d`). · 2

## Testes

- **L9 — Teste cobre o critério inteiro, inclusive a ordem que quebra.** Ex.: inserir no meio DEPOIS de
  já existir xref, não só antes. · Onda 18 review: playbook de `seq` só testava xrefs novos. · 1
- **L10 — Todo `expect`/gate obrigatório precisa poder falhar de verdade.** Expect vazio/nulo é erro;
  falha de transporte/IPC nunca casa com `{"ok":false}`; e um gate de CI não "passa" quando a ferramenta
  está ausente, quando um playbook não tem `expect` nenhum, quando o primeiro resultado bom interrompe a
  checagem dos demais, ou quando o diretório de playbooks está vazio — distinga sempre falha esperada real
  de "nada rodou". · PR #6 (`expect` vazio). PR #12: o loop de `aioffice-cli play` em
  `.cockpit/fleets/audit-codebase.yaml` aceitava qualquer falha (inclusive binário ausente) como
  reprodução confirmada
  (https://github.com/melker22/AI-Office/pull/12#discussion_r4368948993, corrigido em `a3870cd`);
  `qa/audit/A-session-1-undo-path.json` e os demais playbooks de auditoria não tinham `expect`
  (https://github.com/melker22/AI-Office/pull/12#discussion_r4368965483, corrigido em `f8d2701`);
  `scripts/check-audit-playbooks.sh` saía `0` com "0 audit playbook(s) checked" quando o diretório estava
  vazio (corrigido em `af6d8c9`); e `A-docx-3-odf-9-rtf-3-write-failure-ignored.json` testava DOCX, ODT e
  RTF num só playbook, parando no primeiro `ok:true` e nunca checando os outros dois formatos
  (https://github.com/melker22/AI-Office/pull/12#discussion_r4368989107, corrigido em `af6d8c9`). PR #13:
  `qa/check_wave23_lok.py` passava quando `document_export_pdf` falhava antes de chamar o LibreOffice (destino
  inalterado + CLI com saída não-zero), sem exigir evidência de que a publicação foi de fato tentada —
  corrigido exigindo que o interposer registre uma publicação no destino
  (https://github.com/melker22/AI-Office/pull/13#discussion_r4370637434, corrigido em `9e74fdc`). · 4
- **L11 — Ambiente hermético.** Tudo que a CI usa vem do Nix (fontes, Python, soffice opcional); nada
  do sistema do desenvolvedor. Teste com `nix-shell --pure`. · PR #3 fontes; PR #4 `python3`. PR #17
  (onda 25): os autotestes (`--self-test`) de RTF, EPUB e PDF chamavam a suíte da onda sem repassar o
  `--cli` que o CTest lhes passava, caindo num `build/linux` fixo que não existe num build
  `linux-debug`/Windows
  (https://github.com/melker22/AI-Office/pull/17#discussion_r4407576885, corrigido em `d0d649f`;
  https://github.com/melker22/AI-Office/pull/17#discussion_r4408490311, corrigido em `773dead`;
  https://github.com/melker22/AI-Office/pull/17#discussion_r4408490304, corrigido em `9c6ae89`;
  https://github.com/melker22/AI-Office/pull/17#discussion_r4409259834, corrigido em `3ef16ae`); e um
  playbook de teste que o CTest também roda direto (sem o rewrite de caminho temporário da suíte)
  dependia do mesmo `build/linux` fixo
  (https://github.com/melker22/AI-Office/pull/17#discussion_r4409010083, corrigido em `4df7899`). PR #19
  (onda 26): `qa/wave26-d8b-filepath.py` usava uma barra invertida dentro de uma expressão de f-string —
  sintaxe só válida a partir do Python 3.12 — e o `CMakeLists.txt` só exige Python 3, sem mínimo 3.12,
  então o script quebrava com `SyntaxError` num Python 3.10/3.11 válido para o projeto
  (https://github.com/melker22/AI-Office/pull/19#discussion_r4413566267, corrigido em `e82d179`,
  calculando a contagem fora da f-string). · 4
- **L21 — Diretório de trabalho compartilhado entre execuções usa lock com caminho estável e controlado
  pelo dono, nunca um caminho fixo em `/tmp` nem uma variável de ambiente que varia por processo.** Um
  caminho fixo em `/tmp` é alvo de symlink/TOCTOU por outro usuário local; um lock derivado de
  `XDG_RUNTIME_DIR` muda entre processos, então duas execuções tomam locks diferentes e corrompem as
  fixtures uma da outra. · PR #12: `qa/audit/setup.sh` apagava e recriava `/tmp/aio-audit` sem exclusão
  nenhuma com os playbooks que o liam (https://github.com/melker22/AI-Office/pull/12#discussion_r4368989111,
  corrigido em `af6d8c9`); o lock subsequente usava `exec 9>/tmp/aio-audit.lock`, um caminho previsível e
  symlinkável (https://github.com/melker22/AI-Office/pull/12#discussion_r4369326580, corrigido em
  `eb2d611`); e mesmo corrigido para um diretório do dono, a chave ainda vinha de `XDG_RUNTIME_DIR`, que
  varia por invocação (https://github.com/melker22/AI-Office/pull/12#discussion_r4369354620); corrigido com
  lock único em `$HOME/.cache` cobrindo `setup.sh` e todos os playbooks, mais diretório de trabalho privado
  por execução (commit `a8870a7`). PR #13: `scripts/fleet-verify-merged.sh` escrevia a saída do self-test em
  caminhos fixos `/tmp/fleet-verify-self-*.txt`; outro usuário local que criasse esses caminhos como symlink
  antes podia truncar um arquivo seu, arbitrário, pelo redirecionamento do self-test
  (https://github.com/melker22/AI-Office/pull/13#discussion_r4370398843, corrigido com `mktemp` em
  `061ea50`). · 2
- **L16 — Script de QA que chama ferramenta externa propaga a falha dela, não só imprime.** Passe
  `check=False` explícito em todo `subprocess.run` (nunca dependa do default) e trate o `returncode`/saída
  à mão; detecte "ferramenta ausente" com `shutil.which`/`FileNotFoundError`, nunca procurando "not found"
  no stderr (combina com diagnóstico normal da ferramenta); e se o resultado reportar não-conformidade,
  o script levanta/sai não-zero — nunca imprime e segue com `exit 0`. · PR #7 `qa/check_pdf.py` confundia
  "verapdf ausente" com qualquer stderr contendo "not found"
  (https://github.com/melker22/AI-Office/pull/7#discussion_r4331347189); PR #7
  `qa/check_wave19_pdf_tags.py` imprimia o resultado do veraPDF UA-1 e saía `ok`/0 mesmo não-conforme
  (https://github.com/melker22/AI-Office/pull/7#discussion_r4331347201). · 2
- **L19 — Autoteste exercita o ponto de entrada real, não só as funções puras.** Um `--self-test` que
  chama `evaluate()` com dados falsos passa mesmo com `main()` quebrado; inclua pelo menos um caso de
  ponta a ponta pelo `main` com a rede/IO falsos. · PR #10: o refactor do `scripts/pr-merge-gate.py`
  passou no autoteste e no QA 75/75, mas o caminho real lançava `TypeError` (tupla não desempacotada) e
  só apareceu no primeiro uso de verdade. · 1
- **L22 — Teste não depende de privilégio: a CI roda como root.** `chmod` de diretório, permissão de
  arquivo e "sem acesso" não bloqueiam root (imagem `nixos/nix` da CircleCI). Provoque a falha por um
  meio que vale para qualquer usuário — `/dev/full`, FIFO, nome acima de `NAME_MAX`, entrada malformada —
  e nunca faça o teste "recusar root" (vira CI vermelha, não proteção). · PR #13: `wave23_io_hostile`
  passava local e falhava na CI com "refusing to run as root". Idem para a glibc: um `LD_PRELOAD` de
  teste herdado por processos filhos quebrou na CI (`GLIBC_ABI_DT_X86_64_PLT`), onde `/usr/bin/env`
  usa outra glibc — o interposer remove o `LD_PRELOAD` ao carregar e o teste confere que não vazou. O mesmo
  interposer (`qa/place_convert_interpose.c`) tinha um defeito funcional independente de privilégio: ao
  interceptar `open`/`open64`/`openat`/`openat64` com `O_TMPFILE`, não repassava o argumento `mode`
  variádico ao `open` real, então todo arquivo `O_TMPFILE` criado pelo processo sob teste saía com a
  permissão errada (https://github.com/melker22/AI-Office/pull/13#discussion_r4370637431, corrigido com
  `needs_mode` em `95f66fa`). · 3
- **L23 — Classifique pelo metadado canônico, nunca por heurística de nome/substring sobre texto
  concatenado, nem por "texto vazio" como proxy de "sem conteúdo"; metadado ilegível falha a checagem,
  não passa por omissão.** Um checker de papel de
  documento (navegação vs. conteúdo num EPUB, cabeçalho vs. corpo) que decide por regex solto sobre um bundle
  de vários XML concatenados, ou por substring no nome do arquivo ("contém `nav`"), aceita qualquer entrada
  real porque o fallback nunca é exercitado (self-test só usa fragmentos de raiz única) ou produz falso
  positivo/negativo (um conteúdo chamado `canvas.xhtml`, um TOC com sub-capítulos cujo `<ol>` aninhado é
  legítimo). Decida pelo manifesto/OPF (`properties="nav"`), com href percent-decoded e sem fragmento/query, e
  se o OPF não parsear, falhe a checagem — nunca trate "não sei" como "não é navegação". · PR #13, três
  rodadas de CodeRabbit sobre `qa/check_epub.py`: `html_has_nested_list` usava `ET.fromstring` no bundle
  inteiro do EPUB (sempre `ParseError`, sempre regex) e a regex casava `<li>` e `<ol>` através de arquivos
  diferentes, aceitando listas lineares como aninhadas
  (https://github.com/melker22/AI-Office/pull/13#discussion_r4370398840, corrigido parseando cada XHTML
  isoladamente em `e11c988`); a correção seguinte ainda incluía `nav.xhtml` na busca, cujo TOC aninhado
  mascarava a ausência de lista aninhada nos capítulos
  (https://github.com/melker22/AI-Office/pull/13#discussion_r4370637426, corrigido excluindo documentos de
  navegação em `75fc7c3`); a exclusão usava substring `"nav"` no nome do arquivo, então um capítulo de
  conteúdo chamado algo como `navigator.xhtml` seria ignorado por engano, e o self-test com `canvas.xhtml`
  não cobria essa lacuna (nitpick trivial, não corrigido:
  https://github.com/melker22/AI-Office/pull/13#pullrequestreview-5643827857); os hrefs do manifesto OPF
  precisavam de percent-decoding e remoção de fragmento/query antes de comparar com os nomes do ZIP
  (https://github.com/melker22/AI-Office/pull/13#discussion_r4370742453, corrigido em `82f8385`); e por fim,
  quando o OPF é malformado, `_nav_document_names` pulava seu manifesto e tratava a ausência de metadado como
  "nenhum documento de navegação", deixando `toc.xhtml` contar como conteúdo
  (https://github.com/melker22/AI-Office/pull/13#pullrequestreview-5643779721, corrigido falhando a checagem
  quando o OPF não parseia, em `c545057`). PR #20 (onda 27): `splitNoteParagraphs` descartava todo run
  com texto vazio ao quebrar por U+2029, mas um run vazio pode carregar uma âncora de nota aninhada ou
  metadado de campo que o serializador ODF/DOCX precisa — texto vazio não significa run sem conteúdo
  (https://github.com/melker22/AI-Office/pull/20#discussion_r4451178322, corrigido em `fedd273`,
  preservando runs de texto vazio no parágrafo atual). · 6

## Processo (Fleets)

- **L12 — Executor entrega `commit`; integração junta o hash; gate verifica.** Todo nó que comita —
  inclusive opcional/auditoria — entra em `needs` do gate seguinte e tem `output_contract`/
  `success_output`; senão o gate não enxerga o commit e um nó que para sem commitar ainda passa por
  "completed". · Onda 17: nó caído marcado "completed"; merge de ponta velha → PR #5; PR #6 `qa-final`
  não listava `review-adjudication`/`opus-audit` em `needs` e `opus-audit` não tinha `output_contract`
  (https://github.com/melker22/AI-Office/pull/6#discussion_r4328866979); PR #7 `build-1` não listava o nó
  `lessons` em `needs` apesar de ele comitar
  (https://github.com/melker22/AI-Office/pull/7#discussion_r4331347135); PR #7 `review-adjudication`
  comita e pode retornar `verdict: failed`, mas não tinha `success_output`, então um veredito falho não
  parava `qa-final`/`handoff`/`notion-sync`
  (https://github.com/melker22/AI-Office/pull/7#discussion_r4331347151). · 5
- **L13 — Nó isolado de fase posterior começa com `git merge` da integração anterior.** · Onda 18. · 1
- **L14 — Revisor procura defeitos da lista acima antes de estilo.** O CodeRabbit achou defeitos que
  duas/três passadas do Sonnet deixaram passar em três ondas seguidas (PR #3, #4, #6). · 3
- **L18 — O gate de merge lê o corpo da revisão, não só os comentários inline.** O CodeRabbit põe no
  corpo ("Outside diff range comments") o que o GitHub não deixa postar inline; contar só inline deixou
  passar 5 achados reais. Usar `scripts/pr-merge-gate.py`. · PR #9 (2 achados: undo do título,
  exportação read-only), PR #9 1ª rodada (contagem dupla de notas no check_epub), PR #6
  (`document_stats` com blocos ocultos), PR #4 (catálogo). PR #10: o gate lia só as revisões do HEAD e
  um push logo após a revisão de `343e3c5` escondeu 2 achados de corpo (export read-only altera o
  documento; `stat pages` calculado antes de atualizar campos `if`) — todo achado de corpo, de qualquer
  commit (Outside diff range, Duplicate, Critical, Major, Minor), agora exige `cr-ack <id> <commit ou motivo>`.
  PR #10 também: o gate não paginava `comments`/`reviews` — um achado além da página 100 passaria
  despercebido (https://github.com/melker22/AI-Office/pull/10#discussion_r4343191716); não autenticava, e
  o limite de 60 req/h sem token é suficiente para derrubar checagens repetidas
  (https://github.com/melker22/AI-Office/pull/10#discussion_r4343191711); tratava `mergeable: null`
  (ainda calculando) como problema em vez de pendente
  (https://github.com/melker22/AI-Office/pull/10#discussion_r4343191724); comparava `--since` como texto
  em vez de instante, então um timestamp com offset diferente de `Z` ordenava errado
  (https://github.com/melker22/AI-Office/pull/10#discussion_r4343723124); e podia reportar `CLEAN` com a
  HEAD já obsoleta por não reler o PR antes do `exit 0`
  (https://github.com/melker22/AI-Office/pull/10#discussion_r4343723129) — corrigidos em
  `ee54b67`/`71d16d6`. PR #12: o regex de início de seção (`_SECTION_RE`) e o de limite do prompt
  (`_PROMPT_HEADING_RE`) casavam com qualquer trecho do corpo da revisão, não só com o título na posição
  de cabeçalho — um achado bloqueante cujo próprio texto citava `Nitpick comments (1)` ou `Prompt for all
  review comments` virava, ele mesmo, um limite de seção falso e escondia esse achado e todos os
  seguintes, em três rodadas sucessivas de revisão
  (https://github.com/melker22/AI-Office/pull/12#discussion_r4369491712,
  https://github.com/melker22/AI-Office/pull/12#discussion_r4369617528,
  https://github.com/melker22/AI-Office/pull/12#discussion_r4369645293; corrigidos ancorando o casamento
  à posição estrutural do cabeçalho, commits `bf03420`/`cf5ce76`/`cc094c4`); e `cr-ack <id>` sem commit
  nem motivo era aceito como reconhecimento válido (corrigido no mesmo PR). Regra: um gate que decide
  "limpo" só pode fazê-lo com evidência completa, autenticada, corrente (reler antes do `exit 0`) e
  ancorada em posição estrutural — nunca em substring de texto arbitrário. PR #13: `pr-merge-gate.py` tratava
  qualquer PR que listasse o commit HEAD como "revisão completa" mesmo quando esse PR era uma branch
  empilhada que só compartilha o commit, não a própria revisão
  (https://github.com/melker22/AI-Office/pull/13#discussion_r4370398845); e `head_pr_count` contava PRs
  abertos cujo branch contém o commit HEAD mas cujo próprio head é outro commit — um PR irmão inflava a
  contagem de "revisões pendentes" do PR correto
  (https://github.com/melker22/AI-Office/pull/13#discussion_r4370637439); corrigidos juntos, escopando o
  caminho "sem revisão" a reviews cujo `commit_id` é exatamente o head deste PR (`b67e915`/`fecf1fc`).
  PR #16: a checagem de identidade do bot do CodeRabbit aceitava qualquer login com prefixo
  `coderabbit`, não o bot exato
  (https://github.com/melker22/AI-Office/pull/16#discussion_r4406774015); uma resposta de comando
  ("Review finished"/"Already reviewed the last commit") sem status nem objeto de revisão virava prova
  de revisão completa, mesmo quando esse texto aparece por rate-limit sem revisão de fato
  (https://github.com/melker22/AI-Office/pull/16#discussion_r4406774021); a checagem de frescor dessa
  resposta comparava com a data de commit do HEAD em vez do SHA revisado exato, então editar uma
  resposta antiga (`updated_at`) também passava como evidência fresca
  (https://github.com/melker22/AI-Office/pull/16#discussion_r4406774028); e o fallback de resposta se
  aplicava mesmo quando já havia um status CodeRabbit dizendo "Review paused", mascarando esse pendente
  como limpo
  (https://github.com/melker22/AI-Office/pull/16#discussion_r4406774035); corrigidos todos no mesmo
  commit `d178478`. PR #18 (`scripts/pr-watch.py`, mesma classe de defeito num consumidor diferente da
  API do CodeRabbit): `reply_outcome` aceitava o marcador de resposta
  (`<!-- This is an auto-generated reply by CodeRabbit -->`) e o aviso de rate-limit como substring
  dentro de texto arbitrário, então um comentário comum que citasse esse texto entre aspas virava prova
  de "sem revisão"/"rate limited"
  (https://github.com/melker22/AI-Office/pull/18#pullrequestreview-5699977040,
  https://github.com/melker22/AI-Office/pull/18#discussion_r4414256046, corrigidos em
  `cdf1b5a`/`658d648` exigindo o marcador como linha inteira âncorada por regex `(?m)^...$`, não
  citação); uma resposta de bot dentro de uma thread de review (corpo vazio) contava como revisão do
  head, mascarando um head na verdade sob rate-limit (commit `d3f2080`); e os endpoints de
  status/check-run da CI eram buscados só na primeira página (`per_page=100`), então um status de falha
  além do item 100 escaparia de `--until all` — mesmo padrão do PR #10 (pt. 2 abaixo), agora no watcher
  em vez do gate (https://github.com/melker22/AI-Office/pull/18#pullrequestreview-5705791800, **achado
  da revisão final, sem correção nem `cr-ack` antes do merge** — o autor mergeou 1 minuto depois, sem
  rodar o gate contra esse achado). PR #20: o próprio CodeRabbit trocou o formato do corpo de revisão
  (blocos `<details>` com selo de imagem `alt="Outside diff"`/`"Nitpick"`/... e severidade em negrito,
  abrindo com uma lista de status de marcadores `cr-comment`, em vez das seções antigas por título) e o
  parser do gate/watcher, que só conhecia títulos de seção, não achava NENHUM achado de corpo no novo
  formato — um achado de corpo real (outside-diff) teria sido mergeado sem `cr-ack` (identificado e
  corrigido dentro da própria PR #20, commit `69ba6fae`, com self-test sobre o corpo real da PR: o achado
  outside-diff conta, o nitpick trivial não, e a revisão conta como revisão do head); e, no mesmo novo
  formato, um achado que cita um marcador `cr-comment` dentro de um bloco de código no próprio texto do
  achado podia fazer o parser extrair esse marcador citado (dentro do fence) como o id real do achado em
  vez do marcador verdadeiro no fim do bloco — um achado forjado dentro de um trecho de código citado por
  outro achado escondia o marcador real e podia colidir com o `cr-ack` de outro achado (corrigido no
  commit `48b3ae7`, lendo o id do fim do próprio bloco do selo com o conteúdo de blocos de código apagado
  antes; self-test com marcador forjado dentro de um fence). PR #21 (`scripts/pr-watch.py`, mesma classe
  de defeito): `cr_state` escolhia o evento mais novo entre respostas e avisos de rate-limit por
  timestamp, mas só checava o aviso de "auto-review pausado" depois, num scan de retorno antecipado
  separado — um aviso de pausa mais antigo podia sobrepor uma resposta/rate-limit mais nova, ou o
  inverso, violando a própria regra "o evento mais novo decide" que o código já aplicava aos outros tipos
  (https://github.com/melker22/AI-Office/pull/21#discussion_r4461085755, corrigido em `21a028a`, tornando
  o aviso de pausa um evento com timestamp na MESMA coleção, scan separado removido; self-test cobre as
  duas ordens); e esse aviso de pausa usava `updated_at` do comentário inteiro como prova de "aviso
  novo", mas esse campo também muda quando só um checkbox ou outro trecho do resumo é editado sem tocar
  no marcador de pausa — um editor que resolvesse o checkbox depois de uma revisão já pedida reativava
  `paused` mesmo sem pausa nova
  (https://github.com/melker22/AI-Office/pull/21#discussion_r4461214432, corrigido no mesmo commit
  `21a028a`, restringindo a detecção de pausa a quando nenhuma revisão foi pedida para o head atual). O
  "evento mais novo decide" vale para TODO tipo de sinal, inclusive casos especiais tratados em código
  separado; e o timestamp de um objeto composto (comentário/resumo) não prova que o campo específico que
  importa mudou — restrinja a janela em que esse sinal é válido em vez de confiar no timestamp do
  contêiner. · 18
- **L17 — O nó `lessons` lê handoff atual E arquivo antes de avançar o marcador.** Se a entrada de
  handoff de um PR ainda não processado já foi movida para `AI_HANDOFF_ARCHIVE.md`, ler só
  `AI_HANDOFF.md` perde o registro de intervenção e o Passo 4 marca esse PR como processado sem revisitar
  essa evidência. · PR #7 `.cockpit/fleets/task.yaml` não instruía a busca no arquivo
  (https://github.com/melker22/AI-Office/pull/7#discussion_r4331347131, corrigido em `1ae66dc` e
  replicado na instrução deste nó). · 1
- **L25 — Nó Claude não usa skills, subagentes nem tarefas em segundo plano, e nunca submete saída
  "provisória".** Um nó que roda uma etapa incompleta e ainda assim preenche o `output_contract` como se a
  etapa tivesse terminado (ex.: `issues: 0` sem ter revisado nada, `qa_green: false` como resposta
  definitiva em vez de rodar a correção e tentar de novo) corrompe a decisão dos nós seguintes do DAG, que
  não têm como distinguir "revisado e limpo" de "não revisado". · Fleet wave24 (Run `4712f7db`):
  `review-pass` submeteu `issues: 0` "provisório" e `fix-pass` submeteu `qa_green: false` "provisório";
  Fleet audit (Run `6fda0b72`). · 2
- **L26 — Nó de agente não roda build completo do zero.** O tempo de QA longo é do gate de comando
  (timeout do nó) ou de um build já aquecido (cache/`ccache`/árvore de build reaproveitada); um nó que
  decide recompilar tudo do zero gasta o próprio orçamento de tempo só na compilação e nunca chega a
  rodar o QA que deveria reportar. · Fleet wave24-finish (Run `a41c009a`): o nó `stabilize` estourou o
  tempo de um comando com um build completo do zero. · 1
- **L27 — Ninguém usa `git stash`.** A stash é compartilhada entre `main` e todos os worktrees; `stash
  pop`/`stash apply` sem filtro explícito pode restaurar o WIP de outro agente no checkout errado,
  destruindo ou misturando trabalho que não é seu. Use um commit WIP na própria branch/worktree, nunca a
  stash global. · Um `pop` no checkout principal recolheu a stash de um agente que trabalhava no worktree
  do PR #15. · 1
- **L28 — Uma condição sobre um grupo de itens (nós interrompidos, réplicas, markers) só vale quando
  TODOS os membros a satisfazem — nunca quando algum membro a satisfaz; e um membro com dado
  ausente/inválido não libera a condição por omissão.** Um `any()`/existência basta para "há um item
  limitado", mas "pode retomar"/"pode concluir" exige "todos os itens satisfazem", incluindo os que o
  código não sabe avaliar. · PR #14 (`scripts/usage-guard.py`): `fleet resume` disparava ao existir
  QUALQUER nó interrompido por limite de uso no Run, sem esperar o reset de TODOS os nós limitados —
  um Run com dois nós de reset diferente retomava no primeiro reset, antes do segundo
  (https://github.com/melker22/AI-Office/pull/14#pullrequestreview-5669475245, corrigido em `f92f9b0`); e
  um nó reconhecido como limitado por uso mas cujo reset é nulo/não interpretável escapava do
  `max(... if t)` porque a comprehension descarta o `None`, liberando o resume mesmo com esse nó ainda
  potencialmente limitado
  (https://github.com/melker22/AI-Office/pull/14#pullrequestreview-5677842045, corrigido em `04efa55`).
  PR #18 (`scripts/pr-watch.py`): um objeto de review sem `submitted_at` (ou em estado `PENDING`) virava
  `reviewed` por padrão em vez de pendente — dado ausente/inválido liberando a condição por omissão, a
  mesma classe de defeito do PR #14, aqui num item em vez de num grupo
  (https://github.com/melker22/AI-Office/pull/18#pullrequestreview-5704553222, corrigido em `f13df51`
  exigindo `submitted_at` não-nulo e `state != "PENDING"`). · 2
- **L29 — `--dry-run`/simulação nunca persiste estado real (audite todo caminho de escrita, não só o
  óbvio); e um marcador de evento pendente (retry/warning/done) é reaproveitado para a MESMA
  interrupção, nunca recriado com timestamp novo a cada verificação.** Recriar o marcador a cada scan
  retenta uma falha lenta em todo ciclo, atrasando itens não relacionados; e uma simulação que grava o
  marcador como concluído suprime o scan real do mesmo evento pelo período inteiro de dedupe. · PR #14
  (`scripts/usage-guard.py`): `once --dry-run` encontrava um Run com loop limitado por uso vencido,
  simulava o resume com sucesso e ainda assim marcava o marcador do hook como concluído, deixando o Run
  pausado de verdade e suprimindo o próximo scan normal pela janela de dedupe de 7 dias
  (https://github.com/melker22/AI-Office/pull/14#pullrequestreview-5669475245, corrigido em `f92f9b0`);
  PR #16: `add_marker()` atribuía um timestamp novo à mesma interrupção em cada scan em vez de reusar o
  marcador pendente existente, então uma falha repetida de `fleet resume` reexecutava a cada ciclo em
  vez de esperar o intervalo de retry
  (https://github.com/melker22/AI-Office/pull/16#discussion_r4406774042, corrigido no commit `d178478`).
  · 2
