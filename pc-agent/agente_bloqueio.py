"""Agente do PC (versao 2): o BOTAO do cracha desbloqueia; o afastamento bloqueia.

Ideia (12/11/2026), como um cartao passado no leitor:
  1) O PC esta BLOQUEADO (tela cheia propria cobrindo tudo).
  2) O funcionario aperta o botao do cracha. O cracha avisa por Bluetooth (BLE) mudando um contador no
     anuncio. O PC ve o contador mudar e mede o RSSI (forca do sinal) DAQUELE pacote.
     Se esta perto (RSSI > --liberar), o PC LIBERA na hora. Sem media, sem espera.
  3) Com o PC liberado comeca o MONITORAMENTO: se o cracha se afasta, o PC avisa ("bloqueando em 5 s")
     e bloqueia. Para nao bloquear quando alguem tapa o cracha com o corpo/mao, usamos o MELHOR RSSI
     dos ultimos segundos (o pico), e so agimos se ficar fraco por bastante tempo.
  4) Apertar o botao com o PC ja liberado NAO faz nada no PC (o ponto e registrado por outro caminho:
     LoRa -> gateway -> banco). O PC nunca registra ponto: so bloqueia/libera.

Por que assim? No teste da versao 1 (media + limiares) o PC desbloqueava quando o usuario tapava o cracha
e demorava para liberar por causa da media. Veja docs/FLUXO_BLE_E_BLOQUEIO.md e docs/DIARIO_DE_BORDO.md.

ISTO E UM BLOQUEIO DE DEMONSTRACAO, nao de seguranca: o Windows nao deixa programas comuns desbloquear
a tela de verdade, entao desenhamos uma tela propria por cima de tudo. Ctrl+Alt+Del ou outro usuario
contornam. Um outro ESP32 poderia copiar o anuncio (sem assinatura). Diga isso na apresentacao.

SAIDA DE EMERGENCIA: com a tela de bloqueio aparecendo, aperte ESC para encerrar o agente (ou Ctrl+C no terminal).

Instalar (uma vez):   pip install bleak        (opcional, para avisar o servidor: pip install paho-mqtt)
Rodar:                python agente_bloqueio.py
Desenvolver sem se trancar:   python agente_bloqueio.py --iniciar-liberado
Ajustar:              --liberar -60  (clique valido se o RSSI do clique for MAIOR que isto)
                      --manter -70 --janela 8 --seg-ausente 10 --aviso 5   (politica de bloqueio)
Opcional (ainda nao testado):  --mqtt 192.168.0.10   publica o status do PC em unifesp/ponto/pc/<badge>/status
"""
import argparse
import asyncio
import json
import threading
import time
import tkinter as tk
from collections import deque

from bleak import BleakScanner

PREFIXO = "UNIFESP-BADGE"   # nome anunciado pelo cracha
ID_EMPRESA = 0xFFFF         # id generico usado nos dados do fabricante (ver firmware 05)

ap = argparse.ArgumentParser()
ap.add_argument("--badge", type=int, default=101, help="numero do cracha que desbloqueia este PC")
ap.add_argument("--liberar", type=float, default=-60, help="clique so vale se o RSSI dele for maior que isto (dBm)")
ap.add_argument("--manter", type=float, default=-70, help="liberado: 'longe' = melhor RSSI da janela abaixo disto (dBm)")
ap.add_argument("--janela", type=float, default=8, help="segundos considerados para o melhor RSSI (pico)")
ap.add_argument("--seg-ausente", type=float, default=10, help="segundos 'longe' seguidos antes do aviso")
ap.add_argument("--aviso", type=float, default=5, help="segundos de contagem 'bloqueando em N s' antes de bloquear")
ap.add_argument("--iniciar-liberado", action="store_true", help="comeca liberado (util para desenvolver)")
ap.add_argument("--mqtt", default=None, help="IP do broker MQTT (opcional): publica o status do PC")
ap.add_argument("--mqtt-porta", type=int, default=1883)
ARGS = ap.parse_args()

JANELA_CLIQUE = 1.0         # segundos esperando um pacote forte depois de ver o clique
pacotes = deque()           # (hora, rssi, boot, contador): a thread do Bluetooth coloca, a tela le
parar = threading.Event()


# ---------------------------------------------------------------- Bluetooth (thread de fundo)
def ao_detectar(dispositivo, anuncio):
    """O PC chama isto a cada anuncio BLE que escuta; guardamos so os do NOSSO cracha."""
    nome = anuncio.local_name or dispositivo.name or ""
    dados = anuncio.manufacturer_data.get(ID_EMPRESA)
    if not nome.startswith(PREFIXO) or not dados or len(dados) < 6 or dados[0] != 1:
        return
    badge = dados[1] | (dados[2] << 8)
    if badge != ARGS.badge:
        return
    boot = dados[3]
    contador = dados[4] | (dados[5] << 8)
    pacotes.append((time.time(), anuncio.rssi, boot, contador))


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


