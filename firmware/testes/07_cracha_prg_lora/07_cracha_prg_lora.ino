// Teste 7 - "CRACHA DE TESTE": o botao PRG da Heltec bate o ponto por LoRa (sem componente externo)
//
// Serve para testar o caminho completo ate o banco enquanto o botao/buzzer/relogio reais nao chegam.
// Aperta PRG -> manda pacote LoRa de 11 bytes ao gateway -> espera o ACK (2 s, ate 3 tentativas).
// O contador "seq" fica gravado na memoria da placa (NVS) para NAO repetir depois de desligar
// (o banco recusa o mesmo seq duas vezes, e com razao: evita ponto duplicado).
//
// Usa o cracha numero 102 (o 101 e o da demo). O gateway precisa estar rodando (teste 6).
// Sem relogio (DS1302): a hora vai como 0 e o gateway carimba com a hora da internet.
//
// Bibliotecas: "RadioLib" 8.x e "U8g2". ATENCAO: NUNCA ligue a Heltec sem a antena.

#include <RadioLib.h>
#include <U8g2lib.h>
#include <Preferences.h>

const uint16_t ID_CRACHA = 102;

const uint32_t FREQ_HZ = 915000000, LARGURA_HZ = 125000;
const int SF = 7, CR = 5, SYNC_WORD = 0x12, POTENCIA = 14;

const int PINO_PRG = 0;                     // botao "PRG" da placa (LOW quando apertado)
const int PINO_VEXT = 36, PINO_LED = 35;
const int LORA_NSS = 8, LORA_SCK = 9, LORA_MOSI = 10, LORA_MISO = 11;
const int LORA_RST = 12, LORA_BUSY = 13, LORA_DIO1 = 14;

SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
U8G2_SSD1306_128X64_NONAME_F_SW_I2C tela(U8G2_R0, 18, 17, 21);
Preferences memoria;

volatile bool chegouPacote = false;
void IRAM_ATTR aoChegarPacote() { chegouPacote = true; }

uint16_t seq = 0;

void mostrar(const String& l1, const String& l2, const String& l3) {
  tela.clearBuffer();
  tela.setFont(u8g2_font_ncenB10_tr);
  tela.drawStr(0, 14, l1.c_str());
  tela.setFont(u8g2_font_6x10_tr);
  tela.drawStr(0, 36, l2.c_str());
  tela.drawStr(0, 52, l3.c_str());
  tela.sendBuffer();
  Serial.println(l1 + " | " + l2 + " | " + l3);
}

void montarPacote(uint8_t* b, uint8_t tipo, uint16_t badge, uint16_t s, uint32_t hora, uint8_t flags) {
  b[0] = 1; b[1] = tipo;
  b[2] = badge & 0xFF; b[3] = badge >> 8;
  b[4] = s & 0xFF;     b[5] = s >> 8;
  for (int i = 0; i < 4; i++) b[6 + i] = (hora >> (8 * i)) & 0xFF;
  b[10] = flags;
}

// Espera ate 2 s por um ACK deste cracha com este seq. Devolve a hora do gateway (>0) ou 0 se nao veio.
uint32_t esperarAck(uint16_t s) {
  unsigned long inicio = millis();
  while (millis() - inicio < 2000) {
    if (chegouPacote) {
      chegouPacote = false;
      uint8_t b[32]; size_t len = radio.getPacketLength();
      if (len > sizeof(b)) len = sizeof(b);
      if (radio.readData(b, len) == RADIOLIB_ERR_NONE && len == 11 && b[1] == 2 &&
          (b[2] | (b[3] << 8)) == ID_CRACHA && (b[4] | (b[5] << 8)) == s) {
        return b[6] | (b[7] << 8) | (b[8] << 16) | ((uint32_t)b[9] << 24);
      }
      radio.startReceive();
    }
    delay(5);
  }
  return 0;
}

void baterPonto() {
  seq++;
  memoria.putUShort("seq", seq);            // grava ANTES de enviar: nunca reusa um numero
  uint8_t pacote[11];
  montarPacote(pacote, 1, ID_CRACHA, seq, 0, 0);

  for (int tentativa = 1; tentativa <= 3; tentativa++) {
    mostrar("Enviando...", "seq " + String(seq), "tentativa " + String(tentativa) + "/3");
    digitalWrite(PINO_LED, HIGH);
    radio.transmit(pacote, 11);
    digitalWrite(PINO_LED, LOW);
    chegouPacote = false;
    radio.startReceive();
    uint32_t hora = esperarAck(seq);
    if (hora) {
      time_t t = (time_t)hora - 3 * 3600;   // UTC-3 (Brasilia) so para mostrar
      struct tm tmv; gmtime_r(&t, &tmv);
      char h[16]; strftime(h, sizeof(h), "%H:%M:%S", &tmv);
      mostrar("PONTO OK", "seq " + String(seq), String("hora ") + h);
      return;
    }
  }
  mostrar("SEM ACK", "seq " + String(seq), "gateway ligado?");
}

void setup() {
  Serial.begin(115200);
  pinMode(PINO_PRG, INPUT_PULLUP);
  pinMode(PINO_VEXT, OUTPUT); digitalWrite(PINO_VEXT, LOW); delay(100);
  pinMode(PINO_LED, OUTPUT);
  tela.begin();

  memoria.begin("cracha", false);
  seq = memoria.getUShort("seq", 0);

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  radio.tcxoVoltage = RadioLibTCXOVoltage_t::Voltage1V8;
  ConfigLoRa_t cfg;
  cfg.frequency = FREQ_HZ; cfg.bandwidth = LARGURA_HZ; cfg.spreadingFactor = SF;
  cfg.codingRate = CR; cfg.syncWord = SYNC_WORD; cfg.power = POTENCIA; cfg.preambleLength = 8;
  int estado = radio.begin(cfg);
  if (estado != RADIOLIB_ERR_NONE) { mostrar("ERRO LoRa", "codigo " + String(estado), ""); while (true) delay(1000); }
  radio.setDio1Action(aoChegarPacote);
  radio.startReceive();
  mostrar("CRACHA " + String(ID_CRACHA), "aperte PRG p/ ponto", "ultimo seq " + String(seq));
}

void loop() {
  if (digitalRead(PINO_PRG) == LOW) {
    delay(50);                               // debounce: confirma que nao foi so ruido
    if (digitalRead(PINO_PRG) == LOW) {
      baterPonto();
      while (digitalRead(PINO_PRG) == LOW) delay(10);   // espera soltar (um clique = um ponto)
      delay(2000);
      mostrar("CRACHA " + String(ID_CRACHA), "aperte PRG p/ ponto", "ultimo seq " + String(seq));
    }
  }
}
