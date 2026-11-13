# Crachá Inteligente de Ponto — Projeto de IoT (UNIFESP)

Projeto da disciplina de IoT (prof. Gabriel de Freitas Fernandes). Apresentação: **04/12/2026**.

Um crachá (Heltec LoRa 32 V3) registra o ponto pelo **botão** (LoRa → gateway). O mesmo clique também
**desbloqueia o PC** por Bluetooth (BLE), como um cartão passado no leitor: se o crachá estiver perto, o agente
Python libera a tela; depois, se o crachá se afasta, o PC bloqueia. Tudo que é ponto chega por MQTT ao Node-RED,
é gravado no PostgreSQL (Supabase) e aparece num painel de gerência.

```text
                  ┌─LoRa──► Gateway ──MQTT──► Node-RED ──► PostgreSQL (Supabase) ──► Painel   (registra o PONTO)
Crachá (botão) ───┤
                  └─BLE───► Agente Python (PC) ──► libera / bloqueia a tela                  (só controla o PC)
```

O PC **não registra ponto**: só libera e bloqueia. Detalhes e motivo da mudança em `docs/FLUXO_BLE_E_BLOQUEIO.md`.

## Onde ler
| Arquivo | Conteúdo |
| --- | --- |
| `PENDENCIAS.md` | **Comece por aqui:** estado atual, o que falta e divergências entre documentos |
| `CLAUDE.md` | Convenções do projeto e armadilhas já encontradas |
| `BANCO_DE_DADOS.md` | Especificação do banco (regras R1–R8, testes T1–T10) |
| `cloud/db/README.md` | Como criar e testar o banco |
| `cloud/servidor/README.md` | Como rodar broker MQTT e Node-RED |
| `docs/DIARIO_DE_BORDO.md` | Relato do projeto: linha do tempo, **problemas e soluções**, decisões, medições (base do relatório) |
| `docs/FLUXO_BLE_E_BLOQUEIO.md` | Como o botão desbloqueia o PC e como o PC bloqueia ao afastar (máquina de estados, anúncio BLE) |
| `diagrama/` | Diagrama de blocos e descrição dos blocos (imagens; **desenhado antes do fluxo novo do BLE**, o texto acima vale) |
| `Projeto_IoT_Cracha_Inteligente_de_Ponto.pdf` | Especificação e cronograma do projeto |

## Estrutura
```text
cloud/db/        schema, funções, views, usuários/RLS, seed e testes (SQL)
cloud/servidor/  broker MQTT (aedes), gerador do fluxo Node-RED, painel
diagrama/        diagrama de blocos
docs/            diário de bordo e fluxo do BLE/bloqueio
firmware/testes/ testes na protoboard: 01 OLED+LED, 02 ping-pong LoRa, 03 relógio DS1302, 05 BLE (contador de
                 cliques), 06 gateway MQTT, 07 crachá de teste pelo botão PRG
pc-agent/        agente_bloqueio.py (libera/bloqueia a tela) e teste_ble_rssi.py (só mede o RSSI)
```
Ainda por criar: `firmware/cracha`, `firmware/gateway`, `hardware/`, `enclosure/`.

## Segredos
`cloud/.env` **não** vai ao Git (modelo em `cloud/.env.example`). Futuramente, o mesmo vale para `config.h`.
