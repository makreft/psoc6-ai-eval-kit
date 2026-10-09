/*
 * Klatschen erkennen mit dem selbst trainierten Modell, unter Zephyr (Port von arduino/sketchbook/05-clap-model).
 *
 * Mikrofon ueber die Infineon-HAL (mic.c), je 16-ms-Rahmen Hann, FFT 256, 32 Mel-Baender, log10 (identisch zu
 * ml/clap/features.py). Pegelanstieg startet einen Kandidaten; 24 Rahmen spaeter wird das 32x32-Fenster
 * pegelnormiert, int8-quantisiert und durch das Modell (TensorFlow Lite Micro) geschickt.
 * Konsole wie Sketch 05: "MODELL other=.. clap=.. snap=.. -> clap  Inferenz x ms", "KLATSCH #n ...", "L,<max>,<floor>".
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
#define REFRACTORY_MS    400
#define CLAP_THRESHOLD   0.35f

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
static bool model_run(int onsetIdx, float *probs, int64_t *usec) {
	float mx = -1e9f;
	for (int f = 0; f < FEAT_N_FRAMES; f++) { int r = (onsetIdx - FEAT_ONSET_FRAME + f + FEAT_N_FRAMES) % FEAT_N_FRAMES; for (int b = 0; b < FEAT_N_BANDS; b++) if (logbands[r][b] > mx) mx = logbands[r][b]; }
	int8_t *q = inT->data.int8;
	for (int f = 0; f < FEAT_N_FRAMES; f++) { int r = (onsetIdx - FEAT_ONSET_FRAME + f + FEAT_N_FRAMES) % FEAT_N_FRAMES;
		for (int b = 0; b < FEAT_N_BANDS; b++) { float v = (logbands[r][b] - mx + FEAT_DYN_RANGE) / FEAT_DYN_RANGE; if (v < 0) v = 0; if (v > 1) v = 1;
			int qi = (int)lroundf(v / MODEL_IN_SCALE) + MODEL_IN_ZERO; if (qi < -128) qi = -128; if (qi > 127) qi = 127; q[f * FEAT_N_BANDS + b] = (int8_t)qi; } }
	int64_t t0 = k_uptime_ticks();
	if (interp->Invoke() != kTfLiteOk) return false;
	*usec = k_ticks_to_us_floor64(k_uptime_ticks() - t0);
	for (int c = 0; c < 3; c++) probs[c] = (outT->data.int8[c] - MODEL_OUT_ZERO) * MODEL_OUT_SCALE;
	return true;
}

/* ---------- Ablauf ---------- */
static int16_t pcm[FEAT_FRAME]; static size_t fill = 0;
static float noiseFloor = 0.02f, prevRms = 0.0f, windowMax = 0.0f;
static bool pending = false; static int pendingOnset = 0, pendingWait = 0;
static int64_t lastClapMs = 0, lastLevelMs = 0; static uint32_t claps = 0; static bool led1on = false;

static void process_frame() {
	float rms = frame_features(pcm, logbands[ringHead]);
	int idx = ringHead; ringHead = (ringHead + 1) % FEAT_N_FRAMES; frameCount++;
	int64_t now = k_uptime_get(); float ratio = rms / noiseFloor;
	if (!pending) {
		if (ratio < ONSET_RATIO) noiseFloor += FLOOR_ALPHA * (rms - noiseFloor);
		if (noiseFloor < 50.0f / 32768.0f) noiseFloor = 50.0f / 32768.0f;
		bool loud = ratio >= ONSET_RATIO && rms >= MIN_RMS, sudden = prevRms * SUDDEN_FACTOR <= rms, fr_ = now - lastClapMs >= REFRACTORY_MS;
		if (loud && sudden && fr_ && frameCount >= (uint32_t)FEAT_N_FRAMES) { pending = true; pendingOnset = idx; pendingWait = FEAT_N_FRAMES - FEAT_ONSET_FRAME - 1; }
	} else if (--pendingWait <= 0) {
		pending = false; float p[3]; int64_t us = 0;
		if (model_run(pendingOnset, p, &us)) {
			int best = 0; for (int c = 1; c < 3; c++) if (p[c] > p[best]) best = c;
			printk("MODELL %s=%.2f %s=%.2f %s=%.2f -> %s  Inferenz %.1f ms\n", MODEL_CLASSES[0], (double)p[0], MODEL_CLASSES[1], (double)p[1], MODEL_CLASSES[2], (double)p[2], MODEL_CLASSES[best], (double)us / 1000.0);
			if (p[1] >= CLAP_THRESHOLD) {
				claps++; lastClapMs = now; led1on = house_toggle_light();
				printk("KLATSCH #%u  Dauer 0 ms  Peak 0  x%d  LED1 %s\n", claps, (int)(p[1] * 100), led1on ? "an" : "aus");
			}
		}
	}
	prevRms = rms;
	if (rms > windowMax) windowMax = rms;
	if (now - lastLevelMs >= 100) { lastLevelMs = now; printk("L,%d,%d\n", (int)(windowMax * 32768.0f), (int)(noiseFloor * 32768.0f)); windowMax = 0; }
}

void clap_thread(void *a, void *b, void *c) {
	(void)a; (void)b; (void)c;
	printk("AUDIO: Klatsch-Modell (TensorFlow Lite Micro)\n");
	fft_init();
	if (!model_init()) { printk("FEHLER: Modell\n"); for (;;) { k_msleep(100); } }
	printk("Modell geladen, Arena %u Bytes, Eingabe %u Bytes\n", (unsigned)interp->arena_used_bytes(), (unsigned)inT->bytes);
	if (mic_init()) { printk("FEHLER: Mikrofon\n"); for (;;) { k_msleep(200); } }
	printk("Mikrofon laeuft: 16 kHz mono, 16-ms-Rahmen, 32 Baender\n");
	int16_t tmp[256];
	for (;;) {
		size_t n = mic_read(tmp, 256);
		for (size_t i = 0; i < n; i++) { pcm[fill++] = tmp[i]; if (fill == FEAT_FRAME) { process_frame(); fill = 0; } }
		if (n == 0) k_msleep(2);
	}
}
