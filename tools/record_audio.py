#!/usr/bin/env python3
"""Trainingsdaten fuers Klatsch-Modell aufnehmen (Board mit Sketch 04-audio-stream an J1).

Befehle:
  monitor     [--seconds 5]                 Pegel und Rahmenverluste anzeigen (Funktionstest)
  session     [--per-class 40] [--clip 1.5] gefuehrte Aufnahme: Klatschen, Schnipsen, Stoergeraeusche im Wechsel
  background  [--seconds 60]                Hintergrund am Stueck aufnehmen (Raum, Reden, Tippen, Laerm)
Gemeinsam:    [--port PORT] [--baud 1000000] [--out data/audio]

Ablage: <out>/<klasse>/<klasse>_<datum>_<nr>.wav (16 kHz, Mono, 16 Bit) und <out>/index.csv.
Waehrend eines Clips leuchtet LED1 auf dem Board. Abbruch jederzeit mit Strg+C, Index bleibt konsistent.
Wichtig: Nur ein Programm darf den Port halten. Laeuft tools/live_viewer.py, vorher beenden.
"""
import argparse, array, csv, datetime, glob, os, random, struct, sys, time, wave
import serial

MAGIC = b"\xa5\x5a"
FRAME = 256
RATE = 16000
FRAME_BYTES = 4 + FRAME * 2 + 2


def find_port():
    hits = sorted(glob.glob("/dev/serial/by-id/*KitProg3*"))
    return hits[0] if hits else "/dev/ttyACM0"


def port_holders(port):
    real = os.path.realpath(port); out = []
    for p in glob.glob("/proc/[0-9]*/fd"):
        try:
            for fd in os.listdir(p):
                if os.path.realpath(os.path.join(p, fd)) == real:
                    pid = p.split("/")[2]
                    if pid != str(os.getpid()):
                        cmd = open(f"/proc/{pid}/cmdline", "rb").read().replace(b"\0", b" ").decode(errors="replace").strip()
                        out.append((pid, cmd)); break
        except (PermissionError, FileNotFoundError):
            continue
    return out


class Stream:
    """Liest Rahmen vom Board, synchronisiert ueber Magic und Pruefsumme, zaehlt Verluste."""
    def __init__(self, port, baud):
        self.ser = serial.Serial(port, baud, timeout=0.05, dsrdtr=False, rtscts=False)
        self.ser.dtr = True; self.ser.rts = True
        self.buf = b""; self.last_seq = None; self.lost = 0; self.frames = 0; self.bad = 0

    def led(self, on):
        self.ser.write(b"1" if on else b"0"); self.ser.flush()

    def flush(self):
        self.ser.reset_input_buffer(); self.buf = b""; self.last_seq = None

    def frame(self):
        """Naechster gueltiger Rahmen als array('h') mit 256 Samples; blockiert bis einer da ist."""
        while True:
            i = self.buf.find(MAGIC)
            if i >= 0 and len(self.buf) >= i + FRAME_BYTES:
                blk = self.buf[i:i + FRAME_BYTES]
                seq = struct.unpack_from("<H", blk, 2)[0]
                samples = array.array("h"); samples.frombytes(blk[4:4 + FRAME * 2])
                if sys.byteorder != "little": samples.byteswap()
                chk = struct.unpack_from("<H", blk, 4 + FRAME * 2)[0]
                if (sum(s & 0xFFFF for s in samples) & 0xFFFF) == chk:
                    self.buf = self.buf[i + FRAME_BYTES:]
                    if self.last_seq is not None:
                        gap = (seq - self.last_seq - 1) & 0xFFFF
                        if 0 < gap < 1000: self.lost += gap
                    self.last_seq = seq; self.frames += 1
                    return samples
                self.bad += 1; self.buf = self.buf[i + 1:]
                continue
            if i < 0 and len(self.buf) > 2: self.buf = self.buf[-1:]
            chunk = self.ser.read(8192)
            if chunk: self.buf += chunk

    def record(self, seconds, on_progress=None):
        need = int(seconds * RATE); out = array.array("h")
        while len(out) < need:
            out.extend(self.frame())
            if on_progress: on_progress(len(out) / need)
        del out[need:]
        return out


