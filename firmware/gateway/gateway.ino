// GATEWAY DO CRACHA INTELIGENTE DE PONTO - firmware FINAL (Heltec LoRa 32 V3)
//
// Analogia: o gateway e o "carteiro" entre o radio (LoRa) e a internet (MQTT), nos dois sentidos.
//
//   SUBIDA (ponto):   cracha --LoRa--> [GATEWAY] --Wi-Fi/MQTT--> broker --> Node-RED --> Supabase --> painel
//   DESCIDA (comando): painel --> Node-RED --> broker --> [GATEWAY] --LoRa--> cracha (BIPE)
//
// O que ele faz:
//   1) Wi-Fi + MQTT com Last Will (se cair sem avisar, o broker publica "offline"; retained).
//   2) Hora da internet (NTP): carimba pontos sem relogio e devolve a hora certa no ACK.
//   3) Ponto (LoRa tipo 1) valido -> publica em unifesp/ponto/evento e responde ACK (tipo 2).
//      Se NAO conseguiu publicar, NAO responde ACK: o cracha guarda na fila e tenta de novo.
//   4) Comando BIPE vindo do painel (unifesp/ponto/cmd/{badge}) -> manda ao cracha por LoRa (tipo 3),
//      repete ate 6 vezes (3 s entre elas) e, quando o cracha confirma (tipo 4), publica o ACK em
//      unifesp/ponto/cmd/{badge}/ack. Sem confirmacao: publica ok=false (o painel mostra FALHOU).
//      Comando OTA: nao suportado por LoRa; responde ok=false na hora (plano B: gravar por USB).
//   5) Publica "estou vivo" (online + RSSI/SNR) a cada 30 s e a cada ponto.
//
// Pacote LoRa de 11 bytes (ver firmware/cracha/cracha.ino):
//   [0]=versao(1) [1]=tipo [2-3]=badge [4-5]=seq ou id do comando [6-9]=hora Unix UTC [10]=flags
//   1 = ponto   2 = ACK do ponto   3 = comando ([6]=1 BIPE)   4 = ACK do comando
//
// ANTES: copie config.example.h para config.h e preencha (Wi-Fi 2,4 GHz e IP do PC do broker).
// Bibliotecas (Gerenciar Bibliotecas): "PubSubClient" (Nick O'Leary), "RadioLib" 8.x, "U8g2".
// Placa: "WiFi LoRa 32(V3)". ATENCAO: NUNCA ligue a Heltec sem a antena conectada.

#include <WiFi.h>
#include <PubSubClient.h>
#include <RadioLib.h>
#include <U8g2lib.h>
#include <time.h>
#include "config.h"

// ---- LoRa (identico ao cracha) ----
const uint32_t FREQ_HZ = 915000000, LARGURA_HZ = 125000;
const int SF = 7, CR = 5, SYNC_WORD = 0x12, POTENCIA = 14;

const int PINO_VEXT = 36, PINO_LED = 35;
const int LORA_NSS = 8, LORA_SCK = 9, LORA_MOSI = 10, LORA_MISO = 11;
const int LORA_RST = 12, LORA_BUSY = 13, LORA_DIO1 = 14;

const char* TOPICO_EVENTO = "unifesp/ponto/evento";
const char* TOPICO_GW     = "unifesp/ponto/gateway/status";
const char* TOPICO_CMD    = "unifesp/ponto/cmd/+";           // so um nivel: nao pega ".../ack"

const unsigned long STATUS_MS = 30000;
const unsigned long CMD_INTERVALO_MS = 3000;
const int CMD_TENTATIVAS = 6;

SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
U8G2_SSD1306_128X64_NONAME_F_SW_I2C tela(U8G2_R0, 18, 17, 21);
WiFiClient rede;
PubSubClient mqtt(rede);

volatile bool chegouPacote = false;
void IRAM_ATTR aoChegarPacote() { chegouPacote = true; }

float ultimoRssi = 0, ultimoSnr = 0;
bool temQualidade = false;
String ultimoEvento = "(nenhum ainda)";
unsigned long ultimoStatus = 0, ultimoDesenho = 0, ultimaTentativa = 0;

// Comando pendente (um de cada vez; o painel so manda quando o usuario clica)
struct Comando { bool ativo; uint32_t id; uint16_t badge; int tentativas; unsigned long proximo; } cmd = { false, 0, 0, 0, 0 };

bool horaValida() { return time(nullptr) > 1700000000; }   // passou de nov/2023 = NTP ja acertou

void desenhar() {
  tela.clearBuffer();
  tela.setFont(u8g2_font_ncenB10_tr);
  tela.drawStr(0, 12, "GATEWAY");
  tela.setFont(u8g2_font_6x10_tr);
  String l2 = String("WiFi ") + (WiFi.status() == WL_CONNECTED ? "ok" : "--") +
              "  MQTT " + (mqtt.connected() ? "ok" : "--");
  tela.drawStr(0, 28, l2.c_str());
  tela.drawStr(0, 42, ultimoEvento.c_str());
  String l4 = temQualidade ? "RSSI " + String(ultimoRssi, 0) + " SNR " + String(ultimoSnr, 1) : "aguardando LoRa";
  tela.drawStr(0, 56, l4.c_str());
  tela.sendBuffer();
}

