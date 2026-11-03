# Servidor local: broker MQTT + Node-RED

Tudo aqui roda **sem instalar nada e sem ser administrador**: é só Node.js e pacotes `npm` dentro desta pasta.

```text
 crachá/gateway/PC ──MQTT──► broker.mjs (aedes) ──► Node-RED ──SQL──► Supabase (banco)
                              "correio"             "carteiro"         "arquivo"
```

| Arquivo | Para que serve |
| --- | --- |
| `broker.mjs` | O **broker MQTT** (o "correio"). Usamos `aedes` no lugar do Mosquitto, pois o Mosquitto no Windows só tem instalador. O protocolo é o mesmo |
| `gerar_fluxo.mjs` | Gera `nodered-dados/flows.json`, o desenho do fluxo (caixinhas e ligações), com cada caixinha comentada |
| `nodered-dados/flows.json` | O fluxo pronto. **Não tem senha**: usuário, senha e host vêm de `../.env` |
| `prod-ca-2021.crt` | Certificado público do Supabase, usado para **verificar** que estamos falando com o Supabase de verdade |
| `publicar_teste.mjs` | Finge ser um crachá, para testar sem o hardware |

## Como ligar (2 terminais, nesta pasta `cloud/servidor`)

```text
Terminal 1:   npm run broker      → "[broker] MQTT no ar na porta 1883"
Terminal 2:   npm run nodered     → "Started flows" e "Connected to broker"
```

Abra **http://127.0.0.1:1880** no navegador. Para ver o que acontece, abra a aba de **debug** (ícone de inseto 🐞, à direita).
Para desligar: `Ctrl+C` em cada terminal. **Ligue sempre o broker antes do Node-RED.**

## Editor com senha e painel de gerência

- **Editor** (`http://127.0.0.1:1880`): pede usuário **`admin`** e a senha. Só o *hash* da senha fica em `cloud/.env` (`NODERED_ADMIN_HASH`); se esse campo sumir, o Node-RED se recusa a iniciar (em vez de abrir sem senha).
  Para trocar a senha, gere outro hash (bcrypt, custo 10) e substitua a linha no `.env`.
- **Painel de gerência** (`http://127.0.0.1:1880/dashboard`): presença, jornada do dia, semana (tabela e gráfico), histórico dos últimos 50 pontos, dispositivos, comandos (BIPE e OTA) e exportação CSV. Atualiza a cada 10 s e logo após cada ponto.
  **O painel e os CSV não têm senha**; por isso o Node-RED só escuta em `127.0.0.1` (este computador).
- **CSV** (abre no Excel, separador `;`): `/exportar/historico.csv` e `/exportar/jornada.csv`.
- **OTA:** preencha "URL do .bin" e "Versão" e clique em *Atualizar firmware*. O comando é gravado na tabela `comando` e publicado em `unifesp/ponto/cmd/101`. A confirmação pelo crachá ainda não existe (veja `PENDENCIAS.md`).

## Como testar sem o crachá

Com os dois ligados, num terceiro terminal:

```text
npm run publicar-teste -- botao 102      # botão do crachá 102 (de teste)
npm run publicar-teste -- ble PRESENTE   # BLE do crachá 101
npm run publicar-teste -- botao 999      # crachá que não existe → vai para evento_rejeitado
```

A resposta do banco aparece no debug do Node-RED (`CHEGADA (conta horas)`, `REJEITADO - CRACHA_DESCONHECIDO`...).
Para conferir no Supabase: `SELECT * FROM vw_historico ORDER BY ts_evento DESC;`

> Prefira o crachá **102** nos testes: o 101 é o da demonstração. Limpe os testes com
> `DELETE FROM evento WHERE seq > 0 AND badge_id = 102;` (o seed usa `seq` negativo e não é afetado).

## Tópicos MQTT

| Tópico | Quem publica | O que acontece |
| --- | --- | --- |
| `unifesp/ponto/evento` | gateway (botão) e agente do PC (BLE) | Node-RED chama `registrar_ponto()` |
| `unifesp/ponto/gateway/status` | gateway | atualiza `ultimo_contato` do `gw-lab` (ou do `id` enviado no JSON) |
| `unifesp/ponto/pc/<badge>/status` | agente do PC | atualiza `ultimo_contato` do `pc-lab-01` (ou do `id` enviado no JSON) |

## Se der problema

| Sintoma | Causa provável |
| --- | --- |
| `port in use` ao ligar o Node-RED | Já tem um Node-RED rodando. Feche o outro terminal |
| `ECONNREFUSED 1883` no teste | O broker não está ligado |
| Erro `password authentication failed` no debug | Senha do `nodered` diferente da de `cloud/.env` |
| Erro `tenant/user not found` | Host ou usuário do pooler errados no `.env` (veja **Connect → Session pooler**) |
| `self-signed certificate` | O arquivo `prod-ca-2021.crt` sumiu da pasta, ou rode `npm run gerar-fluxo` de novo |
| Ponto não aparece e **nenhum erro** | Veja se o broker mostra a mensagem. Se mostra, confira o debug do Node-RED |

## Observações
- O editor do Node-RED (porta 1880) **não tem senha**. Na rede da faculdade, qualquer um que alcance o seu notebook poderia mexer no fluxo. Use só em rede confiável (ou hotspot) e feche ao terminar.
- Se alterar `gerar_fluxo.mjs`: `npm run gerar-fluxo` e reinicie o Node-RED.
- Se instalar o Mosquitto no futuro, o crachá e o fluxo não mudam: basta apontar o `broker_local` para ele.
