// CRACHA INTELIGENTE DE PONTO - firmware FINAL do cracha (Heltec LoRa 32 V3)
//
// O cracha e quem VAI ate o local remoto, SEM internet: a pessoa aperta o botao e o ponto viaja por LoRa
// ate o gateway (que tem Wi-Fi) e dai ate o banco. Tambem anuncia o sinal BLE para o PC.
//
// Analogia: o cracha e um "carteiro com caderno". Anota o ponto no caderno (fila na memoria), tenta
// entregar ao gateway por radio e so risca da lista quando recebe o "recebi" (ACK).
//
// Funcoes:
//   1) BOTAO (PRG da placa e/ou botao externo no GPIO 4, debounce 50 ms): UM clique = UM ponto.
//   2) LoRa: manda o pacote ao gateway e espera o ACK (2 s, ate 3 tentativas).
//   3) FILA: sem ACK (fora de alcance), o ponto fica guardado na memoria (sobrevive a desligar) e e
//      reenviado a cada 30 s ate o gateway confirmar. Nenhum ponto se perde.
//   4) HORA: o ponto leva a hora do DS1302 (CLK 40, DAT 41, RST 42) para valer a hora do CLIQUE, mesmo que o
//      envio so aconteca depois. O ACK do gateway traz a hora certa e acerta o DS1302 (erro < 2 s).
//      Sem DS1302 (ou parado) a hora vai 0 e o gateway carimba na CHEGADA do pacote.
//   5) BLE: anuncia "UNIFESP-BADGE" nº 101 (o PC mede a distancia pelo RSSI). Este botao NAO mexe no PC:
//      quem libera/prende o PC e o botao do gateway/recipiente.
//   6) Buzzer (GPIO 5), LED e OLED: 1 bipe curto = clique; 2 bipes = ponto confirmado; 3 graves = sem ACK
//      (ficou na fila). O gateway tambem manda avisos: BIPE do painel (1 longo) e PONTO OK do botao do gateway.
//
// Pacote LoRa de 11 bytes (igual ao gateway):
//   [0]=versao(1) [1]=tipo [2-3]=badge [4-5]=seq ou id [6-9]=hora Unix UTC [10]=flags
//   1 = ponto (cracha->gateway, flags bit0 = veio da fila)   2 = ACK do ponto (gateway->cracha, [6-9] = hora certa)
//   3 = comando (gateway->cracha, [6] = 1 BIPE longo / 2 PONTO OK)   4 = ACK do comando (cracha->gateway)
// seq do cracha: 1..32767 (o gateway usa 32768 em diante para o botao dele; o banco recusa seq repetido).
//
// Placa: "WiFi LoRa 32(V3)" ou "ESP32S3 Dev Module". Bibliotecas: "RadioLib" 8.x e "U8g2".
// ATENCAO: NUNCA ligue a Heltec sem a antena LoRa conectada.

#include <RadioLib.h>
#include <U8g2lib.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>

// ============================ CONFIGURACAO ============================
const uint16_t ID_CRACHA = 101;            // numero do cracha (101 = demo; 102 = teste)
const char* NOME_BLE = "UNIFESP-BADGE";    // o agente do PC procura por este nome
const bool BUZZER_PASSIVO = false;         // false = buzzer ATIVO; true = passivo
const bool TEM_DS1302 = true;              // sem o modulo ligado, o relogio le "invalido" e a hora vai 0 (sem problema)

const uint32_t FREQ_HZ = 915000000, LARGURA_HZ = 125000;     // identico ao gateway
const int SF = 7, CR = 5, SYNC_WORD = 0x2B, POTENCIA = 14;

const int PINO_PRG = 0;                    // botao PRG da placa (LOW = apertado): serve para simular o ponto
const int PINO_BOTAO = 4;                  // botao externo (um lado no GPIO 4, outro no GND)
const int PINO_BUZZER = 5;
const int PINO_VEXT = 36, PINO_LED = 35;
const int PINO_CLK = 40, PINO_DAT = 41, PINO_RST_RTC = 42;
const int LORA_NSS = 8, LORA_SCK = 9, LORA_MOSI = 10, LORA_MISO = 11;
const int LORA_RST = 12, LORA_BUSY = 13, LORA_DIO1 = 14;

