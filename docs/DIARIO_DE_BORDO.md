# Diário de bordo — Crachá Inteligente de Ponto

> **Para que serve:** registro corrido de **tudo** que a equipe fez, decidiu, mediu e errou. É a matéria-prima do
> **relatório final** e do **relato** da apresentação (04/12/2026). **Atualize a cada passo** (inclusive os
> problemas: erro registrado vale ponto na documentação da PCI e na arguição). Datas em dd/mm/aaaa.
> Fonte de verdade do estado atual: `PENDENCIAS.md`. Detalhes técnicos: `docs/FLUXO_BLE_E_BLOQUEIO.md`.

## 1. Linha do tempo

| Data | O que aconteceu |
| --- | --- |
| 27/10/2026 | Início do repositório (descrição do projeto e imagens, Flávio). Lista de materiais v1 (ESP32-C3, nó de estação, Docker). |
| 29/10/2026 | **Revisão do escopo** (PDF do projeto): o crachá passa a ser a própria Heltec LoRa 32 V3 com botão, LED, buzzer e DS1302; a 2ª Heltec é o gateway. Escopo: 1 crachá, 1 gateway, 1 PC, 1 servidor. A lista de 27/10 fica desatualizada. |
| até 03/11/2026 | **Nuvem pronta:** banco no Supabase (5 tabelas, RLS, regras R1–R8 em `registrar_ponto()`, views da jornada, usuário `nodered` de menor privilégio, seed) com **31 testes passando**; broker MQTT local (aedes); Node-RED com fluxo e **painel de gerência**; exportação CSV; senha no editor. |
| 03/11/2026 | **ACK de comandos** (`cmd/{badge}/ack`), estado do gateway no painel (Last Will + retained), simuladores (`publicar-teste`), **servidor de OTA** (`firmware-http`), filtros do painel (Dia / Nome). **Painel fechado para a rede** (`uiHost` 127.0.0.1). Teste de persistência (T10): 4 linhas antes e depois de reiniciar o Node-RED. |
| 11/11/2026 | **Começo da parte física** no PC do Flávio (admin + Arduino IDE; 2 Heltec V3, 2 antenas, 2 cabos). Plano de testes em protoboard; Teste 1 escrito. Cronograma já atrasado (protoboard era 09/11). |
| 12/11/2026 | **Teste 1 (OLED + LED): passou.** **Teste 2 (ping-pong LoRa): passou** (Flávio ajustou o código para a RadioLib 8.x). Teste 3 (DS1302) escrito, **adiado: bateria CR2032 não chegou**. Foco redefinido: só **BLE crachá↔PC** e **comunicação com o banco**. **Teste 5 (BLE): passou**, com medições de RSSI. Testes 6 e 7 (gateway MQTT e crachá por PRG) escritos e **adiados para outro dia**. |
| 12/11/2026 | **Agente v1 (bloqueio por média de RSSI) testado: dois problemas** (ver seção 2). Equipe propõe o **fluxo novo: o botão desbloqueia**. Decisões 1, 2 e 3 aprovadas. Código e documentos refeitos (agente v2, firmware 05 com contador de cliques). **Ainda não testado.** |
| 12/11/2026 | **Fim da sessão.** Flávio saiu do laboratório; tudo enviado ao Git (`b2964ab` e seguintes). Próximo encontro: testar o agente v2 (ver `PENDENCIAS.md`, seção 0). |
| 17/11/2026 | **Preparação sem hardware:** pinagem proposta para a PCI (`hardware/PINAGEM.md`), pastas do Git criadas (`firmware/cracha`, `firmware/gateway`, `hardware`, `enclosure`) e caminho para o broker sem administrador (P17). |
| 17/11/2026 | **Teste do agente v2 (PC do Flávio, Heltec com o teste 05):** clique perto liberou em **< 1 s** (RSSI −58 dBm); clique longe foi ignorado. A equipe redefiniu o ciclo: 1º clique libera, **2º clique arma** (crachá fora do porta-crachá), só então afastar bloqueia; 3º clique em diante repete o ciclo. Agente v2.1 escrito (estado ARMADO), a testar. Depois a equipe precisou que **o clique que coloca o crachá nunca seja ignorado e não use BLE** (o porta-crachá está sempre perto): os cliques viram um interruptor, e o BLE só vigia a distância em ARMADO; `--liberar` removido. |
| 17/11/2026 | **Agente v2.1 testado: deu certo** (clique = interruptor). Em seguida a equipe redefiniu de novo a lógica (**agente v3**): modo **automático** (chegar perto desbloqueia, sair de perto bloqueia) e modo **fixo** (1º clique mantém liberado até o 2º clique, sem medir BLE); cada clique alterna FIXO ↔ AUTOMÁTICO; `--seg-ausente` de 10 s para 5 s. Escrito, a testar. |
| 17/11/2026 | **Bloqueio demorava demais ao afastar** (≈ 8+5+5 s no pior caso): padrões mudados para `--janela 3 --seg-ausente 3 --aviso 3` (≈ 9 s). Risco: janela curta pode bloquear ao tapar o crachá; **a medir**. |
| 17/11/2026 | **Agente v3 com `--janela 3 --seg-ausente 3 --aviso 3` testado: passou** (desbloqueio ao chegar perto, bloqueio ao afastar, modo fixo pelo clique e tapar o crachá sem bloquear). Tempos exatos não foram anotados; **medir com cronômetro** na tabela de metas. |
| 17/11/2026 | **Nova arquitetura de teste (2 placas):** Heltec 1 = **crachá** (só anuncia o sinal; botão sem efeito), Heltec 2 = **recipiente** com o push button no GPIO 4 (conta os cliques; depois vira a parte LoRa do recipiente). O agente passou a ouvir os dois rádios BLE (`UNIFESP-BADGE` #101 e `UNIFESP-HOLDER` #201). Escrito, a testar. |
| 17/11/2026 | **Sinal de clique:** o recipiente mostra "CLIQUE! número N" no OLED por 1,2 s (e acende o LED); o PC mostra um aviso verde por 2 s e toca um bipe. Também: `__pycache__` tinha entrado no Git por engano; removido e ignorado. |
| 17/11/2026 | **Duas placas (crachá + recipiente) com o agente v3 e o sinal de clique: todos os testes passaram.** Parte A (Bluetooth) fechada. Próximo: testes 6 e 7 (LoRa → gateway → MQTT → banco), que exigem as duas Heltec, então o arranjo BLE é desmontado por enquanto. |
| 17/11/2026 | **Fim da sessão.** Parte B (testes 6 e 7) não começou. Descobertas de rede (P18): o PC do Flávio estava em `172.21.221.229` (rede de instituição, gateway `172.21.220.1`) e o notebook em `172.20.10.9`; o teste de porta 1883 falhou. Decisão: hotspot 2,4 GHz e, de preferência, broker + Node-RED num PC só (o do Flávio). Retomada em `PENDENCIAS.md`, seção 0. |

## 2. Problemas encontrados e como foram resolvidos (alimenta o "registro de erros" do relatório)

| # | Quando | Problema | Causa | Solução / situação |
| --- | --- | --- | --- | --- |
| P1 | até 03/11 | `UPDATE` rodava sem erro e alterava 0 linhas | **RLS** ligado sem política = falha silenciosa | Políticas em `04_usuarios.sql`; testes de permissão conferem `ROW_COUNT` |
| P2 | até 03/11 | Views falhavam para o `nodered` | Funções chamadas dentro de views rodam com a permissão de **quem consulta** | `GRANT EXECUTE` em `dia_local()`; testar **todas** as views como `nodered` |
| P3 | até 03/11 | Erros em PL/pgSQL | `array \|\| 'literal'` ambíguo; conflito de nomes em `RETURNS TABLE` | `array_append(a, 'x'::TEXT)`; `#variable_conflict use_column` |
| P4 | até 03/11 | "Dia" errado perto da meia-noite | Supabase roda em **UTC** | Todo dia passa por `dia_local()` (`America/Sao_Paulo`); hora formatada no SQL |
| P5 | até 03/11 | Consultas do painel misturavam parâmetros | `msg.query`/`msg.params` do passo anterior contaminam o próximo | Painel começa de mensagem limpa; sempre `$1, $2...` |
| P6 | até 03/11 | Porta 1880 presa ao parar o Node-RED | Parar o `npm` não mata o `node red.js` | Achar o PID pela porta e encerrar só ele |
| P7 | até 03/11 | Notebook sem administrador e sem Docker | Restrição do ambiente | **Supabase** no lugar do PostgreSQL local; **aedes** no lugar do Mosquitto |
| P8 | até 03/11 | Coluna `'Obs.'` aparecia vazia na tabela do painel | Ponto no nome vira caminho de objeto no Dashboard 2.0 | Nome sem ponto (`'Observação'`) |
| P9 | 03/11 | `nodered` dava `permission denied` ao atualizar o firmware | `UPDATE` que *lê* coluna sem permissão | `UPDATE` escrito sem ler a coluna |
| P10 | 03/11 | **Painel e CSV abertos para a rede, sem senha** | `uiHost` estava comentado: Node-RED escutava em `0.0.0.0` | `uiHost: "127.0.0.1"`; conferido com `netstat` |
| P11 | 12/11 | Código do ping-pong não compilou/funcionou como escrito | A **RadioLib 8.x** mudou a API (frequência em Hz, `ConfigLoRa_t`, `tcxoVoltage`) | Flávio adaptou (`59a6480`); firmwares seguintes já usam a API nova |
| P12 | 12/11 | Não dá para testar o relógio DS1302 em definitivo | **Bateria CR2032 não chegou** (Mercado Livre) | Teste 3 segue só com a placa ligada; persistência adiada |
| P13 | 12/11 | **PC desbloqueava quando o usuário obstruía o crachá** (agente v1) | Corpo/mão atenuam o sinal; com média de RSSI e limiar único a leitura oscilava e cruzava o limite de "presente" | Fim do desbloqueio por proximidade: **botão desbloqueia**; o monitoramento usa o **pico** da janela, não a média |
| P14 | 12/11 | **Desbloqueio demorava** (agente v1) | A média das 5 leituras + 3 s de "presença" estável atrasavam | Leitura **única** no pacote do clique (anúncio rápido de 100 ms após o botão) |
| P15 | 12/11 | **Windows não permite desbloquear por programa** | Só `LockWorkStation` (bloquear) existe para apps comuns | Tela de bloqueio **própria** em tela cheia (demonstração; limitação declarada) |
| P16 | 12/11 | Meta "afastar→bloqueio ≤ 10 s" pode estourar | Política aprovada (janela 8 s + 10 s + aviso 5 s) prioriza não bloquear por obstrução | **A medir**; `--janela 4 --seg-ausente 3 --aviso 3` encurta |
| P17 | 12/11 | Windows bloqueia a porta 1883 para outros PCs? | Notebook do Pedro sem administrador para liberar firewall | **Encaminhado (17/11), a validar no laboratório:** o **broker roda no PC do Flávio** (tem administrador, libera a porta 1883 uma vez) e o **Node-RED roda no notebook do Pedro** (que tem o `cloud/.env`), conectando para fora ao IP do Flávio; saída não precisa de firewall. Gerar o fluxo com `MQTT_HOST=<IP do Flávio> npm run gerar-fluxo` |
| P18 | 17/11 | PC do Flávio e notebook não se enxergavam; porta 1883 sem resposta | Redes diferentes (`172.21.220.x` institucional × `172.20.10.x` hotspot); Heltec não entra em Wi-Fi corporativo; notebook sem administrador para liberar a 1883 | Usar o **hotspot 2,4 GHz**; **broker + Node-RED num PC só** (do Flávio, copiando o `.env` à mão). A validar |

## 3. Decisões (e por quê)

| Data | Decisão | Por quê / alternativas |
| --- | --- | --- |
| 17/11 | **Cliques vêm do recipiente (Heltec 2), distância vem do crachá (Heltec 1)** | Simula o porta-crachá real: o botão fica no recipiente, não no crachá. Mesmo firmware 05 com a chave `SOU_RECIPIENTE`. Sem sinal do crachá no 2º clique, o PC bloqueia em ≈ 6 s |
| 17/11 | **Agente v3:** chegar perto desbloqueia e sair bloqueia (automático); 1º clique prende liberado (sem BLE) até o 2º clique; `--seg-ausente` 5 s | Pedido da equipe. Riscos: volta o desbloqueio por proximidade que falhou na v1 (P13); mitigado com pico + histerese (−60/−70). Tempo total do bloqueio ≈ 8+5+5 s: a medir (`--janela 3 --seg-ausente 3 --aviso 3` encurta) |
| 17/11 | **Cliques são um interruptor: colocar o crachá libera sem BLE (nunca ignorado); tirar (2º clique) arma; só então longe bloqueia; repete** | Simula crachá no/fora do porta-crachá e casa com CHEGADA/SAÍDA; substitui a regra de 12/11 (2º clique ignorado). Risco aceito: PC aberto se a pessoa sair sem o 2º clique |
| 17/11 | **Proposta:** botão no GPIO 4, buzzer no GPIO 5, LED externo opcional no GPIO 6 | Pinos livres, sem função de boot, longe de LoRa/OLED/DS1302; **a confirmar na protoboard** (Teste 4) |
| 17/11 | **Proposta:** broker no PC do Flávio, Node-RED no notebook do Pedro (`MQTT_HOST`) | Só o PC com administrador precisa liberar a 1883; alternativa: tudo no PC do Flávio, copiando o `cloud/.env` à mão (nunca pelo Git) |
| 03/11 | Banco no **Supabase**, broker **aedes** | Sem Docker nem administrador; protocolo MQTT idêntico ao Mosquitto |
| 03/11 | Regra de negócio **no banco** (`registrar_ponto()`); jornada em **views** | Regra num lugar só, testável em SQL; nunca fica desatualizada |
| 03/11 | Alternância obrigatória (R4) no lugar da "regra dos 30 min" do PDF | Mais simples de explicar; **explicar na arguição** |
| 03/11 | Mesma senha para admin do banco, `nodered` e editor do Node-RED | Pedido do usuário; risco conhecido e aceito |
| 03/11 | Painel só em `127.0.0.1`; OTA em porta separada (8080, só `.bin`) | Painel sem senha não pode ficar aberto |
| 11/11 | Programar no **PC do Flávio** (tem admin e Arduino IDE) | O notebook do Pedro não instala programas |
| 12/11 | Fazer `pull` + `commit` + `push` ao fim de **cada passo** | A equipe alterna entre dois PCs; o Git é a ponte |
| 12/11 | **Adiar** testes de relógio, botão e buzzer; foco em BLE e banco | Peças ainda não chegaram |
| 12/11 | Bloqueio do PC por **tela própria** (opção A), não Linux nem só `LockWorkStation` | Cumpre desbloquear sem administrador; custo: não é segurança real |
| 12/11 | **Fluxo novo: o botão desbloqueia o PC**; depois o PC monitora o RSSI e bloqueia ao afastar | Resolve P13 e P14 (ideia da equipe) |
| 12/11 | Clique chega ao PC por **contador no anúncio BLE** (não pelo MQTT) | Instantâneo e funciona sem rede; o RSSI do próprio pacote prova a proximidade |
| 12/11 | Botão com o PC **já liberado**: ignorar | O ponto já vai por LoRa; o PC só bloqueia/libera |
| 12/11 | Bloqueio por **pico** de 8 s abaixo de −70 dBm por 10 s, com aviso de 5 s | Tolera obstrução; falso bloqueio vira só um susto |
| 12/11 | O agente **não publica `origem:"ble"`**; só o status do PC | Senão cada bloqueio por distância viraria uma SAÍDA. **Sem mudar tabelas.** |
| 12/11 | **Requisito do PDF alterado:** "aproximar→desbloqueio ≤ 5 s" vira "apertar o botão→desbloqueio ≤ 2 s" | Consequência do fluxo novo. **Avisar o professor.** |

## 4. Resultados dos testes

| Teste | Data | Resultado | Observações |
| --- | --- | --- | --- |
| 1 — OLED + LED | 12/11 | ✅ passou | Nas placas do laboratório |
| 2 — ping-pong LoRa | 12/11 | ✅ passou | Falta registrar distância e RSSI de alcance |
| 3 — relógio DS1302 | — | ⏸ adiado | Sem bateria |
| 4 — botão + LED + buzzer | — | ⏸ adiado | Peças não chegaram |
| 5 — BLE (anúncio + RSSI no PC) | 12/11 | ✅ passou (v1) | O PC enxergou o crachá; medições abaixo. **v2 (contador de cliques) reescrita, não testada** |
| Agente v1 (média + tela própria) | 12/11 | ❌ reprovado na prática | P13 e P14 → fluxo novo |
| Agente v2: clique perto libera / clique longe ignorado | 17/11 | ✅ passou | < 1 s, −58 dBm |
| Agente v2.1 (clique = interruptor; 2º clique arma; longe bloqueia) | 17/11 | ✅ passou | superado pela v3 |
| Duas placas: crachá (BLE) + recipiente (botão) com sinal de clique | 17/11 | ✅ passou | todos os testes |
| Agente v3 (automático por sinal + modo fixo por clique) | 17/11 | ✅ passou | janela 3 s / ausente 3 s / aviso 3 s; tapar o crachá não bloqueou | `docs/FLUXO_BLE_E_BLOQUEIO.md` |
| 6 — gateway MQTT; 7 — crachá por PRG | — | 🔲 adiados | Código escrito |

### Medições de RSSI (crachá sobre a mesa, sem corpo, 12/11/2026)

| Distância | RSSI |
| --- | --- |
| 0,15 m (máximo do cabo) | −39 dBm |
| 0,5 m | −41 dBm |
| 1 m | −48 dBm |
| 1,5 m | −52 dBm |
| 2 m | −56,5 dBm |

Queda ≈ 7 dB a cada dobra de distância; extrapolando, −65 dBm ≈ 4 m e −75 dBm ≈ 10 m. **A medir:** crachá no corpo
(espera-se −5 a −10 dB), corredor, atrás de parede, e a taxa de falsos bloqueios.

### Metas de desempenho (preencher ao medir)

| Meta | Valor pedido | Medido |
| --- | --- | --- |
| Botão → desbloqueio do PC (novo) | ≤ 2 s | < 1 s (17/11, a 1 m, RSSI −58) |
| Afastar → bloqueio | ≤ 10 s | — |
| Botão → bipe | ≤ 2 s | — |
| Evento → painel | ≤ 5 s | — |
| Alcance LoRa no prédio | registrar distância | — |
| Perdas / duplicatas | 0 / 0 | — |
| Erro do relógio | < 2 s | — |

## 5. Evidências a coletar (para relatório e apresentação)
- [ ] Vídeo: crachá → clique → PC desbloqueia; afastar → aviso → bloqueio; tapar o crachá **sem** bloquear.
- [ ] Print do OLED nos testes 1, 2 e 5; foto da protoboard e das duas Heltec.
- [ ] Gráfico/tabela de RSSI × distância (sem corpo e com corpo).
- [ ] Print do painel com um ponto vindo do crachá; print do T10 (persistência) sem senha.
- [ ] Registro dos erros acima (seção 2) no relatório da PCI (erros A→B).

## 6. Pendências abertas que afetam o relato
- Avisar o professor da mudança de requisito (desbloqueio pelo botão).
- Bateria CR2032 e demais peças (relógio, botão, buzzer): conferir a chegada.
- Testes 6 e 7 (caminho até o banco) ficam para a próxima sessão no laboratório.

## 7. Sessão de 24/11/2026: versão final dos códigos (ainda sem gravar nas placas)
- **O que foi feito:** escritos `firmware/cracha/cracha.ino` e `firmware/gateway/gateway.ino` (finais, a partir dos testes 03, 05, 06 e 07) e ajustado `pc-agent/agente_bloqueio.py`.
- **Decisão (a confirmar com a equipe):** no sistema final há **um crachá só** (botão + LoRa + BLE na mesma Heltec) e o gateway na outra. O arranjo "recipiente" de 17/11 (duas placas BLE) sai de cena; o agente passou a contar os cliques pelo contador do **próprio crachá** (`--recipiente 201` reativa o arranjo antigo).
- **Novidades:** fila persistente (NVS) com reenvio a cada 30 s e bit "fila"; DS1302 acertado pelo ACK do gateway; comando BIPE ponta a ponta (painel → MQTT → gateway → LoRa → crachá → ACK); gateway assina `cmd/+`. OTA por LoRa **não** foi implementado (o gateway devolve `ok:false`; plano B: USB).
- **Limitação:** não havia `arduino-cli` no notebook, então os `.ino` **não foram compilados** (só o Python foi verificado). Os primeiros erros de compilação, se houver, entram aqui como problemas.

### 7.1 Revisão no mesmo dia: o botão fica no gateway (recipiente)
- **Pedido da equipe:** o botão fica no gateway, "naquela lógica de ter um recipiente". Escolha: **LoRa mantido**.
- **Mudança:** o `gateway.ino` virou **gateway + recipiente** (botão GPIO 4/PRG, buzzer GPIO 5, DS1302, BLE `UNIFESP-HOLDER` nº 201 com contador de cliques, fila de pontos na NVS). O clique publica o ponto direto no MQTT e manda ao crachá um aviso LoRa (tipo 3, código 2 = PONTO OK) para ele apitar. O `cracha.ino` ficou só com BLE de sinal + escuta LoRa + buzzer + OLED. O agente do PC voltou a usar o recipiente (`--recipiente 201` é o padrão).
- **Efeito no requisito:** o LoRa deixa de levar o ponto e passa a levar só confirmações/comandos para o crachá. O ponto vai pelo Wi-Fi do gateway.
- **Riscos a observar na bancada:** Wi-Fi e BLE na mesma placa (dividem a antena 2,4 GHz; se o anúncio BLE falhar, é aqui); tamanho do programa; ainda **não compilado**.

### 7.2 Versão final fechada
- Removido o PRG como clique no gateway (só o botão externo, GPIO 4). `README.md` e `PENDENCIAS.md` atualizados; criado `docs/ROTEIRO_FINAL.md` (montagem, gravação, testes e entrega). `firmware/testes/` fica como histórico.
- **Gateway compilou** no PC do Flávio (lição: o `config.h` não é biblioteca; é copiado de `config.example.h` na pasta do sketch).

### 7.3 Problema: "Pacote inválido" no OLED do gateway (24/11/2026)
- **Sintoma:** com `W:ok M:ok`, o OLED mostra "Pacote inválido" sem nenhum crachá transmitindo.
- **Causa (provável):** o rádio recebeu LoRa de outro sistema ou ruído; nossa configuração usava o sync word 0x12, padrão dos exemplos, que outros grupos também usam.
- **Solução:** sync word próprio da equipe (0x2B) no gateway e no crachá (os dois precisam ser regravados juntos) e log da causa no Monitor Serial (`LoRa ignorado: estado=... tamanho=... RSSI=...`).

### 7.4 Problema: PRG não bate ponto no arranjo final (24/11/2026)
- **Sintoma:** apertar o PRG não registrava ponto à distância (LoRa → gateway → banco).
- **Causa:** no firmware final o PRG não é mais botão de ponto (o botão é o externo no gateway); o crachá final só anuncia BLE e escuta LoRa. O caminho antigo (PRG no crachá → LoRa) é o teste 07.
- **Diagnóstico em duas metades:** (A) botão externo do gateway → MQTT → banco; (B) LoRa nos dois sentidos pelo BIPE do painel; (C) teste 07 (sync word atualizado para 0x2B) para o ponto por LoRa.

### 7.5 Correção de lógica: o crachá bate ponto remoto por LoRa (24/11/2026)
- **Correção da equipe:** a lógica do arranjo estava errada; o crachá deve simular uma pessoa batendo ponto **remotamente, num local sem internet, via LoRa**. O `cracha.ino` voltou a ter botão (PRG da placa e GPIO 4), fila persistente, ACK com 3 tentativas, DS1302 opcional e hora certa pelo ACK do gateway; mantém BLE de sinal e os avisos do gateway.
- **Problema evitado:** o banco tem `UNIQUE (badge_id, origem, seq)`. Crachá e botão do gateway usam o badge 101; para não colidirem, o crachá usa `seq` 1–32767 e o gateway 32768 em diante.
- **Gateway:** o ponto LoRa do crachá é publicado, confirmado com ACK (hora NTP) e o gateway também bipa.

### 7.6 Fim da sessão de 24/11/2026: onde paramos
- **Feito hoje (software):** versão final dos códigos (`firmware/gateway`, `firmware/cracha`, agente do PC), roteiro final (`docs/ROTEIRO_FINAL.md`), sync word próprio 0x2B, seq separados (crachá 1–32767, gateway 32768+), teste 07 atualizado.
- **Feito hoje (bancada, relato da equipe):** o gateway compilou e gravou (lição: o `config.h` não é biblioteca, é copiado de `config.example.h` na pasta do sketch); Wi-Fi e MQTT ok; "Pacote inválido" resolvido com o sync word próprio; o resto do roteiro "deu certo".
- **Linha do tempo das decisões:** 1) crachá com tudo (botão+LoRa+BLE) e gateway; 2) pedido de botão no gateway/recipiente (escolha: LoRa mantido) → gateway+recipiente com botão, crachá só sinal; 3) **correção da equipe:** o crachá deve bater ponto **remoto via LoRa** (local sem internet) → botão PRG volta no crachá, mantendo o botão do gateway.
- **Não testado ainda:** ponto remoto crachá → LoRa → gateway → banco; fila; BIPE; agente do PC com o gateway final (Wi-Fi + BLE); metas de desempenho. O firmware novo do crachá (commit `3e99bec`) ainda não foi compilado nem gravado.
- **Retomar por:** `PENDENCIAS.md` seção 0 (passos em ordem).

