#!/usr/bin/env python3
"""Live-Viewer fuer alle Funktionen des Boards (Gesten, Klatschen, Anwesenheit, Haus).

Liest die KitProg3-Konsole (J1, 115200 Baud) und zeigt die Zeilen der Firmware live im Browser:
  Gesten      "#12 none=0.01 circle=0.02 shaking=0.97 -> shaking", Rohdaten "D,ax,ay,az,gx,gy,gz"
  Klatschen   "L,rms,floor", "KLATSCH #n ...", "VERWORFEN ...", "MODELL clap=0.9 ... -> clap Inferenz 53 ms"
              Doppelklatschen (Zephyr house): "KLATSCH #n  einzel  Konfidenz 0.72",
              "KLATSCH #n  doppel  Abstand 310 ms  Konfidenz 0.80  LED1 an", "VERWORFEN einzel  Grund: ..."
  Anwesenheit "P,rssi,n", "PRAESENZ an|aus ...", "CLIENT join|leave mac", "AP ssid=... ip=... kanal=..."
  Haus        "LICHT an|aus  (Grund)"  (Zephyr-Firmware house: Klatschen + Bluetooth zusammen)
Die Ansicht folgt automatisch den Daten; ueber das Menue (Burger-Symbol links oben) laesst sie sich
fest auf eine Funktion stellen. Nur Python-Standardbibliothek plus pyserial.

Aufruf:  tools/live_viewer.py [--port /dev/ttyACM0] [--http 8765] [--log datei.csv]
Dann im Browser: http://localhost:8765
"""
import argparse, glob, json, queue, re, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import serial

INFER_RE = re.compile(r'^#(\d+)\s+(.*?)\s*->\s*(\S+)\s*$')
CLAP_RE = re.compile(r'^KLATSCH #(\d+)\s+Dauer (\d+) ms\s+Peak (\d+)\s+x([\d.]+)\s+LED1 (an|aus)(?:\s+ZCR (\d+))?(?:\s+Abkl\. (\d+) ms)?')
PRESENCE_RE = re.compile(r'^PRAESENZ (an|aus)\s+clients=(\d+)(?:\s+rssi=(-?\d+) dBm\s+(\S+))?')
CLIENT_RE = re.compile(r'^CLIENT (join|leave) (\S+)')
AP_RE = re.compile(r'^AP ssid=(\S+) ip=(\S+) kanal=(\d+)')
CLAP2_RE = re.compile(r'^KLATSCH #(\d+)\s+(einzel|doppel)(?:\s+Abstand (\d+) ms)?\s+Konfidenz ([\d.]+)(?:\s+LED1 (an|aus))?')
REJECT2_RE = re.compile(r'^VERWORFEN einzel\s+Grund: (.*?)\s*$')
LIGHT_RE = re.compile(r'^LICHT (an|aus)\s+\((.*?)\)')
MODEL_RE = re.compile(r'^MODELL\s+(.*?)\s*->\s*(\S+)\s+Inferenz ([\d.]+) ms')
REJECT_RE = re.compile(r'^VERWORFEN\s+Dauer (\d+) ms\s+Peak (\d+)\s+x([\d.]+)\s+Grund: (.*?)(?:\s+ZCR (\d+))?(?:\s+Abkl\. (\d+) ms)?\s*$')
PAIR_RE = re.compile(r'(\S+?)=([-\d.]+)')

clients_lock = threading.Lock()
clients = []            # eine Queue pro SSE-Client
state = {"port": None, "connected": False, "frames": 0, "inferences": 0, "labels": [], "started": time.time()}
pending = {"frames": [], "inf": [], "log": [], "level": [], "clap": [], "reject": [], "model": [], "pres": [], "presev": [], "house": []}
pending_lock = threading.Lock()


def find_port():
    hits = sorted(glob.glob("/dev/serial/by-id/*KitProg3*"))
    return hits[0] if hits else "/dev/ttyACM0"


def push(kind, item):
    with pending_lock:
        pending[kind].append(item)


def serial_reader(port, logfile):
    log = open(logfile, "a", buffering=1) if logfile else None
    while True:
        try:
            with serial.Serial(port, 115200, timeout=0.2, dsrdtr=False, rtscts=False) as ser:
                ser.dtr = True; ser.rts = True
                state["connected"] = True
                push("log", f"Port {port} geoeffnet")
                buf = b""
                while True:
                    chunk = ser.read(4096)
                    if not chunk:
                        continue
                    buf += chunk
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        handle_line(line.decode(errors="replace").strip(), log)
        except (serial.SerialException, OSError) as e:
            if state["connected"]:
                push("log", f"Port verloren: {e}")
            state["connected"] = False
            time.sleep(1.0)


def handle_line(line, log):
    if not line:
        return
    t = time.time()
    if line.startswith("D,"):
        try:
            vals = [float(x) for x in line[2:].split(",")]
        except ValueError:
            return
        if len(vals) == 6:
            state["frames"] += 1
            # Serielle Daten kommen in Paketen; die Frames eines Pakets wuerden denselben Zeitstempel
            # bekommen. Deshalb ein eigener 20-ms-Takt (50 Hz), der sich an der Ankunftszeit ausrichtet.
            fc = state.get("frame_clock")
            fc = t if fc is None or t - fc > 0.5 or fc > t + 0.1 else fc + 0.02
            state["frame_clock"] = fc
            push("frames", [round(fc, 3)] + vals)
            if log: log.write(f"{fc:.3f},D,{','.join(f'{v:.2f}' for v in vals)}\n")
        return
    if line.startswith("L,"):
        try:
            rms, floor = (float(x) for x in line[2:].split(",")[:2])
        except ValueError:
            return
        push("level", [round(t, 3), rms, floor])
        if log: log.write(f"{t:.3f},L,{rms:.0f},{floor:.0f}\n")
        return
    m = CLAP2_RE.match(line)
    if m:
        ev = {"t": round(t, 3), "n": int(m.group(1)), "kind": m.group(2), "gap": int(m.group(3) or 0), "conf": float(m.group(4)), "led1": (m.group(5) == "an") if m.group(5) else None}
        state["claps"] = ev["n"]
        push("clap", ev); push("log", line)
        if log: log.write(f"{t:.3f},K2,{ev['n']},{ev['kind']},{ev['gap']},{ev['conf']},{'' if ev['led1'] is None else int(ev['led1'])}\n")
        return
    m = REJECT2_RE.match(line)
    if m:
        ev = {"t": round(t, 3), "kind": "einzel", "why": m.group(1), "dur": 0, "peak": 0, "ratio": 0, "zcr": 0, "decay": 0}
        push("reject", ev); push("log", line)
        if log: log.write(f"{t:.3f},R2,{ev['why']}\n")
        return
    m = CLAP_RE.match(line)
    if m:
        ev = {"t": round(t, 3), "n": int(m.group(1)), "dur": int(m.group(2)), "peak": int(m.group(3)), "ratio": float(m.group(4)), "led1": m.group(5) == "an", "zcr": int(m.group(6) or 0), "decay": int(m.group(7) or 0)}
        state["claps"] = ev["n"]
        push("clap", ev)
        if log: log.write(f"{t:.3f},K,{ev['n']},{ev['dur']},{ev['peak']},{ev['ratio']},{int(ev['led1'])},{ev['zcr']},{ev['decay']}\n")
        push("log", line)
        return
    if line.startswith("P,"):
        try:
            rssi, n = (int(x) for x in line[2:].split(",")[:2])
        except ValueError:
            return
        push("pres", [round(t, 3), rssi, n])
        if log: log.write(f"{t:.3f},P,{rssi},{n}\n")
        return
    m = PRESENCE_RE.match(line) or CLIENT_RE.match(line) or AP_RE.match(line)
    if m:
        if m.re is PRESENCE_RE: ev = {"t": round(t, 3), "kind": "presence", "on": m.group(1) == "an", "n": int(m.group(2)), "rssi": int(m.group(3)) if m.group(3) else None, "mac": m.group(4) or ""}
        elif m.re is CLIENT_RE: ev = {"t": round(t, 3), "kind": m.group(1), "mac": m.group(2)}
        else: ev = {"t": round(t, 3), "kind": "ap", "ssid": m.group(1), "ip": m.group(2), "channel": int(m.group(3))}
        push("presev", ev); push("log", line)
        if log: log.write(f"{t:.3f},E,{ev['kind']},{ev.get('mac', ev.get('ssid', ''))}\n")
        return
    m = LIGHT_RE.match(line)
    if m:
        ev = {"t": round(t, 3), "on": m.group(1) == "an", "why": m.group(2)}
        push("house", ev); push("log", line)
        if log: log.write(f"{t:.3f},H,{int(ev['on'])},{ev['why']}\n")
        return
    m = MODEL_RE.match(line)
    if m:
        pairs = PAIR_RE.findall(m.group(1))
        ev = {"t": round(t, 3), "labels": [k for k, _ in pairs], "scores": [float(v) for _, v in pairs], "best": m.group(2), "ms": float(m.group(3))}
        push("model", ev)
        if log: log.write(f"{t:.3f},M,{','.join(f'{x:.2f}' for x in ev['scores'])},{ev['best']},{ev['ms']}\n")
        return
    m = REJECT_RE.match(line)
    if m:
        ev = {"t": round(t, 3), "dur": int(m.group(1)), "peak": int(m.group(2)), "ratio": float(m.group(3)), "why": m.group(4), "zcr": int(m.group(5) or 0), "decay": int(m.group(6) or 0)}
        push("reject", ev)
        if log: log.write(f"{t:.3f},R,{ev['dur']},{ev['peak']},{ev['ratio']},{ev['zcr']},{ev['decay']},{ev['why']}\n")
        push("log", line)
        return
    m = INFER_RE.match(line)
    if m:
        pairs = PAIR_RE.findall(m.group(2))
        if pairs:
            labels = [k for k, _ in pairs]
            scores = [float(v) for _, v in pairs]
            if labels != state["labels"]:
                state["labels"] = labels
            state["inferences"] += 1
            push("inf", {"t": round(t, 3), "n": int(m.group(1)), "labels": labels, "scores": scores, "best": m.group(3)})
            if log: log.write(f"{t:.3f},I,{m.group(1)},{','.join(f'{s:.2f}' for s in scores)},{m.group(3)}\n")
        return
    push("log", line)


