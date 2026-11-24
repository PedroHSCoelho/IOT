# Roteiro final: montar, gravar, testar e entregar (apresentação 04/12/2026)

Cada fase só começa quando a anterior passa. Ao fim de cada uma: `git pull`, commit, `git push` e registro no `docs/DIARIO_DE_BORDO.md` (problemas incluídos).

## Arranjo final
- **Gateway + recipiente (Heltec 2):** botão (GPIO 4 ↔ GND), buzzer (GPIO 5), DS1302 (CLK 40, DAT 41, RST 42), Wi-Fi/MQTT, LoRa, BLE `UNIFESP-HOLDER` nº 201. Firmware: `firmware/gateway/gateway.ino`.
- **Crachá (Heltec 1):** buzzer (GPIO 5), LoRa (escuta), BLE `UNIFESP-BADGE` nº 101. Firmware: `firmware/cracha/cracha.ino`.
- **PC:** `pc-agent/agente_bloqueio.py` (libera/bloqueia a tela; **não registra ponto**). **Servidor:** broker + Node-RED + Supabase.
- Caminho do ponto: botão → gateway → Wi-Fi/MQTT → Node-RED → Supabase → painel. LoRa leva só o aviso "ponto OK" e o BIPE ao crachá.

## 1. Preparar o PC do Flávio (uma vez)
Arduino IDE com a placa **Heltec ESP32** (URL em Preferências: `https://resource.heltec.cn/download/package_heltec_esp32_index.json`); bibliotecas **U8g2**, **RadioLib 8.x**, **PubSubClient**. Cabo USB-C de dados. Para gravar à força: segure PRG, aperte RST, solte PRG.

## 2. Servidor
1. `cloud/.env` copiado por pendrive (nunca por Git/chat/e-mail). Em `cloud/servidor`: `npm install`.
2. Hotspot do celular em **2,4 GHz**; anotar o IPv4 do PC (`ipconfig`); liberar a porta **1883** no firewall.
3. `npm run broker` (terminal 1) e `npm run nodered` (terminal 2); abrir `http://127.0.0.1:1880/dashboard`. Reativar o Supabase se pausado.
4. Teste sem placas: `npm run publicar-teste -- botao 102`; o ponto aparece no painel. Apagar depois (`DELETE FROM evento WHERE seq > 0 ...`).

## 3. Montagem (placas desligadas, antenas conectadas)
Gateway: botão GPIO 4 ↔ GND; buzzer no GPIO 5 (ativo: 1 kΩ → base de NPN, buzzer entre 3,3 V e coletor; passivo: `BUZZER_PASSIVO = true`); DS1302 em 3V3/GND/40/41/42 (nunca 5 V) com a CR2032. Crachá: só o buzzer.

## 4. Gravar
1. **Gateway:** em `firmware/gateway`, copiar `config.example.h` para `config.h` (SSID/senha 2,4 GHz e IP do broker). Gravar `gateway.ino`. OLED `W:ok M:ok`; painel "Gateway: online".
2. **Crachá:** gravar `cracha.ino` (sem `config.h`). OLED "CRACHA 101".
3. Sem DS1302 ligado, `TEM_DS1302 = false` no gateway (o NTP carimba).

## 5. Testes ponta a ponta
1. Apertar o botão: 1 bipe → "PONTO OK" → 2 bipes no gateway **e** no crachá → ponto no painel (origem "Botão"); cliques alternam CHEGADA/SAÍDA.
2. **Fila:** desligar o hotspot/broker, apertar (3 bipes graves, "SEM REDE: na fila"), religar: em até 10 s o ponto chega com a marca de fila, sem perda nem duplicata.
3. **BIPE:** botão no painel; o crachá faz 1 bipe longo e o comando fica `CONFIRMADO`.
4. **Relógio:** o DS1302 é acertado pelo NTP; conferir o erro (< 2 s).
5. **Agente do PC:** `pip install bleak paho-mqtt`; `python pc-agent/agente_bloqueio.py --mqtt <IP do broker>` (use `--iniciar-liberado` para desenvolver; **ESC** sai). Chegar perto desbloqueia; o 1º clique prende (FIXO), o 2º volta ao automático; afastar avisa e bloqueia. Calibrar com o crachá no corpo (`--liberar`, `--manter`, `--janela`).
6. Se o recipiente aparecer "sem sinal" no agente: Wi-Fi e BLE dividem a antena do gateway; registrar no diário.

## 6. Metas a medir (tabela no diário)
Botão→bipe ≤ 2 s · botão→desbloqueio ≤ 2 s · afastar→bloqueio ≤ 10 s · evento→painel ≤ 5 s · alcance LoRa (distância e RSSI) · 0 perdas / 0 duplicatas · erro do relógio < 2 s.

## 7. Entrega
Avisar o professor da mudança de requisito (botão no recipiente; PC não registra ponto) · PCI no KiCad (gateway: botão+buzzer+DS1302; crachá: buzzer) com esquemático, layout, 3D, Gerbers, lista de componentes e registro de erros · caixas 3D (opcional) · divisão de tarefas A/B/C/D · vídeos, prints do painel e do OLED, print do T10 sem senha · de 01 a 03/12: reativar o Supabase, exportar o histórico (CSV), limpar dados de teste, ensaiar com o hotspot de 2,4 GHz e a explicação do banco. Dizer na apresentação que o bloqueio do PC é de demonstração, não de segurança.
