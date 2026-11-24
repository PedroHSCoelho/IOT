// CRACHA INTELIGENTE DE PONTO - firmware FINAL do cracha (Heltec LoRa 32 V3)
//
// Junta tudo o que foi validado nos testes 01 a 07:
//   - Botao (GPIO 4, ou PRG) com debounce de 50 ms: UM clique = UM ponto.
//   - Relogio DS1302 (CLK 40, DAT 41, RST 42): carimba a hora do clique, mesmo sem rede.
//   - LoRa 915 MHz: manda o pacote ao gateway e espera o ACK (2 s, ate 3 tentativas).
//   - Fila: se nao houver ACK, o ponto fica guardado na memoria (sobrevive a desligar) e e
//     reenviado a cada 30 s ate o gateway confirmar. Nenhum ponto se perde.
//   - BLE: anuncia "UNIFESP-BADGE" com o numero do cracha e um CONTADOR DE CLIQUES. O agente do
//     PC mede o sinal (distancia) e percebe o clique pelo contador (ver docs/FLUXO_BLE_E_BLOQUEIO.md).
//   - Buzzer (GPIO 5) e LED: bipe curto ao clicar, bipe duplo = ponto confirmado, 3 graves = sem ACK.
//   - OLED: hora, estado e quantidade de pontos na fila.
//   - Comando BIPE vindo do painel (gateway -> LoRa): toca o buzzer e responde com um ACK de comando.
//
// Analogia: o cracha e um "carteiro com caderno". Anota o ponto no caderno (fila), tenta entregar
// ao gateway, e so risca da lista quando recebe o "recebi" (ACK).
//
// Pacote LoRa de 11 bytes (igual ao gateway):
//   [0]=versao(1) [1]=tipo [2-3]=badge [4-5]=seq (ou id do comando) [6-9]=hora Unix UTC [10]=flags
//   tipo 1 = ponto (cracha->gateway)   flags bit0 = veio da fila (reenvio)
//   tipo 2 = ACK do ponto (gateway->cracha, [6-9] = hora certa do gateway)
//   tipo 3 = comando (gateway->cracha)  [4-5]=id do comando  [6]=codigo (1 = BIPE)
//   tipo 4 = ACK do comando (cracha->gateway)  [4-5]=id  [6]=1 ok
//
// Placa: "WiFi LoRa 32(V3)" (pacote Heltec ESP32) ou "ESP32S3 Dev Module".
// Bibliotecas: "RadioLib" 8.x e "U8g2". (BLE e Preferences ja vem com a placa.)
// ATENCAO: NUNCA ligue a Heltec sem a antena LoRa conectada.

#include <RadioLib.h>
#include <U8g2lib.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>

// ============================ CONFIGURACAO ============================
const uint16_t ID_CRACHA = 101;            // numero do cracha (101 = o da demo; 102 = de teste)
const char* NOME_BLE = "UNIFESP-BADGE";    // o agente do PC procura por este nome
const bool BUZZER_PASSIVO = false;         // false = buzzer ATIVO (so liga/desliga); true = passivo (precisa de onda)
const bool TEM_DS1302 = true;              // false = sem relogio: a hora vai 0 e o gateway carimba (NTP)

// LoRa (identico ao teste 2 e ao gateway)
const uint32_t FREQ_HZ = 915000000, LARGURA_HZ = 125000;
const int SF = 7, CR = 5, SYNC_WORD = 0x12, POTENCIA = 14;

// Pinos
const int PINO_BOTAO = 4;                  // botao externo: um lado no GPIO 4, outro no GND
const int PINO_PRG = 0;                    // botao PRG da placa (tambem vale como clique)
const int PINO_BUZZER = 5;
const int PINO_VEXT = 36, PINO_LED = 35;   // Vext LOW liga o OLED
const int PINO_CLK = 40, PINO_DAT = 41, PINO_RST_RTC = 42;
const int LORA_NSS = 8, LORA_SCK = 9, LORA_MOSI = 10, LORA_MISO = 11;
const int LORA_RST = 12, LORA_BUSY = 13, LORA_DIO1 = 14;

// Tempos
const unsigned long ESPERA_ACK_MS = 2000;       // cada tentativa espera 2 s
const int TENTATIVAS = 3;
const unsigned long REENVIO_FILA_MS = 30000;    // fila: tenta de novo a cada 30 s
const uint16_t INTERVALO_LENTO = 800;           // BLE: 800 x 0,625 ms = 500 ms
const uint16_t INTERVALO_RAPIDO = 160;          // BLE: 100 ms logo apos um clique
const unsigned long DURACAO_RAPIDO_MS = 5000;
const int FUSO_HORAS = -3;                      // Brasilia (so para MOSTRAR; o banco usa UTC)
const int TAM_FILA = 16;                        // maximo de pontos guardados
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