void publicarStatus() {
  char js[96];
  if (temQualidade)
    snprintf(js, sizeof(js), "{\"estado\":\"online\",\"rssi\":%.0f,\"snr\":%.1f}", ultimoRssi, ultimoSnr);
  else
    snprintf(js, sizeof(js), "{\"estado\":\"online\"}");
  mqtt.publish(TOPICO_GW, js, true);                        // true = retained (o ultimo valor fica guardado)
  ultimoStatus = millis();
}

void publicarAckComando(uint16_t badge, uint32_t id, bool ok) {
  char topico[48], js[64];
  snprintf(topico, sizeof(topico), "unifesp/ponto/cmd/%u/ack", badge);
  snprintf(js, sizeof(js), "{\"id\":%lu,\"ok\":%s}", (unsigned long)id, ok ? "true" : "false");
  mqtt.publish(topico, js);
  Serial.printf("ACK de comando -> %s %s\n", topico, js);
}

// Chega do broker: unifesp/ponto/cmd/101  {"id":12,"comando":"BIPE"}
// (so anotamos o comando; quem manda pelo radio e o loop, para nao travar o MQTT)
void aoReceberMqtt(char* topico, byte* dados, unsigned int n) {
  char corpo[200];
  if (n >= sizeof(corpo)) n = sizeof(corpo) - 1;
  memcpy(corpo, dados, n); corpo[n] = 0;

  unsigned badge = 0;
  const char* ult = strrchr(topico, '/');                   // ".../cmd/101" -> "101"
  if (ult) badge = atoi(ult + 1);
  const char* p = strstr(corpo, "\"id\"");
  uint32_t id = p ? strtoul(strchr(p + 4, ':') + 1, nullptr, 10) : 0;
  Serial.printf("MQTT cmd badge=%u: %s\n", badge, corpo);
  if (!badge || !id) return;

  if (strstr(corpo, "\"BIPE\"")) {
    cmd = { true, id, (uint16_t)badge, 0, 0 };
    ultimoEvento = "cmd BIPE #" + String(id);
  } else {                                                  // OTA ou desconhecido: nao ha como por LoRa
    publicarAckComando(badge, id, false);
    ultimoEvento = "cmd nao suportado";
  }
}

// Mantem Wi-Fi e MQTT de pe, sem travar o programa (tenta de novo a cada poucos segundos)
void garantirConexoes() {
  if (millis() - ultimaTentativa < 4000) return;
  if (WiFi.status() != WL_CONNECTED) { ultimaTentativa = millis(); WiFi.reconnect(); return; }
  if (!mqtt.connected()) {
    ultimaTentativa = millis();
    // Ultimo bloco = Last Will: mensagem que o BROKER publica se o gateway sumir sem se despedir
    if (mqtt.connect("gateway-gw-lab", nullptr, nullptr, TOPICO_GW, 1, true, "{\"estado\":\"offline\"}")) {
      mqtt.subscribe(TOPICO_CMD, 1);
      publicarStatus();
    }
  }
}

void montarPacote(uint8_t* b, uint8_t tipo, uint16_t badge, uint16_t seq, uint32_t hora, uint8_t flags) {
  b[0] = 1; b[1] = tipo;
  b[2] = badge & 0xFF; b[3] = badge >> 8;
  b[4] = seq & 0xFF;   b[5] = seq >> 8;
  for (int i = 0; i < 4; i++) b[6 + i] = (hora >> (8 * i)) & 0xFF;
  b[10] = flags;
}