# ---------------------------------------------------------------- MQTT opcional
class Status:
    """Publica 'liberado'/'bloqueado' em unifesp/ponto/pc/<badge>/status (so atualiza ultimo_contato do PC)."""
    def __init__(self):
        self.cliente = None
        if not ARGS.mqtt:
            return
        try:
            import paho.mqtt.client as mqtt
            try:
                self.cliente = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"agente-pc-{ARGS.badge}")
            except AttributeError:                       # paho-mqtt 1.x
                self.cliente = mqtt.Client(client_id=f"agente-pc-{ARGS.badge}")
            self.cliente.connect_async(ARGS.mqtt, ARGS.mqtt_porta)
            self.cliente.loop_start()
        except Exception as erro:
            print(f"MQTT desativado ({erro}). Rode: pip install paho-mqtt")
            self.cliente = None

    def publicar(self, estado, rssi=None):
        if not self.cliente:
            return
        corpo = {"estado": estado, **({"rssi": round(rssi)} if rssi is not None else {})}
        self.cliente.publish(f"unifesp/ponto/pc/{ARGS.badge}/status", json.dumps(corpo), qos=0)


# ---------------------------------------------------------------- Agente (interface + decisoes)
class Agente:
    def __init__(self):
        self.raiz = tk.Tk()
        self.raiz.withdraw()                  # janela principal escondida; so as telas de bloqueio/aviso aparecem
        self.status = Status()
        self.estado = "LIBERADO"
        self.tela = None                      # tela cheia de bloqueio
        self.aviso = None                     # janelinha "bloqueando em N s"
        self.hist = deque()                   # (hora, rssi) dos pacotes recentes (monitoramento)
        self.boot = None                      # ultimo "boot" visto (para nao confundir reinicio com clique)
        self.contador = None                  # ultimo contador de cliques visto
        self.clique = None                    # clique em analise
        self.desde_longe = None
        self.aviso_fim = None
        self.ultimo_pacote = 0.0
        self.ultimo_rssi = None
        self.msg = ""
        self.ultimo_status = 0.0
        threading.Thread(target=thread_ble, daemon=True).start()
        if not ARGS.iniciar_liberado:
            self.bloquear("início")
        self.ciclo()

    # ---------- leitura dos pacotes ----------
    def processar_pacotes(self):
        while pacotes:
            t, rssi, boot, cont = pacotes.popleft()
            self.ultimo_pacote, self.ultimo_rssi = t, rssi
            self.hist.append((t, rssi))
            if self.boot != boot:
                # 1a vez que vemos o cracha, ou ele reiniciou: so sincroniza. NAO e clique.
                self.boot, self.contador = boot, cont
            elif cont != self.contador:
                self.contador = cont          # contador mudou = botao apertado
                self.clique = {"t": t, "melhor": rssi, "feito": False}
                print(f"\n>>> clique detectado (RSSI {rssi:.0f} dBm)")
            elif self.clique and not self.clique["feito"] and t - self.clique["t"] <= JANELA_CLIQUE:
                self.clique["melhor"] = max(self.clique["melhor"], rssi)   # melhor pacote logo apos o clique

    # ---------- decisao do clique ----------
    def decidir_clique(self, agora):
        c = self.clique
        if not c or c["feito"]:
            return
        if c["melhor"] > ARGS.liberar:
            c["feito"] = True
            if self.estado == "BLOQUEADO":
                self.liberar(c["melhor"])
            else:
                self.desde_longe = None       # clique forte com o PC liberado: prova de presenca, cancela aviso
                self.fechar_aviso()
        elif agora - c["t"] >= JANELA_CLIQUE:
            c["feito"] = True
            if self.estado == "BLOQUEADO":
                self.msg = f"Clique ignorado: crachá longe ({c['melhor']:.0f} dBm). Aproxime e aperte de novo."
                print(f"\n>>> {self.msg}")

    # ---------- monitoramento (so com o PC liberado) ----------
    def monitorar(self, agora):
        while self.hist and agora - self.hist[0][0] > ARGS.janela:
            self.hist.popleft()
        melhor = max((r for _, r in self.hist), default=None)    # PICO da janela (ignora quedas por obstrucao)
        longe = melhor is None or melhor < ARGS.manter

        if not longe:
            self.desde_longe = None
            self.fechar_aviso()
            return
        if self.desde_longe is None:
            self.desde_longe = agora
        if self.aviso_fim is None and agora - self.desde_longe >= ARGS.seg_ausente:
            self.aviso_fim = agora + ARGS.aviso
            self.abrir_aviso()
        if self.aviso_fim is not None:
            restante = self.aviso_fim - agora
            if restante <= 0:
                self.bloquear("crachá se afastou")
            elif self.aviso:
                self.rotulo_aviso.config(text=f"Crachá não detectado. Bloqueando em {restante:.0f} s")

    # ---------- ciclo principal (a cada 100 ms) ----------
    def ciclo(self):
        if parar.is_set():
            self.raiz.destroy()
            return
        agora = time.time()
        self.processar_pacotes()
        self.decidir_clique(agora)
        if self.estado == "LIBERADO":
            self.monitorar(agora)
        else:
            while self.hist and agora - self.hist[0][0] > ARGS.janela:
                self.hist.popleft()

        idade = agora - self.ultimo_pacote if self.ultimo_pacote else None
        sinal = "sem sinal" if idade is None or idade > 3 else f"{self.ultimo_rssi:.0f} dBm"
        print(f"\r{self.estado:<10} sinal: {sinal:<10}", end="", flush=True)
        if self.tela:
            self.rotulo_sinal.config(text=f"sinal do crachá: {sinal}")
            self.rotulo_msg.config(text=self.msg)
            self.tela.lift()
            self.tela.attributes("-topmost", True)
        if agora - self.ultimo_status > 30:   # "estou vivo" para o painel
            self.ultimo_status = agora
            self.status.publicar(self.estado.lower(), self.ultimo_rssi if idade and idade < 3 else None)
        self.raiz.after(100, self.ciclo)

    # ---------- telas ----------
    def bloquear(self, motivo):
        self.fechar_aviso()
        self.estado = "BLOQUEADO"
        self.aviso_fim = self.desde_longe = None
        self.msg = "Aproxime o crachá e aperte o botão"
        t = tk.Toplevel(self.raiz)
        t.configure(bg="#0b1d3a")
        t.attributes("-fullscreen", True)
        t.attributes("-topmost", True)
        tk.Label(t, text="BLOQUEADO", font=("Segoe UI", 64, "bold"), fg="white", bg="#0b1d3a").pack(expand=True, pady=(120, 0))
        tk.Label(t, text="Aperte o botão do crachá para desbloquear", font=("Segoe UI", 28),
                 fg="#9fb4d6", bg="#0b1d3a").pack()
        self.rotulo_msg = tk.Label(t, text=self.msg, font=("Segoe UI", 18), fg="#ffd166", bg="#0b1d3a")
        self.rotulo_msg.pack(pady=(30, 0))
        self.rotulo_sinal = tk.Label(t, text="", font=("Segoe UI", 16), fg="#9fb4d6", bg="#0b1d3a")
        self.rotulo_sinal.pack(expand=True)
        tk.Label(t, text="ESC = encerrar o agente (saída de emergência)", font=("Segoe UI", 12),
                 fg="#5d7099", bg="#0b1d3a").pack(side="bottom", pady=20)
        t.bind("<Escape>", lambda e: parar.set())
        t.focus_force()
        self.tela = t
        self.status.publicar("bloqueado")
        print(f"\n>>> PC BLOQUEADO ({motivo})")

    def liberar(self, rssi):
        self.estado = "LIBERADO"
        if self.tela:
            self.tela.destroy()
            self.tela = None
        self.hist.clear()
        self.hist.append((time.time(), rssi))  # o pacote do clique ja conta como "presente"
        self.desde_longe = self.aviso_fim = None
        self.msg = ""
        self.status.publicar("liberado", rssi)
        print(f"\n>>> PC LIBERADO pelo botão (RSSI do clique {rssi:.0f} dBm)")

    def abrir_aviso(self):
        if self.aviso:
            return
        t = tk.Toplevel(self.raiz)
        t.overrideredirect(True)
        t.attributes("-topmost", True)
        t.configure(bg="#b45309")
        largura = t.winfo_screenwidth()
        t.geometry(f"560x80+{(largura - 560) // 2}+40")
        self.rotulo_aviso = tk.Label(t, text="", font=("Segoe UI", 18, "bold"), fg="white", bg="#b45309")
        self.rotulo_aviso.pack(expand=True, fill="both")
        self.aviso = t
        print("\n>>> aviso: crachá longe, vai bloquear")

    def fechar_aviso(self):
        self.aviso_fim = None
        if self.aviso:
            self.aviso.destroy()
            self.aviso = None


if __name__ == "__main__":
    print(f"Agente ligado (crachá {ARGS.badge}). Clique válido > {ARGS.liberar} dBm; "
          f"bloqueia se o pico de {ARGS.janela:.0f} s ficar < {ARGS.manter} dBm por {ARGS.seg_ausente:.0f} s "
          f"(+{ARGS.aviso:.0f} s de aviso).")
    print("Ctrl+C encerra. Na tela de bloqueio, ESC encerra.\n")
    try:
        Agente().raiz.mainloop()
    except KeyboardInterrupt:
        pass
    finally:
        parar.set()
        print("\nEncerrado.")