const unsigned long ESPERA_ACK_MS = 2000;
const int TENTATIVAS = 3;
const unsigned long REENVIO_FILA_MS = 30000;
const uint16_t INTERVALO_BLE = 800;        // 800 x 0,625 ms = 500 ms
const int FUSO_HORAS = -3;                 // Brasilia (so para MOSTRAR)
const int TAM_FILA = 16;
const uint16_t SEQ_MAX = 32767;            // acima disso e o espaco do gateway
// =====================================================================

SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
U8G2_SSD1306_128X64_NONAME_F_SW_I2C tela(U8G2_R0, 18, 17, 21);
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
String mensagem = "aperte o botao";
unsigned long fimMensagem = 0, ultimoReenvio = 0, ultimoDesenho = 0;
uint16_t ultimoId = 0; uint8_t ultimoCodigo = 0; unsigned long quandoUltimoId = 0;   // evita bipe em dobro

void desenhar();   // declarada antes por clareza (usada em varias funcoes)

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
void bipeClique()  { tom(2000, 60); }
void bipeOk()      { tom(2400, 80); delay(70); tom(2400, 80); }
void bipeFalha()   { for (int i = 0; i < 3; i++) { tom(600, 120); delay(80); } }
void bipeComando() { tom(1800, 250); }

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