// ---- estado ----
struct Ponto { uint16_t seq; uint32_t hora; uint8_t reenvio; };   // reenvio=1 quando ja falhou antes
Ponto fila[TAM_FILA];
int nFila = 0;
uint16_t seqAtual = 0;
uint8_t idBoot;
uint16_t cliques = 0;
bool bleRapido = false;
unsigned long fimRapido = 0, ultimoReenvio = 0, ultimoDesenho = 0, fimMensagem = 0;
String mensagem = "";
bool rtcOk = false;

// ====================================================================
// Buzzer e LED
// ====================================================================
void tom(int freq, int ms) {
  if (!BUZZER_PASSIVO) {                       // ativo: HIGH = apita (frequencia fixa do proprio buzzer)
    digitalWrite(PINO_BUZZER, HIGH); delay(ms); digitalWrite(PINO_BUZZER, LOW);
    return;
  }
  unsigned long meio = 500000UL / freq, fim = millis() + ms;   // passivo: onda quadrada "na mao"
  while (millis() < fim) {
    digitalWrite(PINO_BUZZER, HIGH); delayMicroseconds(meio);
    digitalWrite(PINO_BUZZER, LOW);  delayMicroseconds(meio);
  }
}
void bipeClique()   { tom(2000, 60); }
void bipeOk()       { tom(2400, 80); delay(70); tom(2400, 80); }          // ponto confirmado
void bipeFalha()    { for (int i = 0; i < 3; i++) { tom(600, 120); delay(80); } }
void bipeComando()  { tom(1800, 250); }

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

// Conversao data <-> dias desde 1970 (algoritmo de Howard Hinnant; evita depender do fuso do sistema)
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

// Devolve a hora Unix (UTC) do DS1302, ou 0 se o relogio esta parado/invalido
uint32_t lerEpochRtc() {
  if (!TEM_DS1302) return 0;
  uint8_t seg = lerRegistro(0);
  if (seg & 0x80) return 0;                           // bit 7 = relogio parado (sem bateria / nunca acertado)
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
  escreverRegistro(7, 0x00);                          // tira a protecao de escrita
  escreverRegistro(0, 0x80);                          // para o relogio enquanto acerta
  escreverRegistro(1, paraBCD((resto % 3600) / 60));
  escreverRegistro(2, paraBCD(resto / 3600));
  escreverRegistro(3, paraBCD(d));
  escreverRegistro(4, paraBCD(mo));
  escreverRegistro(5, 1);
  escreverRegistro(6, paraBCD(a - 2000));
  escreverRegistro(0, paraBCD(resto % 60));           // relogio volta a andar
}

// ====================================================================
// Memoria (NVS): numero de sequencia e fila de pontos pendentes
// ====================================================================
void salvarFila() {
  memoria.putUChar("nfila", nFila);
  memoria.putBytes("fila", fila, sizeof(Ponto) * nFila);
}
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

// Espera ate ESPERA_ACK_MS por um ACK deste cracha e deste seq. Devolve a hora do gateway (>0) ou 0.
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

