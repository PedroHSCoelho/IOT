// GATEWAY + RECIPIENTE DO CRACHA INTELIGENTE DE PONTO - firmware FINAL (Heltec LoRa 32 V3)
//
// Arranjo final (decisao de 24/11/2026): esta placa e o "recipiente" (porta-cracha) E o gateway.
// Ela tem o BOTAO. O cracha (outra Heltec, firmware/cracha) so anuncia o sinal BLE e apita.
//
// Analogia: o recipiente e o "leitor de ponto" da parede. Voce aperta o botao dele; ele anota o ponto
// no caderno (fila), entrega ao servidor (MQTT) e faz o cracha apitar (LoRa) confirmando.
//
// O que acontece num CLIQUE:
//   1) Bipe curto + contador de cliques do BLE sobe (o agente do PC percebe e libera/prende o PC).
//   2) Ponto (badge 101, origem "botao") com a hora do DS1302/NTP -> fila na memoria (NVS).
//   3) Tenta publicar em unifesp/ponto/evento. Deu certo: 2 bipes aqui, e o CRACHA apita por LoRa.
//      Nao deu (sem Wi-Fi/MQTT): 3 bipes graves, o ponto fica na fila e e reenviado a cada 10 s.
//
// Outras funcoes (mantidas do gateway):
//   - Last Will (offline, retained) e "estou vivo" a cada 30 s em unifesp/ponto/gateway/status.
//   - Comando BIPE do painel (unifesp/ponto/cmd/{badge}) -> LoRa -> cracha; ACK em .../cmd/{badge}/ack.
//     OTA nao existe por LoRa: responde ok=false (plano B: USB).
//   - Pacote LoRa de ponto (tipo 1) do cracha de TESTE antigo (firmware/testes/07) ainda e aceito.
//
// Pacote LoRa de 11 bytes: [0]=versao(1) [1]=tipo [2-3]=badge [4-5]=seq ou id [6-9]=hora Unix UTC [10]=flags
//   1 = ponto (cracha de teste)  2 = ACK do ponto  3 = comando ([6]=1 BIPE longo, 2 = PONTO OK)  4 = ACK do comando
//
// ANTES: copie config.example.h para config.h (Wi-Fi 2,4 GHz e IP do PC do broker; nao vai ao Git).
// Bibliotecas: "PubSubClient", "RadioLib" 8.x, "U8g2". (BLE, WiFi e Preferences vem com a placa.)
// Placa: "WiFi LoRa 32(V3)". NUNCA ligue a Heltec sem a antena conectada.

#include <WiFi.h>
#include <PubSubClient.h>
#include <RadioLib.h>
#include <U8g2lib.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <time.h>
#include "config.h"

// ============================ CONFIGURACAO ============================
const uint16_t BADGE_PONTO = 101;          // de quem e o ponto (o cracha da demo)
const uint16_t ID_RECIPIENTE = 201;        // numero no anuncio BLE (o agente usa --recipiente 201)
const char* NOME_BLE = "UNIFESP-HOLDER";   // o agente do PC procura por este nome
const bool BUZZER_PASSIVO = false;         // false = buzzer ATIVO (liga/desliga); true = passivo
const bool TEM_DS1302 = true;              // false = sem relogio: a hora do ponto vem do NTP

// LoRa (identico ao cracha)
const uint32_t FREQ_HZ = 915000000, LARGURA_HZ = 125000;
const int SF = 7, CR = 5, SYNC_WORD = 0x12, POTENCIA = 14;

// Pinos
const int PINO_BOTAO = 4;                  // um lado no GPIO 4, o outro no GND
const int PINO_PRG = 0;                    // PRG da placa tambem vale como clique
const int PINO_BUZZER = 5;
const int PINO_VEXT = 36, PINO_LED = 35;
const int PINO_CLK = 40, PINO_DAT = 41, PINO_RST_RTC = 42;
const int LORA_NSS = 8, LORA_SCK = 9, LORA_MOSI = 10, LORA_MISO = 11;
const int LORA_RST = 12, LORA_BUSY = 13, LORA_DIO1 = 14;

