"""Teste 5 (lado do PC): escuta o cracha por Bluetooth e diz se ele esta PRESENTE ou AUSENTE.

Ainda NAO bloqueia a tela e NAO envia nada ao MQTT: so mede e mostra. Serve para
  1) provar que o Bluetooth do PC enxerga o cracha (compatibilidade BLE), e
  2) calibrar os limiares de distancia.

Instalar (uma vez):   pip install bleak
Rodar:                python teste_ble_rssi.py          (Ctrl+C para parar)

Regras do projeto (a calibrar):
  - RSSI = forca do sinal em dBm (negativo; mais perto de 0 = mais forte). Usamos a MEDIA das ultimas 5 leituras.
  - PRESENTE: media > -65 dBm por 3 s seguidos.
  - AUSENTE:  media < -75 dBm, ou crachá sumiu, por 8 s seguidos.
  - Entre -75 e -65 nao muda nada (zona de "folga", evita ficar alternando).
"""
import asyncio
import time
from collections import deque

from bleak import BleakScanner

PREFIXO = "UNIFESP-BADGE"   # o cracha anuncia "UNIFESP-BADGE-101"
LIMIAR_PRESENTE = -65       # dBm
LIMIAR_AUSENTE = -75        # dBm
SEGUNDOS_PRESENTE = 3
SEGUNDOS_AUSENTE = 8

ultimas = deque(maxlen=5)   # ultimas 5 leituras de RSSI
visto_em = 0.0              # quando ouvimos o cracha pela ultima vez
nome_visto = ""
estado = "AUSENTE"
desde_forte = None          # desde quando a media esta acima do limiar de presente
desde_fraco = None          # desde quando esta abaixo do limiar de ausente (ou sumiu)


def ao_detectar(dispositivo, anuncio):
    """Chamada toda vez que o PC ouve qualquer anuncio BLE; so guardamos os do cracha."""
    global visto_em, nome_visto
    nome = anuncio.local_name or dispositivo.name or ""
    if nome.startswith(PREFIXO):
        ultimas.append(anuncio.rssi)
        visto_em = time.time()
        nome_visto = nome


def avaliar(agora):
    """Decide PRESENTE/AUSENTE a partir da media e dos tempos. Retorna a media (ou None)."""
    global estado, desde_forte, desde_fraco
    sumido = (agora - visto_em) > 2 or not ultimas
    media = None if sumido else sum(ultimas) / len(ultimas)

    forte = media is not None and media > LIMIAR_PRESENTE
    fraco = media is None or media < LIMIAR_AUSENTE
    desde_forte = (desde_forte or agora) if forte else None
    desde_fraco = (desde_fraco or agora) if fraco else None

    if estado == "AUSENTE" and desde_forte and agora - desde_forte >= SEGUNDOS_PRESENTE:
        estado = "PRESENTE"
        print(f"\n>>> {nome_visto} -> PRESENTE")
    elif estado == "PRESENTE" and desde_fraco and agora - desde_fraco >= SEGUNDOS_AUSENTE:
        estado = "AUSENTE"
        print("\n>>> crachá -> AUSENTE")
    return media


async def principal():
    print(f"Procurando '{PREFIXO}...'. Ctrl+C para parar.\n")
    async with BleakScanner(ao_detectar):
        while True:
            await asyncio.sleep(0.5)
            media = avaliar(time.time())
            texto = "sem sinal" if media is None else f"RSSI medio {media:6.1f} dBm"
            print(f"\r{texto:<24} estado: {estado:<9}", end="", flush=True)


if __name__ == "__main__":
    try:
        asyncio.run(principal())
    except KeyboardInterrupt:
        print("\nEncerrado.")
