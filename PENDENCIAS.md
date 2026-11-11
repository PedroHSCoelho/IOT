# Pendências e estado do projeto — Crachá Inteligente de Ponto

> **Para quem for retomar (pessoa ou IA):** este arquivo é a fonte da verdade do que está feito e do que falta.
> Última atualização: **11/11/2026** (início da parte física; ver seção 3A). Apresentação: **04/12/2026**. Prazos do cronograma (PDF do projeto):
> circuito validado em protoboard até **09/11** · layout da PCI pronto até **14/11** · PCI fabricada e montada até **23/11** · última semana para integração, documentação e ensaio.
>
> Leia também: `CLAUDE.md` (convenções e armadilhas), `BANCO_DE_DADOS.md` (especificação do banco),
> `cloud/db/README.md` e `cloud/servidor/README.md` (como rodar), `cloud/db/RELATORIO.md` (histórico detalhado).

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

Começamos a validação em protoboard. **Nenhum passo foi executado ainda**; só o plano e o primeiro código estão prontos.

**Onde programar:** PC do Flavio (tem admin e Arduino IDE). **Em mãos:** 2 Heltec LoRa 32 V3 com antenas e 2 cabos USB-C. **Regra de ouro:** nunca ligar a Heltec sem a antena conectada.

Plano (cada teste só começa depois que o anterior passa):

- [ ] **Passo 0 — preparar o Arduino IDE:** em *Preferências*, adicionar a URL `https://resource.heltec.cn/download/package_heltec_esp32_index.json`; instalar o pacote **Heltec ESP32** (plano B: pacote Espressif `esp32` + placa *ESP32S3 Dev Module*); instalar a biblioteca **U8g2**; se a porta COM não aparecer, trocar o cabo (precisa ser de dados) ou instalar o driver CP210x. Para gravar à força: segurar PRG, apertar RST, soltar PRG.
- [ ] **Confirmar que a placa é a V3** (os pinos do código são da V3; V2/V3.2 mudam).
- [ ] **Teste 1 — Olá mundo (LED + OLED):** código pronto em `firmware/testes/01_ola_mundo/01_ola_mundo.ino` (LED = GPIO 35, Vext = GPIO 36 em LOW liga o OLED, OLED I2C SDA 17 / SCL 18 / RST 21). Esperado: OLED com contador, LED piscando, linhas no Monitor Serial (115200). Repetir nas **duas** placas.
- [ ] **Teste 2 — ping-pong LoRa** entre as duas Heltec (915 MHz, BW 125 kHz, SF7, CR 4/5, sync word 0x12, 14 dBm). Código ainda **não escrito**.
- [ ] **Teste 3 — ler e acertar o DS1302** (CLK 40, DAT 41, RST 42). Código ainda não escrito.
- [ ] **Teste 4 — botão (debounce 50 ms) + LED + buzzer.** Código ainda não escrito.

Metas do cronograma: protoboard validada até 09/11 (**já passou**), layout da PCI até 14/11, PCI montada até 23/11. Estamos atrasados; priorizar os testes 1–2.

---

## 3. PENDENTE — fora do banco/Node-RED (do PDF do projeto)

