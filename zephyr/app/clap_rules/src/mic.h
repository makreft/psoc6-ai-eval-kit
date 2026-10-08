/* PDM-Mikrofon des CY8CKIT-062S2-AI ueber die Infineon-HAL, 16 kHz mono, 16 Bit. */
#ifndef MIC_H
#define MIC_H
#include <stdint.h>
#include <stddef.h>

#define MIC_RATE 16000

int mic_init(void);                               /* Audio-Takt (PLL0 24,576 MHz -> HF1) und PDM/PCM starten */
size_t mic_read(int16_t *dst, size_t max_samples);  /* so viele Samples wie im FIFO liegen, bis max */
uint32_t mic_overflows(void);
#endif
