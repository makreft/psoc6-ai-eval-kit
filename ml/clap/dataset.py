"""Aufnahmen aus data/audio in Trainingsfenster verwandeln.

Klassen: 0 = other, 1 = clap, 2 = snap. Fenster werden am Einsatz ausgerichtet (lautester Rahmen = ONSET_FRAME),
so wie der Sketch spaeter nach einem Pegelanstieg das Fenster bildet. Hintergrund liefert Fenster an lokalen
Maxima (laute Momente ohne Klatschen) und an Zufallspositionen, beide als other.
Augmentierung (nur Training): Verschiebung um einige Rahmen, Mischen mit Hintergrund bei zufaelligem SNR, Zufallsverstaerkung.
"""
import csv, os, sys, wave
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import features as F

CLASSES = ["other", "clap", "snap"]


def read_wav(path):
    with wave.open(path) as w:
        assert w.getframerate() == F.RATE and w.getnchannels() == 1 and w.getsampwidth() == 2, path
        return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)


def load_index(root):
    rows = list(csv.DictReader(open(os.path.join(root, "index.csv"))))
    return [(os.path.join(root, r["datei"]), r["klasse"], r) for r in rows]


def split_files(items, val_frac=0.2, seed=1):
    """Aufteilung nach Dateien (nie dasselbe Ereignis in Training und Validierung), je Klasse stratifiziert."""
    rng = np.random.default_rng(seed); train, val = [], []
    for cls in ["clap", "snap", "other", "background"]:
        files = [it for it in items if it[1] == cls]; rng.shuffle(files)
        n_val = max(1, int(round(len(files) * val_frac))) if cls != "background" else 0
        val += files[:n_val]; train += files[n_val:]
    return train, val


def onset_frame(pcm_float):
    return int(np.argmax(F.frame_rms(pcm_float)))


def event_windows(pcm_float, shifts):
    """Fenster um den Einsatz, fuer jede Verschiebung in shifts (in Rahmen)."""
    lb = F.frame_bands(pcm_float); on = onset_frame(pcm_float)
    return [F.window_at(lb, on + s) for s in shifts]


def background_windows(pcm_float, n_random, rng, n_peaks=8):
    lb = F.frame_bands(pcm_float); r = F.frame_rms(pcm_float); out = []
    # lokale Maxima: lauteste Rahmen mit Mindestabstand von einem Fenster
    order = np.argsort(r)[::-1]; taken = []
    for i in order:
        if len(taken) >= n_peaks: break
        if all(abs(i - t) > F.N_FRAMES for t in taken): taken.append(int(i))
    for t in taken: out.append(F.window_at(lb, t))
    for _ in range(n_random): out.append(F.window_at(lb, int(rng.integers(F.ONSET_FRAME, len(lb) - F.N_FRAMES + F.ONSET_FRAME))))
    return out


def mix_with_background(pcm_float, bg_float, rng, snr_db_range=(0.0, 25.0)):
    """Ereignis mit einem zufaelligen Hintergrundausschnitt mischen (SNR bezogen auf die Spitze des Ereignisses)."""
    if len(bg_float) <= len(pcm_float): return pcm_float
    start = int(rng.integers(0, len(bg_float) - len(pcm_float))); seg = bg_float[start:start + len(pcm_float)]
    snr = rng.uniform(*snr_db_range)
    ev_peak = np.abs(pcm_float).max() + 1e-6; bg_rms = np.sqrt((seg ** 2).mean()) + 1e-6
    gain = (ev_peak / (10 ** (snr / 20.0))) / bg_rms
    return np.clip(pcm_float + seg * gain, -1.0, 1.0)


def build(root, seed=1, aug_per_event=6):
    rng = np.random.default_rng(seed)
    items = load_index(root); train_items, val_items = split_files(items, seed=seed)
    bgs = [F.pcm_to_float(read_wav(p)) for p, c, _ in items if c == "background"]

    def make(items_, augment):
        X, y, src = [], [], []
        for path, cls, row in items_:
            pcm = F.pcm_to_float(read_wav(path))
            if cls == "background":
                ws = background_windows(pcm, n_random=40 if augment else 10, rng=rng)
                for w in ws: X.append(w); y.append(0); src.append(path)
                continue
            label = CLASSES.index(cls if cls in CLASSES else "other")
            # Grundfenster ohne Augmentierung
            for w in event_windows(pcm, [0]): X.append(w); y.append(label); src.append(path)
            if augment:
                for _ in range(aug_per_event):
                    p = pcm * float(rng.uniform(0.3, 1.0))
                    if bgs and rng.random() < 0.7: p = mix_with_background(p, bgs[int(rng.integers(len(bgs)))], rng)
                    shift = int(rng.integers(-3, 4))
                    for w in event_windows(p, [shift]): X.append(w); y.append(label); src.append(path)
        X = np.stack(X)[..., None]; y = np.array(y, dtype=np.int64)
        return X, y, src

    Xtr, ytr, _ = make(train_items, augment=True)
    Xva, yva, src_va = make(val_items, augment=False)
    return (Xtr, ytr), (Xva, yva, src_va), (train_items, val_items)


if __name__ == "__main__":
    import sys
    root = sys.argv[1] if len(sys.argv) > 1 else "data/audio"
    (Xtr, ytr), (Xva, yva, _), (ti, vi) = build(root)
    print("Training:", Xtr.shape, np.bincount(ytr, minlength=3), " Validierung:", Xva.shape, np.bincount(yva, minlength=3))
    print("Dateien Training/Validierung:", len(ti), len(vi))
