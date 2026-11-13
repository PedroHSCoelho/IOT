# Pendências e estado do projeto — Crachá Inteligente de Ponto

> **Para quem for retomar (pessoa ou IA):** este arquivo é a fonte da verdade do que está feito e do que falta.
> Última atualização: **12/11/2026** (**fluxo novo: o botão desbloqueia o PC**; ver seção 3A e `docs/FLUXO_BLE_E_BLOQUEIO.md`). Apresentação: **04/12/2026**. Prazos do cronograma (PDF do projeto):
> circuito validado em protoboard até **09/11** · layout da PCI pronto até **14/11** · PCI fabricada e montada até **23/11** · última semana para integração, documentação e ensaio.
>
> Leia também: `docs/DIARIO_DE_BORDO.md` (relato, problemas e decisões; **atualizar a cada passo**), `docs/FLUXO_BLE_E_BLOQUEIO.md`, `CLAUDE.md` (convenções e armadilhas), `BANCO_DE_DADOS.md` (especificação do banco),
> `cloud/db/README.md` e `cloud/servidor/README.md` (como rodar), `cloud/db/RELATORIO.md` (histórico detalhado).

---

## 0. RETOMADA: de onde continuar (fim da sessão de 12/11/2026)

**Como retomar:** abrir o Claude Code neste diretório e dizer "continue o projeto". Ele lê `CLAUDE.md` → este arquivo → `docs/DIARIO_DE_BORDO.md`. Antes de qualquer coisa: `git pull`.

**Onde paramos:** testes de protoboard 1 (OLED/LED), 2 (ping-pong LoRa) e 5 (BLE v1) **passaram**. O agente do PC v1 falhou na prática, e o **fluxo novo (botão desbloqueia o PC, pico do RSSI bloqueia ao afastar)** está escrito e enviado ao Git, mas **nunca foi testado**.

**Próximos passos, em ordem:**
1. **Testar o agente v2 no PC do Flávio** (precisa das 2 pessoas/PCs e da Heltec com antena): gravar `firmware/testes/05_ble_anuncio`, `pip install bleak`, `python pc-agent/agente_bloqueio.py`, seguir o roteiro de `docs/FLUXO_BLE_E_BLOQUEIO.md` seção 8 (apertar perto, apertar longe, **tapar o crachá**, afastar). Anotar resultados e tempos no diário (tabela de metas) e ajustar os limiares (`--liberar`, `--manter`; com o crachá no corpo o sinal cai 5–10 dB). Se o bloqueio demorar mais de 10 s: `--janela 4 --seg-ausente 3 --aviso 3`.
2. **Medir RSSI com o crachá no corpo**, no corredor e atrás de parede (`python pc-agent/teste_ble_rssi.py`) e registrar na tabela do diário.
3. **Testes 6 e 7 (caminho até o banco):** gravar `06_gateway_mqtt` numa Heltec e `07_cracha_prg_lora` na outra; copiar `config.example.h` para `config.h` (Wi-Fi 2,4 GHz + IP do broker); instalar a biblioteca **PubSubClient**. **Decidir antes onde roda o broker** (o notebook do Pedro não tem administrador para liberar a porta 1883; o Node-RED precisa do `cloud/.env`, que não vai pelo Git). Ver P17 no diário.
4. **Avisar o professor** da mudança de requisito (botão desbloqueia; PC não registra ponto).
5. **Peças que faltam chegar:** bateria CR2032 (Mercado Livre), botão, buzzer. Quando chegarem: Teste 3 (persistência do DS1302) e Teste 4 (botão + LED + buzzer); depois trocar o PRG pelo botão real (`PINO_BOTAO` no teste 05).
6. **Prazos:** layout da PCI até **14/11** (precisa da pinagem confirmada), PCI montada até **23/11**, apresentação **04/12/2026**. A PCI (3 pts) é o maior risco: começar o KiCad assim que a pinagem fechar.
7. **Pontas soltas:** registrar distância/RSSI de alcance do LoRa (teste 2); print do T10 sem senha; divisão de tarefas A/B/C/D; criar as pastas do Git (`firmware/cracha`, `firmware/gateway`, `hardware/`, `enclosure/`); antes de 04/12 reativar o Supabase, exportar o histórico e ensaiar com o hotspot em 2,4 GHz.

