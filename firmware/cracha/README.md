# Firmware do crachá (Heltec LoRa 32 V3)

Pasta reservada para o firmware **final** do crachá (botão com debounce de 50 ms, DS1302, LoRa com ACK, fila e reenvio,
BLE com contador de cliques, OLED, LED e buzzer). Hoje o que existe está em `firmware/testes/` (testes 01–07);
o código final reaproveita esses testes. Pinagem: `hardware/PINAGEM.md`. Nunca commitar `config.h`.
