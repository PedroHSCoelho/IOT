// Teste 5 (v2) - Cracha "grita" por Bluetooth (BLE) e AVISA quando o botao e apertado
//
// Ideia nova do projeto (12/11/2026): o BOTAO e o "cartao passado no leitor".
//   - Quem desbloqueia o PC e o clique do botao + a prova de que o cracha esta perto.
//   - O PC escuta o anuncio BLE do cracha. Quando o CONTADOR de cliques muda, o PC sabe que o
//     botao foi apertado e mede o RSSI (forca do sinal) DAQUELE pacote: sem esperar media.
//
// Analogia: o cracha e um farol que pisca "UNIFESP-BADGE" a cada 500 ms. Cada clique do botao faz o
// farol piscar mais rapido (a cada 100 ms) por 5 s e muda um numero escondido no sinal (o contador).
//
// Anuncio (28 bytes de 31 possiveis):
//   flags (3) + nome "UNIFESP-BADGE" (15) + dados do fabricante (10):
//   [FF FF] id da empresa generico | [1] versao | [id lo][id hi] numero do cracha |
//   [boot] numero aleatorio sorteado a cada ligada | [cont lo][cont hi] contador de cliques
//   O "boot" existe para o PC nao confundir "o cracha reiniciou e o contador zerou" com um clique.
//
// O ponto em si (CHEGADA/SAIDA) continua indo por LoRa -> gateway -> MQTT -> banco (testes 6 e 7).
// Aqui o botao so serve para o BLE. Por enquanto o botao e o PRG da placa (GPIO 0); o botao
// definitivo sera ligado em outro pino quando chegar (mude PINO_BOTAO).
//
// Placa: "WiFi LoRa 32(V3)" (pacote Heltec ESP32) ou "ESP32S3 Dev Module".
// Biblioteca: BLE ja vem com o pacote da placa (nada para instalar), mais "U8g2" para o OLED.
// No PC, rode:  python pc-agent/agente_bloqueio.py   (ou teste_ble_rssi.py para so medir)

#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <U8g2lib.h>

const uint16_t ID_CRACHA = 101;           // o 101 e o cracha da demo
const char* NOME_BLE = "UNIFESP-BADGE";   // o PC procura por este prefixo

const int PINO_BOTAO = 0;                 // PRG da placa (LOW = apertado)
const int PINO_VEXT = 36;                 // energia do OLED (LOW = ligado)
const int PINO_LED  = 35;

const uint16_t INTERVALO_LENTO  = 800;    // 800 x 0,625 ms = 500 ms entre anuncios (repouso)
const uint16_t INTERVALO_RAPIDO = 160;    // 160 x 0,625 ms = 100 ms (logo apos um clique)
const unsigned long DURACAO_RAPIDO_MS = 5000;

// As versoes 2.x e 3.x do pacote de placas usam tipos de texto diferentes no BLE
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
typedef String TextoBle;
#else
typedef std::string TextoBle;
#endif

U8G2_SSD1306_128X64_NONAME_F_SW_I2C tela(U8G2_R0, /*clock=*/18, /*data=*/17, /*reset=*/21);

uint8_t idBoot;                           // sorteado a cada ligada
uint16_t cliques = 0;                     // contador de cliques desde que ligou
bool rapido = false;
unsigned long fimRapido = 0;

bool ultimoLido = false, estavaApertado = false;
unsigned long ultimaMudanca = 0;

void desenhar() {
  tela.clearBuffer();
  tela.setFont(u8g2_font_ncenB10_tr);
  tela.drawStr(0, 14, "BLE no ar");
  tela.setFont(u8g2_font_6x10_tr);
  tela.drawStr(0, 32, (String(NOME_BLE) + " #" + String(ID_CRACHA)).c_str());
  tela.drawStr(0, 46, ("cliques: " + String(cliques)).c_str());
  tela.drawStr(0, 60, rapido ? "anuncio RAPIDO (100ms)" : "anuncio lento (500ms)");
  tela.sendBuffer();
}

// (Re)monta o anuncio com o contador atual e o intervalo pedido
void anunciar(uint16_t intervalo) {
  BLEAdvertising* anuncio = BLEDevice::getAdvertising();
  anuncio->stop();

  TextoBle fab;
  fab += (char)0xFF; fab += (char)0xFF;               // id de empresa generico (uso livre/teste)
  fab += (char)1;                                     // versao do formato
  fab += (char)(ID_CRACHA & 0xFF); fab += (char)(ID_CRACHA >> 8);
  fab += (char)idBoot;
  fab += (char)(cliques & 0xFF);   fab += (char)(cliques >> 8);

  BLEAdvertisementData dados;
  dados.setFlags(0x06);                               // "so anuncio, sem conexao classica"
  dados.setName(NOME_BLE);
  dados.setManufacturerData(fab);
  anuncio->setAdvertisementData(dados);
  anuncio->setMinInterval(intervalo);
  anuncio->setMaxInterval(intervalo);
  anuncio->start();
}

void aoClicar() {
  cliques++;
  rapido = true;
  fimRapido = millis() + DURACAO_RAPIDO_MS;
  anunciar(INTERVALO_RAPIDO);                         // contador novo + anuncio rapido
  digitalWrite(PINO_LED, HIGH);
  Serial.printf("CLIQUE %u\n", cliques);
  desenhar();
}

void setup() {
  Serial.begin(115200);
  pinMode(PINO_VEXT, OUTPUT); digitalWrite(PINO_VEXT, LOW); delay(100);
  pinMode(PINO_LED, OUTPUT);
  pinMode(PINO_BOTAO, INPUT_PULLUP);
  tela.begin();

  idBoot = esp_random() & 0xFF;
  BLEDevice::init(NOME_BLE);
  anunciar(INTERVALO_LENTO);
  Serial.printf("Anunciando %s #%u (boot %u)\n", NOME_BLE, ID_CRACHA, idBoot);
  desenhar();
}

void loop() {
  // Botao com debounce de 50 ms: so vale quando o estado fica estavel por 50 ms
  bool lido = (digitalRead(PINO_BOTAO) == LOW);
  if (lido != ultimoLido) { ultimoLido = lido; ultimaMudanca = millis(); }
  if (millis() - ultimaMudanca > 50 && lido != estavaApertado) {
    estavaApertado = lido;
    if (lido) aoClicar();                              // so na hora de APERTAR (um clique = um evento)
  }

  // Passou o tempo do anuncio rapido: volta ao ritmo lento para poupar bateria
  if (rapido && millis() > fimRapido) {
    rapido = false;
    anunciar(INTERVALO_LENTO);
    digitalWrite(PINO_LED, LOW);
    desenhar();
  }
  delay(5);
}
