/*
 * Stubs fuer den HAL-DMA-Treiber. cyhal_pdmpcm.c referenziert DMA nur fuer den asynchronen Lesepfad
 * (cyhal_pdm_pcm_read_async), den diese Anwendung nicht benutzt; gelesen wird synchron aus dem FIFO.
 * Zephyr baut den HAL-DMA-Treiber fuer CAT1A nicht mit, daher diese leeren Implementierungen.
 */
#include "cyhal_dma.h"

void cyhal_dma_free(cyhal_dma_t *obj) { (void)obj; }
bool cyhal_dma_is_busy(cyhal_dma_t *obj) { (void)obj; return false; }
void cyhal_dma_enable_event(cyhal_dma_t *obj, cyhal_dma_event_t event, uint8_t intr_priority, bool enable) { (void)obj; (void)event; (void)intr_priority; (void)enable; }
cy_rslt_t cyhal_dma_configure(cyhal_dma_t *obj, const cyhal_dma_cfg_t *cfg) { (void)obj; (void)cfg; return CYHAL_DMA_RSLT_ERR_INVALID_PARAMETER; }
cy_rslt_t cyhal_dma_enable(cyhal_dma_t *obj) { (void)obj; return CYHAL_DMA_RSLT_ERR_INVALID_PARAMETER; }
cy_rslt_t cyhal_dma_start_transfer(cyhal_dma_t *obj) { (void)obj; return CYHAL_DMA_RSLT_ERR_INVALID_PARAMETER; }