def broadcaster():
    last_stats = 0
    while True:
        time.sleep(0.05)
        with pending_lock:
            if not any(pending.values()) and time.time() - last_stats < 1.0:
                continue
            batch = dict(pending)
            for k in pending: pending[k] = []
        if time.time() - last_stats >= 1.0:
            last_stats = time.time()
            batch["stats"] = {"connected": state["connected"], "port": state["port"],
                              "frames": state["frames"], "inferences": state["inferences"],
                              "uptime": round(time.time() - state["started"])}
        data = json.dumps(batch, separators=(",", ":"))
        with clients_lock:
            for q in clients:
                try: q.put_nowait(data)
                except queue.Full: pass


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a): pass

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/events":
            q = queue.Queue(maxsize=200)
            with clients_lock: clients.append(q)
            try:
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Cache-Control", "no-cache")
                self.end_headers()
                hello = json.dumps({"hello": True, "labels": state["labels"], "port": state["port"]})
                self.wfile.write(f"data: {hello}\n\n".encode()); self.wfile.flush()
                while True:
                    try:
                        data = q.get(timeout=10)
                        self.wfile.write(f"data: {data}\n\n".encode())
                    except queue.Empty:
                        self.wfile.write(b": keepalive\n\n")
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError):
                pass
            finally:
                with clients_lock:
                    if q in clients: clients.remove(q)
            return
        if path in ("/", "/index.html"):
            body = HTML.encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        self.send_response(404); self.end_headers()


