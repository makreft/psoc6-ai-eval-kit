/*
 * Klatschen erkennen mit einem selbst trainierten Modell (CY8CKIT-062S2-AI, Arduino-Core).
 *
 * Ablauf:
 *   1. Mikrofon 16 kHz mono, Rahmen von 256 Samples (16 ms). Je Rahmen: Hann, FFT 256, 32 Mel-Baender,
 *      log10 der Bandleistung (identisch zu ml/clap/features.py). Die letzten 32 Rahmen liegen im Ringpuffer.
 *   2. Pegelanstieg (RMS springt ueber das Grundrauschen) startet einen Kandidaten. 24 Rahmen (384 ms)
 *      spaeter wird das Fenster aus 8 Rahmen vor und 24 Rahmen ab dem Einsatz gebildet, pegelnormiert,
 *      int8-quantisiert und durch das Modell (TensorFlow Lite Micro) geschickt.
 *   3. Ergebnis: Wahrscheinlichkeiten other / clap / snap. Bei clap >= CLAP_THRESHOLD schaltet LED1 um
 *      (auch wenn other knapp vorne liegt; Schnipsen und Stoergeraeusche kommen nie in diese Naehe),
 *      LED2 blitzt. Konsole: "MODELL other=.. clap=.. snap=.. -> clap  Dauer .. ms" und bei Klatschen
 *      zusaetzlich "KLATSCH #n ..." im Format des regelbasierten Sketches (Viewer-kompatibel).
 *
 * model_data.h und features_config.h werden von ml/clap/train.py erzeugt.
 */
#include <math.h>
#include "PDM.h"
#include "model_data.h"
#include "features_config.h"
#include <Chirale_TensorFlowLite.h>
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

#define RING_SAMPLES     2048
#define ONSET_RATIO      6.0f
#define SUDDEN_FACTOR    3.0f
#define MIN_RMS          (300.0f / 32768.0f)
#define FLOOR_ALPHA      0.02f
#define REFRACTORY_MS    400
#define CLAP_THRESHOLD   0.35f    /* Messung 2026-10-08: Klatscher 0,52 bis 1,00, Schnipsen und Stoergeraeusche hoechstens 0,13; daher ohne Argmax-Bedingung */
#define SERIAL_LEVELS    1

/* ---------- FFT 256 (Radix-2, komplex, Eingang reell) ---------- */
static float twr[FEAT_FRAME / 2], twi[FEAT_FRAME / 2], hann[FEAT_FRAME];
static float fr[FEAT_FRAME], fi[FEAT_FRAME];
static uint16_t bitrev[FEAT_FRAME];

static void fft_init() {
    for (int i = 0; i < FEAT_FRAME / 2; i++) { twr[i] = cosf(-2.0f * (float)M_PI * i / FEAT_FRAME); twi[i] = sinf(-2.0f * (float)M_PI * i / FEAT_FRAME); }
    for (int i = 0; i < FEAT_FRAME; i++) { hann[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / FEAT_FRAME); }
    int bits = 0; while ((1 << bits) < FEAT_FRAME) bits++;
    for (int i = 0; i < FEAT_FRAME; i++) { int r = 0; for (int b = 0; b < bits; b++) if (i & (1 << b)) r |= 1 << (bits - 1 - b); bitrev[i] = r; }
}

static void fft256() {
    for (int i = 0; i < FEAT_FRAME; i++) if (bitrev[i] > i) { float t = fr[i]; fr[i] = fr[bitrev[i]]; fr[bitrev[i]] = t; t = fi[i]; fi[i] = fi[bitrev[i]]; fi[bitrev[i]] = t; }
    for (int len = 2; len <= FEAT_FRAME; len <<= 1) {
        int half = len >> 1, step = FEAT_FRAME / len;
        for (int i = 0; i < FEAT_FRAME; i += len)
            for (int j = 0; j < half; j++) {
                float wr = twr[j * step], wi = twi[j * step];
                float xr = fr[i + j + half], xi = fi[i + j + half];
                float tr = xr * wr - xi * wi, ti = xr * wi + xi * wr;
                fr[i + j + half] = fr[i + j] - tr; fi[i + j + half] = fi[i + j] - ti;
                fr[i + j] += tr; fi[i + j] += ti;
            }
    }
}