const char* TOPICO_EVENTO = "unifesp/ponto/evento";
const char* TOPICO_GW     = "unifesp/ponto/gateway/status";
const char* TOPICO_CMD    = "unifesp/ponto/cmd/+";           // so um nivel: nao pega ".../ack"

const unsigned long STATUS_MS = 30000;
const unsigned long REENVIO_MS = 10000;          // fila: tenta de novo a cada 10 s
const unsigned long CMD_INTERVALO_MS = 3000;
const int CMD_TENTATIVAS = 6;
const uint16_t INTERVALO_LENTO = 800;            // BLE 500 ms
const uint16_t INTERVALO_RAPIDO = 160;           // BLE 100 ms apos um clique
const unsigned long DURACAO_RAPIDO_MS = 5000;
const int FUSO_HORAS = -3;                       // so para MOSTRAR (o banco usa UTC)
const int TAM_FILA = 16;
// =====================================================================

SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
U8G2_SSD1306_128X64_NONAME_F_SW_I2C tela(U8G2_R0, 18, 17, 21);
WiFiClient rede;
PubSubClient mqtt(rede);
Preferences memoria;

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
typedef String TextoBle;
#else
typedef std::string TextoBle;
#endif

volatile bool chegouPacote = false;
void IRAM_ATTR aoChegarPacote() { chegouPacote = true; }

struct Ponto { uint16_t seq; uint32_t hora; uint8_t reenvio; };   // reenvio=1 quando ja falhou antes
Ponto fila[TAM_FILA];
int nFila = 0;
uint16_t seqAtual = 0;
uint8_t idBoot;
uint16_t cliques = 0;
bool bleRapido = false;

float ultimoRssi = 0, ultimoSnr = 0;
bool temQualidade = false;
String ultimoEvento = "(nenhum ainda)";
unsigned long fimRapido = 0, ultimoStatus = 0, ultimoDesenho = 0, ultimaTentativa = 0, ultimoReenvio = 0, ultimoSyncRtc = 0;

struct Comando { bool ativo; uint32_t id; uint16_t badge; int tentativas; unsigned long proximo; } cmd = { false, 0, 0, 0, 0 };

bool horaValida() { return time(nullptr) > 1700000000; }   // NTP ja acertou (passou de nov/2023)

// ====================================================================
// Buzzer
// ====================================================================
void tom(int freq, int ms) {
  if (!BUZZER_PASSIVO) { digitalWrite(PINO_BUZZER, HIGH); delay(ms); digitalWrite(PINO_BUZZER, LOW); return; }
  unsigned long meio = 500000UL / freq, fim = millis() + ms;
  while (millis() < fim) {
    digitalWrite(PINO_BUZZER, HIGH); delayMicroseconds(meio);
    digitalWrite(PINO_BUZZER, LOW);  delayMicroseconds(meio);
  }
}
void bipeClique() { tom(2000, 60); }
void bipeOk()     { tom(2400, 80); delay(70); tom(2400, 80); }
void bipeFalha()  { for (int i = 0; i < 3; i++) { tom(600, 120); delay(80); } }