**Regras que valem sempre:** nunca ligar a Heltec sem antena · `pull` + commit + `push` a cada passo · atualizar o diário (problemas incluídos) · não mudar tabelas sem perguntar.

---

## 1. O que está PRONTO e verificado

| Parte | Estado | Onde |
| --- | --- | --- |
| Banco no Supabase (5 tabelas, RLS, índices) | ✅ | `cloud/db/init/01_schema.sql` |
| Regras de negócio R1–R5 e R8 (`registrar_ponto()`, `dia_local()`) | ✅ | `cloud/db/init/02_funcoes.sql` |
| Views do painel (R6, R7) | ✅ | `cloud/db/init/03_views.sql` |
| Usuário `nodered` com menor privilégio + políticas RLS | ✅ | `cloud/db/init/04_usuarios.sql` |
| Seed (3 funcionários, 3 dispositivos, 1 dia completo do badge 101) | ✅ | `cloud/db/init/05_seed.sql` |
| Testes automatizados (31 checks, todos passam) | ✅ | `cloud/db/testes/testes_regras.sql` |
| Broker MQTT local (`aedes`, porta 1883) | ✅ | `cloud/servidor/broker.mjs` |
| Node-RED 5.0.9 + fluxo (pontos, status, erros) | ✅ | `cloud/servidor/gerar_fluxo.mjs` → `nodered-dados/flows.json` |
| Conexão ao Supabase com TLS verificado (certificado raiz) | ✅ | `cloud/servidor/prod-ca-2021.crt` |
| Painel de gerência (Dashboard 2.0, `http://127.0.0.1:1880/dashboard`) | ✅ | presença, jornada do dia, semana (tabela e gráfico), histórico, dispositivos |
| Comando BIPE do painel (grava em `comando` e publica `unifesp/ponto/cmd/101`) | ✅ testado | painel, grupo "Comandos" |
| ACK do comando (`unifesp/ponto/cmd/{badge}/ack` → `CONFIRMADO`/`FALHOU` + `firmware_versao`) | ✅ testado (simulado) | `gerar_fluxo.mjs` (fluxo 5), `publicar_teste.mjs ack` |
| Exportação CSV (`/exportar/historico.csv`, `/exportar/jornada.csv`) | ✅ testado | painel, grupo "Exportar" |
| Senha no editor do Node-RED (usuário `admin`) | ✅ | hash em `cloud/.env` (`NODERED_ADMIN_HASH`) |

Ponta a ponta testado: crachá simulado → MQTT → Node-RED → `registrar_ponto()` → Supabase → painel.

---

## 2. PENDENTE — software que já tem base pronta

