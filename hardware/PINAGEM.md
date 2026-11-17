# Pinagem — Heltec LoRa 32 V3 (crachá e gateway)

> Estado: pinos marcados **(testado)** vêm dos testes de protoboard 1, 2 e 5 (12/11/2026). Os marcados **(proposto)**
> ainda não foram ligados em protoboard: **confirmar no Teste 3 e no Teste 4** antes de fechar o layout da PCI.
> Conferir também no esquemático oficial da Heltec V3 (versão da placa: V3, não V3.2).

## Pinos já usados pela própria placa (NÃO ligar nada neles)

| GPIO | Uso na placa | Observação |
| --- | --- | --- |
| 0 | Botão PRG | Usado no teste como crachá; strapping de boot (não segurar ao ligar) |
| 8, 9, 10, 11, 12, 13, 14 | Rádio LoRa (SX1262): NSS, SCK, MOSI, MISO, RST, BUSY, DIO1 | Fixos na placa |
| 17, 18, 21 | OLED I2C: SDA, SCL, RST | Fixos na placa |
| 35 | LED branco | Usado nos testes (testado) |
| 36 | Vext (LOW liga o OLED e o conector Vext) | Usado nos testes (testado) |

## Componentes externos (a ligar na PCI)

| Componente | Pino do componente | GPIO | Situação | Observação |
| --- | --- | --- | --- | --- |
| DS1302 (relógio) | CLK | 40 | código do teste 3 pronto, não testado | alimentar em 3,3 V |
| DS1302 | DAT (I/O) | 41 | idem | |
| DS1302 | RST (CE) | 42 | idem | |
| DS1302 | VCC1 / VCC2 / GND | 3,3 V / bateria CR2032 / GND | | CR2032 ainda não chegou |
| Botão de ponto | um lado | **4** | **proposto** | `INPUT_PULLUP`; o outro lado vai ao GND; debounce de 50 ms no software |
| Buzzer | sinal | **5** | **proposto** | Se for ativo: GPIO → resistor 1 kΩ → base de NPN (2N2222/BC547), buzzer entre 3,3 V e coletor, diodo opcional; se passivo, usar `tone()`. Confirmar o tipo comprado |
| LED de status externo (opcional) | ânodo | 6 | **proposto**, opcional | Resistor de 330 Ω em série; o LED branco da placa (GPIO 35) já cumpre o papel |

Por que 4, 5 e 6: são pinos livres no conector da Heltec V3, sem função de boot (evita os de strapping 0, 3, 45 e 46)
e longe de LoRa, OLED e DS1302, o que simplifica o roteamento em face simples.

## Alimentação

- USB-C para teste. Na versão final, bateria LiPo no conector JST da placa (carregador a bordo), a confirmar com a equipe.
- 3,3 V para o DS1302 (pino 3V3 da placa). O buzzer com transistor pode usar 3,3 V.
- **Nunca ligar a placa sem a antena LoRa conectada.**

## Itens a confirmar antes de desenhar a PCI

1. Conferir na protoboard os GPIO 4, 5 (e 6): ligar botão e buzzer e rodar o Teste 4.
2. Tipo do buzzer (ativo ou passivo) e do botão (tátil de 4 pinos?).
3. Se a PCI será uma placa-mãe com barra fêmea 1×20 para encaixar a Heltec (conforme a lista de compras) ou só um módulo de apoio.
4. Pinos exatos de cada conector (J2/J3) da Heltec V3 no esquemático oficial.
