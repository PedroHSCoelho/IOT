// Teste 3 - Ler e acertar o relogio DS1302 (RTC) com a Heltec LoRa 32 V3
// Objetivo: provar que o crachá sabe que horas sao, mesmo desligado (o modulo tem uma bateria CR2032).
//
// Analogia: o DS1302 e um "relogio de pulso" que fica ao lado do ESP32. O ESP32 pergunta as horas
// por 3 fios (CLK = batida de ritmo, DAT = dados, RST = "vou falar com voce agora").
// Nao usamos biblioteca: o codigo abaixo fala o "idioma" do chip direto, e assim nao ha o que instalar.
//
// LIGACAO (placa desligada ao mexer nos fios!):
//   DS1302 VCC -> 3V3 da Heltec     DS1302 GND -> GND
//   DS1302 CLK -> GPIO 40           DS1302 DAT (I/O) -> GPIO 41        DS1302 RST (CE) -> GPIO 42
//   Bateria CR2032 colocada no modulo.  NAO ligue o VCC em 5V.
//
// COMO USAR (2 etapas):
//   1) ACERTAR_RELOGIO = true  -> grava, e o relogio recebe a hora do COMPUTADOR no momento da compilacao.
//   2) Mude para false, grave de novo. Agora ele so LE. Desligue a USB por 1 minuto e religue:
//      a hora deve continuar certa (prova que a bateria funciona).
//
// Biblioteca necessaria: "U8g2". Placa: "WiFi LoRa 32(V3)" ou "ESP32S3 Dev Module".

#include <U8g2lib.h>

const bool ACERTAR_RELOGIO = true;   // <<< true so na primeira gravacao; depois false

const int PINO_CLK = 40, PINO_DAT = 41, PINO_RST = 42;
const int PINO_VEXT = 36;            // energia do OLED (LOW = ligado)
const int PINO_LED  = 35;

U8G2_SSD1306_128X64_NONAME_F_SW_I2C tela(U8G2_R0, /*clock=*/18, /*data=*/17, /*reset=*/21);

// ---------- Conversa de baixo nivel com o DS1302 ----------
void enviarByte(uint8_t b) {                 // manda 8 bits, o menos importante primeiro
  pinMode(PINO_DAT, OUTPUT);
  for (int i = 0; i < 8; i++) {
    digitalWrite(PINO_DAT, (b >> i) & 1);
    digitalWrite(PINO_CLK, HIGH); delayMicroseconds(2);
    digitalWrite(PINO_CLK, LOW);  delayMicroseconds(2);
  }
}

uint8_t receberByte() {                      // le 8 bits (o chip poe cada bit no fio na batida do CLK)
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
  digitalWrite(PINO_RST, HIGH); delayMicroseconds(4);
  enviarByte(0x81 | (reg << 1));             // 0x81 = "quero LER"
  uint8_t v = receberByte();
  digitalWrite(PINO_RST, LOW); delayMicroseconds(4);
  return v;
}

void escreverRegistro(uint8_t reg, uint8_t valor) {
  digitalWrite(PINO_RST, HIGH); delayMicroseconds(4);
  enviarByte(0x80 | (reg << 1));             // 0x80 = "quero ESCREVER"
  enviarByte(valor);
  digitalWrite(PINO_RST, LOW); delayMicroseconds(4);
}