- [ ] **T10, persistência (desafio extra do banco):** ✅ parte do Node-RED feita em 03/11/2026 (`count(*)` de `vw_historico` = 4 antes e 4 depois de reiniciar o Node-RED; o CSV também traz 4 linhas). **Falta:** tirar o print para a entrega (consulta no SQL Editor, sem senha nem string de conexão) e, se der tempo, pausar/restaurar o projeto no painel do Supabase (ação sua; eu não pauso o projeto).
- [x] **Confirmação de comando pelo crachá (lado nuvem pronto, 03/11/2026).** Protocolo: o crachá/gateway publica em `unifesp/ponto/cmd/{badge}/ack` o JSON `{"id":<comando>,"ok":true,"versao":"2.0"}`. O Node-RED atualiza `comando.status` (`CONFIRMADO`/`FALHOU`), `confirmado_em`, `ultimo_contato` e, em OTA ok com `versao`, `dispositivo.firmware_versao`. `04_usuarios.sql` agora dá `UPDATE (ultimo_contato, firmware_versao)` ao `nodered` (testes S1 ajustados, 31/31 ok). Testado ponta a ponta com `npm run publicar-teste -- ack <id> ok 2.0`. **Falta no firmware:** o gateway/crachá realmente publicar esse ACK (o gateway repassa o comando por LoRa e devolve a resposta).
- [ ] **OTA ponta a ponta (desafio extra):** ✅ lado do notebook pronto (03/11/2026): o painel pede o OTA (botão + URL + versão), o crachá confirma por ACK (`firmware_versao` é gravada) e `npm run firmware-http` serve o `.bin` da pasta `cloud/servidor/firmware/` na porta 8080 (só GET de `.bin`, sem listar a pasta; o painel continua fechado). Na demo: copiar o `.bin`, rodar o comando, colar no painel o endereço que ele imprime (IP do hotspot) e liberar a porta 8080 no firewall do Windows se pedir. **Falta no firmware:** o gateway repassar o comando por LoRa ao crachá, que liga o Wi-Fi, baixa o `.bin` (HTTPUpdate), reinicia e publica o ACK com a `versao`. Plano B: gravar por USB.
- [x] **Gateway: Last Will e retained (lado nuvem pronto, 03/11/2026).** Formato esperado em `unifesp/ponto/gateway/status` (*retained*): `{"estado":"online","rssi":-71,"snr":9.5}` e, como Last Will, `{"estado":"offline"}` (texto simples `online`/`offline` também é aceito). O painel mostra uma linha "Gateway: online/offline · RSSI · SNR · último aviso há X" (estado guardado na memória do fluxo, sem mudar o banco; `offline` não atualiza `ultimo_contato`). Testado: retained entregue após reiniciar o Node-RED e Last Will disparado por queda sem aviso (`npm run publicar-teste -- gateway online|offline|cair`). **Falta no firmware do gateway:** conectar ao MQTT com `will` (tópico acima, `retain=true`, QoS 1) e publicar `online` + RSSI/SNR (a cada ~30 s e a cada pacote LoRa). Obs.: o `online` retained entregue na partida do Node-RED também renova `ultimo_contato` (aceitável; o aviso retained pode ser antigo).
- [ ] **Painel:** ✅ (03/11/2026) quem não bateu ponto aparece com 0h 00min e "sem ponto"; filtros **Dia** (vazio = hoje) e **Nome contém...** valem para jornada, semana e histórico (a presença é sempre "agora"). Consultas parametrizadas (`$1`, `$2`), testadas como `nodered`; sem mudar tabelas nem views. Limites conhecidos: a **carga** de quem nunca teve ponto aparece como `-` (o `nodered` não lê `funcionario`; só dá para inferi-la de outro dia do mesmo funcionário); funcionário inativo some dos filtros (as consultas partem de `vw_presenca_atual`); o filtro é global (todo mundo que abrir o painel vê o mesmo). **Falta:** conferir a legibilidade na tela da apresentação e refinar o visual.
- [ ] **Acesso ao painel por outros dispositivos.** `uiHost: "127.0.0.1"` está definido em `nodered-dados/settings.js` (corrigido em 03/11/2026: antes estava comentado e o Node-RED escutava em `0.0.0.0`, expondo painel e CSV sem senha). Para abrir o painel no celular ou em outro PC: trocar para `"0.0.0.0"` **e** proteger com `httpNodeAuth` (o dashboard e os CSV **não têm senha**; só o editor tem). Usar só na rede do hotspot.
- [ ] **Segredos:** `credentialSecret` do Node-RED não definido (sem credenciais salvas hoje, então sem urgência). Decisão do usuário: **a mesma senha** para `postgres`, `nodered` e editor do Node-RED (risco conhecido e aceito; se a do `nodered` vazar, a do admin também vaza).
- [ ] **Antes de 04/12:** reativar o projeto Supabase se pausado (plano gratuito pausa após ~1 semana sem uso); **exportar dump/CSV** do histórico (sem backup automático); testar com hotspot do celular em **2,4 GHz**; limpar dados de teste e decidir o que a demonstração mostra; ensaiar a explicação do banco (chaves, índice, view, função, `REVOKE`, RLS).
- [ ] **Docker é opcional.** `cloud/docker-compose.yml` está vazio de propósito (notebook sem Docker). Se alguém tiver Docker, pode trocar o `aedes` pelo Mosquitto sem mudar fluxo nem firmware.

