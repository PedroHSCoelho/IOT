# Firmware do crachá (Heltec LoRa 32 V3) — versão FINAL

Arquivo: `cracha.ino` (abrir a pasta `firmware/cracha` no Arduino IDE). Placa: *WiFi LoRa 32(V3)*.
Bibliotecas: **RadioLib 8.x** e **U8g2** (BLE e Preferences já vêm com a placa). **Nunca ligar sem a antena.**

Junta os testes 01–07: botão (GPIO 4 ou PRG, debounce 50 ms) → hora do DS1302 → LoRa com ACK (2 s, 3 tentativas)
→ **fila persistente** reenviada a cada 30 s → BLE com contador de cliques (`UNIFESP-BADGE`, nº 101) → buzzer (GPIO 5),
LED e OLED. Aceita o comando **BIPE** do painel (via gateway). O DS1302 é acertado sozinho pela hora do gateway (NTP)
quando difere mais de 2 s.

Antes de gravar, conferir no topo do arquivo: `ID_CRACHA` (101 demo, 102 teste), `BUZZER_PASSIVO` (ativo = `false`),
`TEM_DS1302` (`false` se o relógio não estiver ligado: a hora vai 0 e o gateway carimba).

Sons: 1 bipe curto = clique · 2 bipes = ponto confirmado · 3 graves = sem ACK (ficou na fila) · 1 longo = comando BIPE.
Pacote LoRa e tipos (1 ponto, 2 ACK, 3 comando, 4 ACK de comando): cabeçalho do `.ino`. Pinagem: `hardware/PINAGEM.md`.
Limites conhecidos: durante os até 6 s de envio o botão não é lido; OTA por LoRa não existe (plano B: gravar por USB).
