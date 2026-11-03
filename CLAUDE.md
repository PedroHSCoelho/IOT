# CLAUDE.md — Crachá Inteligente de Ponto (UNIFESP, Projeto IoT)

Projeto da disciplina de IoT (prof. Gabriel de Freitas Fernandes). Entrega e apresentação: **04/12/2026**.
Um crachá (Heltec LoRa 32 V3) bate ponto pelo botão (LoRa → gateway) e por BLE (agente Python no PC). Tudo chega por MQTT ao Node-RED, é gravado no PostgreSQL (Supabase) e aparece num painel de gerência.

**COMECE POR `PENDENCIAS.md`**: estado atual, o que falta e divergências entre documentos.

## Como trabalhar com este usuário
- Os alunos **nunca usaram Node-RED, MQTT nem banco de dados**. Explique cada conceito novo em português simples (analogias), antes de escrever código. Comentários dos arquivos em português.
- **Não mude o modelo de dados (tabelas/colunas) sem perguntar.** Decisões novas: registre em `PENDENCIAS.md` (seção 5).
- Notebook **sem administrador e sem Docker**: tudo é portátil (Node.js + `npm`). Não proponha instaladores.
- Não exiba, repita nem grave em documentos a senha do usuário. Ela vive só em `cloud/.env` (fora do Git). O usuário decidiu usar a **mesma senha** para o admin do banco, o `nodered` e o editor do Node-RED.
- Nunca desligue verificação de TLS (o sistema de segurança do Claude Code bloqueia). A conexão ao Supabase usa `cloud/servidor/prod-ca-2021.crt`.

## Mapa do repositório
```text
BANCO_DE_DADOS.md        especificação do banco (regras R1-R8, testes T1-T10)
PENDENCIAS.md            estado e pendências  ← leia primeiro
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

## Segurança (estado atual)
Dashboard e CSV **sem senha**; Node-RED escuta só em `127.0.0.1`. Se abrir para a rede (`uiHost`), proteja com `httpNodeAuth` primeiro.