## 3A. Parte física: ONDE PARAMOS (11/11/2026)

Começamos a validação em protoboard em 11/11. Em 12/11: testes 1, 2 e 5 passaram; 3 e 4 adiados (peças não chegaram); fluxo do BLE refeito (ver abaixo). Histórico completo, com problemas, em `docs/DIARIO_DE_BORDO.md`.

**Onde programar:** PC do Flavio (tem admin e Arduino IDE). **Em mãos:** 2 Heltec LoRa 32 V3 com antenas e 2 cabos USB-C. **Regra de ouro:** nunca ligar a Heltec sem a antena conectada.

Plano (cada teste só começa depois que o anterior passa):

- [ ] **Passo 0 — preparar o Arduino IDE:** em *Preferências*, adicionar a URL `https://resource.heltec.cn/download/package_heltec_esp32_index.json`; instalar o pacote **Heltec ESP32** (plano B: pacote Espressif `esp32` + placa *ESP32S3 Dev Module*); instalar a biblioteca **U8g2**; se a porta COM não aparecer, trocar o cabo (precisa ser de dados) ou instalar o driver CP210x. Para gravar à força: segurar PRG, apertar RST, soltar PRG.
- [ ] **Confirmar que a placa é a V3** (os pinos do código são da V3; V2/V3.2 mudam).
- [ ] **Teste 1 — Olá mundo (LED + OLED):** código pronto em `firmware/testes/01_ola_mundo/01_ola_mundo.ino` (LED = GPIO 35, Vext = GPIO 36 em LOW liga o OLED, OLED I2C SDA 17 / SCL 18 / RST 21). Esperado: OLED com contador, LED piscando, linhas no Monitor Serial (115200). Repetir nas **duas** placas.
- [x] **Teste 1 passou** (12/11/2026, no PC do Flavio).
- [x] **Teste 2 — ping-pong LoRa passou** (12/11/2026; falta registrar a distância/RSSI de alcance) entre as duas Heltec (915 MHz, BW 125 kHz, SF7, CR 4/5, sync word 0x12, 14 dBm). Código em `firmware/testes/02_pingpong_lora/02_pingpong_lora.ino` (ajustado pelo Flavio para a **RadioLib 8.x**); precisa da biblioteca **RadioLib** 8.x; mesma sketch nas duas placas, trocando `SOU_MESTRE` (true/false).
- [ ] **Teste 3 — ler e acertar o DS1302** (CLK 40, DAT 41, RST 42). Código escrito (**ainda não testado**): `firmware/testes/03_relogio_ds1302/03_relogio_ds1302.ino`, sem biblioteca extra. Duas etapas: `ACERTAR_RELOGIO = true` (acerta com a hora do PC), depois `false`; religar a USB após 1 min e conferir que a hora segue certa (bateria CR2032). **Sem bateria por enquanto:** manter `true` (acerta a cada gravação), validar só a leitura com a placa ligada e deixar a etapa de persistência para quando a bateria chegar.
- [ ] **Teste 4 — botão (debounce 50 ms) + LED + buzzer.** Código ainda não escrito. ⏸ **Adiado** (12/11/2026): os componentes ainda não chegaram.

**Foco atual (decisão de 12/11/2026): só (A) Bluetooth crachá↔PC e (B) comunicação com o banco.** Testes de componentes físicos (relógio, botão, buzzer) ficam para quando chegarem.

