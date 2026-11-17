# Fluxo do botão, do BLE e do bloqueio do PC (versão 3, 17/11/2026)

> Este documento descreve o esquema **novo**, que substituiu o "bloqueio por média de RSSI" (versão 1).
> O porquê da mudança está em `docs/DIARIO_DE_BORDO.md` (seção "Problemas").

## 1. A ideia em uma frase
O **sinal BLE** controla a tela sozinho (chegou perto → desbloqueia; saiu → bloqueia). O **botão** "prende" o PC
liberado enquanto o crachá está no porta-crachá, e o próximo clique devolve o controle ao sinal.

Dois caminhos independentes saem do mesmo clique:

```text
                         ┌── BLE (anúncio com contador) ──► Agente Python no PC ──► libera / bloqueia a tela
 Clique no botão ────────┤
 do crachá               └── LoRa (pacote de 11 bytes) ──► Gateway ──► MQTT ──► Node-RED ──► Banco ──► Painel
                                                                                          (registra o PONTO)
```

- **Desbloquear o PC** = BLE (rápido, local, funciona sem rede nem banco).
- **Registrar o ponto** (CHEGADA/SAÍDA) = LoRa → gateway → banco (como já estava planejado).
- O PC **nunca registra ponto**. Ele só libera e bloqueia a tela.

## 2. Passo a passo do usuário (versão 3, decisão de 17/11/2026)
1. **Modo automático (sem clicar):** o PC está bloqueado. O usuário **chega perto** (pico do RSSI > `--liberar`,
   −60 dBm) e o PC **desbloqueia sozinho**. Se **sai de perto** (pico < `--manter`, −70 dBm, por `--seg-ausente` = 5 s),
   aparece o aviso **"Bloqueando em 5 s"** e o PC **bloqueia**.
2. **1º clique** = crachá colocado no porta-crachá. O PC **fica desbloqueado até o 2º clique**, mesmo longe, sem
   sinal ou com sinal fraco (o BLE não é medido). O clique nunca é ignorado.
3. **2º clique** = crachá tirado do porta-crachá. Volta ao **modo automático**: desbloqueia e bloqueia conforme o sinal.
4. O 3º clique repete o 1º, e assim por diante: **cada clique alterna FIXO ↔ AUTOMÁTICO**.
5. O ponto (CHEGADA/SAÍDA) continua indo pelo LoRa; o PC nunca registra ponto.

Isto substitui as regras de 12/11 e do começo de 17/11 ("o botão desbloqueia", "2º clique arma"). **Aviso:** o
desbloqueio automático por proximidade foi o que falhou na v1 (P13/P14); a v3 usa o **pico** e a **histerese**
(−60 para desbloquear, −70 para bloquear) para reduzir o problema. **A validar na prática.**

## 3. Máquina de estados do agente (v3)

```text
 BLOQUEADO --pacote > --liberar (chegou perto)--> AUTO
 BLOQUEADO --clique--> FIXO              AUTO --clique--> FIXO
 AUTO --pico < --manter por --seg-ausente + aviso--> BLOQUEADO
 FIXO --clique--> AUTO (sem vigiar nem bloquear enquanto estiver em FIXO)
```

## 4. O anúncio BLE do crachá (firmware `firmware/testes/05_ble_anuncio`)
Nome `UNIFESP-BADGE` + dados do fabricante (id de empresa genérico `0xFFFF`), 28 de 31 bytes:

| Byte | Conteúdo |
| --- | --- |
| 0 | versão do formato (`1`) |
| 1–2 | número do crachá (ex.: 101), little-endian |
| 3 | `boot`: número aleatório sorteado a cada vez que o crachá liga |
| 4–5 | contador de cliques, little-endian |

- **Repouso:** anuncia a cada 500 ms. **Após um clique:** a cada 100 ms por 5 s (acha o clique rápido).
- **`boot`:** se o crachá reiniciar, o contador volta a 0; o `boot` novo avisa o PC para só **ressincronizar**,
  sem tratar como clique. Mesmo motivo para o agente ignorar o contador que encontra ao iniciar.
