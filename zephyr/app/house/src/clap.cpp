/*
 * Doppelklatschen erkennen mit dem selbst trainierten Modell, unter Zephyr (aus arduino/sketchbook/05-clap-model).
 *
 * Audio-Thread: Mikrofon ueber die Infineon-HAL (mic.c), je 16-ms-Rahmen Hann, FFT 256, 32 Mel-Baender, log10
 * (identisch zu ml/clap/features.py). Ein Pegelanstieg startet einen Kandidaten; 24 Rahmen spaeter wird das
 * 32x32-Fenster pegelnormiert, int8-quantisiert und per Message-Queue an den Modell-Thread gegeben. Mehrere
 * Kandidaten duerfen sich ueberlappen (zweiter Klatscher waehrend das Fenster des ersten noch laeuft).
 * Modell-Thread (niedrigere Prioritaet): Inferenz (TensorFlow Lite Micro, ca. 53 ms), waehrenddessen liest der
 * Audio-Thread weiter, es geht kein Audio verloren. Zwei vom Modell bestaetigte Klatscher im Abstand von
 * DOUBLE_MIN_MS bis DOUBLE_MAX_MS schalten das Licht um; ein einzelner wird nach Ablauf verworfen.
 * Konsole: "MODELL other=.. clap=.. snap=.. -> clap  Inferenz x ms",
 *          "KLATSCH #n  einzel  Konfidenz 0.72", "KLATSCH #n  doppel  Abstand 310 ms  Konfidenz 0.80  LED1 an",
 *          "VERWORFEN einzel  Grund: kein zweiter Klatscher innerhalb 800 ms", "L,<max>,<floor>".
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <math.h>
#include <string.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "mic.h"
#include "house.h"
#include "model_data.h"
#include "features_config.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

#define ONSET_RATIO      6.0f
#define SUDDEN_FACTOR    3.0f
#define MIN_RMS          (300.0f / 32768.0f)
#define FLOOR_ALPHA      0.02f
#define ONSET_GAP_MS     100      /* Mindestabstand zweier Kandidaten (Abklingen desselben Klatschers nicht neu zaehlen) */
#define CLAP_THRESHOLD   0.35f
#define DOUBLE_MIN_MS    120      /* zwei Klatscher naeher zusammen: unplausibel */
#define DOUBLE_MAX_MS    800      /* zweiter Klatscher spaeter: der erste verfaellt */
#define MAX_PENDING      4

/* Auftrag an den Modell-Thread: fertig quantisiertes Fenster plus Zeit des Einsatzes */
struct infer_job { int64_t onsetMs; float onsetRms; int8_t in[FEAT_N_FRAMES * FEAT_N_BANDS]; };
K_MSGQ_DEFINE(job_q, sizeof(struct infer_job), 4, 4);

extern "C" void DebugLog(const char *s) { printk("%s", s); }   /* TFLM-Logausgabe */


/* ---------- FFT 256 ---------- */
static float twr[FEAT_FRAME / 2], twi[FEAT_FRAME / 2], hann[FEAT_FRAME], fr[FEAT_FRAME], fi[FEAT_FRAME];
static uint16_t bitrev[FEAT_FRAME];
static void fft_init() {
	for (int i = 0; i < FEAT_FRAME / 2; i++) { twr[i] = cosf(-2.0f * (float)M_PI * i / FEAT_FRAME); twi[i] = sinf(-2.0f * (float)M_PI * i / FEAT_FRAME); }
	for (int i = 0; i < FEAT_FRAME; i++) hann[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / FEAT_FRAME);
	int bits = 0; while ((1 << bits) < FEAT_FRAME) bits++;
	for (int i = 0; i < FEAT_FRAME; i++) { int r = 0; for (int b = 0; b < bits; b++) if (i & (1 << b)) r |= 1 << (bits - 1 - b); bitrev[i] = r; }
}
static void fft256() {
	for (int i = 0; i < FEAT_FRAME; i++) if (bitrev[i] > i) { float t = fr[i]; fr[i] = fr[bitrev[i]]; fr[bitrev[i]] = t; t = fi[i]; fi[i] = fi[bitrev[i]]; fi[bitrev[i]] = t; }
	for (int len = 2; len <= FEAT_FRAME; len <<= 1) {
		int half = len >> 1, step = FEAT_FRAME / len;
		for (int i = 0; i < FEAT_FRAME; i += len) for (int j = 0; j < half; j++) {
			float wr = twr[j * step], wi = twi[j * step], xr = fr[i + j + half], xi = fi[i + j + half];
			float tr = xr * wr - xi * wi, ti = xr * wi + xi * wr;
			fr[i + j + half] = fr[i + j] - tr; fi[i + j + half] = fi[i + j] - ti; fr[i + j] += tr; fi[i + j] += ti;
		}
	}
}