- ❌ **Agente v1 (média de RSSI, limiar ~1 m) reprovado no teste prático (12/11/2026):** desbloqueava quando o usuário tapava o crachá e demorava para liberar (média). Detalhes em `docs/DIARIO_DE_BORDO.md` (P13, P14).
- [ ] **Agente v2 — fluxo novo (12/11/2026): o BOTÃO desbloqueia, o afastamento bloqueia.** Código escrito, **ainda não testado**: `firmware/testes/05_ble_anuncio` (v2: anúncio com `boot` + **contador de cliques**, anúncio rápido de 100 ms por 5 s após o clique; botão = PRG) e `pc-agent/agente_bloqueio.py` (tela cheia "BLOQUEADO"; clique com RSSI do pacote > −60 dBm libera; liberado, o **pico** de 8 s abaixo de −70 dBm por 10 s dispara o aviso "bloqueando em 5 s"; ESC = saída de emergência; `--iniciar-liberado` para desenvolver; `--mqtt IP` opcional publica `pc/{badge}/status`, não testado). Decisões aprovadas: canal do clique = contador no anúncio BLE; botão com o PC liberado = ignorar; política de bloqueio acima. **Bloqueio de demonstração, não é segurança** (dizer na apresentação). **A testar:** roteiro em `docs/FLUXO_BLE_E_BLOQUEIO.md` seção 8; medir botão→desbloqueio e afastar→bloqueio (a política padrão pode passar de 10 s; `--janela 4 --seg-ausente 3 --aviso 3` encurta). Falta: calibrar com o crachá no corpo; assinar o contador (HMAC), melhoria opcional.
- ⏸ **Testes 6 e 7 ficam para outro dia** (decisão de 12/11/2026); hoje só o BLE + bloqueio.
- [ ] **Teste 6 + 7 — caminho até o banco (B):** `firmware/testes/06_gateway_mqtt` (gateway: Wi-Fi + NTP + MQTT com Last Will, recebe LoRa de 11 bytes, publica em `unifesp/ponto/evento` e responde ACK só se publicou; precisa **PubSubClient** e `config.h` copiado de `config.example.h`) e `firmware/testes/07_cracha_prg_lora` (crachá **102** de teste: botão PRG manda o ponto, 3 tentativas de 2 s, `seq` guardado na memória da placa). Código escrito, **ainda não testado**. Lembrar: apagar os pontos de teste depois (`DELETE FROM evento WHERE seq > 0 ...`).
- [x] **Teste 5 — BLE (A):** **v1 passou (12/11/2026):** o crachá anunciava `UNIFESP-BADGE-101` a cada 500 ms e o PC media o RSSI (`pc-agent/teste_ble_rssi.py`, precisa `pip install bleak`; hoje é só ferramenta de calibração). O firmware foi reescrito (v2, ver agente v2 acima). **Medições de RSSI (crachá na mesa, sem corpo):** 0,15 m → −39 dBm (máx. que o cabo alcançava) · 0,5 m → −41 · 1 m → −48 · 1,5 m → −52 · 2 m → −56,5. Queda de ~7 dB a cada dobra de distância; extrapolando, −65 ≈ 4 m e −75 ≈ 10 m. **A refazer:** com o crachá no corpo (o corpo absorve 5–10 dB), em corredor e atrás de parede, para calibrar os limiares.
Metas do cronograma: protoboard validada até 09/11 (**já passou**), layout da PCI até 14/11, PCI montada até 23/11. Estamos atrasados (testes 1, 2 e 5 já passaram; 3 e 4 aguardam peças); priorizar o agente v2 e os testes 6–7.

---

## 3. PENDENTE — fora do banco/Node-RED (do PDF do projeto)

