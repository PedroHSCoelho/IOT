# Firmware do gateway (Heltec LoRa 32 V3) — versão FINAL

Arquivo: `gateway.ino`. Copie `config.example.h` para `config.h` (Wi-Fi **2,4 GHz** e IP do PC do broker; **não vai ao Git**).
Bibliotecas: **PubSubClient**, **RadioLib 8.x**, **U8g2**. **Nunca ligar sem a antena.**

Subida: LoRa (ponto) → valida → publica `unifesp/ponto/evento` (`origem:"botao"`) → só então responde ACK com a hora NTP.
Descida: assina `unifesp/ponto/cmd/+`; **BIPE** vai ao crachá por LoRa (até 6 tentativas, 3 s) e o ACK volta em
`unifesp/ponto/cmd/{badge}/ack`; **OTA** responde `ok:false` (não há OTA por LoRa; gravar por USB).
Last Will `{"estado":"offline"}` (retained) e "estou vivo" com RSSI/SNR a cada 30 s em `unifesp/ponto/gateway/status`.
