# CLAUDE.md — Crachá Inteligente de Ponto (UNIFESP, Projeto IoT)

Projeto da disciplina de IoT (prof. Gabriel de Freitas Fernandes). Entrega e apresentação: **04/12/2026**.
Um crachá (Heltec LoRa 32 V3) bate ponto pelo botão (LoRa → gateway). O mesmo clique desbloqueia o PC por BLE (agente Python no PC; **o PC não registra ponto**, só libera/bloqueia a tela; fluxo em `docs/FLUXO_BLE_E_BLOQUEIO.md`). O ponto chega por MQTT ao Node-RED, é gravado no PostgreSQL (Supabase) e aparece num painel de gerência.

**COMECE POR `PENDENCIAS.md`**: estado atual, o que falta e divergências entre documentos.

## Como trabalhar com este usuário
- Os alunos **nunca usaram Node-RED, MQTT nem banco de dados**. Explique cada conceito novo em português simples (analogias), antes de escrever código. Comentários dos arquivos em português.
- **A cada passo concluído:** `git pull`, commit só dos arquivos do passo e `git push` (a equipe alterna entre o notebook do Pedro e o PC do Flávio). Nunca commite `.env` nem `config.h`.
- **Mantenha `docs/DIARIO_DE_BORDO.md` atualizado a cada passo:** o que foi feito, decisões, medições e **principalmente os problemas** (causa e solução). É a base do relatório e do relato da apresentação.
- **Não mude o modelo de dados (tabelas/colunas) sem perguntar.** Decisões novas: registre em `PENDENCIAS.md` (seção 5).
- Notebook **sem administrador e sem Docker**: tudo é portátil (Node.js + `npm`). Não proponha instaladores.
- Não exiba, repita nem grave em documentos a senha do usuário. Ela vive só em `cloud/.env` (fora do Git). O usuário decidiu usar a **mesma senha** para o admin do banco, o `nodered` e o editor do Node-RED.
- Nunca desligue verificação de TLS (o sistema de segurança do Claude Code bloqueia). A conexão ao Supabase usa `cloud/servidor/prod-ca-2021.crt`.

## Mapa do repositório
Remoto: https://github.com/PedroHSCoelho/IOT (branch `main`). `README.md` é a porta de entrada; `diagrama/` tem o diagrama de blocos.
```text
BANCO_DE_DADOS.md        especificação do banco (regras R1-R8, testes T1-T10)
PENDENCIAS.md            estado e pendências  ← leia primeiro
docs/DIARIO_DE_BORDO.md  relato: linha do tempo, problemas, decisões, medições (atualizar a cada passo)
docs/FLUXO_BLE_E_BLOQUEIO.md   botão desbloqueia o PC; PC bloqueia ao afastar (estados, anúncio BLE, parâmetros)
firmware/testes/         testes de protoboard (01 OLED, 02 LoRa, 03 DS1302, 05 BLE, 06 gateway MQTT, 07 crachá PRG)
pc-agent/                agente_bloqueio.py (v2) e teste_ble_rssi.py; precisa de: pip install bleak
cloud/.env               segredos (NÃO vai ao Git); modelo em .env.example
cloud/db/init/01..05_*.sql   schema, funções, views, usuários/RLS, seed (rodar nesta ordem)
cloud/db/testes/testes_regras.sql   31 checks; roda em transação com ROLLBACK
cloud/db/{README,RELATORIO}.md, reset.sql, rodar_sql.py
cloud/servidor/          broker.mjs (aedes), gerar_fluxo.mjs → nodered-dados/flows.json, publicar_teste.mjs
```

## Comandos úteis (a partir de `cloud/servidor`)
```text
npm run broker          # terminal 1: broker MQTT (porta 1883). Ligue ANTES do Node-RED
npm run nodered         # terminal 2: Node-RED em http://127.0.0.1:1880 (editor: usuário admin)
npm run gerar-fluxo     # regenera nodered-dados/flows.json (NUNCA edite o JSON à mão); reinicie o Node-RED
npm run publicar-teste -- botao 102     # simula crachá (use o 102, o 101 é o da demo)
npm run publicar-teste -- ack 12 ok 2.0 # simula o ACK de um comando (ou: gateway online|offline|cair)
npm run firmware-http                   # serve cloud/servidor/firmware/*.bin na porta 8080 (OTA); só na demo
# painel: http://127.0.0.1:1880/dashboard    CSV: /exportar/historico.csv e /exportar/jornada.csv
# testes do banco (de cloud/db):  python rodar_sql.py <arquivo.sql>   (precisa de: pip install --user "psycopg[binary]")
```
O Node-RED lê `cloud/.env` com `node --env-file=../.env`. Para o MCP do Supabase (`mcp__supabase__*`), o projeto é `gyeenaohkdmzvsecpkgj`, região **us-east-1** (pooler `aws-0-us-east-1.pooler.supabase.com`, usuário `nodered.<ref>`, porta 5432).

