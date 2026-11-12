// Teste 2 - Ping-pong LoRa entre as duas Heltec LoRa 32 V3
// Objetivo: provar que uma placa fala com a outra pelo radio LoRa.
//
// Como funciona (analogia: dois radios walkie-talkie):
//   - A placa MESTRE grita "PING 1", "PING 2"... a cada 3 segundos.
//   - A placa ESCRAVA escuta; quando ouve um PING, responde "PONG" com o mesmo numero.
//   - O mestre mostra no OLED se recebeu o PONG, e com que forca o sinal chegou (RSSI/SNR).
//
// COMO USAR: grave este MESMO arquivo nas duas placas, mudando so a linha SOU_MESTRE abaixo:
//   placa 1 -> SOU_MESTRE = true      placa 2 -> SOU_MESTRE = false
//
// ATENCAO: NUNCA ligue a Heltec sem a antena conectada (pode queimar o radio).
//
// Biblioteca necessaria (Gerenciar Bibliotecas): "RadioLib" (autor: Jan Gromes) e "U8g2".
// Placa: "WiFi LoRa 32(V3)" (pacote Heltec ESP32) ou "ESP32S3 Dev Module".

#include <RadioLib.h>
#include <U8g2lib.h>

const bool SOU_MESTRE = true;   // <<< MUDE para false na segunda placa

// ---- Parametros LoRa (iguais aos do projeto; as duas placas PRECISAM ser identicas) ----
const float FREQ_MHZ   = 915.0;  // frequencia (faixa liberada no Brasil)
const float LARGURA_KHZ = 125.0; // largura de banda
const int   SF         = 7;      // fator de espalhamento (menor = mais rapido, menor alcance)
const int   CR         = 5;      // taxa de codificacao 4/5
const int   SYNC_WORD  = 0x12;   // "senha de grupo": so ouve quem usa o mesmo valor
const int   POTENCIA   = 14;     // dBm

// ---- Pinos da Heltec V3 ----
const int PINO_VEXT = 36;        // energia do OLED (LOW = ligado)
const int PINO_LED  = 35;
const int LORA_NSS = 8, LORA_SCK = 9, LORA_MOSI = 10, LORA_MISO = 11;
const int LORA_RST = 12, LORA_BUSY = 13, LORA_DIO1 = 14;

SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
U8G2_SSD1306_128X64_NONAME_F_SW_I2C tela(U8G2_R0, /*clock=*/18, /*data=*/17, /*reset=*/21);

// O radio levanta a "bandeirinha" abaixo quando chega um pacote
volatile bool chegouPacote = false;
void IRAM_ATTR aoChegarPacote() { chegouPacote = true; }

int numero = 0;
unsigned long ultimoPing = 0;

// Escreve 3 linhas no OLED e no Serial
void mostrar(const char* l1, const String& l2, const String& l3) {
  tela.clearBuffer();
  tela.setFont(u8g2_font_ncenB10_tr);
  tela.drawStr(0, 14, l1);
  tela.setFont(u8g2_font_6x10_tr);
  tela.drawStr(0, 34, l2.c_str());
  tela.drawStr(0, 50, l3.c_str());
  tela.sendBuffer();
  Serial.println(String(l1) + " | " + l2 + " | " + l3);
}

void parar(const char* motivo, int codigo) {
  mostrar("ERRO LoRa", motivo, "codigo " + String(codigo));
  while (true) delay(1000);
}

void setup() {
  Serial.begin(115200);
  pinMode(PINO_VEXT, OUTPUT);
  digitalWrite(PINO_VEXT, LOW);   // liga o OLED
  delay(100);
  pinMode(PINO_LED, OUTPUT);
  tela.begin();

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  // 1.8 = tensao do oscilador (TCXO) da Heltec V3
  int estado = radio.begin(FREQ_MHZ, LARGURA_KHZ, SF, CR, SYNC_WORD, POTENCIA, 8, 1.8);
  if (estado != RADIOLIB_ERR_NONE) parar("radio.begin falhou", estado);

  radio.setDio1Action(aoChegarPacote);
  radio.startReceive();           // comeca escutando
  mostrar(SOU_MESTRE ? "MESTRE" : "ESCRAVA", "LoRa 915 MHz SF7", "pronto");
}

void loop() {
  if (SOU_MESTRE) {
    // A cada 3 s: envia PING e espera o PONG por ate 2 s
    if (millis() - ultimoPing >= 3000) {
      ultimoPing = millis();
      numero++;
      String msg = "PING " + String(numero);
      digitalWrite(PINO_LED, HIGH);
      radio.transmit(msg);        // fala (bloqueia ate terminar)
      digitalWrite(PINO_LED, LOW);
      chegouPacote = false;
      radio.startReceive();       // volta a escutar

      unsigned long inicio = millis();
      while (!chegouPacote && millis() - inicio < 2000) delay(5);

      if (chegouPacote) {
        chegouPacote = false;
        String resp;
        radio.readData(resp);
        mostrar("MESTRE", "enviei: " + msg, "recebi: " + resp);
        delay(1500);
        mostrar("MESTRE", "RSSI " + String(radio.getRSSI(), 0) + " dBm",
                "SNR " + String(radio.getSNR(), 1) + " dB");
      } else {
        mostrar("MESTRE", "enviei: " + msg, "SEM RESPOSTA");
      }
      radio.startReceive();
    }
  } else {
    // Escrava: espera um PING e responde PONG com o mesmo numero
    if (chegouPacote) {
      chegouPacote = false;
      String recebido;
      int estado = radio.readData(recebido);
      if (estado == RADIOLIB_ERR_NONE && recebido.startsWith("PING")) {
        String info = "RSSI " + String(radio.getRSSI(), 0) + " SNR " + String(radio.getSNR(), 1);
        delay(100);               // da tempo do mestre voltar a escutar
        digitalWrite(PINO_LED, HIGH);
        radio.transmit("PONG" + recebido.substring(4));
        digitalWrite(PINO_LED, LOW);
        mostrar("ESCRAVA", "ouvi: " + recebido, info);
      }
      radio.startReceive();
    }
  }
}
