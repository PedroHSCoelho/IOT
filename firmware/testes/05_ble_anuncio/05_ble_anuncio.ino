// Teste 5 - Cracha "grita" por Bluetooth (BLE) para o PC ouvir
// Objetivo: o PC enxergar o cracha e medir a forca do sinal (RSSI) para saber se ele esta perto.
//
// Analogia: o cracha e um farol. A cada 500 ms ele pisca "UNIFESP-BADGE-101" pelo ar. Nao precisa
// parear nem conectar: o PC so escuta o farol e mede o quao forte a luz chega (quanto mais perto, mais forte).
//
// O numero do cracha (101) vai no proprio nome do anuncio: "UNIFESP-BADGE-<id>".
//
// Placa: "WiFi LoRa 32(V3)" (pacote Heltec ESP32) ou "ESP32S3 Dev Module".
// Biblioteca: BLE ja vem com o pacote da placa (nada para instalar), mais "U8g2" para o OLED.
//
// Depois de gravar, no PC rode: python pc-agent/teste_ble_rssi.py

#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <U8g2lib.h>

const int   ID_CRACHA = 101;              // o 101 e o cracha da demo
const char* NOME_BLE  = "UNIFESP-BADGE-101";  // se mudar o ID, mude aqui tambem

const int PINO_VEXT = 36;                 // energia do OLED (LOW = ligado)
const int PINO_LED  = 35;

U8G2_SSD1306_128X64_NONAME_F_SW_I2C tela(U8G2_R0, /*clock=*/18, /*data=*/17, /*reset=*/21);

void setup() {
  Serial.begin(115200);
  pinMode(PINO_VEXT, OUTPUT); digitalWrite(PINO_VEXT, LOW); delay(100);
  pinMode(PINO_LED, OUTPUT);
  tela.begin();

  BLEDevice::init(NOME_BLE);              // liga o Bluetooth com este nome

  BLEAdvertisementData dados;
  dados.setFlags(0x06);                   // "so anuncio, sem conexao classica"
  dados.setName(NOME_BLE);                // o nome vai dentro do anuncio

  BLEAdvertising* anuncio = BLEDevice::getAdvertising();
  anuncio->setAdvertisementData(dados);
  anuncio->setMinInterval(800);           // 800 x 0,625 ms = 500 ms entre anuncios
  anuncio->setMaxInterval(800);
  BLEDevice::startAdvertising();

  tela.clearBuffer();
  tela.setFont(u8g2_font_ncenB10_tr);
  tela.drawStr(0, 16, "BLE ligado");
  tela.setFont(u8g2_font_6x10_tr);
  tela.drawStr(0, 36, NOME_BLE);
  tela.drawStr(0, 52, "anuncio a cada 500ms");
  tela.sendBuffer();
  Serial.printf("Anunciando como %s (id %d)\n", NOME_BLE, ID_CRACHA);
}

void loop() {
  digitalWrite(PINO_LED, HIGH); delay(50);   // piscada curta = "estou vivo"
  digitalWrite(PINO_LED, LOW);  delay(950);
}
