"""Merkmalsberechnung fuers Klatsch-Modell. Muss exakt der C-Implementierung im Sketch entsprechen.

Audio: 16 kHz, Mono, int16.
Rahmen: 256 Samples (16 ms), kein Ueberlapp, Hann-Fenster, FFT 256 -> 129 Bins, Leistung |X|^2.
Baender: N_BANDS mel-verteilte, zusammenhaengende Bin-Bereiche zwischen F_LO und F_HI (Tabelle BAND_EDGES).
Logarithmus: log10(Bandleistung + EPS). Pegelnormierung: minus Maximum ueber das ganze Fenster, dann
auf [0, 1] skaliert mit DYN_RANGE Dekaden (alles unter max - DYN_RANGE wird 0). Damit ist das Modell
unabhaengig von Lautstaerke und Abstand.
Fenster: N_FRAMES Rahmen (512 ms), Einsatz des Ereignisses bei Rahmen ONSET_FRAME.
"""
import numpy as np

RATE = 16000
FRAME = 256
N_BINS = FRAME // 2 + 1
N_BANDS = 32
N_FRAMES = 32
ONSET_FRAME = 8
F_LO, F_HI = 125.0, 7500.0
EPS = 1e-7
DYN_RANGE = 5.0      # Dekaden (= 50 dB) unter dem Maximum


def _mel(f): return 2595.0 * np.log10(1.0 + f / 700.0)
def _imel(m): return 700.0 * (10.0 ** (m / 2595.0) - 1.0)


def band_edges():
    """Bin-Grenzen der Baender: Band b umfasst Bins [edges[b], edges[b+1]). Jedes Band hat mindestens einen Bin."""
    mels = np.linspace(_mel(F_LO), _mel(F_HI), N_BANDS + 1)
    edges = np.round(_imel(mels) * FRAME / RATE).astype(int)
    for i in range(1, len(edges)):
        if edges[i] <= edges[i - 1]: edges[i] = edges[i - 1] + 1
    edges[-1] = min(edges[-1], N_BINS)
    return edges


BAND_EDGES = band_edges()
HANN = (0.5 - 0.5 * np.cos(2.0 * np.pi * np.arange(FRAME) / FRAME)).astype(np.float32)   # periodisches Hann wie im C-Code


def frame_bands(pcm_float):
    """Band-Log-Leistungen fuer alle vollstaendigen Rahmen eines Signals. pcm_float: float32 in [-1, 1]. -> (n_frames, N_BANDS)"""
    n = len(pcm_float) // FRAME
    x = pcm_float[:n * FRAME].reshape(n, FRAME) * HANN
    spec = np.fft.rfft(x, axis=1)
    power = (spec.real ** 2 + spec.imag ** 2).astype(np.float32)
    out = np.empty((n, N_BANDS), dtype=np.float32)
    for b in range(N_BANDS):
        out[:, b] = power[:, BAND_EDGES[b]:BAND_EDGES[b + 1]].sum(axis=1)
    return np.log10(out + EPS)


def frame_rms(pcm_float):
    n = len(pcm_float) // FRAME
    x = pcm_float[:n * FRAME].reshape(n, FRAME)
    return np.sqrt((x ** 2).mean(axis=1))


def normalize_window(logbands):
    """(N_FRAMES, N_BANDS) Log-Baender -> Modell-Eingabe in [0, 1], pegelunabhaengig."""
    m = logbands.max()
    return np.clip((logbands - m + DYN_RANGE) / DYN_RANGE, 0.0, 1.0).astype(np.float32)


def pcm_to_float(pcm_int16):
    return (pcm_int16.astype(np.float32) / 32768.0)


def window_at(logbands, onset_frame):
    """Fenster so ausschneiden, dass der Einsatz bei ONSET_FRAME liegt; Raender werden mit dem leisesten Wert gefuellt."""
    start = onset_frame - ONSET_FRAME
    pad_lo = max(0, -start); pad_hi = max(0, start + N_FRAMES - len(logbands))
    seg = logbands[max(0, start):max(0, start) + N_FRAMES - pad_lo - pad_hi]
    fill = logbands.min()
    if pad_lo or pad_hi:
        seg = np.concatenate([np.full((pad_lo, N_BANDS), fill, np.float32), seg, np.full((pad_hi, N_BANDS), fill, np.float32)])
    return normalize_window(seg)


def c_tables():
    """Tabellen fuer den Sketch als C-Code."""
    lines = ["/* Automatisch erzeugt von ml/clap/features.py. Nicht von Hand aendern. */",
             f"#define FEAT_RATE {RATE}", f"#define FEAT_FRAME {FRAME}", f"#define FEAT_N_BANDS {N_BANDS}",
             f"#define FEAT_N_FRAMES {N_FRAMES}", f"#define FEAT_ONSET_FRAME {ONSET_FRAME}",
             f"#define FEAT_EPS {EPS}f", f"#define FEAT_DYN_RANGE {DYN_RANGE}f",
             "static const uint16_t FEAT_BAND_EDGES[FEAT_N_BANDS + 1] = {" + ", ".join(str(int(e)) for e in BAND_EDGES) + "};"]
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    print("Bandgrenzen (Bins):", BAND_EDGES.tolist())
    print("Bandgrenzen (Hz):  ", [round(e * RATE / FRAME) for e in BAND_EDGES])
    print(c_tables())