// ====================================================================
// Relogio DS1302 (mesmo codigo do teste 3). Guarda a hora em UTC.
// ====================================================================
void enviarByte(uint8_t b) {
  pinMode(PINO_DAT, OUTPUT);
  for (int i = 0; i < 8; i++) {
    digitalWrite(PINO_DAT, (b >> i) & 1);
    digitalWrite(PINO_CLK, HIGH); delayMicroseconds(2);
    digitalWrite(PINO_CLK, LOW);  delayMicroseconds(2);
  }
}
uint8_t receberByte() {
  pinMode(PINO_DAT, INPUT);
  uint8_t v = 0;
  for (int i = 0; i < 8; i++) {
    if (digitalRead(PINO_DAT)) v |= (1 << i);
    digitalWrite(PINO_CLK, HIGH); delayMicroseconds(2);
    digitalWrite(PINO_CLK, LOW);  delayMicroseconds(2);
  }
  return v;
}
uint8_t lerRegistro(uint8_t reg) {
  digitalWrite(PINO_RST_RTC, HIGH); delayMicroseconds(4);
  enviarByte(0x81 | (reg << 1));
  uint8_t v = receberByte();
  digitalWrite(PINO_RST_RTC, LOW); delayMicroseconds(4);
  return v;
}
void escreverRegistro(uint8_t reg, uint8_t valor) {
  digitalWrite(PINO_RST_RTC, HIGH); delayMicroseconds(4);
  enviarByte(0x80 | (reg << 1));
  enviarByte(valor);
  digitalWrite(PINO_RST_RTC, LOW); delayMicroseconds(4);
}
uint8_t deBCD(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
uint8_t paraBCD(uint8_t n) { return ((n / 10) << 4) | (n % 10); }

// Data <-> dias desde 1970 (algoritmo de Howard Hinnant; nao depende do fuso do sistema)
int64_t diasDesdeCivil(int a, int m, int d) {
  a -= m <= 2;
  int64_t era = (a >= 0 ? a : a - 399) / 400;
  unsigned yoe = (unsigned)(a - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}
void civilDeDias(int64_t z, int &a, int &m, int &d) {
  z += 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = (unsigned)(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t y = (int64_t)yoe + era * 400;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  a = (int)(y + (m <= 2));
}

uint32_t lerEpochRtc() {                              // 0 = relogio parado/invalido
  if (!TEM_DS1302) return 0;
  uint8_t seg = lerRegistro(0);
  if (seg & 0x80) return 0;
  int s = deBCD(seg & 0x7F), mi = deBCD(lerRegistro(1) & 0x7F), h = deBCD(lerRegistro(2) & 0x3F);
  int d = deBCD(lerRegistro(3) & 0x3F), mo = deBCD(lerRegistro(4) & 0x1F), a = 2000 + deBCD(lerRegistro(6));
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 59 || a < 2024) return 0;
  return (uint32_t)(diasDesdeCivil(a, mo, d) * 86400LL + h * 3600 + mi * 60 + s);
}
void acertarRtc(uint32_t epoch) {
  if (!TEM_DS1302) return;
  int a, mo, d;
  civilDeDias(epoch / 86400, a, mo, d);
  uint32_t resto = epoch % 86400;
  escreverRegistro(7, 0x00);
  escreverRegistro(0, 0x80);
  escreverRegistro(1, paraBCD((resto % 3600) / 60));
  escreverRegistro(2, paraBCD(resto / 3600));
  escreverRegistro(3, paraBCD(d));
  escreverRegistro(4, paraBCD(mo));
  escreverRegistro(5, 1);
  escreverRegistro(6, paraBCD(a - 2000));
  escreverRegistro(0, paraBCD(resto % 60));
}
// NTP e a referencia: se o DS1302 estiver parado ou errado em mais de 2 s, acerta
void sincronizarRelogio() {
  ultimoSyncRtc = millis();
  if (!TEM_DS1302 || !horaValida()) return;
  uint32_t agora = time(nullptr), local = lerEpochRtc();
  long dif = (long)local - (long)agora;
  if (local == 0 || dif > 2 || dif < -2) { acertarRtc(agora); Serial.println("DS1302 acertado pelo NTP"); }
}
// Melhor hora disponivel agora: DS1302, senao NTP, senao 0
uint32_t horaAgora() {
  uint32_t e = lerEpochRtc();
  if (e) return e;
  return horaValida() ? (uint32_t)time(nullptr) : 0;
}

// ====================================================================
// Memoria (NVS): numero de sequencia e fila de pontos
// ====================================================================
void salvarFila() { memoria.putUChar("nfila", nFila); memoria.putBytes("fila", fila, sizeof(Ponto) * nFila); }
void carregarFila() {
  nFila = memoria.getUChar("nfila", 0);
  if (nFila > TAM_FILA) nFila = 0;
  if (nFila > 0) memoria.getBytes("fila", fila, sizeof(Ponto) * nFila);
}
void tirarDaFila(int i) {
  for (int j = i; j < nFila - 1; j++) fila[j] = fila[j + 1];
  nFila--;
  salvarFila();
}

// ====================================================================
// LoRa e MQTT
// ====================================================================
void montarPacote(uint8_t* b, uint8_t tipo, uint16_t badge, uint16_t seq, uint32_t hora, uint8_t flags) {
  b[0] = 1; b[1] = tipo;
  b[2] = badge & 0xFF; b[3] = badge >> 8;
  b[4] = seq & 0xFF;   b[5] = seq >> 8;
  for (int i = 0; i < 4; i++) b[6 + i] = (hora >> (8 * i)) & 0xFF;
  b[10] = flags;
}

// Avisa o cracha por LoRa (codigo 1 = BIPE longo, 2 = PONTO OK). Manda 2 vezes (sem ACK, para nao atrasar o clique).
void avisarCracha(uint16_t id, uint8_t codigo) {
  uint8_t p[11];
  montarPacote(p, 3, BADGE_PONTO, id, 0, 0);
  p[6] = codigo;
  for (int i = 0; i < 2; i++) { radio.transmit(p, 11); delay(150); }
  radio.startReceive();
}

void publicarStatus() {
  char js[96];
  if (temQualidade) snprintf(js, sizeof(js), "{\"estado\":\"online\",\"rssi\":%.0f,\"snr\":%.1f}", ultimoRssi, ultimoSnr);
  else snprintf(js, sizeof(js), "{\"estado\":\"online\"}");
  mqtt.publish(TOPICO_GW, js, true);
  ultimoStatus = millis();
}

void publicarAckComando(uint16_t badge, uint32_t id, bool ok) {
  char topico[48], js[64];
  snprintf(topico, sizeof(topico), "unifesp/ponto/cmd/%u/ack", badge);
  snprintf(js, sizeof(js), "{\"id\":%lu,\"ok\":%s}", (unsigned long)id, ok ? "true" : "false");
  mqtt.publish(topico, js);
  Serial.printf("ACK de comando -> %s %s\n", topico, js);
}

// Publica um ponto. true = o broker aceitou.
bool publicarPonto(uint16_t badge, uint16_t seq, uint32_t hora, bool fila, bool comRssi) {
  if (!mqtt.connected()) return false;
  if (hora < 1700000000) hora = horaValida() ? (uint32_t)time(nullptr) : 0;   // sem hora: nao publica
  if (!hora) return false;
  time_t quando = hora;
  char iso[24];
  struct tm tmv; gmtime_r(&quando, &tmv);
  strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &tmv);
  char js[200];
  if (comRssi)
    snprintf(js, sizeof(js), "{\"ts\":\"%s\",\"badge\":%u,\"origem\":\"botao\",\"seq\":%u,\"fila\":%s,\"rssi_lora\":%.0f,\"snr\":%.1f}",
             iso, badge, seq, fila ? "true" : "false", ultimoRssi, ultimoSnr);
  else
    snprintf(js, sizeof(js), "{\"ts\":\"%s\",\"badge\":%u,\"origem\":\"botao\",\"seq\":%u,\"fila\":%s}",
             iso, badge, seq, fila ? "true" : "false");
  bool ok = mqtt.publish(TOPICO_EVENTO, js);
  if (ok) Serial.println(js);
  return ok;
}

// Esvazia a fila (mais antigo primeiro). Para no primeiro que falhar. Devolve quantos foram.
int processarFila() {
  int enviados = 0;
  while (nFila > 0) {
    Ponto p = fila[0];
    if (!publicarPonto(BADGE_PONTO, p.seq, p.hora, p.reenvio, false)) {
      if (!fila[0].reenvio) { fila[0].reenvio = 1; salvarFila(); }
      break;
    }
    tirarDaFila(0);
    enviados++;
  }
  ultimoReenvio = millis();
  return enviados;
}

// ====================================================================
// BLE (igual ao teste 5 v2: contador de cliques no anuncio)
// ====================================================================
void anunciar(uint16_t intervalo) {
  BLEAdvertising* anuncio = BLEDevice::getAdvertising();
  anuncio->stop();
  TextoBle fab;
  fab += (char)0xFF; fab += (char)0xFF;
  fab += (char)1;
  fab += (char)(ID_RECIPIENTE & 0xFF); fab += (char)(ID_RECIPIENTE >> 8);
  fab += (char)idBoot;
  fab += (char)(cliques & 0xFF); fab += (char)(cliques >> 8);
  BLEAdvertisementData dados;
  dados.setFlags(0x06);
  dados.setName(NOME_BLE);
  dados.setManufacturerData(fab);
  anuncio->setAdvertisementData(dados);
  anuncio->setMinInterval(intervalo);
  anuncio->setMaxInterval(intervalo);
  anuncio->start();
}

// ====================================================================
// Tela
// ====================================================================
void desenhar() {
  tela.clearBuffer();
  tela.setFont(u8g2_font_ncenB10_tr);
  tela.drawStr(0, 12, "GATEWAY+BOTAO");
  tela.setFont(u8g2_font_6x10_tr);
  uint32_t e = horaAgora();
  char h[12] = "--:--:--";
  if (e) {
    uint32_t loc = e + FUSO_HORAS * 3600;
    snprintf(h, sizeof(h), "%02u:%02u:%02u", (unsigned)((loc % 86400) / 3600), (unsigned)((loc % 3600) / 60), (unsigned)(loc % 60));
  }
  String l2 = String("W:") + (WiFi.status() == WL_CONNECTED ? "ok" : "--") + " M:" + (mqtt.connected() ? "ok" : "--") +
              " " + h;
  tela.drawStr(0, 26, l2.c_str());
  tela.drawStr(0, 40, ultimoEvento.c_str());
  String l4 = "fila " + String(nFila) + " cliques " + String(cliques);
  tela.drawStr(0, 54, l4.c_str());
  tela.sendBuffer();
}

void avisar(const String& m) { ultimoEvento = m; Serial.println(m); desenhar(); }

// ====================================================================
// Clique do botao = um ponto
// ====================================================================
void aoClicar() {
  bipeClique();                                       // feedback imediato (meta: botao -> bipe <= 2 s)
  cliques++;                                          // o PC percebe este numero mudando (BLE)
  bleRapido = true; fimRapido = millis() + DURACAO_RAPIDO_MS;
  anunciar(INTERVALO_RAPIDO);

  seqAtual++;
  memoria.putUShort("seq", seqAtual);                 // grava ANTES de enviar: nunca reusa um numero
  if (nFila >= TAM_FILA) { avisar("FILA CHEIA: descartei o mais antigo"); tirarDaFila(0); }
  fila[nFila++] = { seqAtual, lerEpochRtc(), 0 };     // hora 0 = sem relogio; publicarPonto usa o NTP
  salvarFila();

  int ok = processarFila();
  if (nFila == 0 && ok > 0) {
    avisar("PONTO OK seq " + String(seqAtual));
    bipeOk();
    avisarCracha(seqAtual, 2);                        // o cracha tambem apita: ponto confirmado
  } else {
    avisar("SEM REDE: na fila");
    bipeFalha();
  }
}

bool botaoApertado() { return digitalRead(PINO_BOTAO) == LOW || digitalRead(PINO_PRG) == LOW; }

// ====================================================================
// Comandos do painel (MQTT -> LoRa) e pacotes recebidos
// ====================================================================
// Chega do broker: unifesp/ponto/cmd/101  {"id":12,"comando":"BIPE"}
// So anotamos; quem manda pelo radio e o loop (para nao travar o MQTT)
void aoReceberMqtt(char* topico, byte* dados, unsigned int n) {
  char corpo[200];
  if (n >= sizeof(corpo)) n = sizeof(corpo) - 1;
  memcpy(corpo, dados, n); corpo[n] = 0;
  unsigned badge = 0;
  const char* ult = strrchr(topico, '/');
  if (ult) badge = atoi(ult + 1);
  const char* p = strstr(corpo, "\"id\"");
  const char* doisPontos = p ? strchr(p + 4, ':') : nullptr;
  uint32_t id = doisPontos ? strtoul(doisPontos + 1, nullptr, 10) : 0;
  Serial.printf("MQTT cmd badge=%u: %s\n", badge, corpo);
  if (!badge || !id) return;
  if (strstr(corpo, "\"BIPE\"")) {
    cmd = { true, id, (uint16_t)badge, 0, 0 };
    ultimoEvento = "cmd BIPE #" + String(id);
  } else {
    publicarAckComando(badge, id, false);             // OTA ou desconhecido: sem LoRa
    ultimoEvento = "cmd nao suportado";
  }
}

void atenderComando() {
  if (!cmd.ativo || millis() < cmd.proximo) return;
  if (cmd.tentativas >= CMD_TENTATIVAS) {
    publicarAckComando(cmd.badge, cmd.id, false);
    ultimoEvento = "cmd #" + String(cmd.id) + " sem resposta";
    cmd.ativo = false;
    return;
  }
  uint8_t p[11];
  montarPacote(p, 3, cmd.badge, (uint16_t)(cmd.id & 0xFFFF), 0, 0);
  p[6] = 1;                                           // BIPE
  digitalWrite(PINO_LED, HIGH);
  radio.transmit(p, 11);
  digitalWrite(PINO_LED, LOW);
  radio.startReceive();
  cmd.tentativas++;
  cmd.proximo = millis() + CMD_INTERVALO_MS;
  ultimoEvento = "BIPE #" + String(cmd.id) + " (" + String(cmd.tentativas) + "/" + String(CMD_TENTATIVAS) + ")";
}

void processarPacote() {
  size_t len = radio.getPacketLength();
  uint8_t b[32];
  if (len > sizeof(b)) len = sizeof(b);
  int estado = radio.readData(b, len);
  if (estado != RADIOLIB_ERR_NONE || len != 11 || b[0] != 1) { ultimoEvento = "pacote invalido"; return; }
  uint16_t badge = b[2] | (b[3] << 8), seq = b[4] | (b[5] << 8);
  ultimoRssi = radio.getRSSI(); ultimoSnr = radio.getSNR(); temQualidade = true;

  if (b[1] == 4) {                                    // ACK de comando (cracha -> gateway)
    if (cmd.ativo && badge == cmd.badge && seq == (uint16_t)(cmd.id & 0xFFFF)) {
      publicarAckComando(cmd.badge, cmd.id, b[6] == 1);
      ultimoEvento = "cmd #" + String(cmd.id) + " confirmado";
      cmd.ativo = false;
    }
    return;
  }
  if (b[1] != 1) { ultimoEvento = "tipo desconhecido"; return; }

  // Ponto vindo de um cracha de TESTE (firmware/testes/07): publica e responde ACK
  uint32_t horaCracha = b[6] | (b[7] << 8) | (b[8] << 16) | ((uint32_t)b[9] << 24);
  if (publicarPonto(badge, seq, horaCracha, b[10] & 1, true)) {
    ultimoEvento = "cracha " + String(badge) + " seq " + String(seq) + " OK";
    uint8_t ack[11];
    montarPacote(ack, 2, badge, seq, horaValida() ? time(nullptr) : 0, 0);
    delay(100);
    radio.transmit(ack, 11);
    publicarStatus();
  } else {
    ultimoEvento = "sem MQTT/hora: sem ACK";
  }
}

void garantirConexoes() {
  if (millis() - ultimaTentativa < 4000) return;
  if (WiFi.status() != WL_CONNECTED) { ultimaTentativa = millis(); WiFi.reconnect(); return; }
  if (!mqtt.connected()) {
    ultimaTentativa = millis();
    // Last Will: mensagem que o BROKER publica se o gateway sumir sem se despedir
    if (mqtt.connect("gateway-gw-lab", nullptr, nullptr, TOPICO_GW, 1, true, "{\"estado\":\"offline\"}")) {
      mqtt.subscribe(TOPICO_CMD, 1);
      publicarStatus();
    }
  }
}

// ====================================================================
void setup() {
  Serial.begin(115200);
  pinMode(PINO_VEXT, OUTPUT); digitalWrite(PINO_VEXT, LOW); delay(100);
  pinMode(PINO_LED, OUTPUT);
  pinMode(PINO_BUZZER, OUTPUT); digitalWrite(PINO_BUZZER, LOW);
  pinMode(PINO_BOTAO, INPUT_PULLUP);
  pinMode(PINO_PRG, INPUT_PULLUP);
  tela.begin();
  ultimoEvento = "iniciando...";
  desenhar();

  if (TEM_DS1302) {
    pinMode(PINO_CLK, OUTPUT); pinMode(PINO_RST_RTC, OUTPUT); pinMode(PINO_DAT, INPUT);
    digitalWrite(PINO_CLK, LOW); digitalWrite(PINO_RST_RTC, LOW);
    delay(100);
  }
  memoria.begin("gateway", false);
  seqAtual = memoria.getUShort("seq", 0);
  carregarFila();

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  radio.tcxoVoltage = RadioLibTCXOVoltage_t::Voltage1V8;
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
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  mqtt.setServer(MQTT_HOST, MQTT_PORTA);
  mqtt.setBufferSize(512);
  mqtt.setCallback(aoReceberMqtt);
  garantirConexoes();

  idBoot = esp_random() & 0xFF;
  BLEDevice::init(NOME_BLE);                          // BLE depois do Wi-Fi (os dois dividem a antena 2,4 GHz)
  anunciar(INTERVALO_LENTO);

  radio.startReceive();
  ultimoEvento = "pronto";
  bipeClique();
  desenhar();
}

void loop() {
  garantirConexoes();
  mqtt.loop();

  // Botao com debounce de 50 ms; so conta ao APERTAR e de novo so depois de soltar
  if (botaoApertado()) {
    delay(50);
    if (botaoApertado()) { aoClicar(); while (botaoApertado()) delay(10); }
  }

  if (chegouPacote) {
    chegouPacote = false;
    digitalWrite(PINO_LED, HIGH);
    processarPacote();
    digitalWrite(PINO_LED, LOW);
    radio.startReceive();
  }
  atenderComando();

  if (bleRapido && millis() > fimRapido) { bleRapido = false; anunciar(INTERVALO_LENTO); }

  if (nFila > 0 && mqtt.connected() && millis() - ultimoReenvio > REENVIO_MS) {
    int ok = processarFila();
    if (ok > 0) { avisar(String(ok) + " ponto(s) da fila enviado(s)"); bipeOk(); avisarCracha(seqAtual, 2); }
  }
  if (millis() - ultimoSyncRtc > 600000UL || (ultimoSyncRtc == 0 && horaValida())) sincronizarRelogio();   // a cada 10 min
  if (mqtt.connected() && millis() - ultimoStatus > STATUS_MS) publicarStatus();
  if (millis() - ultimoDesenho > 1000) { ultimoDesenho = millis(); desenhar(); }
}
