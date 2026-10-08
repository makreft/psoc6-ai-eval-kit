/*
 * Regelbasierter Klatsch-Detektor unter Zephyr: Port von arduino/sketchbook/03-clap-led (Stand Abschnitt 44),
 * als Test fuer den Mikrofonpfad vor dem Modell. Gleiche Konsolenzeilen, damit tools/live_viewer.py passt:
 *   L,<max RMS im 100-ms-Fenster>,<Grundrauschen>        10x pro Sekunde
 *   KLATSCH #n  Dauer d ms  Peak p  xR  LED1 an|aus  ZCR z  Abkl. a ms
 *   VERWORFEN  Dauer d ms  Peak p  xR  Grund: ...  ZCR z  Abkl. a ms
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <math.h>
#include "mic.h"

#define BLOCK            128
#define ONSET_RATIO      6.0f
#define QUIET_RATIO      2.0f
#define END_FRACTION     0.10f
#define SUDDEN_FACTOR    3.0f
#define MIN_RMS          300.0f
#define MIN_PEAK_RMS     1500.0f
#define MIN_CLAP_MS      24
#define MAX_CLAP_MS      150
#define ABORT_MS         400
#define REFRACTORY_MS    400
#define FLOOR_ALPHA      0.02f

static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_NODELABEL(user_led1), gpios);

static int16_t pcm[BLOCK]; static size_t fill;
static float noiseFloor = 500.0f, prevRms = 0.0f, peakRms = 0.0f, peakRatio = 0.0f, windowMax = 0.0f;
static uint32_t peakZcr = 0; static bool inPulse = false;
static int64_t pulseStartMs = 0, lastClapMs = 0, led2OffMs = 0, lastLevelMs = 0, endFloorMs = 0;
static uint32_t claps = 0, blocks = 0; static bool led1on = false;

static void reject(uint32_t dur, uint32_t decay, const char *why)
{
	printk("VERWORFEN  Dauer %u ms  Peak %d  x%.1f  Grund: %s  ZCR %u  Abkl. %u ms\n", dur, (int)peakRms, (double)peakRatio, why, peakZcr, decay);
}

static void process_block(const int16_t *s, size_t n)
{
	int64_t sum = 0; for (size_t i = 0; i < n; i++) sum += s[i];
	float mean = (float)sum / n, acc = 0.0f; uint32_t zcr = 0;
	for (size_t i = 0; i < n; i++) { float d = s[i] - mean; acc += d * d; if (i && ((s[i] - mean) < 0) != ((s[i - 1] - mean) < 0)) zcr++; }
	float rms = sqrtf(acc / n); blocks++;
	float ratio = rms / noiseFloor; int64_t now = k_uptime_get();

	if (!inPulse) {
		if (ratio < ONSET_RATIO) noiseFloor += FLOOR_ALPHA * (rms - noiseFloor);
		if (noiseFloor < 50.0f) noiseFloor = 50.0f;
		bool loud = ratio >= ONSET_RATIO && rms >= MIN_RMS, sudden = prevRms * SUDDEN_FACTOR <= rms, free = now - lastClapMs >= REFRACTORY_MS;
		if (loud && sudden && free) { inPulse = true; pulseStartMs = now; peakRms = rms; peakRatio = ratio; peakZcr = zcr; endFloorMs = 0; }
	} else {
		uint32_t dur = (uint32_t)(now - pulseStartMs);
		if (rms > peakRms) { peakRms = rms; peakZcr = zcr; }
		if (ratio > peakRatio) peakRatio = ratio;
		if (ratio < QUIET_RATIO && endFloorMs == 0) endFloorMs = now;
		if (rms < peakRms * END_FRACTION || ratio < QUIET_RATIO) {
			inPulse = false;
			uint32_t decay = endFloorMs ? (uint32_t)(endFloorMs - pulseStartMs) : dur;
			if (dur < MIN_CLAP_MS) reject(dur, decay, "zu kurz (Schnipsen, Klicken)");
			else if (peakRms < MIN_PEAK_RMS) reject(dur, decay, "zu leise");
			else if (dur > MAX_CLAP_MS) reject(dur, decay, "zu lang");
			else {
				claps++; lastClapMs = now; led1on = !led1on; gpio_pin_set_dt(&led1, led1on); gpio_pin_set_dt(&led2, 1); led2OffMs = now + 80;
				printk("KLATSCH #%u  Dauer %u ms  Peak %d  x%.1f  LED1 %s  ZCR %u  Abkl. %u ms\n", claps, dur, (int)peakRms, (double)peakRatio, led1on ? "an" : "aus", peakZcr, decay);
			}
		} else if (dur > ABORT_MS) { inPulse = false; reject(dur, dur, "Dauerlaerm"); }
	}
	prevRms = rms;
	if (rms > windowMax) windowMax = rms;
	if (now - lastLevelMs >= 100) { lastLevelMs = now; printk("L,%d,%d\n", (int)windowMax, (int)noiseFloor); windowMax = 0; }
}

int main(void)
{
	gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE); gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);
	printk("\nKlatsch-Schalter (regelbasiert) unter Zephyr\n");
	int err = mic_init();
	if (err) { printk("FEHLER: Mikrofon %d\n", err); for (;;) { gpio_pin_toggle_dt(&led2); k_msleep(100); } }
	printk("Mikrofon laeuft: 16 kHz mono, Bloecke von 8 ms\n");
	int16_t tmp[256];
	for (;;) {
		if (led2OffMs && k_uptime_get() >= led2OffMs) { gpio_pin_set_dt(&led2, 0); led2OffMs = 0; }
		size_t n = mic_read(tmp, 256);
		for (size_t i = 0; i < n; i++) { pcm[fill++] = tmp[i]; if (fill == BLOCK) { process_block(pcm, BLOCK); fill = 0; } }
		if (n == 0) k_msleep(2);
	}
	return 0;
}