## 8. Sessão de 26/11/2026: revisão do firmware e bloqueio/desbloqueio do PC no banco

### 8.1 Problemas achados na revisão (causa e solução)
- **`gateway.ino` não compilava:** uma quebra de linha de verdade dentro da string do `Serial.printf("LoRa ignorado...")` (linha 440). **Solução:** trocada por `\n`.
- **"LoRa ignorado" falso no gateway:** o fim de uma TRANSMISSÃO (ACK, BIPE, PONTO OK) também dispara o pino DIO1, que o código entende como "chegou pacote". Depois de cada ACK o gateway lia um pacote inexistente e escrevia "LoRa de outro sistema" no OLED. **Solução:** zerar `chegouPacote` antes de `startReceive()` (o crachá já fazia). Ainda **não** conferido na bancada.

### 8.2 Decisão: bloqueio/desbloqueio do PC vai para o banco (tabela nova)
- **Pedido:** registrar no banco quando o PC bloqueia e desbloqueia. Não havia onde guardar (o status do PC só atualizava `ultimo_contato`). A equipe escolheu uma **tabela nova**, `evento_pc` (`cloud/db/init/06_evento_pc.sql`), separada de `evento` para não virar ponto (armadilha 14).
- **Caminho:** agente do PC publica em `unifesp/ponto/pc/{badge}/evento` (QoS 1) **só nas transições** (bloqueou; saiu do bloqueio). Trocar AUTO↔FIXO não conta. Novo fluxo no Node-RED: `pc/+/evento` → `INSERT INTO evento_pc ... SELECT ... FROM dispositivo` (PC não cadastrado = 0 linhas, aviso no nó).
- **Teste (simulado, sem hardware):** `BLOQUEADO` e `DESBLOQUEADO (FIXO, -55 dBm)` gravados com o usuário `nodered`; mensagem inválida ignorada; ponto simulado (badge 102) gravado. Dados de teste apagados.
- **Risco:** `evento` já tem pontos do badge 101 com `seq` 13 e 14 (gateway antigo). O crachá novo usa `seq` 1–32767 e esses números serão recusados como duplicata. Apagar os pontos de teste antes do teste do crachá.