/* ---------- Merkmale ---------- */
static float logbands[FEAT_N_FRAMES][FEAT_N_BANDS];   /* Ringpuffer der letzten Rahmen */
static int   ringHead = 0;                            /* naechster Schreibplatz */
static uint32_t frameCount = 0;

static float frame_features(const int32_t *s, float *bands) {
    /* liefert RMS (Skala [-1,1]) und schreibt 32 Log-Baender */
    float mean = 0; for (int i = 0; i < FEAT_FRAME; i++) mean += s[i]; mean /= FEAT_FRAME;
    float acc = 0;
    for (int i = 0; i < FEAT_FRAME; i++) { float v = (s[i] - mean) / 32768.0f; acc += v * v; fr[i] = v * hann[i]; fi[i] = 0.0f; }
    fft256();
    for (int b = 0; b < FEAT_N_BANDS; b++) {
        float e = 0; for (int k = FEAT_BAND_EDGES[b]; k < FEAT_BAND_EDGES[b + 1]; k++) e += fr[k] * fr[k] + fi[k] * fi[k];
        bands[b] = log10f(e + FEAT_EPS);
    }
    return sqrtf(acc / FEAT_FRAME);
}

/* ---------- TFLM ---------- */
namespace {
constexpr int kArenaSize = 40 * 1024;
alignas(16) uint8_t arena[kArenaSize];
tflite::MicroMutableOpResolver<8> resolver;
tflite::MicroInterpreter *interp = nullptr;
alignas(tflite::MicroInterpreter) uint8_t interpMem[sizeof(tflite::MicroInterpreter)];
TfLiteTensor *inT = nullptr, *outT = nullptr;
}

static bool model_init() {
    const tflite::Model *model = tflite::GetModel(g_clap_model);
    if (model->version() != TFLITE_SCHEMA_VERSION) return false;
    resolver.AddConv2D(); resolver.AddMaxPool2D(); resolver.AddReshape(); resolver.AddFullyConnected();
    resolver.AddSoftmax(); resolver.AddQuantize(); resolver.AddDequantize(); resolver.AddMean();
    interp = new (interpMem) tflite::MicroInterpreter(model, resolver, arena, kArenaSize);
    if (interp->AllocateTensors() != kTfLiteOk) return false;
    inT = interp->input(0); outT = interp->output(0);
    return inT && outT && inT->type == kTfLiteInt8 && outT->type == kTfLiteInt8;
}

static bool model_run(int onsetIdx, float *probs, uint32_t *usec) {
    /* Fenster: FEAT_N_FRAMES Rahmen, Rahmen 0 = onsetIdx - FEAT_ONSET_FRAME (Ringindex) */
    float mx = -1e9f;
    for (int f = 0; f < FEAT_N_FRAMES; f++) { int r = (onsetIdx - FEAT_ONSET_FRAME + f + FEAT_N_FRAMES) % FEAT_N_FRAMES; for (int b = 0; b < FEAT_N_BANDS; b++) if (logbands[r][b] > mx) mx = logbands[r][b]; }
    int8_t *q = inT->data.int8;
    for (int f = 0; f < FEAT_N_FRAMES; f++) {
        int r = (onsetIdx - FEAT_ONSET_FRAME + f + FEAT_N_FRAMES) % FEAT_N_FRAMES;
        for (int b = 0; b < FEAT_N_BANDS; b++) {
            float v = (logbands[r][b] - mx + FEAT_DYN_RANGE) / FEAT_DYN_RANGE;
            if (v < 0) v = 0; if (v > 1) v = 1;
            int qi = (int)lroundf(v / MODEL_IN_SCALE) + MODEL_IN_ZERO;
            if (qi < -128) qi = -128; if (qi > 127) qi = 127;
            q[f * FEAT_N_BANDS + b] = (int8_t)qi;
        }
    }
    uint32_t t0 = micros();
    if (interp->Invoke() != kTfLiteOk) return false;
    *usec = micros() - t0;
    for (int c = 0; c < 3; c++) probs[c] = (outT->data.int8[c] - MODEL_OUT_ZERO) * MODEL_OUT_SCALE;
    return true;
}

/* ---------- Ablauf ---------- */
static int32_t pcm[FEAT_FRAME];
static float noiseFloor = 0.02f, prevRms = 0.0f, windowMax = 0.0f;
static bool  pending = false; static int pendingOnset = 0, pendingWait = 0;
static uint32_t lastClapMs = 0, led2OffMs = 0, lastLevelMs = 0, claps = 0, events = 0;
static bool led1 = false;