def rms(samples):
    if not samples: return 0.0
    return (sum(s * s for s in samples) / len(samples)) ** 0.5


def write_wav(path, samples):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE); w.writeframes(samples.tobytes())


def next_index(out, cls):
    n = len(glob.glob(os.path.join(out, cls, f"{cls}_*.wav")))
    return n + 1


def add_index(out, row):
    path = os.path.join(out, "index.csv"); new = not os.path.exists(path)
    with open(path, "a", newline="") as f:
        w = csv.writer(f)
        if new: w.writerow(["datei", "klasse", "ansage", "dauer_s", "peak", "rms", "rahmen_verloren", "zeit"])
        w.writerow(row)


def save_clip(st, out, cls, prompt, samples, lost_before):
    n = next_index(out, cls)
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    rel = os.path.join(cls, f"{cls}_{stamp}_{n:03d}.wav")
    write_wav(os.path.join(out, rel), samples)
    peak = max(abs(s) for s in samples) if samples else 0
    add_index(out, [rel, cls, prompt, f"{len(samples)/RATE:.2f}", peak, f"{rms(samples):.0f}", st.lost - lost_before, stamp])
    return rel, peak


def countdown(label, secs):
    for i in range(secs, 0, -1):
        print(f"\r  {label}  in {i} ...   ", end="", flush=True); time.sleep(1.0)


def cmd_monitor(st, a):
    print(f"Pegel und Rahmen fuer {a.seconds} s (RMS der 16-Bit-Skala):")
    end = time.time() + a.seconds; last = time.time(); acc = array.array("h")
    while time.time() < end:
        acc.extend(st.frame())
        if time.time() - last >= 1.0:
            bar = "#" * min(60, int(rms(acc) / 100))
            print(f"  RMS {rms(acc):6.0f}  Peak {max(abs(s) for s in acc):6d}  Rahmen {st.frames:6d}  verloren {st.lost:4d}  defekt {st.bad:3d}  {bar}")
            acc = array.array("h"); last = time.time()
    print("Fertig. Verlorene Rahmen sollten 0 sein, sonst Baudrate oder Kabel pruefen.")


OTHER_PROMPTS = ["auf den Tisch klopfen", "mit Schluesseln klimpern", "einmal husten", "ein Wort laut sagen",
                 "ein Buch zuklappen", "mit den Fingern auf den Tisch trommeln", "einen Stift fallen lassen", "laut 'Hallo' rufen",
                 "Stuhl ruecken", "Tastatur anschlagen", "schnalzen mit der Zunge", "Hand auf den Tisch legen (leise)"]


VARIANTS = ["nah am Board (30 cm)", "aus 1 m", "aus 2 bis 3 m", "leise", "kraeftig", "vom Tisch aus, beilaeufig"]