### 8.3 Painel: bloqueios do PC e revalidação do LoRa (26/11/2026)
- **Painel:** nova view `vw_eventos_pc` (só leitura para o `nodered`) e tabela "Bloqueio / desbloqueio do PC (últimos 30)" no dashboard; atualiza a cada 10 s e logo após cada evento do PC. Conferido: o `nodered` lê a view; Node-RED sem erros.
- **Revalidação do LoRa (sem as placas e sem compilador no notebook):** (1) configuração do rádio idêntica nos dois `.ino` e igual à do teste 02 que passou (915 MHz, BW 125, SF7, CR 4/5, sync 0x2B, TCXO 1,8 V, mesma forma da RadioLib 8.x); (2) layout do pacote de 11 bytes conferido byte a byte entre `montarPacote`/leituras do crachá e do gateway (tipo 1 ponto, 2 ACK, 3 comando, 4 ACK do comando); (3) ACK casa por badge + seq, hora em little-endian; (4) lado gateway → banco simulado com o JSON exato do firmware: ponto da fila (`fila=true`, rssi/snr gravados) e **retransmissão do mesmo seq recusada (0 duplicatas)**. Dados de teste apagados.
- **Achado:** `firmware/testes/06_gateway_mqtt` ainda usa sync word 0x12; se for gravado junto com o crachá final, os dois não se ouvem. Usar só o gateway final.
- **Continua sem prova:** o ar (PRG → antena → gateway). Só a bancada confirma.
