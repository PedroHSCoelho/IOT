# Firmware do gateway + recipiente (Heltec LoRa 32 V3) — versão FINAL

Arquivo: `gateway.ino`. Copie `config.example.h` para `config.h` (Wi-Fi **2,4 GHz** e IP do PC do broker; **não vai ao Git**).
Bibliotecas: **PubSubClient**, **RadioLib 8.x**, **U8g2**. **Nunca ligar sem a antena.**

Esta placa é o **recipiente** (porta-cracha) **e** o gateway; ela tem o **botão** (GPIO 4 ou PRG), o **buzzer** (GPIO 5) e o **DS1302** (40/41/42).
Um clique: bipe curto → contador de cliques do BLE (`UNIFESP-HOLDER` nº 201; o PC libera/prende) → ponto do badge 101 na **fila** (NVS) →
publica em `unifesp/ponto/evento` (`origem:"botao"`). Deu certo: 2 bipes aqui e o **crachá apita por LoRa**. Sem rede: 3 bipes graves,
o ponto fica na fila e é reenviado a cada 10 s (`fila=true`). Hora do ponto: DS1302 → NTP; o DS1302 é acertado pelo NTP.

Também: Last Will `{"estado":"offline"}` (retained) e "estou vivo" a cada 30 s em `unifesp/ponto/gateway/status`;
comando BIPE do painel (`unifesp/ponto/cmd/{badge}`) vai ao crachá por LoRa (6 tentativas) e o ACK volta em `.../cmd/{badge}/ack`;
OTA responde `ok:false` (gravar por USB). Ainda aceita o pacote de ponto do crachá de teste (`firmware/testes/07`).