void setup() {
    pinMode(LED1, OUTPUT); pinMode(LED2, OUTPUT); digitalWrite(LED1, LOW); digitalWrite(LED2, LOW);
    Serial.begin(115200); delay(1500);
    Serial.println(); Serial.println("Klatsch-Modell: TensorFlow Lite Micro auf dem PSOC 6");
    fft_init();
    if (!model_init()) { Serial.println("FEHLER: Modell konnte nicht geladen werden"); for (;;) { digitalWrite(LED2, !digitalRead(LED2)); delay(100); } }
    Serial.print("Modell geladen, Arena "); Serial.print(interp->arena_used_bytes()); Serial.print(" Bytes, Eingabe ");
    Serial.print(inT->bytes); Serial.print(" Bytes, Klassen "); Serial.println(MODEL_CLASSES[0]);
    PDM.setBufferSize(RING_SAMPLES);
    if (PDM.begin(1, FEAT_RATE) != 1) { Serial.println("FEHLER: PDM"); for (;;) { digitalWrite(LED2, !digitalRead(LED2)); delay(100); } }
    Serial.println("Mikrofon laeuft: 16 kHz mono, 16-ms-Rahmen, 32 Baender");
}

static void process_frame() {
    float rms = frame_features(pcm, logbands[ringHead]);
    int idx = ringHead; ringHead = (ringHead + 1) % FEAT_N_FRAMES; frameCount++;
    uint32_t now = millis();
    float ratio = rms / noiseFloor;

    if (!pending) {
        if (ratio < ONSET_RATIO) noiseFloor += FLOOR_ALPHA * (rms - noiseFloor);
        if (noiseFloor < 50.0f / 32768.0f) noiseFloor = 50.0f / 32768.0f;
        bool loud = ratio >= ONSET_RATIO && rms >= MIN_RMS, sudden = prevRms * SUDDEN_FACTOR <= rms, free = now - lastClapMs >= REFRACTORY_MS;
        if (loud && sudden && free && frameCount >= FEAT_N_FRAMES) { pending = true; pendingOnset = idx; pendingWait = FEAT_N_FRAMES - FEAT_ONSET_FRAME - 1; }
    } else if (--pendingWait <= 0) {
        pending = false;
        float p[3]; uint32_t us = 0;
        if (model_run(pendingOnset, p, &us)) {
            events++;
            int best = 0; for (int c = 1; c < 3; c++) if (p[c] > p[best]) best = c;
            Serial.print("MODELL "); for (int c = 0; c < 3; c++) { Serial.print(MODEL_CLASSES[c]); Serial.print('='); Serial.print(p[c], 2); Serial.print(' '); }
            Serial.print("-> "); Serial.print(MODEL_CLASSES[best]); Serial.print("  Inferenz "); Serial.print(us / 1000.0f, 1); Serial.println(" ms");
            if (p[1] >= CLAP_THRESHOLD) {
                claps++; lastClapMs = now; led1 = !led1; digitalWrite(LED1, led1 ? HIGH : LOW); digitalWrite(LED2, HIGH); led2OffMs = now + 80;
                Serial.print("KLATSCH #"); Serial.print(claps); Serial.print("  Dauer 0 ms  Peak 0  x"); Serial.print(p[1] * 100, 0); Serial.print("  LED1 "); Serial.println(led1 ? "an" : "aus");
            }
        }
    }
    prevRms = rms;
#if SERIAL_LEVELS
    if (rms > windowMax) windowMax = rms;
    if (now - lastLevelMs >= 100) { lastLevelMs = now; Serial.print("L,"); Serial.print(windowMax * 32768.0f, 0); Serial.print(','); Serial.println(noiseFloor * 32768.0f, 0); windowMax = 0; }
#endif
}

void loop() {
    if (led2OffMs && (int32_t)(millis() - led2OffMs) >= 0) { digitalWrite(LED2, LOW); led2OffMs = 0; }
    while (PDM.available() >= FEAT_FRAME) {
        if (PDM.read(pcm, FEAT_FRAME) != (uint32_t)FEAT_FRAME) break;
        process_frame();
    }
}
