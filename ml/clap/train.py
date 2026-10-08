"""Klatsch-Modell trainieren, quantisieren und als C-Array exportieren.

Aufruf (aus ml/):  .venv/bin/python clap/train.py --data ../data/audio --out clap/out
Ergebnis: out/clap_model.tflite (int8), out/model_data.h, out/features_config.h, out/report.txt
"""
import argparse, os, sys, time
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import features as F
import dataset as D

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")
import tensorflow as tf
import keras
from keras import layers


def build_model():
    inp = keras.Input(shape=(F.N_FRAMES, F.N_BANDS, 1), name="spectrogram")
    x = layers.Conv2D(8, 3, padding="same", activation="relu")(inp)
    x = layers.MaxPooling2D(2)(x)
    x = layers.Conv2D(16, 3, padding="same", activation="relu")(x)
    x = layers.MaxPooling2D(2)(x)
    x = layers.Conv2D(32, 3, padding="same", activation="relu")(x)
    x = layers.MaxPooling2D(2)(x)
    x = layers.Reshape((4 * 4 * 32,))(x)   # feste Form statt Flatten: vermeidet SHAPE/STRIDED_SLICE/PACK im TFLite-Graphen
    x = layers.Dropout(0.3)(x)
    x = layers.Dense(24, activation="relu")(x)
    out = layers.Dense(len(D.CLASSES), activation="softmax", name="classes")(x)
    return keras.Model(inp, out)


def confusion(y_true, y_pred, n):
    m = np.zeros((n, n), dtype=int)
    for t, p in zip(y_true, y_pred): m[t, p] += 1
    return m


def fmt_confusion(m):
    head = "            " + "".join(f"{c:>8s}" for c in D.CLASSES)
    rows = [f"  ist {D.CLASSES[i]:6s}" + "".join(f"{m[i, j]:8d}" for j in range(len(D.CLASSES))) for i in range(len(D.CLASSES))]
    return head + "\n" + "\n".join(rows) + "\n  (Zeilen: wahre Klasse, Spalten: Vorhersage)"


