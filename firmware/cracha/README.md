# Firmware do crachá (Heltec LoRa 32 V3) — versão FINAL

Arquivo: `cracha.ino` (abrir a pasta `firmware/cracha` no Arduino IDE). Placa: *WiFi LoRa 32(V3)*.
Bibliotecas: **RadioLib 8.x** e **U8g2** (BLE já vem com a placa). **Nunca ligar sem a antena.**

**O botão NÃO fica no crachá** (decisão de 24/11/2026): fica no gateway+recipiente (`firmware/gateway`). O crachá faz três coisas:
1. **BLE:** anuncia `UNIFESP-BADGE` nº 101; o agente do PC mede o RSSI (distância) para desbloquear/bloquear.
2. **LoRa (só escuta):** quando o gateway grava um ponto, o crachá faz **2 bipes**; o comando BIPE do painel faz **1 bipe longo** e o crachá responde com ACK.
3. **OLED/LED:** último aviso, RSSI do gateway e há quanto tempo.

Hardware: só a Heltec + buzzer (GPIO 5). Conferir no topo: `ID_CRACHA` (101) e `BUZZER_PASSIVO` (ativo = `false`).
Pacote LoRa e tipos (3 = comando, 4 = ACK do comando): cabeçalho do `.ino`. Pinagem: `hardware/PINAGEM.md`.