/* ---------- Merkmale ---------- */
static float logbands[FEAT_N_FRAMES][FEAT_N_BANDS];
static int ringHead = 0; static uint32_t frameCount = 0;
static float frame_features(const int16_t *s, float *bands) {
	float mean = 0; for (int i = 0; i < FEAT_FRAME; i++) mean += s[i]; mean /= FEAT_FRAME;
	float acc = 0;
	for (int i = 0; i < FEAT_FRAME; i++) { float v = (s[i] - mean) / 32768.0f; acc += v * v; fr[i] = v * hann[i]; fi[i] = 0.0f; }
	fft256();
	for (int b = 0; b < FEAT_N_BANDS; b++) { float e = 0; for (int k = FEAT_BAND_EDGES[b]; k < FEAT_BAND_EDGES[b + 1]; k++) e += fr[k] * fr[k] + fi[k] * fi[k]; bands[b] = log10f(e + FEAT_EPS); }
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
/* Audio-Thread: Fenster um den Einsatz pegelnormieren und quantisieren (wie ml/clap/features.py) */
static void quantize_window(int onsetIdx, int8_t *q) {
	float mx = -1e9f;
	for (int f = 0; f < FEAT_N_FRAMES; f++) { int r = (onsetIdx - FEAT_ONSET_FRAME + f + FEAT_N_FRAMES) % FEAT_N_FRAMES; for (int b = 0; b < FEAT_N_BANDS; b++) if (logbands[r][b] > mx) mx = logbands[r][b]; }
	for (int f = 0; f < FEAT_N_FRAMES; f++) { int r = (onsetIdx - FEAT_ONSET_FRAME + f + FEAT_N_FRAMES) % FEAT_N_FRAMES;
		for (int b = 0; b < FEAT_N_BANDS; b++) { float v = (logbands[r][b] - mx + FEAT_DYN_RANGE) / FEAT_DYN_RANGE; if (v < 0) v = 0; if (v > 1) v = 1;
			int qi = (int)lroundf(v / MODEL_IN_SCALE) + MODEL_IN_ZERO; if (qi < -128) qi = -128; if (qi > 127) qi = 127; q[f * FEAT_N_BANDS + b] = (int8_t)qi; } }
}
/* Modell-Thread: Inferenz auf einem fertigen Fenster */
static bool model_run(const int8_t *in, float *probs, int64_t *usec) {
	memcpy(inT->data.int8, in, FEAT_N_FRAMES * FEAT_N_BANDS);
	int64_t t0 = k_uptime_ticks();
	if (interp->Invoke() != kTfLiteOk) return false;
	*usec = k_ticks_to_us_floor64(k_uptime_ticks() - t0);
	for (int c = 0; c < 3; c++) probs[c] = (outT->data.int8[c] - MODEL_OUT_ZERO) * MODEL_OUT_SCALE;
	return true;
}

/* ---------- Audio-Thread: Rahmen, Einsaetze, Kandidaten ---------- */
static int16_t pcm[FEAT_FRAME]; static size_t fill = 0;
static float noiseFloor = 0.02f, prevRms = 0.0f, windowMax = 0.0f;
struct candidate { int onsetIdx, wait; int64_t onsetMs; float rms; };
static struct candidate pend[MAX_PENDING]; static int nPend = 0;
static int64_t lastOnsetMs = 0, lastLevelMs = 0; static uint32_t dropped = 0;

static void process_frame() {
	float rms = frame_features(pcm, logbands[ringHead]);
	int idx = ringHead; ringHead = (ringHead + 1) % FEAT_N_FRAMES; frameCount++;
	int64_t now = k_uptime_get(); float ratio = rms / noiseFloor;
	if (ratio < ONSET_RATIO) noiseFloor += FLOOR_ALPHA * (rms - noiseFloor);
	if (noiseFloor < 50.0f / 32768.0f) noiseFloor = 50.0f / 32768.0f;
	bool loud = ratio >= ONSET_RATIO && rms >= MIN_RMS, sudden = prevRms * SUDDEN_FACTOR <= rms, gap = now - lastOnsetMs >= ONSET_GAP_MS;
	if (loud && sudden && gap && frameCount >= (uint32_t)FEAT_N_FRAMES && nPend < MAX_PENDING) {
		pend[nPend++] = (struct candidate){ idx, FEAT_N_FRAMES - FEAT_ONSET_FRAME - 1, now, rms }; lastOnsetMs = now;
	}
	/* fertige Fenster an den Modell-Thread geben (aeltester Kandidat zuerst) */
	for (int i = 0; i < nPend; ) {
		if (--pend[i].wait > 0) { i++; continue; }
		struct infer_job job; job.onsetMs = pend[i].onsetMs; job.onsetRms = pend[i].rms; quantize_window(pend[i].onsetIdx, job.in);
		if (k_msgq_put(&job_q, &job, K_NO_WAIT) != 0) dropped++;
		for (int j = i + 1; j < nPend; j++) pend[j - 1] = pend[j];
		nPend--;
	}
	prevRms = rms;
	if (rms > windowMax) windowMax = rms;
	if (now - lastLevelMs >= 100) { lastLevelMs = now; printk("L,%d,%d\n", (int)(windowMax * 32768.0f), (int)(noiseFloor * 32768.0f)); windowMax = 0; }
}

/* ---------- Modell-Thread: Inferenz und Doppelklatsch-Logik ---------- */
static uint32_t claps = 0; static bool led1on = false;
static int64_t firstMs = 0;        /* Einsatz des ersten bestaetigten Klatschers, 0 = keiner offen */

static void model_thread(void *a, void *b, void *c) {
	(void)a; (void)b; (void)c;
	struct infer_job job;
	for (;;) {
		if (k_msgq_get(&job_q, &job, K_MSEC(50)) != 0) {
			/* kein Auftrag: offenen ersten Klatscher verfallen lassen (Ergebnis des zweiten kaeme spaetestens ~450 ms nach seinem Einsatz) */
			if (firstMs && k_uptime_get() - firstMs > DOUBLE_MAX_MS + 500) { firstMs = 0; printk("VERWORFEN einzel  Grund: kein zweiter Klatscher innerhalb %d ms\n", DOUBLE_MAX_MS); }
			continue;
		}
		float p[3]; int64_t us = 0;
		if (!model_run(job.in, p, &us)) continue;
		int best = 0; for (int k = 1; k < 3; k++) if (p[k] > p[best]) best = k;
		printk("MODELL %s=%.2f %s=%.2f %s=%.2f -> %s  Inferenz %.1f ms\n", MODEL_CLASSES[0], (double)p[0], MODEL_CLASSES[1], (double)p[1], MODEL_CLASSES[2], (double)p[2], MODEL_CLASSES[best], (double)us / 1000.0);
		if (p[1] < CLAP_THRESHOLD) continue;
		claps++;
		int64_t gap = firstMs ? job.onsetMs - firstMs : 0;
		if (firstMs && gap >= DOUBLE_MIN_MS && gap <= DOUBLE_MAX_MS) {
			firstMs = 0; led1on = house_toggle_light();
			printk("KLATSCH #%u  doppel  Abstand %d ms  Konfidenz %.2f  LED1 %s\n", claps, (int)gap, (double)p[1], led1on ? "an" : "aus");
		} else {
			if (firstMs) printk("VERWORFEN einzel  Grund: Abstand %d ms ausserhalb %d..%d ms\n", (int)gap, DOUBLE_MIN_MS, DOUBLE_MAX_MS);
			firstMs = job.onsetMs;
			printk("KLATSCH #%u  einzel  Konfidenz %.2f\n", claps, (double)p[1]);
		}
	}
}
K_THREAD_STACK_DEFINE(model_stack, 8192);
static struct k_thread model_td;

void clap_thread(void *a, void *b, void *c) {
	(void)a; (void)b; (void)c;
	printk("AUDIO: Klatsch-Modell (TensorFlow Lite Micro)\n");
	fft_init();
	if (!model_init()) { printk("FEHLER: Modell\n"); for (;;) { k_msleep(100); } }
	printk("Modell geladen, Arena %u Bytes, Eingabe %u Bytes\n", (unsigned)interp->arena_used_bytes(), (unsigned)inT->bytes);
	if (mic_init()) { printk("FEHLER: Mikrofon\n"); for (;;) { k_msleep(200); } }
	printk("Mikrofon laeuft: 16 kHz mono, 16-ms-Rahmen, 32 Baender\n");
	printk("Doppelklatschen: zwei Klatscher im Abstand %d..%d ms schalten das Licht\n", DOUBLE_MIN_MS, DOUBLE_MAX_MS);
	/* Modell-Thread eine Stufe unter dem Audio-Thread: die Inferenz (ca. 53 ms) unterbricht das Lesen nicht */
	k_thread_create(&model_td, model_stack, K_THREAD_STACK_SIZEOF(model_stack), model_thread, NULL, NULL, NULL, 6, 0, K_NO_WAIT);
	k_thread_name_set(&model_td, "model");
	int16_t tmp[256]; int64_t lastDiag = 0;
	for (;;) {
		size_t n = mic_read(tmp, 256);
		for (size_t i = 0; i < n; i++) { pcm[fill++] = tmp[i]; if (fill == FEAT_FRAME) { process_frame(); fill = 0; } }
		if (n == 0) k_msleep(2);
		int64_t now = k_uptime_get();
		if (dropped && now - lastDiag > 5000) { lastDiag = now; printk("WARNUNG: %u Fenster verworfen (Modell-Warteschlange voll)\n", dropped); }
	}
}