def to_c_array(data, name):
    lines = [f"/* Automatisch erzeugt von ml/clap/train.py, {time.strftime('%Y-%m-%d %H:%M')}. int8-TFLite-Modell, {len(data)} Bytes. */",
             "#include <stdint.h>", f"alignas(16) const unsigned char {name}[] = {{"]
    for i in range(0, len(data), 16):
        lines.append("  " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",")
    lines.append("};"); lines.append(f"const unsigned int {name}_len = {len(data)};")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default="../data/audio"); ap.add_argument("--out", default="clap/out")
    ap.add_argument("--epochs", type=int, default=40); ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    keras.utils.set_random_seed(a.seed)

    (Xtr, ytr), (Xva, yva, src_va), (ti, vi) = D.build(a.data, seed=a.seed)
    rep = [f"Daten: Training {Xtr.shape[0]} Fenster {np.bincount(ytr, minlength=3).tolist()} (other, clap, snap), "
           f"Validierung {Xva.shape[0]} Fenster {np.bincount(yva, minlength=3).tolist()}, Dateien {len(ti)}/{len(vi)}"]
    print(rep[-1])

    # Klassengewichte gegen Ungleichgewicht
    counts = np.bincount(ytr, minlength=3).astype(float); cw = {i: float(counts.sum() / (3 * counts[i])) for i in range(3)}
    model = build_model(); model.summary(print_fn=lambda s: rep.append(s))
    model.compile(optimizer=keras.optimizers.Adam(1e-3), loss="sparse_categorical_crossentropy", metrics=["accuracy"])
    cb = [keras.callbacks.EarlyStopping(monitor="val_loss", patience=8, restore_best_weights=True),
          keras.callbacks.ReduceLROnPlateau(monitor="val_loss", factor=0.5, patience=4)]
    hist = model.fit(Xtr, ytr, validation_data=(Xva, yva), epochs=a.epochs, batch_size=32, class_weight=cw, callbacks=cb, verbose=2)
    best = int(np.argmin(hist.history["val_loss"])) + 1
    rep.append(f"Training: {len(hist.history['loss'])} Epochen, beste Epoche {best}, val_loss {min(hist.history['val_loss']):.3f}, val_acc {hist.history['val_accuracy'][best-1]:.3f}")

    pred = model.predict(Xva, verbose=0).argmax(axis=1)
    rep.append("Konfusionsmatrix Validierung (float):\n" + fmt_confusion(confusion(yva, pred, 3)))
    wrong = [(os.path.basename(s), D.CLASSES[t], D.CLASSES[p]) for s, t, p in zip(src_va, yva, pred) if t != p]
    if wrong: rep.append("Fehler (Datei, wahr, vorhergesagt): " + "; ".join(f"{f}: {t}->{p}" for f, t, p in wrong[:20]))

    # int8-Quantisierung
    def rep_data():
        idx = np.random.default_rng(0).choice(len(Xtr), size=min(300, len(Xtr)), replace=False)
        for i in idx: yield [Xtr[i:i + 1].astype(np.float32)]
    # Export als SavedModel mit fester Batch-Groesse 1: ohne feste Signatur erzeugt der Konverter
    # SHAPE/STRIDED_SLICE/PACK fuer die dynamische Batch-Dimension, die auf dem Chip nur Ballast sind
    export_dir = os.path.join(a.out, "saved_model")
    model.export(export_dir, input_signature=[tf.TensorSpec([1, F.N_FRAMES, F.N_BANDS, 1], tf.float32, name="spectrogram")])
    conv = tf.lite.TFLiteConverter.from_saved_model(export_dir)
    conv.optimizations = [tf.lite.Optimize.DEFAULT]; conv.representative_dataset = rep_data
    conv.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    conv.inference_input_type = tf.int8; conv.inference_output_type = tf.int8
    tfl = conv.convert()
    tfl_path = os.path.join(a.out, "clap_model.tflite"); open(tfl_path, "wb").write(tfl)
    rep.append(f"TFLite int8: {len(tfl)} Bytes -> {tfl_path}")

    # Quantisiertes Modell auf der Validierung pruefen
    try:
        Interp = tf.lite.Interpreter
    except AttributeError:
        from ai_edge_litert.interpreter import Interpreter as Interp
    it = Interp(model_content=tfl); it.allocate_tensors()
    i_d, o_d = it.get_input_details()[0], it.get_output_details()[0]
    s_in, z_in = i_d["quantization"]; s_out, z_out = o_d["quantization"]
    qpred = []
    for x in Xva:
        q = np.clip(np.round(x / s_in + z_in), -128, 127).astype(np.int8)[None]
        it.set_tensor(i_d["index"], q); it.invoke()
        qpred.append(int(it.get_tensor(o_d["index"])[0].argmax()))
    qpred = np.array(qpred)
    rep.append(f"Quantisierung: Eingabe scale {s_in:.6f} zero {z_in}, Ausgabe scale {s_out:.6f} zero {z_out}")
    rep.append("Konfusionsmatrix Validierung (int8):\n" + fmt_confusion(confusion(yva, qpred, 3)))
    rep.append(f"Genauigkeit Validierung: float {np.mean(pred == yva):.3f}, int8 {np.mean(qpred == yva):.3f}")

    # Operatoren auflisten (fuer den MicroMutableOpResolver im Sketch)
    try:
        from tensorflow.lite.python import schema_py_generated as schema
        m = schema.Model.GetRootAsModel(tfl, 0); ops = []
        for i in range(m.OperatorCodesLength()):
            oc = m.OperatorCodes(i); code = max(oc.BuiltinCode(), oc.DeprecatedBuiltinCode())
            names = {v: k for k, v in schema.BuiltinOperator.__dict__.items() if isinstance(v, int)}
            ops.append(names.get(code, str(code)))
        rep.append("Operatoren: " + ", ".join(ops))
    except Exception as e:
        rep.append(f"Operatoren: nicht ermittelt ({e})")

    open(os.path.join(a.out, "model_data.h"), "w").write(to_c_array(tfl, "g_clap_model"))
    open(os.path.join(a.out, "features_config.h"), "w").write(F.c_tables() +
        f"#define MODEL_IN_SCALE {s_in}f\n#define MODEL_IN_ZERO ({z_in})\n#define MODEL_OUT_SCALE {s_out}f\n#define MODEL_OUT_ZERO ({z_out})\n"
        "static const char *MODEL_CLASSES[] = {" + ", ".join(f'"{c}"' for c in D.CLASSES) + "};\n")
    open(os.path.join(a.out, "report.txt"), "w").write("\n".join(rep) + "\n")
    print("\n".join(rep))


if __name__ == "__main__":
    main()
