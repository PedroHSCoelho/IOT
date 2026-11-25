# Firmware do crachá (Heltec LoRa 32 V3) — versão FINAL

Arquivo: `cracha.ino` (abrir a pasta `firmware/cracha` no Arduino IDE). Placa: *WiFi LoRa 32(V3)*.
Bibliotecas: **RadioLib 8.x** e **U8g2** (BLE e Preferences já vêm com a placa). Sem `config.h`. **Nunca ligar sem a antena.**

O crachá simula a pessoa **num local remoto, sem internet**: o botão (**PRG da placa** e/ou botão externo no GPIO 4) bate o ponto por **LoRa**;
o gateway (que tem Wi-Fi) leva ao banco.
- **Clique:** 1 bipe → ponto na **fila** (memória, sobrevive a desligar) → LoRa com ACK (2 s, 3 tentativas). ACK: **2 bipes**. Sem ACK: **3 bipes graves**, o ponto fica na fila e é reenviado a cada 30 s (`fila=true`).
- **Hora:** DS1302 (40/41/42) para valer a hora do clique; o ACK do gateway acerta o relógio (< 2 s). Sem o módulo, a hora vai 0 e o gateway carimba na chegada.
- **BLE:** anuncia `UNIFESP-BADGE` nº 101 (o PC mede a distância). Este botão **não** mexe no PC; quem libera/prende o PC é o botão do gateway.
- **Avisos do gateway:** BIPE do painel (1 longo, com ACK) e "PONTO OK" do botão do gateway (2 bipes).
- `seq` do crachá: 1 a 32767 (o gateway usa 32768+ para o botão dele; o banco recusa `seq` repetido).

Conferir no topo: `ID_CRACHA` (101; 102 para teste), `BUZZER_PASSIVO` (ativo = `false`), `TEM_DS1302`. Pacote LoRa: cabeçalho do `.ino`.
