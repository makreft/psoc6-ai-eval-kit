#include "mic.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "cyhal_clock.h"
#include "cyhal_pdmpcm.h"
#include "cyhal_hw_types.h"
#include "cy_sysclk.h"
#include "cy_pdm_pcm.h"

/* Pins laut Infineon-BSP: PDM_CLK P10.4, PDM_DATA P10.5 */
#define MIC_PIN_CLK   P10_4
#define MIC_PIN_DATA  P10_5
#define AUDIO_SYS_CLOCK_HZ 24576000u     /* wie in Infineons Arduino-PDM-Bibliothek */

static cyhal_clock_t pll_clock, audio_clock;
static cyhal_pdm_pcm_t pdm;
static int32_t fifo_buf[256];
static uint32_t overflows;

int mic_init(void)
{
	cy_rslt_t r;
	/* Takt: PLL0 auf 24,576 MHz, HF1 (Audio-Subsystem) daraus speisen. Zephyr nutzt die FLL fuer HF0, PLL0 ist frei.
	 * Die PLL0 haengt an Taktpfad 1; dessen Quelle muss der IMO (8 MHz) sein. Unter ModusToolbox macht das die
	 * Board-Initialisierung, unter Zephyr setzen wir es selbst. */
	Cy_SysClk_ClkPathSetSource(1, CY_SYSCLK_CLKPATH_IN_IMO);
	printk("mic: Pfad1-Quelle %d, Pfad1 %u Hz vor PLL\n", (int)Cy_SysClk_ClkPathGetSource(1), (unsigned)Cy_SysClk_ClkPathMuxGetFrequency(1));
	r = cyhal_clock_reserve(&pll_clock, &CYHAL_CLOCK_PLL[0]);
	if (r != CY_RSLT_SUCCESS) { printk("mic: PLL reserve 0x%08x\n", (unsigned)r); return -1; }
	r = cyhal_clock_set_frequency(&pll_clock, AUDIO_SYS_CLOCK_HZ, NULL);
	if (r != CY_RSLT_SUCCESS) { printk("mic: PLL freq 0x%08x\n", (unsigned)r); return -2; }
	r = cyhal_clock_set_enabled(&pll_clock, true, true);
	if (r != CY_RSLT_SUCCESS) { printk("mic: PLL enable 0x%08x\n", (unsigned)r); return -3; }
	r = cyhal_clock_reserve(&audio_clock, &CYHAL_CLOCK_HF[1]);
	if (r != CY_RSLT_SUCCESS) { printk("mic: HF1 reserve 0x%08x\n", (unsigned)r); return -4; }
	r = cyhal_clock_set_source(&audio_clock, &pll_clock);
	if (r != CY_RSLT_SUCCESS) { printk("mic: HF1 source 0x%08x\n", (unsigned)r); return -5; }
	r = cyhal_clock_set_enabled(&audio_clock, true, true);
	if (r != CY_RSLT_SUCCESS) { printk("mic: HF1 enable 0x%08x\n", (unsigned)r); return -6; }
	printk("mic: PLL0 %u Hz, HF1 %u Hz\n", (unsigned)cyhal_clock_get_frequency(&pll_clock), (unsigned)cyhal_clock_get_frequency(&audio_clock));

	const cyhal_pdm_pcm_cfg_t cfg = {
		.sample_rate = MIC_RATE,
		.decimation_rate = 64,
		.mode = CYHAL_PDM_PCM_MODE_LEFT,
		.word_length = 16,
		.left_gain = 20,
		.right_gain = 20,
	};
	r = cyhal_pdm_pcm_init(&pdm, MIC_PIN_DATA, MIC_PIN_CLK, &audio_clock, &cfg);
	if (r != CY_RSLT_SUCCESS) { printk("mic: pdm init 0x%08x\n", (unsigned)r); return -7; }
	r = cyhal_pdm_pcm_start(&pdm);
	if (r != CY_RSLT_SUCCESS) { printk("mic: pdm start 0x%08x\n", (unsigned)r); return -8; }
	k_msleep(50);
	printk("mic: FIFO nach 50 ms: %u Woerter, Interrupt-Status 0x%08x\n", (unsigned)Cy_PDM_PCM_GetNumInFifo(PDM0), (unsigned)Cy_PDM_PCM_GetInterruptStatus(PDM0));
	return 0;
}

size_t mic_read(int16_t *dst, size_t max_samples)
{
	size_t n = max_samples < 256 ? max_samples : 256;
	if (cyhal_pdm_pcm_read(&pdm, fifo_buf, &n) != CY_RSLT_SUCCESS) return 0;
	for (size_t i = 0; i < n; i++) dst[i] = (int16_t)fifo_buf[i];
	return n;
}

uint32_t mic_overflows(void) { return overflows; }
uint32_t mic_fifo_count(void) { return Cy_PDM_PCM_GetNumInFifo(PDM0); }