uint32_t lerEpochRtc() {                              // 0 = relogio parado/invalido/ausente
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
// O gateway manda a hora certa no ACK: se o DS1302 esta parado ou errado em mais de 2 s, acerta
void sincronizarRelogio(uint32_t horaGateway) {
  if (!TEM_DS1302 || horaGateway < 1700000000) return;
  uint32_t local = lerEpochRtc();
  long dif = (long)local - (long)horaGateway;
  if (local == 0 || dif > 2 || dif < -2) {
    acertarRtc(horaGateway);
    Serial.println("Relogio acertado pelo gateway");
  }
}

// ====================================================================
// Memoria (NVS): numero de sequencia e fila de pontos pendentes
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
// LoRa
// ====================================================================
void montarPacote(uint8_t* b, uint8_t tipo, uint16_t badge, uint16_t s, uint32_t hora, uint8_t flags) {
  b[0] = 1; b[1] = tipo;
  b[2] = badge & 0xFF; b[3] = badge >> 8;
  b[4] = s & 0xFF;     b[5] = s >> 8;
  for (int i = 0; i < 4; i++) b[6 + i] = (hora >> (8 * i)) & 0xFF;
  b[10] = flags;
}

void escutar() { chegouPacote = false; radio.startReceive(); }

// Espera ate ESPERA_ACK_MS por um ACK de ponto deste cracha e deste seq. Devolve a hora do gateway (>0) ou 0.
uint32_t esperarAck(uint16_t s) {
  unsigned long inicio = millis();
  while (millis() - inicio < ESPERA_ACK_MS) {
    if (chegouPacote) {
      chegouPacote = false;
      uint8_t b[32]; size_t len = radio.getPacketLength();
      if (len > sizeof(b)) len = sizeof(b);
      if (radio.readData(b, len) == RADIOLIB_ERR_NONE && len == 11 && b[0] == 1 && b[1] == 2 &&
          (b[2] | (b[3] << 8)) == ID_CRACHA && (b[4] | (b[5] << 8)) == s) {
        return b[6] | (b[7] << 8) | (b[8] << 16) | ((uint32_t)b[9] << 24);
      }
      radio.startReceive();
    }
    delay(5);
  }
  return 0;
}

// Uma rodada de ate 3 tentativas para UM ponto. true = o gateway confirmou.
bool enviarPonto(const Ponto& p) {
  uint8_t pacote[11];
  montarPacote(pacote, 1, ID_CRACHA, p.seq, p.hora, p.reenvio ? 1 : 0);
  for (int t = 1; t <= TENTATIVAS; t++) {
    digitalWrite(PINO_LED, HIGH);
    radio.transmit(pacote, 11);
    digitalWrite(PINO_LED, LOW);
    escutar();
    uint32_t hora = esperarAck(p.seq);
    if (hora) { sincronizarRelogio(hora); return true; }
    Serial.printf("sem ACK (seq %u, tentativa %d/%d)\n", p.seq, t, TENTATIVAS);
  }
  escutar();
  return false;
}

void avisar(const String& m, unsigned long ms) { mensagem = m; fimMensagem = millis() + ms; Serial.println(m); desenhar(); }

// Tenta esvaziar a fila (mais antigo primeiro). Para no primeiro que falhar. Devolve quantos foram confirmados.
int processarFila() {
  int enviados = 0;
  while (nFila > 0) {
    Ponto p = fila[0];
    avisar("enviando seq " + String(p.seq), 3000);
    if (!enviarPonto(p)) {
      if (!fila[0].reenvio) { fila[0].reenvio = 1; salvarFila(); }   // da proxima vez vai com a marca "fila"
      break;
    }
    tirarDaFila(0);
    enviados++;
  }
  ultimoReenvio = millis();
  return enviados;
}

// ====================================================================
// Clique do botao = um ponto remoto por LoRa
// ====================================================================
void aoClicar() {
  bipeClique();                                       // feedback imediato
  seqAtual = (seqAtual >= SEQ_MAX) ? 1 : seqAtual + 1;
  memoria.putUShort("seq", seqAtual);                 // grava ANTES de enviar: nunca reusa um numero
  if (nFila >= TAM_FILA) { avisar("FILA CHEIA: descartei o mais antigo", 4000); tirarDaFila(0); }
  fila[nFila++] = { seqAtual, lerEpochRtc(), 0 };     // hora 0 = sem relogio; o gateway carimba
  salvarFila();

  int ok = processarFila();
  if (nFila == 0 && ok > 0) { avisar("PONTO OK seq " + String(seqAtual), 4000); bipeOk(); }
  else                       { avisar("SEM ACK: na fila", 5000); bipeFalha(); }
}

bool botaoApertado() { return digitalRead(PINO_PRG) == LOW || digitalRead(PINO_BOTAO) == LOW; }

// ====================================================================
// Avisos do gateway (BIPE do painel, PONTO OK do botao do gateway)
// ====================================================================
void tratarPacoteRecebido() {
  uint8_t b[32]; size_t len = radio.getPacketLength();
  if (len > sizeof(b)) len = sizeof(b);
  int estado = radio.readData(b, len);
  escutar();
  if (estado != RADIOLIB_ERR_NONE || len != 11 || b[0] != 1 || b[1] != 3) return;      // so comandos
  if ((b[2] | (b[3] << 8)) != ID_CRACHA) return;
  uint16_t id = b[4] | (b[5] << 8);
  uint8_t codigo = b[6];
  bool repetido = (id == ultimoId && codigo == ultimoCodigo && millis() - quandoUltimoId < 2000);   // gateway manda 2x
  ultimoId = id; ultimoCodigo = codigo; quandoUltimoId = millis();

  if (codigo == 2) {                                   // PONTO OK (do botao do gateway)
    if (repetido) return;
    avisar("PONTO OK no gateway (seq " + String(id) + ")", 3000);
    bipeOk();
  } else if (codigo == 1) {                            // BIPE do painel: apita e confirma
    if (!repetido) { avisar("comando BIPE #" + String(id), 2000); bipeComando(); }
    uint8_t ack[11];
    montarPacote(ack, 4, ID_CRACHA, id, 0, 1);
    delay(150);                                        // o gateway precisa voltar a escutar
    radio.transmit(ack, 11);
    escutar();
  }
}

// ====================================================================
// BLE (so sinal; o contador de cliques fica em 0: quem clica pelo PC e o gateway/recipiente)
// ====================================================================
void anunciarBle() {
  BLEAdvertising* anuncio = BLEDevice::getAdvertising();
  TextoBle fab;
  fab += (char)0xFF; fab += (char)0xFF;
  fab += (char)1;
  fab += (char)(ID_CRACHA & 0xFF); fab += (char)(ID_CRACHA >> 8);
  fab += (char)idBoot;
  fab += (char)0; fab += (char)0;
  BLEAdvertisementData dados;
  dados.setFlags(0x06);
  dados.setName(NOME_BLE);
  dados.setManufacturerData(fab);
  anuncio->setAdvertisementData(dados);
  anuncio->setMinInterval(INTERVALO_BLE);
  anuncio->setMaxInterval(INTERVALO_BLE);
  anuncio->start();
}

// ====================================================================
void desenhar() {
  tela.clearBuffer();
  tela.setFont(u8g2_font_ncenB10_tr);
  tela.drawStr(0, 13, ("CRACHA " + String(ID_CRACHA)).c_str());
  char h[12] = "--:--:--";
  uint32_t e = lerEpochRtc();
  if (e) {
    uint32_t loc = e + FUSO_HORAS * 3600;
    snprintf(h, sizeof(h), "%02u:%02u:%02u", (unsigned)((loc % 86400) / 3600), (unsigned)((loc % 3600) / 60), (unsigned)(loc % 60));
  }
  tela.setFont(u8g2_font_logisoso16_tn);
  tela.drawStr(0, 36, h);
  tela.setFont(u8g2_font_6x10_tr);
  String linha = millis() < fimMensagem ? mensagem : String(nFila > 0 ? "aguardando envio" : "aperte o botao");
  tela.drawStr(0, 50, linha.c_str());
  tela.drawStr(0, 62, ("fila: " + String(nFila) + "  ultimo seq: " + String(seqAtual)).c_str());
  tela.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  pinMode(PINO_VEXT, OUTPUT); digitalWrite(PINO_VEXT, LOW); delay(100);
  pinMode(PINO_LED, OUTPUT);
  pinMode(PINO_BUZZER, OUTPUT); digitalWrite(PINO_BUZZER, LOW);
  pinMode(PINO_PRG, INPUT_PULLUP);
  pinMode(PINO_BOTAO, INPUT_PULLUP);
  tela.begin();

  if (TEM_DS1302) {
    pinMode(PINO_CLK, OUTPUT); pinMode(PINO_RST_RTC, OUTPUT); pinMode(PINO_DAT, INPUT);
    digitalWrite(PINO_CLK, LOW); digitalWrite(PINO_RST_RTC, LOW);
    delay(100);
  }
  memoria.begin("cracha", false);
  seqAtual = memoria.getUShort("seq", 0);
  if (seqAtual > SEQ_MAX) seqAtual = 0;               // protecao: o crachá nunca invade o espaco do gateway
  carregarFila();

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

  Serial.printf("Cracha %u pronto. seq=%u, fila=%d, relogio=%s\n", ID_CRACHA, seqAtual, nFila,
                lerEpochRtc() ? "ok" : "parado/ausente (hora 0: o gateway carimba)");
  tom(2000, 60);
  desenhar();
}

void loop() {
  // Botao com debounce de 50 ms; so conta ao APERTAR e de novo so depois de soltar
  if (botaoApertado()) {
    delay(50);
    if (botaoApertado()) { aoClicar(); while (botaoApertado()) delay(10); }
  }
  if (chegouPacote) { chegouPacote = false; tratarPacoteRecebido(); }

  // Fila pendente (sem alcance antes): tenta de novo a cada 30 s
  if (nFila > 0 && millis() - ultimoReenvio > REENVIO_FILA_MS) {
    int ok = processarFila();
    if (ok > 0) { avisar(String(ok) + " ponto(s) da fila enviado(s)", 4000); bipeOk(); }
  }
  if (millis() - ultimoDesenho > 1000) { ultimoDesenho = millis(); desenhar(); }
  delay(5);
}