def cmd_session(st, a):
    out = a.out; os.makedirs(out, exist_ok=True)
    plan = []
    if a.plan:
        # eigener Plan: "klasse=Ansage,klasse=Ansage"; Klassen mit Endung 2 sind Clips mit zwei Ereignissen (z. B. clap2)
        entries = [e.split("=", 1) for e in a.plan.split(",")]
        for i in range(a.per_class):
            for cls, prompt in entries:
                plan.append((cls.strip(), f"{prompt.strip()}  ({VARIANTS[i % len(VARIANTS)]})"))
    else:
        for i in range(a.per_class):
            plan.append(("clap", "KLATSCHEN")); plan.append(("snap", "SCHNIPSEN"))
            if i % 2 == 0: plan.append(("other", "Stoergeraeusch: " + OTHER_PROMPTS[(i // 2) % len(OTHER_PROMPTS)]))
    total = len(plan)
    print(f"Aufnahmesitzung: {total} Clips a {a.clip:.1f} s, Ablage {out}/")
    print("Ablauf je Clip: Ansage, Countdown, dann 'JETZT' und LED1 an. Geraeusch waehrend LED1 leuchtet machen.")
    print("Abstand zum Board 30 bis 100 cm, gern variieren. Abbruch mit Strg+C.\n")
    try:
        input("Enter zum Start ...")
    except KeyboardInterrupt:
        print(); return
    done = {}
    try:
        for k, (cls, prompt) in enumerate(plan, 1):
            print(f"\n[{k}/{total}] {prompt}")
            countdown(prompt, 2)
            st.flush(); st.led(True); lost0 = st.lost
            print("\r  JETZT!                      ", end="", flush=True)
            samples = st.record(a.clip, on_progress=lambda f: print(f"\r  JETZT!  {'#' * int(f * 20):20s}", end="", flush=True))
            st.led(False)
            rel, peak = save_clip(st, out, cls, prompt, samples, lost0)
            done[cls] = done.get(cls, 0) + 1
            note = "  (sehr leise, war da etwas?)" if cls != "other" and peak < 1500 else ""
            print(f"\r  gespeichert {rel}  Peak {peak}{note}      ")
            time.sleep(0.4)
    except KeyboardInterrupt:
        print("\n\nAbgebrochen.")
    st.led(False)
    print("\nAufgenommen: " + ", ".join(f"{c} {n}" for c, n in done.items()) + f". Index: {out}/index.csv")


def cmd_background(st, a):
    out = a.out; os.makedirs(out, exist_ok=True)
    print(f"Hintergrund {a.seconds} s am Stueck: normal verhalten, reden, tippen, Geraete laufen lassen. Nicht klatschen.")
    st.flush(); st.led(True); lost0 = st.lost
    samples = st.record(a.seconds, on_progress=lambda f: print(f"\r  Aufnahme {'#' * int(f * 40):40s} {int(f * 100):3d} %", end="", flush=True))
    st.led(False); print()
    rel, peak = save_clip(st, out, "background", "Hintergrund am Stueck", samples, lost0)
    print(f"gespeichert {rel}  Peak {peak}  RMS {rms(samples):.0f}  verlorene Rahmen {st.lost - lost0}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["monitor", "session", "background"])
    ap.add_argument("--port", default=None); ap.add_argument("--baud", type=int, default=1000000)
    ap.add_argument("--out", default="data/audio")
    ap.add_argument("--seconds", type=float, default=5.0, help="monitor/background: Dauer")
    ap.add_argument("--per-class", type=int, default=40, help="session: Clips je Klasse Klatschen/Schnipsen")
    ap.add_argument("--plan", default=None, help='session: eigener Plan "klasse=Ansage,...", z. B. "clap2=ZWEIMAL KLATSCHEN,clap=EINMAL KLATSCHEN,snap2=ZWEIMAL SCHNIPSEN"')
    ap.add_argument("--clip", type=float, default=1.5, help="session: Clip-Laenge in s")
    ap.add_argument("--force", action="store_true", help="auch wenn ein anderes Programm den Port haelt")
    a = ap.parse_args()
    if a.cmd == "background" and a.seconds == 5.0: a.seconds = 60.0
    port = a.port or find_port()
    holders = port_holders(port)
    if holders and not a.force:
        sys.exit("Port " + port + " wird schon benutzt von:\n" + "\n".join(f"  PID {p}: {c}" for p, c in holders) + "\nErst beenden (z. B. Strg+C im Viewer oder: fuser -k 8765/tcp), oder --force.")
    st = Stream(port, a.baud)
    print(f"Port {port} @ {a.baud} Baud")
    {"monitor": cmd_monitor, "session": cmd_session, "background": cmd_background}[a.cmd](st, a)


if __name__ == "__main__":
    main()
