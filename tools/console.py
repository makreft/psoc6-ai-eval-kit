#!/usr/bin/env python3
"""Konsole der DEEPCRAFT-Streaming-Firmware auf dem KitProg3-UART (J1, 115200 8N1).

Aufruf:
  tools/console.py [--port PORT] [--password PW] [--listen SEK] CMD [CMD ...]
Beispiel:
  tools/console.py version help
  tools/console.py --listen 8          (nur mithoeren, z. B. fuer das Boot-Banner)
Standard-Port ist der erste KitProg3-Port unter /dev/serial/by-id, Standard-Passwort "imagimob".
"""
import argparse, glob, sys, time
import serial

def find_port():
    hits = sorted(glob.glob("/dev/serial/by-id/*KitProg3*"))
    return hits[0] if hits else "/dev/ttyACM0"

def listen(ser, seconds):
    buf = b""; end = time.time() + seconds
    while time.time() < end:
        n = ser.in_waiting
        if n: buf += ser.read(n)
        else: time.sleep(0.03)
    return buf

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=None, help="Standard: erster KitProg3-Port unter /dev/serial/by-id")
    ap.add_argument("--password", default="imagimob")
    ap.add_argument("--listen", type=float, default=0.0, help="erst N Sekunden passiv mithoeren")
    ap.add_argument("--wait", type=float, default=1.5, help="Antwortzeit pro Befehl in s")
    ap.add_argument("cmds", nargs="*")
    a = ap.parse_args()
    port = a.port or find_port()
    print(f"[Port {port}]")
    with serial.Serial(port, 115200, timeout=0, dsrdtr=False, rtscts=False) as ser:
        ser.dtr = True; ser.rts = True
        time.sleep(0.2)
        if a.listen:
            print(f"[hoere {a.listen:g} s]")
            sys.stdout.write(listen(ser, a.listen).decode(errors="replace")); sys.stdout.flush()
        ser.write(b"\r"); ser.flush()
        out = listen(ser, 1.0)
        sys.stdout.write(out.decode(errors="replace"))
        low = out.lower()
        if b"password" in low or b"login" in low or b"passwort" in low:
            ser.write(a.password.encode() + b"\r"); ser.flush()
            sys.stdout.write(listen(ser, 1.5).decode(errors="replace"))
        for c in a.cmds:
            ser.write(c.encode() + b"\r"); ser.flush()
            sys.stdout.write(listen(ser, a.wait).decode(errors="replace"))
        sys.stdout.flush()

if __name__ == "__main__":
    main()
