# Diagnóstico: o ponto remoto (PRG → LoRa → gateway → MQTT → banco) não chega

Regrave **as duas placas** com a versão `diag-26/11-b` (aparece no OLED do crachá na partida e na 1ª linha do Serial de cada placa).
Abra **dois** Monitores Serial (115200): um do crachá, um do gateway. Aperte PRG **uma vez** e siga as etapas. A primeira que falhar é o defeito.

| Etapa | Onde aparece | Mensagem OK | Se NÃO aparece, o defeito é... |
|---|---|---|---|
| 0 | Serial do crachá | `=== CRACHA versao diag-26/11-b ===` e, a cada 5 s, `[vivo] PRG=1 ...` | firmware antigo gravado (ou não gravou) ou travado |
| 0b | Serial do crachá | ao segurar o PRG, `[vivo] PRG=0` e o OLED mostra "PRG" no canto | botão/pino (PRG = GPIO 0) |
| 1 | crachá | `[ETAPA 1] CLIQUE detectado` | leitura do botão |
| 2 | crachá | `[ETAPA 2] ponto gravado na fila` | memória (NVS) |
| 3 | crachá | `[ETAPA 3] transmit ... codigo 0` | rádio do crachá (código ≠ 0 = erro do SX1262; antena?) |
| A | gateway | `[ETAPA A] LoRa recebido: tipo=1 badge=101 ...` | **o ar**: antena, sync word 0x2B nos dois, gateway regravado, distância muito curta (<30 cm pode saturar) |
| B | gateway | `[ETAPA B] publicando (mqtt=conectado, hora valida=sim)` | Wi-Fi/MQTT: ver `W:` e `M:` no OLED; "DESCONECTADO" = broker desligado ou IP errado em `config.h`; "NAO (NTP)" = sem internet no hotspot |
| C | gateway | `[ETAPA C] MQTT aceitou` e depois a linha JSON | falha ao publicar |
| 4 | crachá | `[ETAPA 4] ACK do gateway recebido` | ACK não volta (gateway não transmitiu) |
| D | terminal do Node-RED / painel | ponto novo (origem Botão) | fluxo ou banco (ver abaixo) |

O OLED do crachá mostra na última linha `f:fila s:seq tx:codigo rx:pacotes ouvidos`; o do gateway, `rx N` (pacotes LoRa ouvidos).
`tx:0` = o rádio mandou. Se `rx` do gateway não sobe quando você aperta, o pacote não chegou (etapa A).

## Já verificado (26/11/2026, notebook do Pedro)
Lado nuvem OK: broker + Node-RED (fluxo regenerado com o ramo `evento_pc`) + banco gravaram um ponto simulado (`npm run publicar-teste -- botao 102`) sem erro. Portanto o defeito está nas placas/no ar/no Wi-Fi do gateway, não no servidor.
Atenção: o `seq` 1–9 do badge 101/`botao` pode já existir no banco (pontos antigos do gateway): o banco recusa **em silêncio** (0 duplicatas). Se tudo chegar mas não aparecer, apague os pontos de teste do badge 101 antes.
