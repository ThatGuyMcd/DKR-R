# Relatório — Como funciona a IA (bots) do Diddy Kong Racing

> Contributor research record. The original language, attribution and retail
> findings are retained. For current custom AI/online behaviour, use the
> [mod guide](docs/LEGACY-MODS-BETA.md) and [documentation index](docs/README.md).

> **Fontes:** decomp em [extern/dkr-decomp/](extern/dkr-decomp/) (a lógica é a mesma em US v77 e v80), mais uma varredura dos
> 136 mapas de objetos e dos level headers extraídos do ROM (`extern/dkr-decomp/assets/.vanilla/us.v77/`).
> Os nomes `unkXX` e `func_XXXXXXXX` são os do decomp. Quando dou um nome em português ("faixa", "tag", "estado"),
> é interpretação minha, baseada no que o código faz com o campo.

---

## TL;DR

- **Existem três IAs diferentes.** O jogo escolhe uma pelo tipo da fase, em
  [`racer_AI_pathing_inputs`](extern/dkr-decomp/src/racer.c#L609):

  | Tipo de fase (`race_type`) | Função | Como navega |
  |---|---|---|
  | Corrida, chefe, desafio do Taj, hub | [`func_80045C48`](extern/dkr-decomp/src/racer.c#L1360) | Spline pelos **checkpoints**, em uma de 4 "faixas" |
  | `CHALLENGE_BATTLE` (64) e `CHALLENGE_BANANAS` (65) | [`racer_ai_challenge`](extern/dkr-decomp/src/racer.c#L682) | Grafo de **AI Nodes** com busca de menor caminho |
  | `CHALLENGE_EGGS` (66) | [`racer_ai_eggs`](extern/dkr-decomp/src/racer.c#L1052) | Mira direto em objetos (sem caminho nenhum) |

- **O checkpoint é um "trilho" com 4 faixas.** Cada faixa tem um deslocamento lateral, um vertical e uma
  "route flag". O bot passa por uma spline que liga os checkpoints, deslocada pela faixa em que ele está, e troca de
  faixa para ultrapassar.
- **Na batalha, o bot não sabe que precisa eliminar ninguém.** Ele roda uma máquina de 8 estados: pegar balão,
  passear, caçar uma vítima (escolhida por quantidade de bananas, com preferência pelo jogador 1) e fugir para se rearmar.
  Por cima disso, a cada frame **atira em qualquer oponente que entre num cone de ±11° à frente, a menos de 800
  unidades**. A regra "bananas = vidas" é aplicada fora da IA. A eliminação é consequência desse comportamento.
- A dificuldade vem de 10 tabelas binárias de comportamento. Essas tabelas **se adaptam ao jogador durante a
  corrida**: se você faz boosts e dispara armas, os bots passam a fazer mais disso.

---

## 1. Arquitetura geral: o bot aperta botões num controle virtual

[`update_AI_racer`](extern/dkr-decomp/src/racer.c#L8656) roda para cada bot, a cada frame, em um de dois modos.

### 1.1 Modo "física completa" (`unk201 != 0`)

A IA preenche um controle virtual (`gCurrentStickX/Y`, `A`, `B`, `Z`) e os **mesmos** handlers de física do jogador
consomem essas entradas: `func_8004F7F4` (carro), `func_80046524` (hover), `func_80049794` (avião), `update_tricky`
etc. Ou seja, o bot dirige com a mesma física que você.

`unk201` é uma janela de 30 frames. Ela é rearmada quando:

- o bot é desenhado em alguma tela ([objects.c:3761](extern/dkr-decomp/src/objects.c#L3761));
- o bot está a menos de 400 unidades do P1 ou P2 (`160000 = 400²`);
- a fase é qualquer desafio (`race_type & 0x40`), desafio do Taj, corrida de chefe ou tem o looping;
- faltam poucos frames da largada (`gRaceStartTimer`, `D_8011D544 > 120`);
- o próximo checkpoint tem route flag 5.

O decremento fica em [racer.c:4203](extern/dkr-decomp/src/racer.c#L4203).

### 1.2 Modo "trilho" (fora da tela)

[`func_8005B818`](extern/dkr-decomp/src/racer.c#L8961) desliga a física. O bot é **arrastado ao longo da spline dos
checkpoints** na velocidade-alvo (×1,3 com boost), com passo máximo de 35 unidades por frame. Nesse modo ele continua
decidindo e usando itens (`func_80042D20` + `handle_racer_items`). Por isso bots longe da câmera nunca batem nem erram
curva. Esse modo exige checkpoints: sem nenhum, a função simplesmente retorna.

### 1.3 Anti-travamento (todas as IAs)

Em [racer.c:627-670](extern/dkr-decomp/src/racer.c#L627): se o bot fica quase parado e no chão por 60 frames, ele dá
**ré por 60 frames** (stick invertido, `B`, stick Y = −50) e só pode repetir depois de 120 frames. Ao destravar:

- **na corrida**, muda de faixa (`faixa = (faixa + 1) & 3`);
- **na batalha e nas bananas**, volta a mirar no nó anterior.

O stick virtual é sempre limitado a ±75.

---

## 2. O objeto Checkpoint, campo a campo

Struct [`LevelObjectEntry_Checkpoint`](extern/dkr-decomp/include/level_object_entries.h#L131), com 28 bytes.

O objeto em si não faz nada: [`obj_loop_checkpoint`](extern/dkr-decomp/src/object_functions.c#L3209) é vazio. Quem
trabalha é [`checkpoint_update_all`](extern/dkr-decomp/src/objects.c#L5533). Ela filtra os checkpoints, ordena, pareia
as rotas alternativas e copia tudo para o array global `gTrackCheckpoints` (`CheckpointNode`,
[objects.h:233](extern/dkr-decomp/src/objects.h#L233)). Os corredores leem esse array.

| Campo no addon | Byte | O que vira em runtime | O que faz |
|---|---|---|---|
| `scale` | 0x08 | `trans.scale = max(scale, 5) / 64`<br>`node.scale = trans.scale × 2`<br>`node.checkpointID = scale × 2` (o campo é **sobrescrito**) | Largura do portal. É também o **"metro" das faixas**: 1 unidade de offset = `node.scale` unidades de mundo (com `scale` 64, dá 2). E define o **raio de captura da rota alternativa**: `scale × 2` unidades. |
| `index` | 0x09 | posição no array | Ordem do percurso (bubble sort). Index duplicado imprime `"Error: Multiple checkpoint no: %d !!"` na tela. O último checkpoint principal liga de volta ao 0, e **a volta conta quando `nextCheckpoint` volta a 0**. |
| `angleY` | 0x0A | `rotation.y = v << 10` (64 passos de 5,625°) | Orienta o **plano** do portal. A normal desse plano define o que é "atravessar". Só há rotação Y, então o portal é sempre vertical. |
| Lateral offset, lane 1–4 (`unkB`–`unkE`) | 0x0B–0x0E | `node.unk2E[0..3]` | Desloca o ponto da faixa *i* para o lado: `pos + node.scale × offset × (rotZ, 0, −rotX)`. O padrão do retail é **−64, −22, 22, 64**. |
| Vertical offset, lane 1–4 (`unkF`–`unk12`) | 0x0F–0x12 | `node.unk32[0..3]` | Desloca a faixa em Y (`× node.scale`). Útil para aviões. O menor valor visto no retail é −51. |
| Route flag, lane 1–4 (`unk13`–`unk16`) | 0x13–0x16 | `node.unk36[0..3]` | Comportamento especial **por faixa**, avaliado quando este é o **próximo** checkpoint do corredor. Detalhes em [§2.1](#21-route-flags). |
| `isAltCheckpoint` | 0x17 | `id += 255` | Portal da rota alternativa. Faz par com o portal principal de mesmo `index` (`altRouteID`). |
| `unk18` | 0x18 | — | **Não é lido** pelo jogo. Vale sempre −1 no retail. |
| `unk19` | 0x19 | `node.unk3B` → `racer.indicator_type` por 120 frames | **Seta de direção no HUD**, mostrada ao atravessar o portal. Detalhes em [§2.2](#22-setas-do-hud-unk19). |
| `vehicleType` | 0x1A | filtro de carga | **Conjunto de checkpoints.** Só entram os que têm `vehicleType == header.unk4F[veículo]`. Detalhes em [§2.3](#23-vehicletype--headerunk4f). |
| `unk1B` | 0x1B | — | Não usado (sempre 0). |

Na carga, o jogo ainda calcula para cada nó:

- o plano (normal + `d`);
- `distance`, a distância até o próximo nó;
- `altDistance`, a distância até o próximo nó na rota alternativa.

`distance` é o que o jogo usa para medir o quanto um corredor está à frente de outro
([`racer_calc_distance_to_opponent`](extern/dkr-decomp/src/objects.c#L7052)).

### 2.1 Route flags

| Valor | Efeito | Afeta | Onde aparece no retail |
|---|---|---|---|
| 0 | nada | — | quase todos |
| 1 | O chefe que corre a pé toca a animação de **curva** enquanto vai até este checkpoint ([vehicle_bluey.c:120](extern/dkr-decomp/src/vehicle_bluey.c#L120), [vehicle_wizpig.c:134](extern/dkr-decomp/src/vehicle_wizpig.c#L134)) | Bluey e Wizpig | Wizpig 1 (19 checkpoints, só na faixa 1) |
| 2 | Ao atravessar o portal, **força a rota alternativa** (`isOnAlternateRoute = TRUE`) | só IA ([racer.c:4479](extern/dkr-decomp/src/racer.c#L4479), [racer.c:8846](extern/dkr-decomp/src/racer.c#L8846)) | Fossil Canyon, Crescent Island, Snowball Valley — 1 checkpoint cada, **só na faixa 1** |
| 3 | não é lido | — | nunca |
| 4 | **Zona de freio**: se a velocidade passa de 4, multiplica por 0,9 a cada frame | só IA em física completa ([racer.c:8840](extern/dkr-decomp/src/racer.c#L8840)) | Tricky 1 e 2 (3 checkpoints) |
| 5 | Força física completa (`unk201 = 30`) e, **se o bot estiver na água**, manda para a rota alternativa | só IA ([racer.c:8831](extern/dkr-decomp/src/racer.c#L8831), [racer.c:4471](extern/dkr-decomp/src/racer.c#L4471)) | Boulder Canyon (1 checkpoint, a ponte levadiça) |
| 6 | `lap = laps + 1`: **encerra a corrida** de quem está indo para este checkpoint | todos ([racer.c:4475](extern/dkr-decomp/src/racer.c#L4475), [racer.c:8837](extern/dkr-decomp/src/racer.c#L8837)) | Final das corridas de chefe: Tricky 1/2, Bluey 1/2 |

> **Por que ela é por faixa:** a flag 2 aparece só na faixa 1 no retail. Resultado: **só os bots que estão naquela
> faixa pegam o atalho**, e a divisão entre os caminhos parece natural. Chefes andam sempre na faixa 1 (índice 0),
> então a flag 1 também só precisa estar nela.

### 2.2 Setas do HUD (`unk19`)

Enum `INDICATOR_*` ([game_ui.h:70](extern/dkr-decomp/src/game_ui.h#L70)); desenho em
[`hud_course_arrows`](extern/dkr-decomp/src/game_ui.c#L980).

| Valor | Seta |
|---|---|
| 0 | nenhuma |
| 1 / 2 / 3 | esquerda 45° / esquerda 90° / retorno em U à esquerda |
| 4 / 5 / 6 | direita 45° / direita 90° / retorno em U à direita |
| 7 / 8 | subir / descer |
| 9 (e qualquer outro) | "!" (exclamação) |

A seta só aparece com 1 jogador e com a opção "course directions" ligada. Com as pistas espelhadas (Adventure 2), os
valores menores que 30 são espelhados automaticamente.

### 2.3 `vehicleType` + `header.unk4F`

`gTajChallengeType = header.unk4F[veículo]` ([game.c:497](extern/dkr-decomp/src/game.c#L497)).
[`checkpoint_update_all`](extern/dkr-decomp/src/objects.c#L5562) carrega **apenas** os checkpoints cujo `vehicleType`
bate com esse valor. Assim, uma mesma pista pode ter um caminho diferente para cada veículo.

| Fase | `unk4F` [carro, hover, avião] | Uso |
|---|---|---|
| Central Area (hub) | [0, 1, 2] | Os 3 desafios do Taj: 11, 21 e 20 checkpoints |
| Ancient Lake, Fossil Canyon, Treasure Caves | [0, 0, 1] | O avião segue o conjunto 1 |
| Everfrost Peak, Star City, Windmill Plains | [1, 1, 0] | Carro e hover seguem o 1; o avião, o 0 |
| demais | [0, 0, 0] | Um conjunto só |

O validador do addon já trata a unicidade do `index` por par `(vehicleType, isAltCheckpoint)`, que é o correto.

### 2.4 Como "atravessar" funciona

[`checkpoint_is_passed`](extern/dkr-decomp/src/objects.c#L5699):

- Mede a fração `checkpoint_distance` entre o portal anterior e o atual. **Basta cruzar o plano infinito** do portal.
  A largura (`scale`) não limita a contagem; ela só serve para as faixas e para o raio da rota alternativa.
- Para bots, se a fração sair de [−0,3 ; 1,3], a função retorna −100 e o bot **recua um checkpoint**
  (`racer_update_progress`).
- **Entrada na rota alternativa (vale para todos):** num ponto de bifurcação (o anterior não tem par e o atual tem),
  se o corredor está a menos de `scale × 2` unidades do portal alternativo, ele troca de rota. Ao chegar num
  checkpoint sem par, volta para a rota principal.
- **Contramão:** se a direção do jogador difere mais de 90° da tangente da spline, o contador de "wrong way" sobe
  ([`func_80059208`](extern/dkr-decomp/src/racer.c#L8131)). A mesma função mede o offset lateral do jogador em relação
  à linha central.
- **Desafio do Taj:** se esse offset lateral passar de ±400 (×`node.scale`), o desafio termina por **fora dos
  limites** (`CHALLENGE_END_OOB`, [racer.c:4542](extern/dkr-decomp/src/racer.c#L4542)).

---

## 3. Como o bot dirige numa corrida

### 3.1 Seguindo a spline

[`func_80045C48`](extern/dkr-decomp/src/racer.c#L1360) faz o seguinte a cada frame:

1. Pega 4 checkpoints (`next − 2` até `next + 1`), respeitando a rota alternativa se o bot estiver nela.
2. Soma a cada um o **offset da faixa atual**, lateral e vertical.
3. Interpola com Catmull-Rom/spline cúbica e mira num ponto **500 unidades à frente** na tangente.
4. Converte o erro de ângulo em stick X (direção) e stick Y (inclinação, para avião). Há ajustes por veículo: o hover
   não usa inclinação, e Tricky e Smokey têm sensibilidades próprias.
5. Se o erro passa de cerca de 67° (`0x3000`), mira no fim do trecho em vez de adiantar.

`unk1BA`/`unk1BC` guardam o offset lateral/vertical atual interpolado, limitado a ±64/±40. O **míssil teleguiado** usa
esses valores para seguir a mesma faixa de quem o disparou ([object_functions.c:5079](extern/dkr-decomp/src/object_functions.c#L5079)).

Sem nenhum checkpoint, o bot só aplica stick +25 e **anda em círculo**.

### 3.2 Faixas (`unk1CA`, de 0 a 3)

- **Primeiros 5 s** (`D_8011D544 = 300` frames): a faixa depende da posição. 1º a 3º vão para a faixa 0, 4º e 5º para
  a 1, e 6º a 8º para a 2 (`D_800DCDA0`, [racer.c:319](extern/dkr-decomp/src/racer.c#L319)).
- **Depois, a cada 5 s:** faixa = `D_800DCDA8[nº de bots à frente]` = {1,1,1,2,3,2,3,2}. Com a chance da linha
  "blue balloon" da tabela, ainda desce uma faixa ([racer.c:411](extern/dkr-decomp/src/racer.c#L411)).
- **Ultrapassagem:** se o bot da frente está na **mesma faixa** a menos de 100 unidades, sobe uma faixa (até a 3).
  Na faixa 3, se o da frente se afasta mais de 500, volta para a 2 ([racer.c:442](extern/dkr-decomp/src/racer.c#L442)).
- **Faixas externas são mais rápidas:** a velocidade-alvo ganha +0,2 (no espaço da raiz quadrada) por faixa acima da 1
  ([racer.c:432](extern/dkr-decomp/src/racer.c#L432)).
- **Bot líder disparado:** se ninguém à frente é bot e o de trás está a mais de 2500 unidades, ele fica na faixa 1.
- **Chefes e Taj** ficam sempre na faixa 0, sem modos de mira.

### 3.3 Dificuldade e velocidade

A tabela de comportamento é o asset `ASSET_AI_BEHAVIOUR`, com 10 níveis, carregado em
[`aitable_init`](extern/dkr-decomp/src/game.c#L705). O nível é escolhido pelo bloco `ai-levels` do header da fase:

- `base` → `completed` (fase vencida) → `silver-coins` (moedas de prata) → `tracks-mode` → `trophy-race`;
- a **Adventure 2** usa o segundo conjunto de valores;
- o cheat **Ultimate AI** força o nível 9; o menu e o modo demonstração usam o 5.

As 10 tabelas, decodificadas:

| Nível | speedMin | speedMax | empowered (min,max,+min,+max) | ganha upgrade | ataque | balão azul |
|---|---|---|---|---|---|---|
| 0 | −6,0 | −1,5 | 0,5,10,20 | 0,0,5,10 | 20,30,10,20 | 0,10,5,10 |
| 1 | −5,0 | 0,0 | 5,10,10,20 | 5,10,10,20 | 20,40,15,20 | 5,15,10,20 |
| 2 | −4,0 | 1,5 | 10,20,15,25 | 10,20,15,25 | 40,60,20,40 | 10,20,15,25 |
| 3 | −3,0 | 2,5 | 15,25,20,30 | 15,25,20,30 | 50,70,20,50 | 15,25,20,30 |
| 4 | −1,5 | 3,5 | 20,30,35,50 | 20,30,35,50 | 50,80,30,60 | 20,30,35,50 |
| 5 | −0,5 | 4,0 | 30,100,10,0 | 30,100,10,0 | 60,100,5,0 | 100,100,0,0 |
| 6 | 0,0 | 6,0 | 70,70,100,100 | 70,70,100,100 | 70,70,100,100 | 70,70,100,100 |
| 7 | 2,0 | 8,0 | todos 100 | todos 100 | todos 100 | todos 100 |
| 8 | 5,0 | 10,0 | todos 100 | todos 100 | todos 100 | todos 100 |
| 9 | 7,0 | 10,0 | todos 100 | todos 100 | todos 100 | todos 100 |

Como esses números são usados:

- **Velocidade-alvo (`unk124`):** interpolada entre `speedMin` (bot com 7 bots à frente) e `speedMax` (bot que lidera
  entre os bots), convertida por `sqrt((x·0,025 + 0,561) / 0,004)`. Os bots da frente andam mais rápido, o que
  **espalha o pelotão**.
- **Percentuais:** também são interpolados entre `min` (último) e `max` (primeiro) conforme a posição do bot.
- **Habilidade por personagem:** `header.unkC[personagem]` (normal) e `header.unk16[personagem]` (trophy race) viram
  `aiSkill` (Master…Easy). Isso controla a reação na largada: quanto mais hábil, antes o bot acelera. Em corrida rápida
  multiplayer, `aiSkill` é sorteado entre Master e Hard.

**A IA copia as suas técnicas.** [`increment_ai_behaviour_chances`](extern/dkr-decomp/src/racer.c#L535) roda para cada
**jogador humano** ([objects.c:2897](extern/dkr-decomp/src/objects.c#L2897)) e aumenta, pelos passos `+min/+max`:

- a linha "balão azul", quando você começa um boost;
- a linha "empowered", quando você solta o A por mais de 20 frames durante um boost;
- a linha "ganha upgrade", quando você melhora um balão;
- a linha "ataque", quando você dispara um projétil.

Tudo é limitado a 100, e a tabela é recarregada a cada fase.

### 3.4 Itens na corrida ([`func_80042D20`](extern/dkr-decomp/src/racer.c#L181))

Quando o bot pega um balão:

- **Upgrade grátis:** com a chance "ganha upgrade", um balão de nível 0 sobe de nível.
- **Trapaça contra o líder:** se **o humano está em 1º** e este bot está entre os 3 primeiros bots, com a chance
  "balão azul" o balão dele vira **boost**.
- Depois, o bot age conforme [`gRacerAIBalloonActionTable`](extern/dkr-decomp/src/racer.c#L94):

  | Ação | Armas | O que o bot faz |
  |---|---|---|
  | 1 | foguete, teleguiado | **Mira à frente** (`unk1C9 = 4`): copia a linha lateral de quem está à frente e dispara após um atraso **por personagem** (misc asset 2 × 60 frames). Contra outro bot, dispara sempre; contra humano, só com a chance "ataque" e se o humano estiver em 1º ou 2º. |
  | 2 | mina, óleo, bolha | **Mira atrás** (`unk1C9 = 5`): copia a linha de quem vem atrás e solta a armadilha. |
  | 4 | nitro | Usa quando não está em boost, com a chance "balão azul". |
  | 3/5/6/0 | escudos, ímãs | **Nenhuma lógica dedicada.** Ímã de bot nem ativa: `handle_racer_items` só liga o ímã para humanos. |

Outros comportamentos:

- **Alvo do teleguiado:** o bot mira no corredor **imediatamente à frente na classificação**, não no mais próximo
  geometricamente ([racer.c:7278](extern/dkr-decomp/src/racer.c#L7278)).
- **Bloqueio:** o bot fecha a passagem copiando a linha lateral do humano (`unk1C9 = 5`), mesmo sem arma
  ([racer.c:407](extern/dkr-decomp/src/racer.c#L407)), quando todas estas condições valem:
  - o humano está menos de 200 unidades atrás;
  - o humano está entre os 3 primeiros;
  - o bot não é o primeiro dos bots;
  - a "relação" entre os dois personagens (misc asset 1, matriz 10×10) é menor que 5.
- **Boost "empowered":** durante um boost, com a chance "empowered", o bot solta o acelerador para ganhar o boost maior.
- **Vozes:** quando ultrapassa ou é ultrapassado por um humano, o bot fala.

### 3.5 Chefes e Taj

- A velocidade vem de uma tabela fixa: misc asset 17 (desafio do Taj) e misc asset 18 (chefes).
- **Rubber band dos chefes** ([racer.c:481](extern/dkr-decomp/src/racer.c#L481)), durante 15 s depois da largada:
  - +5 de velocidade nos primeiros 3 s;
  - depois disso, +10 se o chefe estiver em 2º ou se o jogador estiver a até 200 unidades atrás.
- Passados os 15 s: +2 se o chefe estiver em 2º e o líder estiver a mais de 650 unidades à frente.
- Ao terminar a corrida, o chefe freia até parar.

---

## 4. Batalha e desafios: sem checkpoints

### 4.1 Por que não usam checkpoints

`racer_AI_pathing_inputs` desvia os tipos de desafio para outras funções. As arenas do retail (Darkwater Beach, Icicle
Pyramid, Smokey Castle e Fire Mountain) têm **zero checkpoints**. No lugar deles, há um grafo de **AI Nodes**
(batalha e bananas) ou alvos diretos (ovos).

Três detalhes importantes:

- A IA de desafio **só funciona com exatamente 4 corredores** ([racer.c:710](extern/dkr-decomp/src/racer.c#L710)).
  Com menos, os bots ficam parados.
- Nos desafios, o bot está **sempre** em física completa.
- `func_80042D20` (a lógica de itens da corrida) **não roda** nos desafios. Toda a decisão fica na máquina de estados.

### 4.2 AI Nodes: o grafo

Struct [`LevelObjectEntry_AiNode`](extern/dkr-decomp/include/level_object_entries.h#L184), com 16 bytes. O grafo é
montado em [`ainode_update`](extern/dkr-decomp/src/objects.c#L7328).

| Campo | Função |
|---|---|
| `unk8` → **tag** | Tipo do nó (tabela abaixo). |
| `nodeID` | ID do nó. 255 significa "atribuir automaticamente" ([`obj_init_ainode`](extern/dkr-decomp/src/object_functions.c#L4183)). O retail vai até 38. |
| `adjacent[4]` | IDs dos vizinhos (255 = nenhum). As distâncias são calculadas na carga. **A ligação é por nó**, então deixe os links recíprocos. |
| `elevation` | Andar (0 a 3). O jogo ordena os nós por altura e grava os limites entre andares (a média das alturas nas transições) em `gElevationHeights`. [`obj_elevation(y)`](extern/dkr-decomp/src/objects.c#L7436) diz em que andar está um objeto. |
| `padF` | Não usado. |

| Tag | Significado (pelo uso no código) | Retail |
|---|---|---|
| 0 | nó comum | todos |
| 1 | **ponto de balão**, alvo dos estados 3 e 5 | Darkwater 13, Icicle 6, Smokey 3 |
| 2 | não é lido pela IA de desafio | 1 nó em Icicle |
| 3 | **nó de resgate**, usado no estado 6 (exige a mesma elevação do bot) | Icicle 3, Smokey 4 |
| 4–7 | **baú do jogador 0–3** (`tag = racerIndex + 4`), alvo do estado 7 | Smokey Castle, um de cada |

Os hubs e algumas pistas também têm AI nodes, com `elevation = −1`. Nesses casos eles guiam **NPCs**:
[`func_8001C6C4`](extern/dkr-decomp/src/objects.c#L7511) segue os nós com Catmull-Rom. Os corredores não os usam.

**Pathfinding.** [`func_8001CD28`](extern/dkr-decomp/src/objects.c#L7675) é uma busca de menor caminho (estilo
Dijkstra, com lista ordenada pela distância acumulada):

- parte do nó atual e não volta pela aresta de onde o bot veio;
- para no primeiro nó cuja **tag** é igual ao alvo, ou cujo **ID** é o alvo (quando o bit `0x100` está ligado);
- devolve **só o próximo passo** (o vizinho imediato). O caminho é recalculado a cada nó.

Sem alvo definido, [`ainode_find_next`](extern/dkr-decomp/src/objects.c#L7637) faz um passeio: gira entre os vizinhos
e evita voltar.

**Direção.** O stick é `erro de ângulo >> 4`:

- acelera quando |stick| < 30;
- freia antes de curvas acima de ~30° (`0x1500`);
- troca de nó a menos de 50 unidades, ou quando já passou dele (mais de 320 unidades com o próximo nó já planejado);
- planeja o próximo nó a menos de 300 unidades.

### 4.3 Máquina de estados (`unk1CD`)

Nesta tabela, `P[i]` são os percentuais do header da fase ([§4.5](#45-percentuais-por-fase-headerunk2a10)).

| Estado | Nome | Quando entra | O que faz | Quando sai |
|---|---|---|---|---|
| 0 | inicial | largada / perdeu o caminho | acha o nó mais próximo | → 1 |
| 1 | **decidir** | — | sorteia o próximo estado com base em `P[i]` | imediato |
| 2 | passear | caso padrão | `ainode_find_next` | fim do timer (5 ou 20 s) → 1 |
| 3 | **buscar balão** | desarmado, chance `P[3]` | caminho até um nó de tag 1 | pegou um balão que não é boost → 2 (3 s) |
| 4 | **caçar** | armado, chance `P[0]` | caminho até a vítima (ver [§4.4](#44-como-os-bots-sabem-que-devem-se-atacar)) | sem munição ou 20 s → 1, e libera a reserva |
| 5 | rearmar sob ameaça | desarmado, **alguém está me caçando**, chance `P[5]` | caminho até um nó de tag 1 | o caçador desiste ou 20 s → 1 |
| 6 | resgate | a elevação do bot não está entre as dos dois nós do trecho atual (caiu ou subiu de andar) | vai ao nó de tag 3 mais próximo **no mesmo andar** | → 1 |
| 7 | depositar (só bananas) | tem 2 ou mais bananas, chance `P[6]` | caminho até o nó de tag `4 + índice` (o próprio baú) | bananas = 0 → 1 |

### 4.4 Como os bots "sabem" que devem se atacar

A resposta curta: **eles não sabem.** Existem dois mecanismos, e a eliminação é consequência deles.

**1) Caça deliberada (estado 4)**, [racer.c:775-827](extern/dkr-decomp/src/racer.c#L775):

- **Quando:** o bot está armado e passa na chance `P[0]` (85% na Icicle, 100% na Darkwater).
- **Candidatos:** os outros 3 corredores, percorridos numa ordem sorteada (crescente ou decrescente, 50%).
  Quem **já está sendo caçado por outro bot é pulado**. A tabela `D_8011D58C[vítima] = caçador + 1` funciona como
  uma reserva, então **dois bots nunca caçam o mesmo alvo**.
- **Critério:** com chance `P[1]`, escolhe quem tem **mais** bananas. Senão, quem tem **menos** (mas mais que zero).
  Na batalha, bananas são vidas, então isso vira "ir no líder" ou "finalizar quem está quase morto".
- **Preferência pelo humano:** com chance `P[2]`, em tela única, o alvo passa a ser o **jogador 1**. É 85% na Icicle e
  100% na Darkwater: **os bots caçam você de propósito**. Há um bug nessa checagem: ela confere as bananas do último
  índice do laço, não as do jogador 1.
- **Rota:** se a vítima é bot, o caçador vai até o nó-alvo dela; se é humano, até o nó mais próximo da posição dele.
  A rota é recalculada a cada nó.

**2) Tiro oportunista (em qualquer estado, a cada frame)**, [racer.c:990-1020](extern/dkr-decomp/src/racer.c#L990).
Para cada oponente:

- que esteja **no mesmo andar** (`elevation`);
- a **menos de 800 unidades**;
- dentro de um cone de **±11,25°** à frente do bot (±22,5° se o balão for de nível 1),

o bot solta o `Z` e **dispara o que tiver**. Isso vale para qualquer oponente, inclusive outros bots. Não existe time.

**3) A regra do jogo, fora da IA:**

- Cada corredor começa com **8 bananas** ([objects.c:1337](extern/dkr-decomp/src/objects.c#L1337)).
- Cada golpe tira **2 bananas** ([`racer_attack_handler_car`](extern/dkr-decomp/src/racer.c#L6223) →
  [`drop_bananas`](extern/dkr-decomp/src/racer.c#L7575)). Óleo (spin) e esmagamento não contam, e o escudo anula o
  golpe.
- Na batalha (`level_type() == 0x40`), as bananas perdidas **não caem no chão**: somem.
- Com 0 bananas, o corredor é **eliminado**: fica invisível, sem colisão e sem balões, e a posição final é contada de
  trás para frente ([objects.c:6168](extern/dkr-decomp/src/objects.c#L6168)).
- A partida acaba quando todos os humanos foram eliminados ou quando 3 corredores foram eliminados. A classificação
  final é invertida: **o último sobrevivente vence**.

### 4.5 Percentuais por fase (`header.unk2A[10]`)

No JSON do header, esse array aparece dividido em `unk2A` (primeiro valor) e `unk2B` (os outros 9).

| Índice | Batalha e bananas | Ovos | Icicle | Darkwater | Padrão* |
|---|---|---|---|---|---|
| 0 | chance de caçar quando armado | — | 85 | 100 | 80 |
| 1 | chance de mirar quem tem **mais** bananas (senão, menos) | — | 90 | 100 | 50 |
| 2 | chance de preferir o **jogador 1** | chance de atacar o jogador 1 em vez do líder | 85 | 100 | 50 |
| 3 | chance de buscar balão quando desarmado | — | 100 | 100 | 50 |
| 4 | *não encontrei leitura* | — | 100 | 66 | 20 |
| 5 | chance de buscar balão quando caçado (se `[3]` falhou) | — | 20 | 20 | 20 |
| 6 | chance de ir depositar | chance de **roubar um ovo que está chocando** | 100 | 100 | 100 |

\* O padrão vale para Smokey Castle, Fire Mountain e todas as pistas de corrida.

### 4.6 Desafio das bananas (Smokey Castle)

- Usa a mesma `racer_ai_challenge`, com o estado 7 e o critério "quem tem mais bananas", para **roubar**.
- Ninguém carrega mais de 2 bananas por vez: com 2, a banana não é coletada
  ([object_functions.c:4472](extern/dkr-decomp/src/object_functions.c#L4472)). Bots têm +30 unidades de raio de coleta.
- O baú ([`obj_loop_treasuresucker`](extern/dkr-decomp/src/object_functions.c#L4211)) suga as bananas do dono a menos
  de 225 unidades.
- Aqui, ao contrário da batalha, as bananas perdidas num golpe **caem no chão** e podem ser pegas por outros.

### 4.7 Desafio dos ovos (Fire Mountain)

[`racer_ai_eggs`](extern/dkr-decomp/src/racer.c#L1052) **não usa nós**. A cada decisão, o bot escolhe um tipo de
objeto, pega o mais próximo e voa direto até ele: stick X = ângulo >> 5, e stick Y também (Fire Mountain é de avião).

A pontuação de cada jogador é `ovos × 3 + 2 (se tem um ovo chocando) + 1 (se está carregando um ovo)`. Esse critério
define o "líder".

| Estado | Alvo |
|---|---|
| 1 | ovo solto no centro (`EGG_SPAWNED`) |
| 4 | acabou de pegar um ovo: vai ao HeadForPoint de **tag 2 do próprio ninho** (`unk9` = jogador), até ficar a menos de 200 unidades |
| 2 | carregando um ovo: vai ao HeadForPoint de **tag 0** mais próximo e solta (`Z`) a menos de 100 unidades |
| 5 | pegar balão |
| 6 | atacar o líder (ou o jogador 1, com chance `[2]`); dispara a menos de 500 unidades |
| 7 → 8 | ir ao ninho (tag 2) de quem tem um ovo chocando e pegar esse ovo (`EGG_IN_BASE`) |
| 3 | corrida encerrada: vai a qualquer HeadForPoint de tag 2 |

O objeto **HeadForPoint** (`BHV_UNK_5C`) vem em 2 por jogador (`unk8` ∈ {0, 2}, `unk9` = jogador). Pelo fluxo
4 → 2, a tag 2 é o ponto de aproximação do ninho e a tag 0 é o ponto de entrega. Essa leitura é inferida.

Anti-travamento próprio:

- se o bot vira muito (|stick| > 60) por 110 frames, mergulha por 40 frames (o código reaproveita `aiSkill` como
  contador);
- se fica 1 s parado no chão, volta a mirar um ninho para decolar.

---

## 5. Implicações para o editor Blender

**Pistas de corrida:**

- **Checkpoints são obrigatórios.** Sem eles o bot anda em círculo, e sem eles o modo fora da tela não funciona.
- O `index` precisa ser contíguo dentro de cada par `(vehicleType, isAltCheckpoint)`.
- Para pista mais larga, **aumente o `scale`** em vez de usar offsets enormes: o offset é multiplicado por `scale / 32`.
  Com `scale` 64, as faixas ±64 ficam a ±128 unidades do centro.
- **Ponha mais checkpoints nas curvas.** A spline usa 4 pontos e o bot mira 500 unidades à frente.
- **Receitas de route flag:**
  - flag 2 **só na faixa 1** para que só alguns bots peguem o atalho;
  - flag 4 antes de curvas fechadas, para bots de chefe;
  - flag 6 no último checkpoint de corridas de ponto a ponto;
  - flag 5 onde o bot pode cair na água.
- **Caminho por veículo:** use `vehicleType` com o `unk4F` do header correspondente.

**Arenas de batalha e bananas:**

- **Não use checkpoints.** Use AI Nodes com links recíprocos.
- Ponha um nó de **tag 1 perto de cada balão** e **pelo menos um de tag 3 em cada andar**.
- Mantenha o `elevation` coerente com a altura: andares mais altos recebem valores maiores.
- A fase precisa de **exatamente 4 corredores**.
- **Bananas:** ponha nós de tags 4 a 7 junto aos baús.
- **Ovos:** ponha um par de HeadForPoint (tag 0 e tag 2) por ninho.

**Sugestões para o catálogo do addon:**

- rotular `unk19` como "HUD arrow", com o enum de [§2.2](#22-setas-do-hud-unk19);
- rotular `unk18` como "unused";
- no AI Node, rotular `unk8` como "Tag", com o enum de [§4.2](#42-ai-nodes-o-grafo);
- no HeadForPoint, rotular `unk8`/`unk9` como "Tipo"/"Jogador";
- expor os percentuais `unk2A` do header da fase.

---

## 6. Pontos em aberto

- A **tag 2** de AI node, o **índice 4** do `header.unk2A` e a **route flag 3** não aparecem em nenhuma leitura no
  código decompilado.
- O papel das tags 0 e 2 do HeadForPoint foi inferido pelo fluxo de estados. O código não dá nomes.
- A caça com "preferência pelo jogador 1" tem um bug de índice ([racer.c:810-818](extern/dkr-decomp/src/racer.c#L810)).
- Os nomes `unk1CA` (faixa), `unk1C9` (modo de mira: 0 = faixa, 4 = à frente, 5 = atrás), `unk1CD` (estado de
  desafio), `unk154` (nó ou objeto alvo), `unk124` (velocidade-alvo), `unk201` (janela de física) e `D_8011D58C`
  (reservas de caça) são interpretações minhas. O decomp ainda não os nomeou.