HTML = r"""<!doctype html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Gesten live</title>
<link rel="icon" href="data:image/svg+xml,%3Csvg xmlns=%27http://www.w3.org/2000/svg%27 viewBox=%270 0 16 16%27%3E%3Ccircle cx=%278%27 cy=%278%27 r=%276%27 fill=%27%232a78d6%27/%3E%3C/svg%3E">
<style>
:root{color-scheme:light;
  --surface:#fcfcfb;--page:#f9f9f7;--ink:#0b0b0b;--ink2:#52514e;--muted:#898781;--grid:#e1e0d9;--axis:#c3c2b7;--border:rgba(11,11,11,.10);
  --c1:#2a78d6;--c2:#eb6834;--c3:#1baf7a;            /* Klassen: none, circle, shaking (Slots 1-3) */
  --ax:#008300;--ay:#4a3aa7;--az:#e34948;             /* Sensorachsen x, y, z (Slots 6-8) */
  --good:#0ca30c;--critical:#d03b3b;}
@media (prefers-color-scheme:dark){
  :root:where(:not([data-theme="light"])){color-scheme:dark;
    --surface:#1a1a19;--page:#0d0d0d;--ink:#fff;--ink2:#c3c2b7;--muted:#898781;--grid:#2c2c2a;--axis:#383835;--border:rgba(255,255,255,.10);
    --c1:#3987e5;--c2:#d95926;--c3:#199e70;--ax:#008300;--ay:#9085e9;--az:#e66767;}}
:root[data-theme="dark"]{color-scheme:dark;
    --surface:#1a1a19;--page:#0d0d0d;--ink:#fff;--ink2:#c3c2b7;--muted:#898781;--grid:#2c2c2a;--axis:#383835;--border:rgba(255,255,255,.10);
    --c1:#3987e5;--c2:#d95926;--c3:#199e70;--ax:#008300;--ay:#9085e9;--az:#e66767;}
*{box-sizing:border-box}
body{margin:0;background:var(--page);color:var(--ink);font:14px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif;padding:16px;max-width:1100px;margin-inline:auto}
header{display:flex;flex-wrap:wrap;align-items:baseline;gap:8px 16px;margin-bottom:12px}
h1{font-size:18px;font-weight:600;margin:0}
.status{color:var(--ink2);font-size:13px;display:flex;gap:14px;flex-wrap:wrap}
.status b{font-weight:600;color:var(--ink)}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:5px;vertical-align:middle;background:var(--muted)}
.dot.on{background:var(--good)}.dot.off{background:var(--critical)}
button{font:inherit;color:var(--ink);background:var(--surface);border:1px solid var(--border);border-radius:6px;padding:4px 10px;cursor:pointer}
button:hover{background:var(--grid)}
.grid{display:grid;grid-template-columns:1fr;gap:16px}
@media(min-width:760px){.grid.two{grid-template-columns:minmax(0,1fr) minmax(0,1fr)}}
.card{background:var(--surface);border:1px solid var(--border);border-radius:10px;padding:16px;min-width:0}
.card h2{font-size:13px;font-weight:600;color:var(--ink2);margin:0 0 10px;text-transform:none}
.hero{display:flex;align-items:center;gap:16px;flex-wrap:wrap}
.hero .sw{width:20px;height:20px;border-radius:4px;background:var(--muted);flex:none}
.hero .val{font-size:52px;font-weight:600;line-height:1.05}
.hero .sub{color:var(--ink2)}
.leds{display:flex;gap:10px;flex-wrap:wrap;margin-top:12px}
.led{display:flex;align-items:center;gap:8px;border:1px solid var(--border);border-radius:999px;padding:4px 12px 4px 6px;color:var(--ink2)}
.led .bulb{width:14px;height:14px;border-radius:50%;border:2px solid var(--axis);background:transparent}
.led.on .bulb{border-color:transparent}
.led.on{color:var(--ink)}
.legend{display:flex;gap:14px;flex-wrap:wrap;color:var(--ink2);font-size:13px;margin-bottom:8px}
.legend span{display:inline-flex;align-items:center;gap:6px}
.legend i{display:inline-block;width:12px;height:12px;border-radius:3px}
.legend i.line{height:2px;width:16px;border-radius:1px}
canvas{display:block;width:100%;touch-action:none}
.bars{display:grid;grid-template-columns:auto 1fr auto;gap:8px 12px;align-items:center}
.bars .lbl{color:var(--ink2)}
.bars .track{position:relative;height:24px}
.bars .fill{position:absolute;left:0;top:0;height:24px;border-radius:0 4px 4px 0;width:0;transition:width .12s linear}
.bars .v{font-variant-numeric:tabular-nums;color:var(--ink);min-width:3.5em;text-align:right}
.tip{position:fixed;pointer-events:none;background:var(--surface);color:var(--ink);border:1px solid var(--border);border-radius:6px;padding:6px 10px;font-size:12px;box-shadow:0 4px 14px rgba(0,0,0,.12);display:none;z-index:9;white-space:nowrap}
.tip b{font-weight:600}
.tip .k{display:inline-block;width:14px;height:2px;vertical-align:middle;margin-right:6px}
table{border-collapse:collapse;width:100%;font-size:13px;font-variant-numeric:tabular-nums}
th,td{text-align:right;padding:4px 8px;border-bottom:1px solid var(--grid)}
th:first-child,td:first-child,th:last-child,td:last-child{text-align:left}
th{color:var(--ink2);font-weight:600}
.log{font:12px/1.4 ui-monospace,SFMono-Regular,Menlo,monospace;color:var(--ink2);white-space:pre-wrap;max-height:120px;overflow:auto;margin:0}
.hidden{display:none}
.note{color:var(--muted);font-size:13px}
body[data-mode="clap"] .only-gesture, body[data-mode="presence"] .only-gesture, body[data-mode="house"] .only-gesture{display:none}
body:not([data-mode="clap"]):not([data-mode="house"]) .only-clap{display:none}
body:not([data-mode="presence"]):not([data-mode="house"]) .only-presence{display:none}
/* Funktionsmenue (Burger) */
.menuwrap{position:relative;align-self:center}
.burger{font-size:18px;line-height:1;padding:3px 9px}
.menu{position:absolute;left:0;top:calc(100% + 6px);background:var(--surface);border:1px solid var(--border);border-radius:10px;box-shadow:0 8px 24px rgba(0,0,0,.14);padding:6px;min-width:300px;max-width:min(92vw,420px);z-index:20}
.menu button{display:flex;width:100%;text-align:left;border:0;background:transparent;border-radius:6px;padding:7px 10px;gap:10px;align-items:flex-start}
.menu button:hover:not(:disabled){background:var(--grid)}
.menu button:disabled{cursor:default}
.menu button:disabled .t,.menu button:disabled .d{color:var(--muted)}
.menu .live{width:8px;height:8px;border-radius:50%;background:var(--axis);margin-top:6px;flex:none}
.menu .live.on{background:var(--good)}
.menu button[data-m="auto"] .live{visibility:hidden}
.menu .t{display:block;font-weight:600;color:var(--ink)}
.menu .t::before{content:"";display:inline-block;width:1.2em}
.menu button[aria-checked="true"] .t::before{content:"\2713"}
.menu .d{display:block;color:var(--ink2);font-size:12px;padding-left:1.2em}
.menu hr{border:0;border-top:1px solid var(--grid);margin:6px 0}
</style>
</head>
<body>
<header>
  <span class="menuwrap">
    <button class="burger" id="burger" aria-label="Funktion wählen" aria-haspopup="menu" aria-expanded="false">&#9776;</button>
    <div class="menu hidden" id="menu" role="menu">
      <button data-m="auto" role="menuitemradio"><i class="live"></i><span><span class="t">Automatisch</span><span class="d">Ansicht folgt den Daten, die das Board gerade sendet</span></span></button>
      <hr>
      <button data-m="gesture" role="menuitemradio"><i class="live"></i><span><span class="t">Gesten: Kreisen / Schütteln</span><span class="d">IMU BMI270, Sketch 02-imu-gesture</span></span></button>
      <button data-m="clap" role="menuitemradio"><i class="live"></i><span><span class="t">Klatschen / Schnipsen</span><span class="d">Mikrofon, Sketch 03-clap-led oder 05-clap-model, Zephyr clap_model</span></span></button>
      <button data-m="presence" role="menuitemradio"><i class="live"></i><span><span class="t">Anwesenheit per Bluetooth</span><span class="d">Handy erreichbar? Zephyr bt_page_presence (WLAN-Variante: Sketch 06-presence-ap)</span></span></button>
      <button data-m="house" role="menuitemradio"><i class="live"></i><span><span class="t">Haus: Klatschen + Bluetooth</span><span class="d">Licht per Klatschen, aus beim Verlassen des Hauses. Zephyr house</span></span></button>
      <hr>
      <button disabled><i class="live"></i><span><span class="t">Radar-Anwesenheit (BGT60TR13C)</span><span class="d">noch ohne Firmware</span></span></button>
      <button disabled><i class="live"></i><span><span class="t">Luftdruck / Höhe (DPS368)</span><span class="d">noch ohne Firmware</span></span></button>
      <button disabled><i class="live"></i><span><span class="t">Magnetfeld (BMM350)</span><span class="d">noch ohne Firmware</span></span></button>
    </div>
  </span>
  <h1 id="h1">Gesten live</h1>
  <div class="status">
    <span><i class="dot" id="dot"></i><span id="conn">verbinde ...</span></span>
    <span>Frames/s <b id="fps">0</b></span>
    <span>Inferenzen/s <b id="ips">0</b></span>
    <span>gesamt <b id="tot">0</b></span>
  </div>
  <span style="flex:1"></span>
  <span style="display:flex;gap:8px"><button id="tbl" class="only-gesture">Tabelle</button><button id="theme">Dunkel/Hell</button></span>
</header>

<div class="grid two">
  <section class="card">
    <h2 id="herotitle">Erkennung</h2>
    <div class="hero"><div class="sw" id="hsw"></div><div><div class="val" id="hval">...</div><div class="sub" id="hsub">warte auf Daten vom Board</div></div></div>
    <div class="leds">
      <span class="led" id="led1"><i class="bulb"></i><span id="led1t">LED1</span></span>
      <span class="led" id="led2"><i class="bulb"></i><span id="led2t">LED2: circle</span></span>
    </div>
  </section>
  <section class="card only-clap">
    <h2>Lautstärke, letzte 10 s (RMS, logarithmisch)</h2>
    <div class="legend"><span><i class="line" style="background:var(--c1)"></i>Pegel</span><span><i class="line" style="background:var(--muted)"></i>Grundrauschen</span><span><i class="line" style="background:var(--c2);width:2px;height:12px"></i>Klatscher (halb: erster von zwei)</span><span><i class="line" style="background:var(--axis);width:2px;height:12px"></i>verworfen</span></div>
    <canvas id="lvl" height="200"></canvas>
  </section>
  <section class="card only-presence">
    <h2 id="prstitle">Signalstärke des Handys, letzte 10 Minuten (dBm)</h2>
    <div class="legend"><span id="prsrssi"><i class="line" style="background:var(--c1)"></i>RSSI stärkster Client</span><span><i style="background:var(--c1);opacity:.35"></i>anwesend</span><span><i class="line" style="background:var(--c2);width:2px;height:12px"></i>An-/Abmeldung</span></div>
    <canvas id="prs" height="200"></canvas>
    <p class="note" id="apnote"></p>
  </section>
  <section class="card only-clap hidden" id="modelcard">
    <h2>Modell: letzte Entscheidung</h2>
    <div class="legend" id="mlegend"></div>
    <div class="bars" id="mbars"></div>
    <p class="note" id="mnote"></p>
  </section>
  <section class="card only-gesture">
    <h2>Scores der letzten Inferenz</h2>
    <div class="legend" id="legend"></div>
    <div class="bars" id="bars"></div>
  </section>
</div>

<section class="card only-gesture" style="margin-top:16px">
  <h2>Erkannte Klasse, letzte 60 Sekunden</h2>
  <canvas id="tl" height="56"></canvas>
</section>
<section class="card only-clap" style="margin-top:16px">
  <h2>Klatscher und Schalterzustand (LED1), letzte 60 Sekunden</h2>
  <div class="legend"><span><i style="background:var(--c1);opacity:.35"></i>LED1 an</span><span><i class="line" style="background:var(--c2);width:2px;height:12px"></i>Klatscher</span><span><i class="line" style="background:var(--axis);width:2px;height:12px"></i>verworfen</span></div>
  <canvas id="ctl" height="56"></canvas>
</section>

<div class="grid two only-gesture" style="margin-top:16px">
  <section class="card">
    <h2>Beschleunigung, letzte 10 s (m/s²)</h2>
    <div class="legend"><span><i class="line" style="background:var(--ax)"></i>x</span><span><i class="line" style="background:var(--ay)"></i>y</span><span><i class="line" style="background:var(--az)"></i>z</span></div>
    <canvas id="acc" height="200"></canvas>
  </section>
  <section class="card">
    <h2>Drehrate, letzte 10 s (°/s)</h2>
    <div class="legend"><span><i class="line" style="background:var(--ax)"></i>x</span><span><i class="line" style="background:var(--ay)"></i>y</span><span><i class="line" style="background:var(--az)"></i>z</span></div>
    <canvas id="gyr" height="200"></canvas>
  </section>
</div>

<section class="card hidden only-gesture" id="tablecard" style="margin-top:16px">
  <h2>Letzte 25 Inferenzen</h2>
  <table><thead><tr id="thead"></tr></thead><tbody id="tbody"></tbody></table>
</section>

<section class="card" style="margin-top:16px">
  <h2>Meldungen des Boards</h2>
  <pre class="log" id="log"></pre>
  <p class="note" id="rawnote"></p>
</section>

<div class="tip" id="tip"></div>

<script>
(function(){
"use strict";
const $ = id => document.getElementById(id);
const css = n => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
const CLASS_VARS = ['--c1','--c2','--c3'];
let labels = [];                 // Reihenfolge wie vom Board gemeldet
let frames = [];                 // [t, ax, ay, az, gx, gy, gz], letzte 10 s
let infs = [];                   // {t, n, scores, best}, letzte 60 s
let last = null;
let fpsCount = 0, ipsCount = 0;
let mode = null;                 // wirksame Ansicht: 'gesture', 'clap', 'presence', 'house' (null: noch keine Daten)
let pinned = null;               // im Menue gewaehlt; null = automatisch nach den Daten des Boards
const seen = { gesture: 0, clap: 0, presence: 0, house: 0 };   // Server-Zeit der letzten Daten je Funktion
let levels = [];                 // [t, rms, floor], letzte 10 s
let claps = [];                  // {t, n, dur, peak, ratio, led1}, letzte 60 s
let rejects = [];                // {t, dur, peak, ratio, why}, letzte 60 s
let led1State = null;
let pres = [];                   // [t, rssi, n], letzte 10 min
let presEv = [];                 // {t, kind, ...}
let apInfo = null, paging = false;   // paging: Bluetooth-Paging statt WLAN-Access-Point (AP-Zeile mit ssid BT-...)
let lastClap = null, lastPres = null, lastHouse = null;
const MODES = {
  gesture:  { title: 'Gesten', hero: 'Erkannte Geste', led1: 'LED1: shaking', led2: 'LED2: circle' },
  clap:     { title: 'Klatsch-Schalter', hero: 'Klatsch-Schalter', led1: 'LED1: Schalter', led2: null },
  presence: { title: 'Anwesenheit', hero: 'Anwesenheit per Access Point', led1: 'LED1: anwesend', led2: null },
  house:    { title: 'Haus', hero: 'Licht im Haus', led1: 'LED1: Licht', led2: 'LED2: Handy anwesend' } };
const srvNow = () => clockOffset ? Date.now() / 1000 - clockOffset : Math.max(...Object.values(seen));
function applyMode(m){
  if (mode === m) return; mode = m; document.body.dataset.mode = m || '';
  const d = MODES[m] || MODES.gesture;
  $('h1').textContent = d.title + ' live'; document.title = d.title + ' live';
  $('herotitle').textContent = m === 'presence' && paging ? 'Anwesenheit per Bluetooth' : d.hero;
  $('led1t').textContent = d.led1; $('led2').classList.toggle('hidden', !d.led2); if (d.led2) $('led2t').textContent = d.led2;
  renderHero(); refreshMenu();
}
/* Automatik: beide Datenarten (Mikrofon und Bluetooth) innerhalb von 30 s = gemeinsame Firmware "house",
 * sonst die Funktion, von der zuletzt Daten kamen. */
function detect(){
  if (pinned) { applyMode(pinned); return; }
  const now = srvNow(), recent = k => seen[k] > 0 && now - seen[k] < 30;
  let m = null;
  if (recent('house') || (recent('clap') && recent('presence'))) m = 'house';
  else { let best = 0; for (const k of ['gesture', 'clap', 'presence']) if (seen[k] > best) { best = seen[k]; m = k; } }
  if (m) applyMode(m);
}
function setLed(id, on, col){ const el = $(id); el.classList.toggle('on', !!on); el.querySelector('.bulb').style.background = on ? col : 'transparent'; }
function renderHero(){
  const sw = $('hsw'), hv = $('hval'), hs = $('hsub');
  const waiting = txt => { hv.textContent = '...'; hs.textContent = txt; sw.style.background = css('--muted'); setLed('led1', false); };
  if (mode === 'gesture' || mode == null) {
    if (!last) return; const bi = labels.indexOf(last.best);
    hv.textContent = last.best; sw.style.background = colorOf(bi); hs.textContent = 'Konfidenz ' + fmt(last.scores[bi] ?? 0) + '  ·  Inferenz #' + last.n;
    setLed('led1', last.best === 'shaking', colorOf(labels.indexOf('shaking'))); setLed('led2', last.best === 'circle', colorOf(labels.indexOf('circle')));
  } else if (mode === 'clap') {
    if (!lastClap) return waiting('warte auf den ersten Klatscher');
    const ev = lastClap; hv.textContent = ev.led1 ? 'an' : 'aus'; sw.style.background = ev.led1 ? css('--c1') : css('--muted');
    hs.textContent = clapText(ev); setLed('led1', ev.led1, css('--c1'));
  } else if (mode === 'presence') {
    if (!lastPres) return waiting('warte auf die erste Anwesenheitsmeldung');
    const ev = lastPres; hv.textContent = ev.on ? 'anwesend' : 'abwesend'; sw.style.background = ev.on ? css('--c1') : css('--muted');
    hs.textContent = paging ? (ev.on ? 'Handy antwortet auf Paging  ·  ' + ev.mac : 'Handy antwortet nicht (aus oder außer Reichweite)')
                            : ev.n + (ev.n === 1 ? ' Gerät' : ' Geräte') + (ev.rssi != null ? '  ·  ' + ev.rssi + ' dBm  ·  ' + ev.mac : '');
    setLed('led1', ev.on, css('--c1'));
  } else if (mode === 'house') {
    const on = lastHouse ? lastHouse.on : lastClap ? lastClap.led1 : false;
    hv.textContent = on ? 'Licht an' : 'Licht aus'; sw.style.background = on ? css('--c1') : css('--muted');
    const waitingSecond = lastClap && lastClap.kind === 'einzel' && (!lastHouse || lastClap.t > lastHouse.t) && srvNow() - lastClap.t < 2;
    hs.textContent = (lastPres ? (lastPres.on ? 'Handy anwesend' : 'Handy abwesend') : 'Handy: noch keine Meldung') + (lastHouse ? '  ·  zuletzt: ' + lastHouse.why : '') + (waitingSecond ? '  ·  1. Klatscher erkannt, warte auf den zweiten' : '');
    setLed('led1', on, css('--c1')); setLed('led2', lastPres && lastPres.on, css('--c3'));
  }
}
function applyPresence(ev){
  if (ev.kind === 'ap') {
    apInfo = ev; paging = /^BT-/.test(ev.ssid);
    $('apnote').textContent = paging ? 'Bluetooth-Paging (Remote Name Request alle 15 s), kein WLAN' : 'WLAN "' + ev.ssid + '", IP ' + ev.ip + ', Kanal ' + ev.channel;
    $('prstitle').textContent = paging ? 'Handy per Bluetooth erreichbar, letzte 10 Minuten' : 'Signalstärke des Handys, letzte 10 Minuten (dBm)';
    $('prsrssi').classList.toggle('hidden', paging);
    if (mode === 'presence') $('herotitle').textContent = paging ? 'Anwesenheit per Bluetooth' : MODES.presence.hero;
    return;
  }
  if (ev.kind === 'presence') { lastPres = ev; renderHero(); }
}
function applyHouse(ev){ lastHouse = ev; renderHero(); }
function clapText(ev){
  if (ev.kind === 'einzel') return '1. Klatscher #' + ev.n + ' erkannt (Konfidenz ' + fmt(ev.conf) + ')  ·  warte auf den zweiten';
  if (ev.kind === 'doppel') return 'Doppelklatscher #' + ev.n + '  ·  Abstand ' + ev.gap + ' ms  ·  Konfidenz ' + fmt(ev.conf);
  return 'Klatscher #' + ev.n + '  ·  Dauer ' + ev.dur + ' ms  ·  Spitze ' + ev.peak.toLocaleString('de-DE') + ' (x' + String(ev.ratio).replace('.', ',') + ')'; }
function drawPresence(now){
  const c = $('prs'); if (!c.offsetParent) return; const { ctx, w, h } = setupCanvas(c);
  const span = 600, padL = 44, padR = 16, padT = 8, padB = 22, pw = w - padL - padR, ph = h - padT - padB;
  ctx.clearRect(0, 0, w, h);
  const lo = -100, hi = -20;
  const x = t => padL + pw - (now - t) / span * pw, y = v => padT + ph - (v - lo) / (hi - lo) * ph;
  /* anwesend-Band aus den P-Zeilen (n > 0) */
  ctx.fillStyle = css('--c1'); ctx.globalAlpha = 0.18;
  let runStart = null;
  for (let i = 0; i <= pres.length; i++) { const on = i < pres.length && pres[i][2] > 0;
    if (on && runStart == null) runStart = pres[i][0];
    if (!on && runStart != null) { const x0 = Math.max(padL, x(runStart)), x1 = Math.min(padL + pw, x(i < pres.length ? pres[i][0] : now)); if (x1 > x0) ctx.fillRect(x0, padT, x1 - x0, ph); runStart = null; } }
  ctx.globalAlpha = 1;
  ctx.strokeStyle = css('--grid'); ctx.lineWidth = 1; ctx.fillStyle = css('--muted'); ctx.font = '11px system-ui, sans-serif'; ctx.textAlign = 'right';
  if (!paging) for (const v of [-100, -80, -60, -40]) { const yy = Math.round(y(v)) + 0.5; ctx.beginPath(); ctx.moveTo(padL, yy); ctx.lineTo(padL + pw, yy); ctx.stroke(); ctx.fillText(String(v), padL - 6, yy + 4); }
  ctx.strokeStyle = css('--axis'); ctx.beginPath(); ctx.moveTo(padL, padT + ph + 0.5); ctx.lineTo(padL + pw, padT + ph + 0.5); ctx.stroke();
  ctx.textAlign = 'center'; for (let s = 0; s <= span; s += 120) ctx.fillText(s === 0 ? 'jetzt' : '-' + (s / 60) + ' min', padL + pw - s / span * pw, h - 6);
  ctx.strokeStyle = css('--c2'); ctx.lineWidth = 2;
  for (const ev of presEv) { if (ev.kind !== 'join' && ev.kind !== 'leave') continue; const xx = x(ev.t); if (xx < padL) continue; ctx.beginPath(); ctx.moveTo(xx, padT); ctx.lineTo(xx, padT + ph); ctx.stroke(); }
  if (paging) { ctx.fillStyle = css('--ink2'); ctx.textAlign = 'left'; ctx.fillText('blau = erreichbar, Strich = Wechsel', padL + 6, padT + 14); return; }
  ctx.strokeStyle = css('--c1'); ctx.lineWidth = 2; ctx.lineJoin = 'round'; ctx.beginPath(); let st = false;
  for (const p of pres) { if (p[2] <= 0) { st = false; continue; } const xx = x(p[0]); if (xx < padL) continue; const yy = y(Math.max(lo, Math.min(hi, p[1]))); if (!st) { ctx.moveTo(xx, yy); st = true; } else ctx.lineTo(xx, yy); }
  ctx.stroke();
  if (pres.length && pres[pres.length - 1][2] > 0) { const p = pres[pres.length - 1]; const yy = y(Math.max(lo, Math.min(hi, p[1]))); ctx.fillStyle = css('--c1'); ctx.beginPath(); ctx.arc(padL + pw, yy, 4, 0, 7); ctx.fill(); ctx.strokeStyle = css('--surface'); ctx.lineWidth = 2; ctx.stroke(); ctx.fillStyle = css('--ink2'); ctx.textAlign = 'right'; ctx.fillText(p[1] + ' dBm', padL + pw - 8, yy - 8); }
}
const hover = { tl: null, acc: null, gyr: null };

/* Theme-Umschalter: data-theme gewinnt gegen die OS-Einstellung; ?theme=dark|light setzt es vorab */
const qt = new URLSearchParams(location.search).get('theme'); if (qt === 'dark' || qt === 'light') document.documentElement.dataset.theme = qt;
$('theme').onclick = () => {
  const r = document.documentElement;
  const dark = r.dataset.theme ? r.dataset.theme === 'dark' : matchMedia('(prefers-color-scheme: dark)').matches;
  r.dataset.theme = dark ? 'light' : 'dark';
};
$('tbl').onclick = () => $('tablecard').classList.toggle('hidden');

function colorOf(i){ return css(CLASS_VARS[i] || '--muted'); }

function buildLegend(){
  const lg = $('legend'); lg.textContent = '';
  const bars = $('bars'); bars.textContent = '';
  labels.forEach((l, i) => {
    const s = document.createElement('span'); const sw = document.createElement('i'); sw.style.background = colorOf(i);
    s.appendChild(sw); s.appendChild(document.createTextNode(l)); lg.appendChild(s);
    const lbl = document.createElement('div'); lbl.className = 'lbl'; lbl.textContent = l;
    const tr = document.createElement('div'); tr.className = 'track';
    const f = document.createElement('div'); f.className = 'fill'; f.style.background = colorOf(i); f.id = 'bar' + i; tr.appendChild(f);
    tr.dataset.i = i; tr.addEventListener('pointermove', barTip); tr.addEventListener('pointerleave', hideTip);
    const v = document.createElement('div'); v.className = 'v'; v.id = 'barv' + i; v.textContent = '0,00';
    bars.appendChild(lbl); bars.appendChild(tr); bars.appendChild(v);
  });
  const th = $('thead'); th.textContent = '';
  ['Zeit', ...labels, 'Ergebnis'].forEach(h => { const c = document.createElement('th'); c.textContent = h; th.appendChild(c); });
}

const fmt = x => x.toFixed(2).replace('.', ',');
const tfmt = t => new Date(t * 1000).toLocaleTimeString('de-DE') + ',' + String(Math.floor((t % 1) * 10));

const MODEL_COLORS = { clap: '--c2', snap: '--c3' };   /* clap = Farbe der Klatscher-Markierungen, other bleibt grau */
let modelLabels = [];
function applyModel(ev){
  const card = $('modelcard'); card.classList.remove('hidden');
  if (ev.labels.join() !== modelLabels.join()) {
    modelLabels = ev.labels; const lg = $('mlegend'); lg.textContent = ''; const bars = $('mbars'); bars.textContent = '';
    modelLabels.forEach((l, i) => {
      const col = css(MODEL_COLORS[l] || '--muted');
      const s = document.createElement('span'); const sw = document.createElement('i'); sw.style.background = col; s.appendChild(sw); s.appendChild(document.createTextNode(l)); lg.appendChild(s);
      const lbl = document.createElement('div'); lbl.className = 'lbl'; lbl.textContent = l;
      const tr = document.createElement('div'); tr.className = 'track'; const f = document.createElement('div'); f.className = 'fill'; f.style.background = col; f.id = 'mbar' + i; tr.appendChild(f);
      const v = document.createElement('div'); v.className = 'v'; v.id = 'mbarv' + i; v.textContent = '0,00';
      bars.appendChild(lbl); bars.appendChild(tr); bars.appendChild(v);
    });
  }
  ev.scores.forEach((sc, i) => { const b = $('mbar' + i); if (b) b.style.width = (Math.max(0, Math.min(1, sc)) * 100) + '%'; const v = $('mbarv' + i); if (v) v.textContent = fmt(sc); });
  $('mnote').textContent = 'Entscheidung: ' + ev.best + '  ·  Inferenz ' + String(ev.ms).replace('.', ',') + ' ms';
}

function applyClap(ev){
  if (ev.kind === 'einzel') { ev.led1 = led1State; }        /* erster Klatscher: Schalter unveraendert, Band laeuft weiter */
  else led1State = ev.led1;
  lastClap = ev; renderHero(); }

function applyInference(inf){
  last = inf;
  inf.scores.forEach((s, i) => { const b = $('bar' + i); if (b) b.style.width = (Math.max(0, Math.min(1, s)) * 100) + '%'; const v = $('barv' + i); if (v) v.textContent = fmt(s); });
  renderHero();
}

function updateTable(){
  const tb = $('tbody'); tb.textContent = '';
  infs.slice(-25).reverse().forEach(inf => {
    const tr = document.createElement('tr');
    [tfmt(inf.t), ...inf.scores.map(fmt), inf.best].forEach(v => { const td = document.createElement('td'); td.textContent = v; tr.appendChild(td); });
    tb.appendChild(tr);
  });
}

/* ---------- Zeichnen ---------- */
function setupCanvas(c){
  const dpr = devicePixelRatio || 1, w = c.clientWidth, h = c.getAttribute('height') | 0;
  if (c.width !== Math.round(w * dpr) || c.height !== Math.round(h * dpr)) { c.width = Math.round(w * dpr); c.height = Math.round(h * dpr); }
  const ctx = c.getContext('2d'); ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  return { ctx, w, h };
}

function drawTimeline(now){
  const c = $('tl'), { ctx, w, h } = setupCanvas(c);
  const span = 60, top = 4, barH = 24, axisY = top + barH + 16;
  ctx.clearRect(0, 0, w, h);
  const x = t => w - (now - t) / span * w;
  /* Laeufe gleicher Klasse als ein Block, 2px Oberflaechenluecke zwischen Bloecken */
  let i = 0;
  while (i < infs.length) {
    let j = i; while (j + 1 < infs.length && infs[j + 1].best === infs[i].best) j++;
    const x0 = Math.max(0, x(infs[i].t) - 0.2 / span * w), x1 = x(infs[j].t);
    if (x1 > 0) { ctx.fillStyle = colorOf(labels.indexOf(infs[i].best)); ctx.fillRect(x0 + 1, top, Math.max(1, x1 - x0 - 2), barH); }
    i = j + 1;
  }
  ctx.strokeStyle = css('--axis'); ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(0, top + barH + 0.5); ctx.lineTo(w, top + barH + 0.5); ctx.stroke();
  ctx.fillStyle = css('--muted'); ctx.font = '11px system-ui, sans-serif'; ctx.textAlign = 'center';
  for (let s = 0; s <= span; s += 10) { const xx = w - s / span * w; ctx.fillText(s === 0 ? 'jetzt' : '-' + s + ' s', Math.min(w - 16, Math.max(16, xx)), axisY); }
  if (hover.tl != null) { ctx.strokeStyle = css('--ink2'); ctx.beginPath(); ctx.moveTo(hover.tl + 0.5, top); ctx.lineTo(hover.tl + 0.5, top + barH); ctx.stroke(); }
}

function niceMax(v, minv){ v = Math.max(v, minv); const p = Math.pow(10, Math.floor(Math.log10(v))); const m = v / p; const n = m <= 1 ? 1 : m <= 2 ? 2 : m <= 5 ? 5 : 10; return n * p; }

function drawLines(id, cols, minRange, now){
  const c = $(id), { ctx, w, h } = setupCanvas(c);
  const span = 10, padL = 44, padR = 58, padT = 8, padB = 22, pw = w - padL - padR, ph = h - padT - padB;
  ctx.clearRect(0, 0, w, h);
  let m = 0; for (const f of frames) for (const k of cols) m = Math.max(m, Math.abs(f[k]));
  const R = niceMax(m * 1.1, minRange);
  const x = t => padL + pw - (now - t) / span * pw, y = v => padT + ph / 2 - v / R * ph / 2;
  /* Gitter: Hairline, eine Stufe von der Oberflaeche */
  ctx.strokeStyle = css('--grid'); ctx.lineWidth = 1; ctx.fillStyle = css('--muted'); ctx.font = '11px system-ui, sans-serif'; ctx.textAlign = 'right';
  for (const v of [-R, -R / 2, 0, R / 2, R]) { const yy = Math.round(y(v)) + 0.5; ctx.beginPath(); ctx.moveTo(padL, yy); ctx.lineTo(padL + pw, yy); ctx.stroke(); ctx.fillText(String(v).replace('.', ','), padL - 6, yy + 4); }
  ctx.strokeStyle = css('--axis'); ctx.beginPath(); ctx.moveTo(padL, Math.round(y(0)) + 0.5); ctx.lineTo(padL + pw, Math.round(y(0)) + 0.5); ctx.stroke();
  ctx.textAlign = 'center';
  for (let s = 0; s <= span; s += 2) { const xx = padL + pw - s / span * pw; ctx.fillText(s === 0 ? 'jetzt' : '-' + s + ' s', xx, h - 6); }
  const colors = [css('--ax'), css('--ay'), css('--az')];
  ctx.lineWidth = 2; ctx.lineJoin = 'round'; ctx.lineCap = 'round';
  cols.forEach((k, si) => {
    ctx.strokeStyle = colors[si]; ctx.beginPath(); let started = false;
    for (const f of frames) { const xx = x(f[0]); if (xx < padL) continue; const yy = y(f[k]); if (!started) { ctx.moveTo(xx, yy); started = true; } else ctx.lineTo(xx, yy); }
    ctx.stroke();
  });
  /* Direktbeschriftung: Achsname plus Endwert je Linie (Identitaet nicht nur ueber Farbe), Text in Texttoken */
  if (frames.length) {
    const f = frames[frames.length - 1]; ctx.textAlign = 'left'; ctx.font = '11px system-ui, sans-serif';
    /* Endpunkte mit 2px Oberflaechenring */
    cols.forEach((k, si) => { const yy = y(f[k]); ctx.fillStyle = colors[si]; ctx.beginPath(); ctx.arc(padL + pw, yy, 4, 0, 7); ctx.fill(); ctx.strokeStyle = css('--surface'); ctx.lineWidth = 2; ctx.stroke(); });
    /* Beschriftungen: bei Kollision auseinanderschieben (min. 13px) und per Leader-Linie am Endpunkt halten */
    const items = cols.map((k, si) => ({ si, y0: y(f[k]), y1: y(f[k]), text: 'xyz'[si] + ' ' + f[k].toFixed(1).replace('.', ',') })).sort((a, b) => a.y0 - b.y0);
    const minGap = 13;
    for (let pass = 0; pass < 4; pass++) {
      for (let i = 1; i < items.length; i++) if (items[i].y1 - items[i - 1].y1 < minGap) { const push = (minGap - (items[i].y1 - items[i - 1].y1)) / 2; items[i - 1].y1 -= push; items[i].y1 += push; }
      const lo = padT + 6, hi = padT + ph - 6;
      if (items[0].y1 < lo) { const d = lo - items[0].y1; items.forEach(it => it.y1 += d); }
      if (items[items.length - 1].y1 > hi) { const d = items[items.length - 1].y1 - hi; items.forEach(it => it.y1 -= d); }
    }
    ctx.lineWidth = 1;
    for (const it of items) {
      if (Math.abs(it.y1 - it.y0) > 1) { ctx.strokeStyle = css('--axis'); ctx.beginPath(); ctx.moveTo(padL + pw + 5, it.y0); ctx.lineTo(padL + pw + 12, it.y1); ctx.stroke(); }
      ctx.fillStyle = css('--ink2'); ctx.fillText(it.text, padL + pw + 14, it.y1 + 4);
    }
  }
  const hx = hover[id];
  if (hx != null && hx >= padL && hx <= padL + pw && frames.length) {
    let best = null, bd = 1e9; for (const f of frames) { const d = Math.abs(x(f[0]) - hx); if (d < bd) { bd = d; best = f; } }
    if (best) { const xx = x(best[0]); ctx.strokeStyle = css('--ink2'); ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(Math.round(xx) + 0.5, padT); ctx.lineTo(Math.round(xx) + 0.5, padT + ph); ctx.stroke(); hover[id + '_f'] = best; }
  }
}

function drawLevel(now){
  const c = $('lvl'); if (!c.offsetParent) return; const { ctx, w, h } = setupCanvas(c);
  const span = 10, padL = 48, padR = 16, padT = 8, padB = 22, pw = w - padL - padR, ph = h - padT - padB;
  ctx.clearRect(0, 0, w, h);
  const lo = 1.5, hi = 4.6;   /* log10: ca. 30 bis 40000 auf der 16-Bit-Skala */
  const x = t => padL + pw - (now - t) / span * pw, y = v => padT + ph - (Math.log10(Math.max(v, 1)) - lo) / (hi - lo) * ph;
  ctx.strokeStyle = css('--grid'); ctx.lineWidth = 1; ctx.fillStyle = css('--muted'); ctx.font = '11px system-ui, sans-serif'; ctx.textAlign = 'right';
  for (const v of [100, 1000, 10000]) { const yy = Math.round(y(v)) + 0.5; ctx.beginPath(); ctx.moveTo(padL, yy); ctx.lineTo(padL + pw, yy); ctx.stroke(); ctx.fillText(v.toLocaleString('de-DE'), padL - 6, yy + 4); }
  ctx.strokeStyle = css('--axis'); ctx.beginPath(); ctx.moveTo(padL, padT + ph + 0.5); ctx.lineTo(padL + pw, padT + ph + 0.5); ctx.stroke();
  ctx.textAlign = 'center'; for (let s = 0; s <= span; s += 2) ctx.fillText(s === 0 ? 'jetzt' : '-' + s + ' s', padL + pw - s / span * pw, h - 6);
  /* verworfene Impulse und Klatscher als Markierungen hinter den Linien */
  ctx.strokeStyle = css('--axis'); ctx.lineWidth = 2;
  for (const ev of rejects) { const xx = x(ev.t); if (xx < padL) continue; ctx.beginPath(); ctx.moveTo(xx, padT); ctx.lineTo(xx, padT + ph); ctx.stroke(); }
  ctx.strokeStyle = css('--c2'); ctx.lineWidth = 2;
  for (const ev of claps) { const xx = x(ev.t); if (xx < padL) continue; ctx.beginPath(); ctx.moveTo(xx, ev.kind === 'einzel' ? padT + ph / 2 : padT); ctx.lineTo(xx, padT + ph); ctx.stroke(); }
  ctx.lineJoin = 'round'; ctx.lineCap = 'round';
  for (const [k, col] of [[2, css('--muted')], [1, css('--c1')]]) {
    ctx.strokeStyle = col; ctx.lineWidth = 2; ctx.beginPath(); let st = false;
    for (const l of levels) { const xx = x(l[0]); if (xx < padL) continue; const yy = y(l[k]); if (!st) { ctx.moveTo(xx, yy); st = true; } else ctx.lineTo(xx, yy); }
    ctx.stroke();
  }
  const hx = hover.lvl;
  if (hx != null && hx >= padL && hx <= padL + pw && levels.length) {
    let best = null, bd = 1e9; for (const l of levels) { const d = Math.abs(x(l[0]) - hx); if (d < bd) { bd = d; best = l; } }
    if (best) { const xx = x(best[0]); ctx.strokeStyle = css('--ink2'); ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(Math.round(xx) + 0.5, padT); ctx.lineTo(Math.round(xx) + 0.5, padT + ph); ctx.stroke(); hover.lvl_f = best; }
  }
}

function drawClapStrip(now){
  const c = $('ctl'); if (!c.offsetParent) return; const { ctx, w, h } = setupCanvas(c);
  const span = 60, top = 4, barH = 24, axisY = top + barH + 16;
  ctx.clearRect(0, 0, w, h);
  const x = t => w - (now - t) / span * w;
  /* LED1-Zustand als Band: zwischen zwei Klatschern gilt der Zustand des frueheren */
  ctx.fillStyle = css('--c1'); ctx.globalAlpha = 0.35;
  for (let i = 0; i < claps.length; i++) { if (!claps[i].led1) continue; const x0 = Math.max(0, x(claps[i].t)), x1 = i + 1 < claps.length ? x(claps[i + 1].t) : w; ctx.fillRect(x0, top, Math.max(0, x1 - x0), barH); }
  ctx.globalAlpha = 1;
  ctx.strokeStyle = css('--axis'); ctx.lineWidth = 2;
  for (const ev of rejects) { const xx = x(ev.t); if (xx < 0) continue; ctx.beginPath(); ctx.moveTo(xx, top + barH / 2); ctx.lineTo(xx, top + barH); ctx.stroke(); }
  ctx.strokeStyle = css('--c2'); ctx.lineWidth = 2;
  for (const ev of claps) { const xx = x(ev.t); if (xx < 0) continue; ctx.beginPath(); ctx.moveTo(xx, ev.kind === 'einzel' ? top + barH / 2 : top); ctx.lineTo(xx, top + barH); ctx.stroke(); }
  ctx.strokeStyle = css('--axis'); ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(0, top + barH + 0.5); ctx.lineTo(w, top + barH + 0.5); ctx.stroke();
  ctx.fillStyle = css('--muted'); ctx.font = '11px system-ui, sans-serif'; ctx.textAlign = 'center';
  for (let s = 0; s <= span; s += 10) { const xx = w - s / span * w; ctx.fillText(s === 0 ? 'jetzt' : '-' + s + ' s', Math.min(w - 16, Math.max(16, xx)), axisY); }
}

function tick(){
  const now = Date.now() / 1000 - clockOffset;
  while (levels.length && now - levels[0][0] > 10.5) levels.shift();
  while (claps.length > 1 && now - claps[1].t > 60.5) claps.shift();
  while (rejects.length && now - rejects[0].t > 60.5) rejects.shift();
  while (pres.length && now - pres[0][0] > 605) pres.shift();
  while (presEv.length && now - presEv[0].t > 605) presEv.shift();
  drawLevel(now); drawClapStrip(now); drawPresence(now);
  while (frames.length && now - frames[0][0] > 10.5) frames.shift();
  while (infs.length && now - infs[0].t > 60.5) infs.shift();
  if (mode === 'gesture' || mode == null) { drawTimeline(now); drawLines('acc', [1, 2, 3], 20, now); drawLines('gyr', [4, 5, 6], 250, now); }
  requestAnimationFrame(tick);
}
let clockOffset = 0;  /* Differenz Browser-Uhr zu Server-Zeitstempeln */

/* ---------- Tooltips ---------- */
const tip = $('tip');
function showTip(x, y, rows){
  tip.textContent = '';
  rows.forEach(r => { const d = document.createElement('div'); if (r.color) { const k = document.createElement('i'); k.className = 'k'; k.style.background = r.color; d.appendChild(k); }
    const b = document.createElement('b'); b.textContent = r.value; d.appendChild(b); d.appendChild(document.createTextNode(' ' + r.label)); tip.appendChild(d); });
  tip.style.display = 'block'; const r = tip.getBoundingClientRect();
  tip.style.left = Math.min(innerWidth - r.width - 8, x + 14) + 'px'; tip.style.top = Math.max(4, y - r.height - 10) + 'px';
}
function hideTip(){ tip.style.display = 'none'; }
function barTip(e){ const i = +e.currentTarget.dataset.i; if (!last) return; showTip(e.clientX, e.clientY, [{ value: fmt(last.scores[i]), label: labels[i], color: colorOf(i) }]); }

$('tl').addEventListener('pointermove', e => {
  const r = e.currentTarget.getBoundingClientRect(); hover.tl = e.clientX - r.left;
  const now = Date.now() / 1000 - clockOffset, t = now - (1 - hover.tl / r.width) * 60;
  let best = null, bd = 1e9; for (const inf of infs) { const d = Math.abs(inf.t - t); if (d < bd) { bd = d; best = inf; } }
  if (best && bd < 0.5) showTip(e.clientX, e.clientY, [{ value: best.best, label: 'vor ' + (now - best.t).toFixed(1).replace('.', ',') + ' s', color: colorOf(labels.indexOf(best.best)) },
    ...best.scores.map((s, i) => ({ value: fmt(s), label: labels[i], color: colorOf(i) }))]); else hideTip();
});
$('tl').addEventListener('pointerleave', () => { hover.tl = null; hideTip(); });
for (const [id, cols, unit] of [['acc', [1, 2, 3], ' m/s²'], ['gyr', [4, 5, 6], ' °/s']]) {
  $(id).addEventListener('pointermove', e => { const r = e.currentTarget.getBoundingClientRect(); hover[id] = e.clientX - r.left;
    const f = hover[id + '_f']; if (!f) return; const colors = [css('--ax'), css('--ay'), css('--az')];
    showTip(e.clientX, e.clientY, cols.map((k, i) => ({ value: f[k].toFixed(2).replace('.', ',') + unit, label: 'xyz'[i], color: colors[i] }))); });
  $(id).addEventListener('pointerleave', () => { hover[id] = null; hover[id + '_f'] = null; hideTip(); });
}

$('lvl').addEventListener('pointermove', e => { const r = e.currentTarget.getBoundingClientRect(); hover.lvl = e.clientX - r.left; const l = hover.lvl_f; if (!l) return;
  showTip(e.clientX, e.clientY, [{ value: Math.round(l[1]).toLocaleString('de-DE'), label: 'Pegel (RMS)', color: css('--c1') }, { value: Math.round(l[2]).toLocaleString('de-DE'), label: 'Grundrauschen', color: css('--muted') }, { value: 'x' + (l[1] / Math.max(l[2], 1)).toFixed(1).replace('.', ','), label: 'Verhältnis' }]); });
$('lvl').addEventListener('pointerleave', () => { hover.lvl = null; hover.lvl_f = null; hideTip(); });

/* ---------- Datenstrom ---------- */
let rawSeen = false;
const es = new EventSource('/events');
es.onmessage = ev => {
  const m = JSON.parse(ev.data);
  if (m.hello) { if (m.labels && m.labels.length) { labels = m.labels; buildLegend(); } return; }
  const lastT = (arr, key) => key ? arr[arr.length - 1][key] : arr[arr.length - 1][0];
  if (m.frames && m.frames.length) {
    if (!clockOffset) clockOffset = Date.now() / 1000 - lastT(m.frames);
    for (const f of m.frames) frames.push(f); fpsCount += m.frames.length; rawSeen = true; seen.gesture = lastT(m.frames);
  }
  if (m.inf && m.inf.length) {
    for (const inf of m.inf) { if (inf.labels.join() !== labels.join()) { labels = inf.labels; buildLegend(); } infs.push(inf); applyInference(inf); }
    if (!clockOffset) clockOffset = Date.now() / 1000 - lastT(m.inf, 't');
    ipsCount += m.inf.length; if (!$('tablecard').classList.contains('hidden')) updateTable(); seen.gesture = lastT(m.inf, 't');
  }
  if (m.level && m.level.length) { if (!clockOffset) clockOffset = Date.now() / 1000 - lastT(m.level); for (const l of m.level) levels.push(l); seen.clap = lastT(m.level); }
  if (m.clap && m.clap.length) { for (const ev of m.clap) { claps.push(ev); applyClap(ev); } seen.clap = lastT(m.clap, 't'); }
  if (m.reject && m.reject.length) { for (const ev of m.reject) rejects.push(ev); seen.clap = lastT(m.reject, 't'); }
  if (m.model && m.model.length) { for (const ev of m.model) applyModel(ev); seen.clap = lastT(m.model, 't'); }
  if (m.pres && m.pres.length) { if (!clockOffset) clockOffset = Date.now() / 1000 - lastT(m.pres); for (const p of m.pres) pres.push(p); seen.presence = lastT(m.pres);
    /* Zustand auch aus den P-Zeilen, falls der Viewer die PRAESENZ-Meldung beim Start des Boards verpasst hat */
    const p = m.pres[m.pres.length - 1], on = p[2] > 0;
    if (!lastPres) lastPres = { kind: 'presence', on, n: p[2], rssi: p[1], mac: '', fromP: true };
    else if (lastPres.on !== on) { lastPres = { kind: 'presence', on, n: p[2], rssi: p[1], mac: lastPres.mac, fromP: true }; }
    else { lastPres.n = p[2]; lastPres.rssi = p[1]; }
    renderHero(); }
  if (m.presev && m.presev.length) { for (const ev of m.presev) { presEv.push(ev); applyPresence(ev); } seen.presence = lastT(m.presev, 't'); }
  if (m.house && m.house.length) { for (const ev of m.house) applyHouse(ev); seen.house = lastT(m.house, 't'); }
  if (m.log && m.log.length) { const l = $('log'); for (const s of m.log) l.textContent += s + '\n'; l.scrollTop = l.scrollHeight; }
  if (m.stats) { $('dot').className = 'dot ' + (m.stats.connected ? 'on' : 'off'); $('conn').textContent = (m.stats.connected ? 'verbunden: ' : 'getrennt: ') + String(m.stats.port || '?').replace(/^.*\//, '');
    $('fps').textContent = fpsCount; $('ips').textContent = ipsCount; $('tot').textContent = m.stats.inferences; fpsCount = 0; ipsCount = 0;
    $('rawnote').textContent = (rawSeen || mode !== 'gesture') ? '' : 'Keine Rohdaten empfangen: im Sketch STREAM_RAW_FRAMES auf 1 setzen, sonst bleiben die Sensordiagramme leer.'; refreshMenu(); }
  detect();
};
es.onerror = () => { $('dot').className = 'dot off'; $('conn').textContent = 'Verbindung zum Viewer verloren'; };
$('tbl').addEventListener('click', updateTable);

/* ---------- Funktionsmenue ---------- */
const menu = $('menu'), burger = $('burger');
function openMenu(o){ menu.classList.toggle('hidden', !o); burger.setAttribute('aria-expanded', String(o)); }
burger.addEventListener('click', e => { e.stopPropagation(); openMenu(menu.classList.contains('hidden')); });
document.addEventListener('click', e => { if (!menu.contains(e.target)) openMenu(false); });
document.addEventListener('keydown', e => { if (e.key === 'Escape') openMenu(false); });
function refreshMenu(){
  const now = srvNow();
  menu.querySelectorAll('button[data-m]').forEach(b => { const k = b.dataset.m; b.setAttribute('aria-checked', String((pinned || 'auto') === k));
    const live = k => seen[k] > 0 && now - seen[k] < 5;
    if (k !== 'auto') b.querySelector('.live').classList.toggle('on', k === 'house' ? (live('clap') && live('presence')) : live(k)); });
}
function setPinned(p){ pinned = p; try { localStorage.setItem('viewer-mode', p || 'auto'); } catch (e) {} if (p) applyMode(p); else detect(); refreshMenu(); }
menu.querySelectorAll('button[data-m]').forEach(b => b.addEventListener('click', () => { setPinned(b.dataset.m === 'auto' ? null : b.dataset.m); openMenu(false); }));
/* Vorwahl: ?mode=gesture|clap|presence|house|auto, sonst die zuletzt im Browser gewaehlte Funktion */
{ let p = new URLSearchParams(location.search).get('mode'); if (!p) { try { p = localStorage.getItem('viewer-mode'); } catch (e) {} }
  if (p && MODES[p]) { pinned = p; applyMode(p); } else refreshMenu(); }
requestAnimationFrame(tick);
})();
</script>
</body>
</html>
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default=None, help="serieller Port (Standard: KitProg3 unter /dev/serial/by-id)")
    ap.add_argument("--http", type=int, default=8765, help="HTTP-Port (Standard 8765)")
    ap.add_argument("--log", default=None, help="alle Zeilen zusaetzlich als CSV mitschreiben")
    a = ap.parse_args()
    state["port"] = a.port or find_port()
    threading.Thread(target=serial_reader, args=(state["port"], a.log), daemon=True).start()
    threading.Thread(target=broadcaster, daemon=True).start()
    try:
        srv = ThreadingHTTPServer(("127.0.0.1", a.http), Handler)
    except OSError as e:
        if e.errno == 98:
            sys.exit(f"Port {a.http} ist schon belegt, vermutlich laeuft der Viewer bereits.\n"
                     f"  Beenden mit:  fuser -k {a.http}/tcp\n"
                     f"  oder anderen Port nehmen:  --http {a.http + 1}")
        raise
    print(f"Live-Viewer: http://localhost:{a.http}   (Port {state['port']}, Beenden mit Strg+C)", flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
