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
| P17 | 12/11 | Windows bloqueia a porta 1883 para outros PCs? | Notebook do Pedro sem administrador para liberar firewall | **Em aberto** (testes 6/7 ficam para outro dia): rodar broker em PC com administrador ou liberar a porta |

## 3. Decisões (e por quê)

| Data | Decisão | Por quê / alternativas |
| --- | --- | --- |
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
| Agente v2 (botão libera, pico bloqueia) | — | 🔲 a testar | Ver `docs/FLUXO_BLE_E_BLOQUEIO.md`, seção 8 |
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
| Botão → desbloqueio do PC (novo) | ≤ 2 s | — |
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
