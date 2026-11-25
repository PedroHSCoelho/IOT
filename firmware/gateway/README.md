# Firmware do gateway + recipiente (Heltec LoRa 32 V3) — versão FINAL

Arquivo: `gateway.ino`. Copie `config.example.h` para `config.h` (Wi-Fi **2,4 GHz** e IP do PC do broker; **não vai ao Git**).
Bibliotecas: **PubSubClient**, **RadioLib 8.x**, **U8g2**. **Nunca ligar sem a antena.**

Esta placa é o **recipiente** (porta-cracha) **e** o gateway; tem o **botão** (GPIO 4, externo), o **buzzer** (GPIO 5) e o **DS1302** (40/41/42).
Dois caminhos de ponto, mesmo badge 101:
- **Botão do gateway (local, com rede):** bipe → contador de cliques do BLE (`UNIFESP-HOLDER` nº 201; o PC libera/prende) → ponto na **fila** (NVS) →
  `unifesp/ponto/evento` (`origem:"botao"`, `seq` ≥ 32768). OK: 2 bipes aqui e o crachá apita por LoRa. Sem rede: 3 graves; reenvio a cada 10 s (`fila=true`).
- **Ponto remoto (botão do crachá, sem internet no local):** o crachá manda por LoRa (tipo 1, `seq` 1–32767); o gateway publica, responde ACK com a hora certa e bipa. Sem MQTT/hora, não dá ACK e o crachá refaz.

Também: Last Will `{"estado":"offline"}` (retained) e "estou vivo" a cada 30 s em `unifesp/ponto/gateway/status`; BIPE do painel (`unifesp/ponto/cmd/{badge}`) vai ao crachá por LoRa e o ACK volta em `.../cmd/{badge}/ack`; OTA responde `ok:false` (gravar por USB).
Sync word LoRa próprio `0x2B` (igual no crachá).
