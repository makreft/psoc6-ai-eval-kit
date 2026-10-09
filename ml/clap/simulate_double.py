#!/usr/bin/env python3
"""Simulation Doppelklatschen: Haelt das Modell den ersten Klatscher noch fuer einen Klatscher, wenn der zweite
in sein 512-ms-Fenster faellt? Aus je zwei Einzelaufnahmen wird ein Doppelklatscher mit Abstand 150..650 ms
gemischt, beide Fenster werden wie in der Firmware normiert, int8-quantisiert und durch clap_model.tflite geschickt.
Gegenprobe: Doppel-Schnipser.

Aufruf (aus ml/):  .venv/bin/python -I clap/simulate_double.py clap clap/out ../data/audio
"""
import sys, wave, glob, numpy as np
sys.path.insert(0, sys.argv[1]); import features as F
import tensorflow as tf
out = sys.argv[2]; data = sys.argv[3]
interp = tf.lite.Interpreter(model_path=out + "/clap_model.tflite"); interp.allocate_tensors()
i_d = interp.get_input_details()[0]; o_d = interp.get_output_details()[0]
i_s, i_z = i_d["quantization"]; o_s, o_z = o_d["quantization"]
def run(win):
    q = np.clip(np.round(win / i_s) + i_z, -128, 127).astype(np.int8).reshape(i_d["shape"])
    interp.set_tensor(i_d["index"], q); interp.invoke()
    return (interp.get_tensor(o_d["index"]).astype(np.float32)[0] - o_z) * o_s   # other, clap, snap
def load(p):
    w = wave.open(p); pcm = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).copy(); w.close(); return pcm
def onset_frame(pcm):
    """erster Rahmen mit mindestens halbem Spitzen-RMS (entspricht dem Einsatzdetektor der Firmware)"""
    x = F.pcm_to_float(pcm); r = F.frame_rms(x)
    return int(np.argmax(r >= 0.5 * r.max()))
def windows(sig, fa, fb):
    x = F.pcm_to_float(sig); n = len(x) // F.FRAME; lb = F.frame_bands(x[:n * F.FRAME])
    return F.window_at(lb, fa), (F.window_at(lb, fb) if fb is not None else None)
claps = sorted(glob.glob(data + "/clap/*.wav"))[:12]
snaps = sorted(glob.glob(data + "/snap/*.wav"))[:6]
offsets = [150, 200, 300, 400, 500, 650]
print(f"{'A':>3} {'B':>3} | einzel |", " ".join(f"{d:>9}" for d in offsets), "   (p_clap Fenster1 / Fenster2)")
agg = {d: [] for d in offsets}; single = []
for k in range(len(claps) - 1):
    A = load(claps[k]); B = load(claps[k + 1]); fa = onset_frame(A); fb0 = onset_frame(B)
    base = np.zeros(len(A) + 2 * F.RATE, dtype=np.int32); base[:len(A)] = A
    pa = run(windows(base.astype(np.int16), fa, None)[0])[1]; single.append(pa)
    row = []
    for d in offsets:
        sig = base.copy(); shift = fa * F.FRAME + int(d * F.RATE / 1000) - fb0 * F.FRAME
        Bc = B[max(0, -shift):]; st = max(0, shift); Bc = Bc[:len(sig) - st]; sig[st:st + len(Bc)] += Bc; sig = np.clip(sig, -32768, 32767).astype(np.int16)
        fb = (fa * F.FRAME + int(d * F.RATE / 1000)) // F.FRAME
        w1, w2 = windows(sig, fa, fb); p1 = run(w1)[1]; p2 = run(w2)[1]; agg[d].append((p1, p2)); row.append(f"{p1:.2f}/{p2:.2f}")
    print(f"{k:>3} {k+1:>3} |  {pa:.2f}  |", " ".join(f"{r:>9}" for r in row))
print("\nMittel p_clap Fenster1 / Fenster2 je Abstand, Anteil >= 0.35 (beide):")
for d in offsets:
    a = np.array(agg[d]); both = np.mean((a[:, 0] >= 0.35) & (a[:, 1] >= 0.35))
    print(f"  {d:>4} ms: {a[:,0].mean():.2f} / {a[:,1].mean():.2f}   beide ueber Schwelle: {both*100:.0f} %")
print(f"Einzelklatscher: Mittel {np.mean(single):.2f}, min {np.min(single):.2f}")
# Gegenprobe: Doppel-Schnipser
print("\nDoppel-Schnipser (p_clap Fenster1 / Fenster2):")
for k in range(len(snaps) - 1):
    A = load(snaps[k]); B = load(snaps[k + 1]); fa = onset_frame(A); fb0 = onset_frame(B)
    base = np.zeros(len(A) + 2 * F.RATE, dtype=np.int32); base[:len(A)] = A; row = []
    for d in [200, 400]:
        sig = base.copy(); shift = fa * F.FRAME + int(d * F.RATE / 1000) - fb0 * F.FRAME
        Bc = B[max(0, -shift):]; st = max(0, shift); Bc = Bc[:len(sig) - st]; sig[st:st + len(Bc)] += Bc
        sig = np.clip(sig, -32768, 32767).astype(np.int16); fb = (fa * F.FRAME + int(d * F.RATE / 1000)) // F.FRAME
        w1, w2 = windows(sig, fa, fb); row.append(f"{d} ms: {run(w1)[1]:.2f}/{run(w2)[1]:.2f}")
    print("  ", k, "  ".join(row))
