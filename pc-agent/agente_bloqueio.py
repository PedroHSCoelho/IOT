"""Agente do PC (versao 3, 17/11/2026): o SINAL (BLE) controla a tela; o BOTAO "prende" o PC liberado.

Dois radios BLE (duas Heltec):
  CRACHA     (Heltec 1, "UNIFESP-BADGE", numero 101): so anuncia o sinal; o PC mede o RSSI (distancia).
  RECIPIENTE (Heltec 2, "UNIFESP-HOLDER", numero 201): o porta-cracha, com o botao; o contador de cliques
             no anuncio dele diz "colocou/tirou". O PC conta os cliques e NAO usa o RSSI do recipiente.

Dois modos, como um cracha que ora esta no porta-cracha, ora esta com a pessoa:
  AUTOMATICO (sem clique)  o sinal manda:
       - o usuario chega perto (pico do RSSI > --liberar)  -> DESBLOQUEIA sozinho;
       - o usuario sai de perto (pico < --manter por --seg-ausente s, + aviso de --aviso s) -> BLOQUEIA.
  FIXO (depois de 1 clique)  cracha no porta-cracha: o PC fica DESBLOQUEADO ate o proximo clique,
       mesmo sem sinal ou com sinal fraco (o BLE nao e medido).
  2o clique -> volta ao AUTOMATICO (desbloqueia e bloqueia conforme o sinal).
  Estados: BLOQUEADO, AUTO (liberado, vigiando), FIXO (liberado, sem vigiar). Todo clique alterna FIXO <-> AUTO
  (de BLOQUEADO ou AUTO o clique vai para FIXO; de FIXO vai para AUTO).

Para nao bloquear quando alguem tapa o cracha com a mao/corpo, usamos o MELHOR RSSI (pico) dos ultimos
--janela segundos. Desbloquear exige sinal forte (> --liberar) e bloquear exige sinal fraco (< --manter):
a diferenca entre os dois (histerese) evita que o PC fique "pisca-pisca" na fronteira.
O ponto e registrado por outro caminho (LoRa -> gateway -> banco). O PC nunca registra ponto.

Historico: a v1 (media de RSSI) desbloqueava ao tapar o cracha e demorava (P13/P14 do diario). A v3 volta ao
desbloqueio automatico por proximidade a pedido da equipe, usando o pico e a histerese. A medir na pratica.

ISTO E UM BLOQUEIO DE DEMONSTRACAO, nao de seguranca: o Windows nao deixa programas comuns desbloquear
a tela de verdade, entao desenhamos uma tela propria por cima de tudo. Ctrl+Alt+Del ou outro usuario
contornam. Um outro ESP32 poderia copiar o anuncio (sem assinatura). Diga isso na apresentacao.

SAIDA DE EMERGENCIA: com a tela de bloqueio aparecendo, aperte ESC para encerrar o agente (ou Ctrl+C no terminal).

Instalar (uma vez):   pip install bleak        (opcional, para avisar o servidor: pip install paho-mqtt)
Rodar:                python agente_bloqueio.py
Desenvolver sem se trancar:   python agente_bloqueio.py --iniciar-liberado
Ajustar:              --liberar -60 (perto) --manter -70 (longe) --janela 3 --seg-ausente 3 --aviso 3
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

PREFIXO_CRACHA = "UNIFESP-BADGE"       # Heltec 1: anuncia o sinal (distancia)
PREFIXO_RECIPIENTE = "UNIFESP-HOLDER"  # Heltec 2: porta-cracha com o botao (cliques)
ID_EMPRESA = 0xFFFF         # id generico usado nos dados do fabricante (ver firmware 05)

ap = argparse.ArgumentParser()
ap.add_argument("--badge", type=int, default=101, help="numero do cracha (mede a distancia)")
ap.add_argument("--recipiente", type=int, default=201, help="numero do recipiente/porta-cracha (conta os cliques)")
ap.add_argument("--liberar", type=float, default=-60, help="automatico: bloqueado desbloqueia quando um pacote passa disto (dBm)")
ap.add_argument("--manter", type=float, default=-70, help="automatico liberado: 'longe' = melhor RSSI da janela abaixo disto (dBm)")
ap.add_argument("--janela", type=float, default=3, help="segundos considerados para o melhor RSSI (pico)")
ap.add_argument("--seg-ausente", type=float, default=3, help="segundos 'longe' seguidos antes do aviso")
ap.add_argument("--aviso", type=float, default=3, help="segundos de contagem 'bloqueando em N s' antes de bloquear")
ap.add_argument("--iniciar-liberado", action="store_true", help="comeca liberado (util para desenvolver)")
ap.add_argument("--mqtt", default=None, help="IP do broker MQTT (opcional): publica o status do PC")
ap.add_argument("--mqtt-porta", type=int, default=1883)
ARGS = ap.parse_args()

pacotes = deque()           # (hora, tipo 'c'/'r', rssi, boot, contador): a thread do Bluetooth coloca, a tela le
parar = threading.Event()


# ---------------------------------------------------------------- Bluetooth (thread de fundo)
def ao_detectar(dispositivo, anuncio):
    """O PC chama isto a cada anuncio BLE que escuta; guardamos so os do NOSSO cracha e do NOSSO recipiente."""
    nome = anuncio.local_name or dispositivo.name or ""
    dados = anuncio.manufacturer_data.get(ID_EMPRESA)
    if not dados or len(dados) < 6 or dados[0] != 1:
        return
    numero = dados[1] | (dados[2] << 8)
    if nome.startswith(PREFIXO_CRACHA) and numero == ARGS.badge:
        tipo = "c"
    elif nome.startswith(PREFIXO_RECIPIENTE) and numero == ARGS.recipiente:
        tipo = "r"
    else:
        return
    pacotes.append((time.time(), tipo, anuncio.rssi, dados[3], dados[4] | (dados[5] << 8)))


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
        self.estado = "AUTO"
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
        self.ultimo_recipiente = 0.0
        self.msg = ""
        self.ultimo_status = 0.0
        threading.Thread(target=thread_ble, daemon=True).start()
        if not ARGS.iniciar_liberado:
            self.bloquear("início")
        self.ciclo()

    # ---------- leitura dos pacotes ----------
    def processar_pacotes(self):
        while pacotes:
            t, tipo, rssi, boot, cont = pacotes.popleft()
            if tipo == "c":
                # Pacote do CRACHA: so serve para medir a distancia (RSSI).
                self.ultimo_pacote, self.ultimo_rssi = t, rssi
                self.hist.append((t, rssi))
            else:
                # Pacote do RECIPIENTE: o contador mudar = botao apertado (colocou/tirou o cracha).
                self.ultimo_recipiente = t
                if self.boot != boot:
                    self.boot, self.contador = boot, cont      # 1a vez ou reinicio: so sincroniza, NAO e clique
                elif cont != self.contador:
                    self.contador = cont
                    self.clique = {"t": t, "feito": False}
                    print("\n>>> clique no recipiente")

    def rssi_recente(self, agora):
        """RSSI do cracha se ele foi ouvido ha menos de 3 s; senao None (cracha sem sinal)."""
        if self.ultimo_pacote and agora - self.ultimo_pacote < 3.0:
            return self.ultimo_rssi
        return None

    # ---------- decisao do clique ----------
    def decidir_clique(self, agora):
        c = self.clique
        if not c or c["feito"]:
            return
        c["feito"] = True
        rssi = self.rssi_recente(agora)
        if self.estado == "FIXO":
            self.liberar(rssi, "AUTO", "2º clique: volta ao automático")
            self.sinal_clique("CLIQUE recebido: modo AUTOMÁTICO (o sinal manda)")
        else:
            self.liberar(rssi, "FIXO", "clique: crachá no recipiente, fica liberado")
            self.sinal_clique("CLIQUE recebido: modo FIXO (liberado até o próximo clique)")

    def sinal_clique(self, texto):
        """Feedback de que o botao do recipiente foi clicado: bipe + aviso verde na tela por 2 s."""
        try:
            import winsound
            winsound.Beep(1200, 120)
        except Exception:
            pass                                   # sem bipe fora do Windows; o aviso na tela continua
        t = tk.Toplevel(self.raiz)
        t.overrideredirect(True)
        t.attributes("-topmost", True)
        t.configure(bg="#15803d")
        largura = t.winfo_screenwidth()
        t.geometry(f"700x80+{(largura - 700) // 2}+40")
        tk.Label(t, text=texto, font=("Segoe UI", 18, "bold"), fg="white", bg="#15803d").pack(expand=True, fill="both")
        t.after(2000, t.destroy)

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

    def checar_proximidade(self, agora):
        """Bloqueado em modo automático: um pacote forte e recente (> --liberar) desbloqueia sozinho."""
        if self.ultimo_pacote and agora - self.ultimo_pacote < 1.0 and self.ultimo_rssi > ARGS.liberar:
            self.liberar(self.ultimo_rssi, "AUTO", "usuário chegou perto")

    # ---------- ciclo principal (a cada 100 ms) ----------
    def ciclo(self):
        if parar.is_set():
            self.raiz.destroy()
            return
        agora = time.time()
        self.processar_pacotes()
        self.decidir_clique(agora)
        if self.estado == "AUTO":
            self.monitorar(agora)      # automático: o pico do sinal decide bloquear
        elif self.estado == "BLOQUEADO":
            self.checar_proximidade(agora)
            while self.hist and agora - self.hist[0][0] > ARGS.janela:
                self.hist.popleft()
        else:
            while self.hist and agora - self.hist[0][0] > ARGS.janela:
                self.hist.popleft()

        idade = agora - self.ultimo_pacote if self.ultimo_pacote else None
        sinal = "sem sinal" if idade is None or idade > 3 else f"{self.ultimo_rssi:.0f} dBm"
        rec = "ok" if agora - self.ultimo_recipiente < 3 else "sem sinal"
        print(f"\r{self.estado:<10} crachá: {sinal:<10} recipiente: {rec:<10}", end="", flush=True)
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
        self.msg = "Aproxime-se do PC com o crachá (ou aperte o botão)"
        t = tk.Toplevel(self.raiz)
        t.configure(bg="#0b1d3a")
        t.attributes("-fullscreen", True)
        t.attributes("-topmost", True)
        tk.Label(t, text="BLOQUEADO", font=("Segoe UI", 64, "bold"), fg="white", bg="#0b1d3a").pack(expand=True, pady=(120, 0))
        tk.Label(t, text="Chegue perto com o crachá ou aperte o botão para desbloquear", font=("Segoe UI", 28),
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

    def liberar(self, rssi, novo_estado, motivo):
        """Libera a tela. novo_estado: 'AUTO' (vigia o sinal) ou 'FIXO' (fica liberado ate o proximo clique)."""
        self.estado = novo_estado
        self.fechar_aviso()
        if self.tela:
            self.tela.destroy()
            self.tela = None
        self.hist.clear()
        if rssi is not None:
            self.hist.append((time.time(), rssi))  # o pacote atual conta como "presente"
        self.desde_longe = self.aviso_fim = None
        self.msg = ""
        self.status.publicar(novo_estado.lower(), rssi)
        sinal = "sem sinal do crachá" if rssi is None else f"RSSI {rssi:.0f} dBm"
        print(f"\n>>> PC LIBERADO, modo {novo_estado} ({motivo}; {sinal})")

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
    print(f"Agente ligado (crachá {ARGS.badge}). Automático: desbloqueia se o sinal > {ARGS.liberar} dBm; "
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
