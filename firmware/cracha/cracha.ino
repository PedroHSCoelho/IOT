// CRACHA INTELIGENTE DE PONTO - firmware FINAL do cracha (Heltec LoRa 32 V3)
//
// Arranjo final (decisao de 24/11/2026): o BOTAO fica no GATEWAY+RECIPIENTE (firmware/gateway).
// O cracha e a pessoa "carregando o sinal": ele faz tres coisas simples.
//   1) BLE: anuncia "UNIFESP-BADGE" (numero 101) a cada 500 ms. O agente do PC mede a forca do
//      sinal (RSSI = distancia): chegou perto -> desbloqueia; afastou -> avisa e bloqueia.
//   2) LoRa (so ESCUTA): quando o gateway manda, o cracha apita.
//        codigo 2 = PONTO OK (2 bipes curtos): o ponto que voce acabou de bater foi gravado;
//        codigo 1 = BIPE longo: comando do painel (teste); o cracha responde com um ACK (tipo 4).
//   3) OLED + LED: mostra o ultimo aviso recebido, o RSSI do gateway e ha quanto tempo.
//
// Analogia: o cracha e a "pulseira" que pisca para o PC saber que voce esta perto, e vibra
// (aqui: apita) quando o leitor da parede confirma o seu ponto.
//
// Pacote LoRa de 11 bytes (igual ao gateway):
//   [0]=versao(1) [1]=tipo [2-3]=badge [4-5]=seq ou id do comando [6-9]=hora [10]=flags
//   tipo 3 = comando (gateway->cracha) [6] = 1 BIPE / 2 PONTO OK    tipo 4 = ACK do comando (cracha->gateway)
//
// Hardware do cracha: so a Heltec + buzzer (GPIO 5). Sem botao e sem DS1302 (eles estao no gateway).
// Placa: "WiFi LoRa 32(V3)" ou "ESP32S3 Dev Module". Bibliotecas: "RadioLib" 8.x e "U8g2".
// ATENCAO: NUNCA ligue a Heltec sem a antena LoRa conectada.

#include <RadioLib.h>
#include <U8g2lib.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>

// ============================ CONFIGURACAO ============================
const uint16_t ID_CRACHA = 101;            // numero do cracha (101 = demo)
const char* NOME_BLE = "UNIFESP-BADGE";    // o agente do PC procura por este nome
const bool BUZZER_PASSIVO = false;         // false = buzzer ATIVO; true = passivo

const uint32_t FREQ_HZ = 915000000, LARGURA_HZ = 125000;     // identico ao gateway
const int SF = 7, CR = 5, SYNC_WORD = 0x2B, POTENCIA = 14;   // proprio da equipe; igual ao do gateway

const int PINO_BUZZER = 5;
const int PINO_VEXT = 36, PINO_LED = 35;
const int LORA_NSS = 8, LORA_SCK = 9, LORA_MOSI = 10, LORA_MISO = 11;
const int LORA_RST = 12, LORA_BUSY = 13, LORA_DIO1 = 14;

const uint16_t INTERVALO_BLE = 800;        // 800 x 0,625 ms = 500 ms
// =====================================================================

SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
U8G2_SSD1306_128X64_NONAME_F_SW_I2C tela(U8G2_R0, 18, 17, 21);

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
typedef String TextoBle;
#else
typedef std::string TextoBle;
#endif

volatile bool chegouPacote = false;
void IRAM_ATTR aoChegarPacote() { chegouPacote = true; }

uint8_t idBoot;
String ultimoAviso = "aguardando gateway";
unsigned long quandoAviso = 0, ultimoDesenho = 0;
float ultimoRssi = 0;
bool temRssi = false;
uint16_t ultimoId = 0; uint8_t ultimoCodigo = 0; unsigned long quandoUltimoId = 0;   // evita bipe em dobro

void tom(int freq, int ms) {
  if (!BUZZER_PASSIVO) { digitalWrite(PINO_BUZZER, HIGH); delay(ms); digitalWrite(PINO_BUZZER, LOW); return; }
  unsigned long meio = 500000UL / freq, fim = millis() + ms;
  while (millis() < fim) {
    digitalWrite(PINO_BUZZER, HIGH); delayMicroseconds(meio);
    digitalWrite(PINO_BUZZER, LOW);  delayMicroseconds(meio);
  }
}
void bipeOk()      { tom(2400, 80); delay(70); tom(2400, 80); }      // ponto confirmado
void bipeComando() { tom(1800, 250); }                               // BIPE do painel

void montarPacote(uint8_t* b, uint8_t tipo, uint16_t badge, uint16_t s, uint32_t hora, uint8_t flags) {
  b[0] = 1; b[1] = tipo;
  b[2] = badge & 0xFF; b[3] = badge >> 8;
  b[4] = s & 0xFF;     b[5] = s >> 8;
  for (int i = 0; i < 4; i++) b[6 + i] = (hora >> (8 * i)) & 0xFF;
  b[10] = flags;
}

void escutar() { chegouPacote = false; radio.startReceive(); }