- O RSSI é medido **no pacote do clique** (e nos que chegam no 1 s seguinte, valendo o melhor). Não há média.

## 5. Parâmetros do agente (`pc-agent/agente_bloqueio.py`)

| Opção | Padrão | Significado |
| --- | --- | --- |
| `--liberar` | −60 dBm | automático: bloqueado desbloqueia quando chega um pacote mais forte que isto |
| `--manter` | −70 dBm | automático liberado: "longe" = pico da janela abaixo disto |
| `--janela` | 8 s | janela do pico (ignora quedas rápidas por obstrução) |
| `--seg-ausente` | 5 s | tempo "longe" seguido antes do aviso |
| `--aviso` | 5 s | contagem regressiva antes de bloquear |
| `--iniciar-liberado` | desligado | começa liberado (para desenvolver sem se trancar) |
| `--mqtt IP` | desligado | publica `liberado`/`bloqueado` em `unifesp/ponto/pc/<badge>/status` (só renova o `ultimo_contato` do PC) |

> **Atenção à meta "afastar → bloqueio ≤ 10 s" do PDF:** com os padrões acima o bloqueio leva até
> ≈ 8 s (janela) + 5 s + 5 s (aviso). É a política aprovada em 12/11/2026 (prioriza não bloquear por obstrução).
> Para encurtar: `--janela 3 --seg-ausente 3 --aviso 3` (≈ 9 s). **Medir e registrar** no diário.

## 6. Relação com o banco (nada muda nas tabelas)
- A origem `ble` (`PRESENTE` = CHEGADA, `AUSENTE` = SAÍDA, regra R3) **continua existindo** no banco e no fluxo,
  mas **o agente não a publica mais**. Motivo: cada bloqueio por distância (ex.: ir ao banheiro) viraria uma SAÍDA.
- O ponto vem **só do botão** (`origem:"botao"`); o banco alterna CHEGADA/SAÍDA (R3) e aplica R4.
- O que o agente pode publicar é o status do PC, tópico `unifesp/ponto/pc/<badge>/status`
  (ex.: `{"estado":"liberado","rssi":-52}`), que só renova `dispositivo.ultimo_contato` do `pc-lab-01`.

## 7. Limitações conhecidas (dizer na apresentação)
- **Não é segurança real.** O Windows não permite desbloquear por programa; a "tela de bloqueio" é uma janela
  própria em tela cheia. Ctrl+Alt+Del, outro usuário ou matar o processo contornam. Só cobre o monitor principal.
- **Sem autenticação do anúncio.** Outro ESP32 poderia copiar nome, id e contador. Melhoria futura: assinar o
  contador com HMAC (segredo compartilhado) e rejeitar contador repetido.
- **RSSI varia** com corpo, parede, orientação da antena e ruído de Wi-Fi 2,4 GHz. Os limiares precisam ser
  calibrados com o crachá **no corpo** (ver diário, tabela de medições).
- O botão definitivo ainda não chegou: nos testes, o botão é o **PRG** da Heltec (GPIO 0).

## 8. Como testar (sem rede, sem banco)
1. Heltec com `firmware/testes/05_ble_anuncio` gravado (antena conectada), ligada na USB.
2. No PC: `pip install bleak` e `python pc-agent/agente_bloqueio.py` (tela bloqueia; ESC sai).
3. **Automático:** com a tela bloqueada, **aproxime** o crachá (< 1 m): deve desbloquear sozinho. Anote o tempo.
4. **Afastar sem clicar** (outra sala): aviso → bloqueio. Cronometre (meta ≤ 10 s). Aproxime de novo: desbloqueia.
5. **1º clique** (PRG): modo FIXO. Afaste o crachá por mais de 20 s: **não** deve bloquear.
6. **2º clique**: volta ao automático. Afaste: aviso → bloqueio. Aproxime: desbloqueia.
7. **Tapar o crachá** com a mão/corpo, perto, no modo automático: não deve bloquear de imediato nem ficar piscando.
8. Para só ver números: `python pc-agent/teste_ble_rssi.py`.
