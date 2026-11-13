# Fluxo do botão, do BLE e do bloqueio do PC (versão 2, 12/11/2026)

> Este documento descreve o esquema **novo**, que substituiu o "bloqueio por média de RSSI" (versão 1).
> O porquê da mudança está em `docs/DIARIO_DE_BORDO.md` (seção "Problemas").

## 1. A ideia em uma frase
O botão do crachá é o **"cartão passado no leitor"**: apertar o botão desbloqueia o PC se o crachá estiver
perto; depois disso, o PC só bloqueia de novo se o crachá se afastar.

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

## 2. Passo a passo do usuário
1. Funcionário chega. O PC está **bloqueado** (tela cheia "BLOQUEADO").
2. Aperta o botão do crachá. O crachá passa a anunciar mais rápido e muda um contador no anúncio.
3. O PC vê o contador mudar e mede o RSSI daquele pacote. Se for maior que `--liberar` (−60 dBm), **libera**.
   Se for fraco, mostra "Clique ignorado: crachá longe" e continua bloqueado.
4. Com o PC liberado, o agente **monitora**. Se o melhor RSSI dos últimos 8 s ficar abaixo de −70 dBm por 10 s
   seguidos (ou o crachá sumir), aparece o aviso **"Crachá não detectado. Bloqueando em 5 s"**.
5. Se o sinal voltar (ou um clique forte chegar) durante o aviso, ele some. Se não, o PC **bloqueia**.
6. Apertar o botão com o PC já liberado **não faz nada no PC** (só registra o ponto pelo LoRa).

## 3. Máquina de estados do agente

```text
          (início, a menos de --iniciar-liberado)
                         │
                         ▼
                  ┌─────────────┐   clique com RSSI > --liberar   ┌─────────────┐
                  │  BLOQUEADO  │ ───────────────────────────────►│  LIBERADO   │
                  │ (tela cheia)│                                  │ (monitora)  │
                  └─────────────┘◄─────────────────────────────── └─────────────┘
                         ▲        pico RSSI < --manter por                │
                         │        --seg-ausente + aviso de --aviso s      │
                         │                                                │
              clique fraco: só mostra mensagem                  sinal volta: cancela o aviso
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
| `--liberar` | −60 dBm | clique só vale se o RSSI dele for maior (≈ 2,5 m na mesa; o usuário está com a mão no crachá) |
| `--manter` | −70 dBm | liberado: "longe" = pico da janela abaixo disto |
| `--janela` | 8 s | janela do pico (ignora quedas rápidas por obstrução) |
| `--seg-ausente` | 10 s | tempo "longe" seguido antes do aviso |
| `--aviso` | 5 s | contagem regressiva antes de bloquear |
| `--iniciar-liberado` | desligado | começa liberado (para desenvolver sem se trancar) |
| `--mqtt IP` | desligado | publica `liberado`/`bloqueado` em `unifesp/ponto/pc/<badge>/status` (só renova o `ultimo_contato` do PC) |

> **Atenção à meta "afastar → bloqueio ≤ 10 s" do PDF:** com os padrões acima o bloqueio leva até
> ≈ 8 s (janela) + 10 s + 5 s. É a política aprovada em 12/11/2026 (prioriza não bloquear por obstrução).
> Para encurtar: `--janela 4 --seg-ausente 3 --aviso 3` (≈ 10 s). **Medir e registrar** no diário.

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
3. Aperte PRG perto do PC: deve liberar em ≈ 1 s. Aperte longe (> 3 m): "Clique ignorado".
4. Liberado, **tape o crachá com a mão/corpo**: **não** deve bloquear de imediato (pico da janela).
5. Afaste o crachá (outra sala): aviso → bloqueio. Anote os tempos no diário.
6. Para só ver números: `python pc-agent/teste_ble_rssi.py`.