void desenhar() {
  tela.clearBuffer();
  tela.setFont(u8g2_font_ncenB10_tr);
  tela.drawStr(0, 13, ("CRACHA " + String(ID_CRACHA)).c_str());
  tela.setFont(u8g2_font_6x10_tr);
  tela.drawStr(0, 28, "BLE: anunciando sinal");
  tela.drawStr(0, 42, ultimoAviso.c_str());
  String l4 = quandoAviso ? "ha " + String((millis() - quandoAviso) / 1000) + " s" : "";
  if (temRssi) l4 += "  RSSI " + String(ultimoRssi, 0);
  tela.drawStr(0, 56, l4.c_str());
  tela.sendBuffer();
}

void aviso(const String& m) { ultimoAviso = m; quandoAviso = millis(); Serial.println(m); desenhar(); }

void tratarPacote() {
  uint8_t b[32]; size_t len = radio.getPacketLength();
  if (len > sizeof(b)) len = sizeof(b);
  int estado = radio.readData(b, len);
  float rssi = radio.getRSSI();
  escutar();
  if (estado != RADIOLIB_ERR_NONE || len != 11 || b[0] != 1 || b[1] != 3) return;      // so comandos
  if ((b[2] | (b[3] << 8)) != ID_CRACHA) return;
  uint16_t id = b[4] | (b[5] << 8);
  uint8_t codigo = b[6];
  ultimoRssi = rssi; temRssi = true;

  // O gateway repete o aviso 2x; ignora a repeticao (mesmo id e codigo em menos de 2 s)
  bool repetido = (id == ultimoId && codigo == ultimoCodigo && millis() - quandoUltimoId < 2000);
  ultimoId = id; ultimoCodigo = codigo; quandoUltimoId = millis();

  if (codigo == 2) {                                   // PONTO OK
    if (repetido) return;
    digitalWrite(PINO_LED, HIGH);
    aviso("PONTO OK (seq " + String(id) + ")");
    bipeOk();
    digitalWrite(PINO_LED, LOW);
  } else if (codigo == 1) {                            // BIPE do painel: apita e confirma (ACK)
    if (!repetido) { aviso("comando BIPE #" + String(id)); bipeComando(); }
    uint8_t ack[11];
    montarPacote(ack, 4, ID_CRACHA, id, 0, 1);
    delay(150);                                        // o gateway precisa voltar a escutar
    radio.transmit(ack, 11);
    escutar();
  }
}

void anunciarBle() {
  BLEAdvertising* anuncio = BLEDevice::getAdvertising();
  TextoBle fab;
  fab += (char)0xFF; fab += (char)0xFF;                // id de empresa generico
  fab += (char)1;                                      // versao do formato
  fab += (char)(ID_CRACHA & 0xFF); fab += (char)(ID_CRACHA >> 8);
  fab += (char)idBoot;
  fab += (char)0; fab += (char)0;                      // contador de cliques: o cracha nao tem botao
  BLEAdvertisementData dados;
  dados.setFlags(0x06);
  dados.setName(NOME_BLE);
  dados.setManufacturerData(fab);
  anuncio->setAdvertisementData(dados);
  anuncio->setMinInterval(INTERVALO_BLE);
  anuncio->setMaxInterval(INTERVALO_BLE);
  anuncio->start();
}

void setup() {
  Serial.begin(115200);
  pinMode(PINO_VEXT, OUTPUT); digitalWrite(PINO_VEXT, LOW); delay(100);
  pinMode(PINO_LED, OUTPUT);
  pinMode(PINO_BUZZER, OUTPUT); digitalWrite(PINO_BUZZER, LOW);
  tela.begin();

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  radio.tcxoVoltage = RadioLibTCXOVoltage_t::Voltage1V8;
  ConfigLoRa_t cfg;
  cfg.frequency = FREQ_HZ; cfg.bandwidth = LARGURA_HZ; cfg.spreadingFactor = SF;
  cfg.codingRate = CR; cfg.syncWord = SYNC_WORD; cfg.power = POTENCIA; cfg.preambleLength = 8;
  int estado = radio.begin(cfg);
  if (estado != RADIOLIB_ERR_NONE) {
    tela.clearBuffer(); tela.setFont(u8g2_font_ncenB10_tr);
    tela.drawStr(0, 20, "ERRO LoRa"); tela.drawStr(0, 40, String(estado).c_str()); tela.sendBuffer();
    Serial.printf("ERRO LoRa %d (antena conectada?)\n", estado);
    while (true) delay(1000);
  }
  radio.setDio1Action(aoChegarPacote);
  escutar();

  idBoot = esp_random() & 0xFF;
  BLEDevice::init(NOME_BLE);
  anunciarBle();
  Serial.printf("Cracha %u pronto: BLE %s + escuta LoRa\n", ID_CRACHA, NOME_BLE);
  tom(2000, 60);
  desenhar();
}

void loop() {
  if (chegouPacote) { chegouPacote = false; tratarPacote(); }
  if (millis() - ultimoDesenho > 1000) { ultimoDesenho = millis(); desenhar(); }
  delay(5);
}