- [ ] **Repositório Git (item obrigatório, RNF10).** ✅ Repositório criado e publicado em https://github.com/PedroHSCoelho/IOT (monorepo, branch `main`; `cloud/` e documentação já enviados). **Falta:** cada membro commitar com a própria conta e criar as demais pastas. Estrutura pedida: `firmware/cracha`, `firmware/gateway`, `cloud/`, `pc-agent/`, `hardware/` (KiCad), `enclosure/` (STL/STEP), `docs/`. `.env` e `config.h` **nunca** vão para o Git (usar `config.example.h`). Já existe `cloud/.gitignore` (ignora `.env`) e `cloud/servidor/.gitignore`.
- [ ] **Divisão de tarefas:** o PDF tem os nomes em branco (A hardware/PCI, B firmware do crachá, C gateway/nuvem, D computador/integração).
- [ ] **Firmware do crachá** (Heltec V3): botão com debounce 50 ms, relógio DS1302 (CLK 40, DAT 41, RST 42), LoRa com ACK (2 s, 3 tentativas, fila e reenvio a cada 30 s), BLE anunciando `UNIFESP-BADGE` com o ID, um `boot` aleatório e um **contador de cliques** (500 ms; 100 ms por 5 s após cada clique; ver `docs/FLUXO_BLE_E_BLOQUEIO.md` seção 4), OLED (ligar Vext, GPIO 36, antes), LED e buzzer.
- [ ] **Firmware do gateway** (Heltec V3): recebe LoRa, valida, responde ACK com a hora (NTP), converte para JSON e publica `unifesp/ponto/evento` (`origem:"botao"`), OLED com último evento e RSSI/SNR, LED/buzzer/botão de teste. LoRa: 915 MHz, BW 125 kHz, SF7, CR 4/5, sync word 0x12, 14 dBm; pacote binário de 11 bytes (versão, tipo, badge_id, seq, hora Unix, flags). **Nunca ligar o módulo sem antena.**
- [ ] **Agente Python do PC** (`bleak`, `paho-mqtt` opcional): **redefinido em 12/11/2026** (ver agente v2 na seção 3A). Não usa mais média, PRESENTE/AUSENTE nem `origem:"ble"`; o botão libera, o pico do RSSI bloqueia, e publica só `unifesp/ponto/pc/{badge}/status`. SO decidido: **Windows com tela de bloqueio própria** (o Windows só permite bloquear com `LockWorkStation`).
- [x] **Teste de compatibilidade BLE do PC** (12/11/2026): o PC do Flávio enxerga o crachá com `bleak` (Teste 5 v1 passou). Plano B (adaptador USB 5.0) não foi necessário.
- [ ] **Validação em protoboard (1 pt):** botão → LoRa → gateway → MQTT, com LED, buzzer e relógio. Meta 09/11. Testes iniciais do PDF: ping-pong LoRa entre as duas Heltec, ler/acertar o DS1302, botão+LED+buzzer.
- [ ] **PCI + documentação (3 pts):** KiCad (esquemático PDF, layout, render 3D, Gerbers e furação, lista de componentes, fotos, registro de erros A→B). Face simples, trilhas ≥ 0,8 mm, 2 cópias (mesmo desenho para crachá e gateway). Pinagem a confirmar na protoboard.
- [ ] **Caixas impressas em 3D (desafio extra):** crachá e gateway, folga 0,3–0,5 mm, STL/STEP no Git.
- [ ] **Requisitos desejáveis:** Wake-on-LAN (só PC no cabo, mesma rede); OLED do crachá com hora e status.
- [ ] **Metas de desempenho a medir e registrar:** botão→bipe ≤ 2 s; **botão→desbloqueio do PC ≤ 2 s (novo; substitui o "aproximar→desbloqueio ≤ 5 s" do PDF)**; afastar→bloqueio ≤ 10 s; evento→painel ≤ 5 s; alcance LoRa no prédio (registrar a distância); 0 perdas (fila no crachá); 0 duplicatas; erro do relógio < 2 s.
- [ ] **Apresentação com evidências:** vídeos, prints do painel, OLED mostrando eventos, fotos da placa.
- [ ] **Compras (conferir se foi feito):** ⚠ em 12/11/2026 a **CR2032 ainda não chegou** (Mercado Livre); o DS1302 já está em mãos. Teste 3 segue **sem bateria** (só a etapa de persistência fica adiada). CR2032 (no lugar da CR2016) e barra fêmea 1×20 (no lugar da 1×15).

