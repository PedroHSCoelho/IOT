"""Agente do PC (versao 1): cobre a tela quando o cracha se afasta e libera quando ele volta.

Como funciona (analogia: uma "coleira eletronica"):
  - O cracha "grita" por Bluetooth (BLE) "UNIFESP-BADGE-101" (firmware teste 5).
  - Este programa mede a forca do sinal (RSSI, em dBm; mais perto de 0 = mais forte = mais perto).
  - Cracha longe (mais de ~1 m) -> aparece uma tela cheia "BLOQUEADO" cobrindo tudo.
  - Cracha perto de novo -> a tela some sozinha (o "desbloqueio").

ISTO E UM BLOQUEIO DE DEMONSTRACAO, nao de seguranca: o Windows nao deixa programas comuns
desbloquear a tela de verdade, entao desenhamos uma tela propria por cima de tudo. Ctrl+Alt+Del ou
outro usuario conseguem contornar. Diga isso na apresentacao.

SAIDA DE EMERGENCIA: com a tela de bloqueio aparecendo, aperte ESC para encerrar o agente.
(Em Ctrl+C no terminal tambem encerra.)

Instalar (uma vez):   pip install bleak        (o tkinter ja vem com o Python do Windows)
Rodar:                python agente_bloqueio.py
Ajustar distancia:    python agente_bloqueio.py --ausente -55 --presente -52   (mais negativo = mais longe)

Calibracao medida em 12/11/2026 (cracha sobre a mesa, sem corpo):
  0,5 m = -41 dBm | 1 m = -48 | 1,5 m = -52 | 2 m = -56,5
  Por isso: bloqueia quando a media fica abaixo de -50 (um pouco alem de 1 m) e so libera acima de -47
  (a diferenca entre os dois evita a tela ficar "piscando" na fronteira). Com o cracha NO CORPO o sinal
  cai 5 a 10 dB: se bloquear cedo demais, use --ausente -58 --presente -55.
"""
import argparse
import asyncio
import threading
import time
import tkinter as tk
from collections import deque

from bleak import BleakScanner

PREFIXO = "UNIFESP-BADGE"   # o cracha anuncia "UNIFESP-BADGE-101"

ap = argparse.ArgumentParser()
ap.add_argument("--ausente", type=float, default=-50, help="bloqueia se a media ficar abaixo disto (dBm)")
ap.add_argument("--presente", type=float, default=-47, help="libera se a media ficar acima disto (dBm)")
ap.add_argument("--seg-ausente", type=float, default=3, help="segundos longe antes de bloquear")
ap.add_argument("--seg-presente", type=float, default=2, help="segundos perto antes de liberar")
ap.add_argument("--sumiu", type=float, default=5, help="segundos sem ouvir o cracha = ausente")
ARGS = ap.parse_args()

ultimas = deque(maxlen=5)    # ultimas 5 leituras de RSSI (usamos a media, que e menos "nervosa")
visto_em = 0.0               # quando ouvimos o cracha pela ultima vez
nome_visto = ""
parar = threading.Event()


def ao_detectar(dispositivo, anuncio):
    """O PC chama isto a cada anuncio BLE que escuta; guardamos so os do cracha."""
    global visto_em, nome_visto
    nome = anuncio.local_name or dispositivo.name or ""
    if nome.startswith(PREFIXO):
        ultimas.append(anuncio.rssi)
        visto_em = time.time()
        nome_visto = nome


async def ouvir():
    async with BleakScanner(ao_detectar):
        while not parar.is_set():
            await asyncio.sleep(0.2)


def thread_ble():
    try:
        asyncio.run(ouvir())
    except Exception as erro:   # Bluetooth desligado, sem permissao, adaptador sem BLE...
        print(f"\nERRO no Bluetooth: {erro}")
        parar.set()


class Agente:
    def __init__(self):
        self.raiz = tk.Tk()
        self.raiz.withdraw()                 # janela principal escondida; so a tela de bloqueio aparece
        self.bloqueado = False
        self.tela = None
        self.desde_perto = None              # desde quando a media esta acima do limiar de liberar
        self.desde_longe = time.time()       # comeca contando: se o cracha nao aparecer, bloqueia
        self.inicio = time.time()
        threading.Thread(target=thread_ble, daemon=True).start()
        self.ciclo()

    # ---------- decisao ----------
    def media(self, agora):
        if not ultimas or agora - visto_em > ARGS.sumiu:
            return None                       # sumiu
        return sum(ultimas) / len(ultimas)

    def ciclo(self):
        if parar.is_set():
            self.raiz.destroy()
            return
        agora = time.time()
        m = self.media(agora)

        perto = m is not None and m > ARGS.presente
        longe = m is None or m < ARGS.ausente
        self.desde_perto = (self.desde_perto or agora) if perto else None
        self.desde_longe = (self.desde_longe or agora) if longe else None

        # 5 s de tolerancia no inicio, para o PC achar o cracha antes de decidir
        if not self.bloqueado and self.desde_longe and agora - self.inicio > 5 \
                and agora - self.desde_longe >= ARGS.seg_ausente:
            self.bloquear()
        elif self.bloqueado and self.desde_perto and agora - self.desde_perto >= ARGS.seg_presente:
            self.liberar()

        texto = "sem sinal" if m is None else f"{m:.1f} dBm"
        estado = "BLOQUEADO" if self.bloqueado else "liberado"
        print(f"\rRSSI medio: {texto:<12} {estado:<10}", end="", flush=True)
        if self.tela:
            self.rotulo_rssi.config(text=f"sinal: {texto}")
            self.tela.lift()
            self.tela.attributes("-topmost", True)
        self.raiz.after(300, self.ciclo)

    # ---------- tela de bloqueio ----------
    def bloquear(self):
        self.bloqueado = True
        t = tk.Toplevel(self.raiz)
        t.configure(bg="#0b1d3a")
        t.attributes("-fullscreen", True)
        t.attributes("-topmost", True)
        tk.Label(t, text="BLOQUEADO", font=("Segoe UI", 64, "bold"), fg="white", bg="#0b1d3a").pack(expand=True, pady=(120, 0))
        tk.Label(t, text="Aproxime o crachá para desbloquear", font=("Segoe UI", 28), fg="#9fb4d6", bg="#0b1d3a").pack()
        self.rotulo_rssi = tk.Label(t, text="", font=("Segoe UI", 18), fg="#9fb4d6", bg="#0b1d3a")
        self.rotulo_rssi.pack(expand=True)
        tk.Label(t, text="ESC = encerrar o agente (saída de emergência)", font=("Segoe UI", 12),
                 fg="#5d7099", bg="#0b1d3a").pack(side="bottom", pady=20)
        t.bind("<Escape>", lambda e: parar.set())
        t.focus_force()
        self.tela = t
        print("\n>>> crachá longe: tela BLOQUEADA")

    def liberar(self):
        self.bloqueado = False
        if self.tela:
            self.tela.destroy()
            self.tela = None
        print("\n>>> crachá perto: tela LIBERADA")


if __name__ == "__main__":
    print(f"Agente ligado. Bloqueia abaixo de {ARGS.ausente} dBm, libera acima de {ARGS.presente} dBm.")
    print("Ctrl+C para encerrar. Na tela de bloqueio, ESC encerra.\n")
    try:
        Agente().raiz.mainloop()
    except KeyboardInterrupt:
        pass
    finally:
        parar.set()
        print("\nEncerrado.")
