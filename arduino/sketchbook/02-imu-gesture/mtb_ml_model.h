/*
 * Adapter fuer Infineons ML-Zwischenschicht (mtb_ml_model.h aus der Bibliothek ml-middleware).
 *
 * Das von DEEPCRAFT Studio generierte model.c benutzt genau drei Funktionen und drei Typen
 * dieser Schicht. Hier werden sie mit derselben Signatur auf TensorFlow Lite Micro
 * (Arduino-Bibliothek Chirale_TensorFLowLite) nachgebildet, damit model.c unveraendert
 * bleiben kann. Siehe SETUP-PROTOKOLL.md Abschnitt 31 und 32.
 */
#ifndef MTB_ML_MODEL_H
#define MTB_ML_MODEL_H

#include <stdint.h>
#include "cy_result.h"   /* cy_rslt_t und CY_RSLT_SUCCESS kommen aus Infineons core-lib, die der Arduino-Core mitbringt */

#ifdef __cplusplus
extern "C" {
#endif

/* Reihenfolge der Felder muss zur Initialisierung in model.c passen:
 *   mtb_ml_model_bin_t model = { "model", model_bin, model_size, arena_size }; */
typedef struct {
    const char   *name;
    const uint8_t *model_bin;
    unsigned int  model_size;
    int           arena_size;
} mtb_ml_model_bin_t;

/*   mtb_ml_model_buffer_t buffer = { arena_buffer, arena_size }; */
typedef struct {
    uint8_t *tensor_arena;
    int      tensor_arena_size;
} mtb_ml_model_buffer_t;

/* model.c liest nur das Feld output (float-Zeiger auf den Ausgabetensor). */
typedef struct mtb_ml_model {
    float *output;
    int    input_size;    /* Anzahl Floats im Eingabetensor  */
    int    output_size;   /* Anzahl Floats im Ausgabetensor  */
    void  *impl;          /* interner Zeiger auf den TFLM-Interpreter */
} mtb_ml_model_t;

cy_rslt_t mtb_ml_model_init(const mtb_ml_model_bin_t *bin, const mtb_ml_model_buffer_t *buffer, mtb_ml_model_t **model);
cy_rslt_t mtb_ml_model_run(mtb_ml_model_t *model, float *input);
cy_rslt_t mtb_ml_model_deinit(mtb_ml_model_t *model);

/* Zusatz fuer Diagnose im Sketch: Arena-Verbrauch in Bytes, negativ = interne Fallback-Arena.
 * model darf NULL sein (dann das zuletzt initialisierte Modell). */
int mtb_ml_model_arena_used(const mtb_ml_model_t *model);

#ifdef __cplusplus
}
#endif
#endif
