// Teste 1 - "Ola mundo" na Heltec LoRa 32 V3
// Objetivo: provar que conseguimos gravar codigo na placa, acender o LED e escrever no OLED.
//
// Biblioteca necessaria (Arduino IDE > Gerenciar Bibliotecas): "U8g2" (autor: oliver)
// Placa: "WiFi LoRa 32(V3)" (pacote Heltec ESP32) ou, se nao aparecer, "ESP32S3 Dev Module".

#include <U8g2lib.h>

// Pinos da Heltec V3 (cada "pino" e um fiozinho do chip que podemos ligar/desligar)
const int PINO_VEXT = 36;  // liga a energia do OLED (LOW = ligado; sim, e invertido)
const int PINO_LED  = 35;  // LED branco que ja vem na placa

// O OLED conversa por 2 fios (I2C): SCL (relogio) = 18, SDA (dados) = 17, RESET = 21
U8G2_SSD1306_128X64_NONAME_F_SW_I2C tela(U8G2_R0, /*clock=*/18, /*data=*/17, /*reset=*/21);

int contador = 0;

void setup() {
  Serial.begin(115200);          // canal de texto com o computador (Monitor Serial)

  pinMode(PINO_VEXT, OUTPUT);
  digitalWrite(PINO_VEXT, LOW);  // 1o: dar energia ao OLED
  delay(100);                    // esperar ele "acordar"

  pinMode(PINO_LED, OUTPUT);

  tela.begin();                  // 2o: iniciar o OLED
  tela.setFont(u8g2_font_ncenB10_tr);
}

void loop() {
  contador++;

  digitalWrite(PINO_LED, HIGH);  // acende
  tela.clearBuffer();
  tela.drawStr(0, 16, "Cracha UNIFESP");
  tela.drawStr(0, 36, "Ola, mundo!");
  char texto[24];
  snprintf(texto, sizeof(texto), "Contagem: %d", contador);
  tela.drawStr(0, 56, texto);
  tela.sendBuffer();
  Serial.println(texto);
  delay(500);

  digitalWrite(PINO_LED, LOW);   // apaga
  delay(500);
}