void processarPacote() {
  size_t len = radio.getPacketLength();
  uint8_t b[32];
  if (len > sizeof(b)) len = sizeof(b);
  int estado = radio.readData(b, len);
  if (estado != RADIOLIB_ERR_NONE || len != 11 || b[0] != 1) {
    ultimoEvento = "pacote invalido";
    return;
  }
  uint16_t badge = b[2] | (b[3] << 8), seq = b[4] | (b[5] << 8);
  ultimoRssi = radio.getRSSI(); ultimoSnr = radio.getSNR(); temQualidade = true;

  // ---- ACK de comando (cracha -> gateway) ----
  if (b[1] == 4) {
    if (cmd.ativo && badge == cmd.badge && seq == (uint16_t)(cmd.id & 0xFFFF)) {
      publicarAckComando(cmd.badge, cmd.id, b[6] == 1);
      ultimoEvento = "cmd #" + String(cmd.id) + " confirmado";
      cmd.ativo = false;
    }
    return;
  }
  if (b[1] != 1) { ultimoEvento = "tipo desconhecido"; return; }

  // ---- ponto (cracha -> gateway) ----
  uint32_t horaCracha = b[6] | (b[7] << 8) | (b[8] << 16) | ((uint32_t)b[9] << 24);
  bool fila = b[10] & 1;

  if (!mqtt.connected() || !horaValida()) {                 // sem como publicar -> sem ACK (cracha tenta de novo)
    ultimoEvento = "sem MQTT/hora: sem ACK";
    return;
  }
  time_t quando = horaCracha > 1700000000 ? horaCracha : time(nullptr);  // usa a hora do cracha se tiver
  char iso[24];
  struct tm tmv; gmtime_r(&quando, &tmv);
  strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &tmv);

  char js[200];
  snprintf(js, sizeof(js),
    "{\"ts\":\"%s\",\"badge\":%u,\"origem\":\"botao\",\"seq\":%u,\"fila\":%s,\"rssi_lora\":%.0f,\"snr\":%.1f}",
    iso, badge, seq, fila ? "true" : "false", ultimoRssi, ultimoSnr);

  if (mqtt.publish(TOPICO_EVENTO, js)) {
    Serial.println(js);
    ultimoEvento = "cracha " + String(badge) + " seq " + String(seq) + " OK";
    uint8_t ack[11];
    montarPacote(ack, 2, badge, seq, time(nullptr), 0);
    delay(100);                                             // da tempo do cracha voltar a escutar
    radio.transmit(ack, 11);
    publicarStatus();
  } else {
    ultimoEvento = "falha ao publicar";
  }
}

// Comando pendente: manda por LoRa a cada 3 s; desiste depois de 6 tentativas
void atenderComando() {
  if (!cmd.ativo || millis() < cmd.proximo) return;
  if (cmd.tentativas >= CMD_TENTATIVAS) {
    publicarAckComando(cmd.badge, cmd.id, false);
    ultimoEvento = "cmd #" + String(cmd.id) + " sem resposta";
    cmd.ativo = false;
    return;
  }
  uint8_t pacote[11];
  montarPacote(pacote, 3, cmd.badge, (uint16_t)(cmd.id & 0xFFFF), 0, 0);
  pacote[6] = 1;                                            // codigo 1 = BIPE
  digitalWrite(PINO_LED, HIGH);
  radio.transmit(pacote, 11);
  digitalWrite(PINO_LED, LOW);
  radio.startReceive();
  cmd.tentativas++;
  cmd.proximo = millis() + CMD_INTERVALO_MS;
  ultimoEvento = "BIPE #" + String(cmd.id) + " (" + String(cmd.tentativas) + "/" + String(CMD_TENTATIVAS) + ")";
}

void setup() {
  Serial.begin(115200);
  pinMode(PINO_VEXT, OUTPUT); digitalWrite(PINO_VEXT, LOW); delay(100);
  pinMode(PINO_LED, OUTPUT);
  tela.begin();
  ultimoEvento = "iniciando...";
  desenhar();

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  radio.tcxoVoltage = RadioLibTCXOVoltage_t::Voltage1V8;    // TCXO da Heltec V3
  ConfigLoRa_t cfg;
  cfg.frequency = FREQ_HZ; cfg.bandwidth = LARGURA_HZ; cfg.spreadingFactor = SF;
  cfg.codingRate = CR; cfg.syncWord = SYNC_WORD; cfg.power = POTENCIA; cfg.preambleLength = 8;
  int estado = radio.begin(cfg);
  if (estado != RADIOLIB_ERR_NONE) {
    ultimoEvento = "LoRa erro " + String(estado);
    desenhar();
    while (true) delay(1000);
  }
  radio.setDio1Action(aoChegarPacote);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_SENHA);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(250);
  Serial.printf("Wi-Fi: %s  IP %s\n", WiFi.status() == WL_CONNECTED ? "conectado" : "FALHOU",
                WiFi.localIP().toString().c_str());

  configTime(0, 0, "pool.ntp.org", "time.google.com");      // hora da internet (UTC)
  mqtt.setServer(MQTT_HOST, MQTT_PORTA);
  mqtt.setBufferSize(512);
  mqtt.setCallback(aoReceberMqtt);
  garantirConexoes();

  radio.startReceive();
  ultimoEvento = "escutando LoRa";
  desenhar();
}

void loop() {
  garantirConexoes();
  mqtt.loop();

  if (chegouPacote) {
    chegouPacote = false;
    digitalWrite(PINO_LED, HIGH);
    processarPacote();
    digitalWrite(PINO_LED, LOW);
    radio.startReceive();
    desenhar();
  }
  atenderComando();
  if (mqtt.connected() && millis() - ultimoStatus > STATUS_MS) publicarStatus();   // "estou vivo"
  if (millis() - ultimoDesenho > 1000) { ultimoDesenho = millis(); desenhar(); }
}