// Se o gateway mandou a hora certa e o relogio esta parado ou errado em mais de 2 s, acerta o DS1302
void sincronizarRelogio(uint32_t horaGateway) {
  if (!TEM_DS1302 || horaGateway < 1700000000) return;
  uint32_t local = lerEpochRtc();
  long dif = (long)local - (long)horaGateway;
  if (local == 0 || dif > 2 || dif < -2) {
    acertarRtc(horaGateway);
    Serial.printf("Relogio acertado pelo gateway (diferenca era %ld s)\n", local == 0 ? 0L : dif);
  }
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

// Tenta esvaziar a fila, do mais antigo para o mais novo. Para no primeiro que falhar.
// Devolve quantos foram confirmados.
int processarFila() {
  int enviados = 0;
  while (nFila > 0) {
    Ponto p = fila[0];
    mensagem = "enviando seq " + String(p.seq);
    desenhar();
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
// BLE (igual ao teste 5 v2: contador de cliques no anuncio)
// ====================================================================
void anunciar(uint16_t intervalo) {
  BLEAdvertising* anuncio = BLEDevice::getAdvertising();
  anuncio->stop();
  TextoBle fab;
  fab += (char)0xFF; fab += (char)0xFF;               // id de empresa generico
  fab += (char)1;                                     // versao do formato
  fab += (char)(ID_CRACHA & 0xFF); fab += (char)(ID_CRACHA >> 8);
  fab += (char)idBoot;                                // muda a cada ligada (reinicio != clique)
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
  String linha = millis() < fimMensagem ? mensagem : String(nFila > 0 ? "aguardando envio" : "pronto");
  tela.drawStr(0, 50, linha.c_str());
  tela.drawStr(0, 62, ("fila: " + String(nFila) + "  cliques: " + String(cliques)).c_str());
  tela.sendBuffer();
}

void avisar(const String& m, unsigned long ms) {   // (sem valor padrao: o Arduino IDE gera prototipos e isso da erro)
  mensagem = m; fimMensagem = millis() + ms;
  Serial.println(m);
  desenhar();
}

// ====================================================================
// Clique do botao = um ponto
// ====================================================================
void aoClicar() {
  bipeClique();
  cliques++;                                          // o PC percebe este numero mudando (BLE)
  bleRapido = true; fimRapido = millis() + DURACAO_RAPIDO_MS;
  anunciar(INTERVALO_RAPIDO);

  seqAtual++;
  memoria.putUShort("seq", seqAtual);                 // grava ANTES de enviar: nunca reusa um numero
  Ponto p = { seqAtual, lerEpochRtc(), 0 };           // hora 0 = sem relogio; o gateway carimba
  if (nFila >= TAM_FILA) {                            // fila cheia: descarta o mais antigo (avisando)
    avisar("FILA CHEIA: descartei o mais antigo", 4000);
    tirarDaFila(0);
  }
  fila[nFila++] = p;
  salvarFila();

  avisar("enviando seq " + String(p.seq), 1500);
  int ok = processarFila();
  if (nFila == 0 && ok > 0) { avisar("PONTO OK seq " + String(p.seq), 4000); bipeOk(); }
  else                       { avisar("SEM ACK: guardado na fila", 5000); bipeFalha(); }
}

bool botaoApertado() { return digitalRead(PINO_BOTAO) == LOW || digitalRead(PINO_PRG) == LOW; }

// ====================================================================
// Comandos vindos do gateway (painel -> MQTT -> gateway -> LoRa)
// ====================================================================
void tratarPacoteRecebido() {
  uint8_t b[32]; size_t len = radio.getPacketLength();
  if (len > sizeof(b)) len = sizeof(b);
  int estado = radio.readData(b, len);
  escutar();
  if (estado != RADIOLIB_ERR_NONE || len != 11 || b[0] != 1 || b[1] != 3) return;     // so comandos
  if ((b[2] | (b[3] << 8)) != ID_CRACHA) return;
  uint16_t idCmd = b[4] | (b[5] << 8);
  if (b[6] == 1) {                                                                       // 1 = BIPE
    avisar("comando BIPE #" + String(idCmd), 2000);
    bipeComando();
    uint8_t ack[11];
    montarPacote(ack, 4, ID_CRACHA, idCmd, 0, 1);
    delay(150);                                       // o gateway precisa voltar a escutar
    radio.transmit(ack, 11);
    escutar();
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

  if (TEM_DS1302) {
    pinMode(PINO_CLK, OUTPUT); pinMode(PINO_RST_RTC, OUTPUT); pinMode(PINO_DAT, INPUT);
    digitalWrite(PINO_CLK, LOW); digitalWrite(PINO_RST_RTC, LOW);
    delay(100);
  }

  memoria.begin("cracha", false);
  seqAtual = memoria.getUShort("seq", 0);
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
  anunciar(INTERVALO_LENTO);

  Serial.printf("Cracha %u pronto. seq=%u, fila=%d, relogio=%s\n", ID_CRACHA, seqAtual, nFila,
                lerEpochRtc() ? "ok" : "parado (sera acertado pelo gateway)");
  bipeClique();
  desenhar();
}

void loop() {
  // Botao com debounce de 50 ms; so conta ao APERTAR e so de novo depois de soltar
  if (botaoApertado()) {
    delay(50);
    if (botaoApertado()) {
      aoClicar();
      while (botaoApertado()) delay(10);
    }
  }

  if (chegouPacote) { chegouPacote = false; tratarPacoteRecebido(); }

  if (bleRapido && millis() > fimRapido) { bleRapido = false; anunciar(INTERVALO_LENTO); }

  // Fila pendente: tenta de novo a cada 30 s
  if (nFila > 0 && millis() - ultimoReenvio > REENVIO_FILA_MS) {
    int ok = processarFila();
    if (ok > 0) { avisar(String(ok) + " ponto(s) da fila enviado(s)", 4000); bipeOk(); }
  }

  if (millis() - ultimoDesenho > 1000) { ultimoDesenho = millis(); desenhar(); }
  delay(5);
}
