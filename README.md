# Crachá Inteligente de Ponto — Projeto de IoT (UNIFESP)

Projeto da disciplina de IoT (prof. Gabriel de Freitas Fernandes). Apresentação: **04/12/2026**.

Um crachá (Heltec LoRa 32 V3) registra o ponto pelo botão (LoRa → gateway) e por proximidade BLE
(agente Python no PC). Tudo chega por MQTT ao Node-RED, é gravado no PostgreSQL (Supabase) e aparece
num painel de gerência.

```text
Crachá ──LoRa──► Gateway ──MQTT──► Node-RED ──► PostgreSQL (Supabase) ──► Painel
   └────BLE────► Agente Python (PC) ──MQTT──┘
```

## Onde ler
| Arquivo | Conteúdo |
| --- | --- |
| `PENDENCIAS.md` | **Comece por aqui:** estado atual, o que falta e divergências entre documentos |
| `CLAUDE.md` | Convenções do projeto e armadilhas já encontradas |
| `BANCO_DE_DADOS.md` | Especificação do banco (regras R1–R8, testes T1–T10) |
| `cloud/db/README.md` | Como criar e testar o banco |
| `cloud/servidor/README.md` | Como rodar broker MQTT e Node-RED |
| `diagrama/` | Diagrama de blocos e descrição dos blocos (imagens) |
| `Projeto_IoT_Cracha_Inteligente_de_Ponto.pdf` | Especificação e cronograma do projeto |

## Estrutura
```text
cloud/db/        schema, funções, views, usuários/RLS, seed e testes (SQL)
cloud/servidor/  broker MQTT (aedes), gerador do fluxo Node-RED, painel
diagrama/        diagrama de blocos
```
Ainda por criar: `firmware/cracha`, `firmware/gateway`, `pc-agent/`, `hardware/`, `enclosure/`, `docs/`.

## Segredos
`cloud/.env` **não** vai ao Git (modelo em `cloud/.env.example`). Futuramente, o mesmo vale para `config.h`.