---

## 4. Divergências entre os documentos (resolvidas ou a confirmar)

1. **`Lista_de_Materiais_Projeto_IoT.pdf` (27/10) está DESATUALIZADA.** Fala de 2 crachás ESP32-C3, nó de estação com ESP32+RFM95W e Docker. A **revisão de 29/10** (no PDF do projeto) tirou isso: o crachá é a própria Heltec com botão, LED, buzzer e DS1302; a 2ª Heltec é o gateway; escopo = 1 crachá, 1 gateway, 1 PC, 1 servidor. **Vale o PDF do projeto.**
2. **Regra dos 30 minutos × alternância (R4).** O PDF diz "segundo ponto do mesmo tipo em menos de 30 min é registrado mas não conta". O `BANCO_DE_DADOS.md` **substituiu** isso pela alternância obrigatória (qualquer segundo ponto do mesmo tipo no dia não conta). Decisão da equipe; manter e **explicar na arguição**.
3. **Tabela `jornada`.** O PDF mostra uma tabela `jornada`; o banco usa **views** (`vw_jornada_diaria`, `vw_jornada_semanal`), sempre calculadas a partir dos eventos. Decisão do `BANCO_DE_DADOS.md`.
4. **Quem calcula chegada/saída.** O PDF diz "Node-RED alterna"; na implementação quem decide é o **banco** (`registrar_ponto()`), e o Node-RED só faz a chamada.
5. **Mosquitto × aedes.** O PDF e o `BANCO_DE_DADOS.md` falam de Mosquitto; usamos **aedes** (mesmo protocolo MQTT) porque o notebook não permite instalar programas. Trocável sem mudar o resto.
6. **Nomes de pasta no Git:** o PDF cita `firmware/badge` e `firmware/node` em um parágrafo e `firmware/cracha`/`firmware/gateway` em outro. Usar `cracha` e `gateway` (o nó de estação saiu do projeto).
7. **BLE por proximidade × botão que desbloqueia (12/11/2026).** O PDF e o `BANCO_DE_DADOS.md` descrevem o BLE como ponto automático ("aproximar → CHEGADA/desbloqueio ≤ 5 s", `origem:"ble"`). Pelo teste prático (agente v1: P13/P14 no diário) a equipe mudou: o **botão** desbloqueia o PC, o PC bloqueia ao afastar e **não registra ponto**. A origem `ble` do banco continua existindo, sem uso pelo agente; **tabelas inalteradas**. **Avisar o professor** da mudança de requisito.

## 5. Decisões minhas (da IA) que a equipe ainda deve confirmar
- Ponto com `fila=true` pode ter mais de 24 h de atraso sem a hora ser corrigida (o adiantamento de mais de 5 min é sempre corrigido).
- O primeiro evento do dia sendo `SAIDA` fica no histórico sem contar horas (`SAIDA_SEM_CHEGADA`).
- Carga semanal = carga diária × 5.
- Seed com `seq` negativo (−1 a −4), para nunca colidir com o contador do crachá.
- O status do PC (`unifesp/ponto/pc/{badge}/status`) atualiza o dispositivo `pc-lab-01` (ou o `id` enviado no JSON); o do gateway atualiza `gw-lab`.
- (12/11/2026, aprovado pela equipe) O botão com o PC já liberado é **ignorado** pelo PC; a política de bloqueio padrão é pico de 8 s < −70 dBm por 10 s + aviso de 5 s; o clique chega ao PC por contador no anúncio BLE; o agente não publica `origem:"ble"`.