// O chip guarda numeros em BCD (cada digito em 4 bits): 45 vira 0x45. Estas funcoes convertem.
uint8_t deBCD(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
uint8_t paraBCD(uint8_t n) { return ((n / 10) << 4) | (n % 10); }

// ---------- Hora ----------
struct Hora { int ano, mes, dia, h, m, s; };

// Registros: 0 seg, 1 min, 2 hora, 3 dia, 4 mes, 5 dia da semana, 6 ano, 7 protecao de escrita
bool lerHora(Hora &t) {
  uint8_t seg = lerRegistro(0);
  if (seg & 0x80) return false;              // bit 7 = "relogio parado" (sem bateria / nunca acertado)
  t.s = deBCD(seg & 0x7F);
  t.m = deBCD(lerRegistro(1) & 0x7F);
  t.h = deBCD(lerRegistro(2) & 0x3F);
  t.dia = deBCD(lerRegistro(3) & 0x3F);
  t.mes = deBCD(lerRegistro(4) & 0x1F);
  t.ano = 2000 + deBCD(lerRegistro(6));
  return true;
}

void acertarHora(const Hora &t) {
  escreverRegistro(7, 0x00);                 // tira a protecao de escrita
  escreverRegistro(0, 0x80);                 // para o relogio enquanto acerta
  escreverRegistro(1, paraBCD(t.m));
  escreverRegistro(2, paraBCD(t.h));         // formato 24 h
  escreverRegistro(3, paraBCD(t.dia));
  escreverRegistro(4, paraBCD(t.mes));
  escreverRegistro(5, 1);                    // dia da semana nao usamos
  escreverRegistro(6, paraBCD(t.ano - 2000));
  escreverRegistro(0, paraBCD(t.s));         // bit 7 = 0 -> relogio volta a andar
}

// Le __DATE__ ("Nov 12 2026") e __TIME__ ("14:30:05") do momento da compilacao
Hora horaDaCompilacao() {
  const char* meses = "JanFebMarAprMayJunJulAugSepOctNovDec";
  Hora t;
  char nome[4] = {0};
  sscanf(__DATE__, "%3s %d %d", nome, &t.dia, &t.ano);
  t.mes = (strstr(meses, nome) - meses) / 3 + 1;
  sscanf(__TIME__, "%d:%d:%d", &t.h, &t.m, &t.s);
  t.s += 8;                                  // compensa ~8 s entre compilar e terminar de gravar
  if (t.s > 59) t.s = 59;
  return t;
}

void setup() {
  Serial.begin(115200);
  pinMode(PINO_VEXT, OUTPUT); digitalWrite(PINO_VEXT, LOW); delay(100);
  pinMode(PINO_LED, OUTPUT);
  tela.begin();
  pinMode(PINO_CLK, OUTPUT); pinMode(PINO_RST, OUTPUT); pinMode(PINO_DAT, INPUT);
  digitalWrite(PINO_CLK, LOW); digitalWrite(PINO_RST, LOW);
  delay(200);

  if (ACERTAR_RELOGIO) {
    acertarHora(horaDaCompilacao());
    Serial.println("Relogio ACERTADO com a hora da compilacao. Agora mude ACERTAR_RELOGIO para false e grave de novo.");
  }
}

void loop() {
  Hora t;
  char l1[24], l2[24];
  tela.clearBuffer();
  tela.setFont(u8g2_font_ncenB10_tr);

  if (lerHora(t) && t.mes >= 1 && t.mes <= 12 && t.dia >= 1 && t.dia <= 31 && t.h < 24 && t.m < 60) {
    snprintf(l1, sizeof(l1), "%02d:%02d:%02d", t.h, t.m, t.s);
    snprintf(l2, sizeof(l2), "%02d/%02d/%04d", t.dia, t.mes, t.ano);
    tela.drawStr(0, 14, "DS1302 OK");
    tela.setFont(u8g2_font_logisoso20_tn);
    tela.drawStr(0, 44, l1);
    tela.setFont(u8g2_font_ncenB10_tr);
    tela.drawStr(0, 62, l2);
    Serial.printf("%s  %s\n", l2, l1);
    digitalWrite(PINO_LED, t.s % 2);         // LED pisca no ritmo dos segundos
  } else {
    tela.drawStr(0, 14, "DS1302 ERRO");
    tela.drawStr(0, 34, "parado ou sem");
    tela.drawStr(0, 50, "resposta (fios?)");
    Serial.println("ERRO: relogio parado ou nao responde. Confira fios, VCC 3V3 e a bateria.");
    digitalWrite(PINO_LED, LOW);
  }
  tela.sendBuffer();
  delay(500);
}