- [ ] **Repositório Git (item obrigatório, RNF10).** ✅ Repositório criado e publicado em https://github.com/PedroHSCoelho/IOT (monorepo, branch `main`; `cloud/` e documentação já enviados). **Falta:** cada membro commitar com a própria conta e criar as demais pastas. Estrutura pedida: `firmware/cracha`, `firmware/gateway`, `cloud/`, `pc-agent/`, `hardware/` (KiCad), `enclosure/` (STL/STEP), `docs/`. `.env` e `config.h` **nunca** vão para o Git (usar `config.example.h`). Já existe `cloud/.gitignore` (ignora `.env`) e `cloud/servidor/.gitignore`.
- [ ] **Divisão de tarefas:** o PDF tem os nomes em branco (A hardware/PCI, B firmware do crachá, C gateway/nuvem, D computador/integração).
- [ ] **Firmware do crachá** (Heltec V3): botão com debounce 50 ms, relógio DS1302 (CLK 40, DAT 41, RST 42), LoRa com ACK (2 s, 3 tentativas, fila e reenvio a cada 30 s), BLE anunciando `UNIFESP-BADGE` com o ID a cada 500 ms, OLED (ligar Vext, GPIO 36, antes), LED e buzzer.
- [ ] **Firmware do gateway** (Heltec V3): recebe LoRa, valida, responde ACK com a hora (NTP), converte para JSON e publica `unifesp/ponto/evento` (`origem:"botao"`), OLED com último evento e RSSI/SNR, LED/buzzer/botão de teste. LoRa: 915 MHz, BW 125 kHz, SF7, CR 4/5, sync word 0x12, 14 dBm; pacote binário de 11 bytes (versão, tipo, badge_id, seq, hora Unix, flags). **Nunca ligar o módulo sem antena.**
- [ ] **Agente Python do PC** (`bleak` + `paho-mqtt`): procura `UNIFESP-BADGE`, média das 5 últimas leituras de RSSI, PRESENTE se > −65 dBm por 3 s, AUSENTE se < −75 dBm ou sumido por 8 s (a calibrar); bloqueia/desbloqueia a tela; publica `origem:"ble"` com `estado` e publica `unifesp/ponto/pc/{badge}/status`. **Windows só permite bloquear** (`LockWorkStation`); para desbloquear, o agente precisa de tela de bloqueio própria ou a demo roda em Linux. Decidir qual SO.
- [ ] **Teste de compatibilidade BLE do PC** (LMP ≥ 6 / Bluetooth ≥ 4.0). Plano B: adaptador USB Bluetooth 5.0.
- [ ] **Validação em protoboard (1 pt):** botão → LoRa → gateway → MQTT, com LED, buzzer e relógio. Meta 09/11. Testes iniciais do PDF: ping-pong LoRa entre as duas Heltec, ler/acertar o DS1302, botão+LED+buzzer.
- [ ] **PCI + documentação (3 pts):** KiCad (esquemático PDF, layout, render 3D, Gerbers e furação, lista de componentes, fotos, registro de erros A→B). Face simples, trilhas ≥ 0,8 mm, 2 cópias (mesmo desenho para crachá e gateway). Pinagem a confirmar na protoboard.
- [ ] **Caixas impressas em 3D (desafio extra):** crachá e gateway, folga 0,3–0,5 mm, STL/STEP no Git.
- [ ] **Requisitos desejáveis:** Wake-on-LAN (só PC no cabo, mesma rede); OLED do crachá com hora e status.
- [ ] **Metas de desempenho a medir e registrar:** botão→bipe ≤ 2 s; aproximar→desbloqueio ≤ 5 s; afastar→bloqueio ≤ 10 s; evento→painel ≤ 5 s; alcance LoRa no prédio (registrar a distância); 0 perdas (fila no crachá); 0 duplicatas; erro do relógio < 2 s.
- [ ] **Apresentação com evidências:** vídeos, prints do painel, OLED mostrando eventos, fotos da placa.
- [ ] **Compras (conferir se foi feito):** CR2032 (no lugar da CR2016) e barra fêmea 1×20 (no lugar da 1×15).

---

## 4. Divergências entre os documentos (resolvidas ou a confirmar)

1. **`Lista_de_Materiais_Projeto_IoT.pdf` (27/10) está DESATUALIZADA.** Fala de 2 crachás ESP32-C3, nó de estação com ESP32+RFM95W e Docker. A **revisão de 29/10** (no PDF do projeto) tirou isso: o crachá é a própria Heltec com botão, LED, buzzer e DS1302; a 2ª Heltec é o gateway; escopo = 1 crachá, 1 gateway, 1 PC, 1 servidor. **Vale o PDF do projeto.**
2. **Regra dos 30 minutos × alternância (R4).** O PDF diz "segundo ponto do mesmo tipo em menos de 30 min é registrado mas não conta". O `BANCO_DE_DADOS.md` **substituiu** isso pela alternância obrigatória (qualquer segundo ponto do mesmo tipo no dia não conta). Decisão da equipe; manter e **explicar na arguição**.
3. **Tabela `jornada`.** O PDF mostra uma tabela `jornada`; o banco usa **views** (`vw_jornada_diaria`, `vw_jornada_semanal`), sempre calculadas a partir dos eventos. Decisão do `BANCO_DE_DADOS.md`.
4. **Quem calcula chegada/saída.** O PDF diz "Node-RED alterna"; na implementação quem decide é o **banco** (`registrar_ponto()`), e o Node-RED só faz a chamada.
5. **Mosquitto × aedes.** O PDF e o `BANCO_DE_DADOS.md` falam de Mosquitto; usamos **aedes** (mesmo protocolo MQTT) porque o notebook não permite instalar programas. Trocável sem mudar o resto.
6. **Nomes de pasta no Git:** o PDF cita `firmware/badge` e `firmware/node` em um parágrafo e `firmware/cracha`/`firmware/gateway` em outro. Usar `cracha` e `gateway` (o nó de estação saiu do projeto).

## 5. Decisões minhas (da IA) que a equipe ainda deve confirmar
- Ponto com `fila=true` pode ter mais de 24 h de atraso sem a hora ser corrigida (o adiantamento de mais de 5 min é sempre corrigido).
- O primeiro evento do dia sendo `SAIDA` fica no histórico sem contar horas (`SAIDA_SEM_CHEGADA`).
- Carga semanal = carga diária × 5.
- Seed com `seq` negativo (−1 a −4), para nunca colidir com o contador do crachá.
- O status do PC (`unifesp/ponto/pc/{badge}/status`) atualiza o dispositivo `pc-lab-01` (ou o `id` enviado no JSON); o do gateway atualiza `gw-lab`.