## Armadilhas já encontradas (não repita)
1. **RLS sem política = falha SILENCIOSA.** O `UPDATE` roda sem erro e altera 0 linhas. As tabelas têm RLS ligado; o `nodered` só funciona por causa das políticas em `04_usuarios.sql`. Em testes de permissão, confira `ROW_COUNT`, não só "sem erro".
2. **Views × funções:** as views leem tabelas com a permissão do dono, mas **funções chamadas dentro delas rodam com a permissão de quem consulta**. Por isso o `nodered` tem `EXECUTE` em `dia_local()`. Teste lendo **todas** as views como `nodered`.
3. **PL/pgSQL:** `array || 'literal'` é ambíguo; use `array_append(a, 'x'::TEXT)`. Em `RETURNS TABLE`, use `#variable_conflict use_column`.
4. **Datas:** o Supabase roda em UTC. Todo "dia" passa por `dia_local()` (`America/Sao_Paulo` explícito). Nas consultas do painel, formate a hora no SQL (`to_char(... AT TIME ZONE ...)`), não em JavaScript.
5. **Node-RED + postgresql:** a mensagem carrega `msg.query`/`msg.params` do passo anterior e contamina as próximas consultas; por isso o painel começa de uma mensagem limpa. Nunca monte SQL com texto; use `$1, $2...`.
6. **Encerrar o Node-RED:** `TaskStop` mata só o `npm`; o processo `node ... red.js` continua segurando a porta 1880. Ache o PID pela porta, confira que a linha de comando tem `red.js` e encerre só ele.
7. **Teste dentro de `DO` + `SET LOCAL ROLE nodered`** exige `GRANT nodered TO postgres WITH SET TRUE` (já está em `04_usuarios.sql`).
8. **Dados de teste:** apague depois (`DELETE FROM evento WHERE seq > 0 ...`). O seed (seq negativo) fica.
9. `npm install` pode falhar uma vez por rede; tente de novo.
10. Mensagem `offline` do gateway **não** atualiza `ultimo_contato` (de propósito).
11. **Tabela do Dashboard 2.0:** nome de coluna com ponto (ex.: `'Obs.'`) vira caminho de objeto e a célula aparece **vazia**. Use nomes sem ponto (`'Observação'`).
12. **Permissão de coluna:** o `nodered` só lê `id, tipo, identificador` de `dispositivo`. Um `UPDATE` que *lê* outra coluna (ex.: `SET x = CASE ... ELSE dispositivo.firmware_versao`) dá `permission denied`. Escreva o `UPDATE` sem ler a coluna (ver o fluxo do ACK).

13. **BLE/RSSI é ruidoso.** Corpo e mão atenuam o sinal (5–10 dB). Média de RSSI + limiar único fez o PC desbloquear ao tapar o crachá e demorar para liberar (agente v1, 12/11/2026). Por isso: desbloqueio só pelo clique do botão (RSSI do pacote do clique) e bloqueio pelo **pico** da janela, com aviso.
14. **O agente do PC não publica `origem:"ble"`.** No banco, `PRESENTE`=CHEGADA e `AUSENTE`=SAÍDA (R3): cada bloqueio por distância viraria uma saída. Só o botão registra ponto; o PC publica apenas `pc/{badge}/status`.
15. **RadioLib 8.x mudou a API** (frequência em Hz, `ConfigLoRa_t`, `radio.tcxoVoltage`); use a forma de `firmware/testes/02_pingpong_lora`. BLE: o pacote de placas 2.x e o 3.x usam tipos de texto diferentes (`std::string` × `String`); o teste 05 trata os dois.
16. **O Windows não deixa um programa comum desbloquear a tela.** A tela de bloqueio do agente é uma janela própria (demonstração, não segurança). Saída de emergência: ESC.

## Segurança (estado atual)
Dashboard e CSV **sem senha**; Node-RED escuta só em `127.0.0.1` (`uiHost` em `settings.js`; confira com `netstat -ano | grep :1880`: deve mostrar `127.0.0.1`, não `0.0.0.0`). O único serviço aberto à rede é o `firmware-http` (porta 8080, só `.bin`), e só enquanto ligado. Se abrir para a rede (`uiHost`), proteja com `httpNodeAuth` primeiro.
