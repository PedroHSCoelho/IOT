// Teste 6 - GATEWAY: recebe o pacote LoRa do cracha e publica no MQTT (que leva ao banco)
//
// Caminho completo (analogia: o gateway e o "carteiro" entre o radio e a internet):
//   cracha --LoRa--> [ESTE GATEWAY] --Wi-Fi/MQTT--> broker --> Node-RED --> Supabase --> painel
//
// O que ele faz:
//   1) Conecta no Wi-Fi e no broker MQTT. Se cair sem avisar, o broker publica o "Last Will" (offline).
//   2) Pega a hora na internet (NTP) para carimbar os pontos e responder o ACK.
//   3) Escuta LoRa. Chegou pacote valido de 11 bytes? Publica em "unifesp/ponto/evento" e responde ACK.
//      Se NAO conseguiu publicar, NAO responde ACK: assim o cracha sabe que deve tentar de novo.
//
// Pacote LoRa de 11 bytes: [0]=versao(1) [1]=tipo (1=ponto, 2=ACK) [2-3]=badge [4-5]=seq
//                          [6-9]=hora Unix [10]=flags (bit0 = veio da fila/reenvio)
//
// ANTES: copie config.example.h para config.h e preencha (Wi-Fi 2,4 GHz e IP do broker).
// Bibliotecas (Gerenciar Bibliotecas): "PubSubClient" (Nick O'Leary), "RadioLib" 8.x, "U8g2".
// ATENCAO: NUNCA ligue a Heltec sem a antena conectada.

#include <WiFi.h>
#include <PubSubClient.h>
#include <RadioLib.h>
#include <U8g2lib.h>
#include <time.h>
#include "config.h"

// ---- LoRa (identico ao teste 2 e ao cracha) ----
const uint32_t FREQ_HZ = 915000000, LARGURA_HZ = 125000;
const int SF = 7, CR = 5, SYNC_WORD = 0x12, POTENCIA = 14;

const int PINO_VEXT = 36, PINO_LED = 35;
const int LORA_NSS = 8, LORA_SCK = 9, LORA_MOSI = 10, LORA_MISO = 11;
const int LORA_RST = 12, LORA_BUSY = 13, LORA_DIO1 = 14;

const char* TOPICO_EVENTO = "unifesp/ponto/evento";
const char* TOPICO_GW     = "unifesp/ponto/gateway/status";

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

// Mantem Wi-Fi e MQTT de pe, sem travar o programa (tenta de novo a cada poucos segundos)
void garantirConexoes() {
  if (millis() - ultimaTentativa < 4000) return;
  if (WiFi.status() != WL_CONNECTED) { ultimaTentativa = millis(); WiFi.reconnect(); return; }
  if (!mqtt.connected()) {
    ultimaTentativa = millis();
    // Ultimo argumento = Last Will: mensagem que o BROKER publica se o gateway sumir sem se despedir
    if (mqtt.connect("gateway-gw-lab", nullptr, nullptr, TOPICO_GW, 1, true, "{\"estado\":\"offline\"}"))
      publicarStatus();
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
  if (estado != RADIOLIB_ERR_NONE || len != 11 || b[0] != 1 || b[1] != 1) {
    ultimoEvento = "pacote invalido";
    return;
  }
  uint16_t badge = b[2] | (b[3] << 8), seq = b[4] | (b[5] << 8);
  uint32_t horaCracha = b[6] | (b[7] << 8) | (b[8] << 16) | ((uint32_t)b[9] << 24);
  bool fila = b[10] & 1;
  ultimoRssi = radio.getRSSI(); ultimoSnr = radio.getSNR(); temQualidade = true;

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
  if (mqtt.connected() && millis() - ultimoStatus > 30000) publicarStatus();   // "estou vivo" a cada 30 s
  if (millis() - ultimoDesenho > 1000) { ultimoDesenho = millis(); desenhar(); }
}
