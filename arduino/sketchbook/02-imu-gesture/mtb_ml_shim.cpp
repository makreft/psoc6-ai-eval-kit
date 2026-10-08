/*
 * Implementierung des mtb_ml-Adapters auf TensorFlow Lite Micro.
 * Versucht zuerst die vom Modell mitgelieferte Arena (16 KB laut model.c). Reicht sie nicht,
 * wird auf eine interne, groessere Arena ausgewichen und das im Sketch gemeldet.
 */
#include <new>
#include <string.h>
#include "mtb_ml_model.h"

#include <Chirale_TensorFlowLite.h>
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace {

constexpr int kFallbackArenaSize = 96 * 1024;
alignas(16) uint8_t g_fallback_arena[kFallbackArenaSize];

/* Genau die Operatoren, die das DEEPCRAFT-Modell enthaelt (siehe Protokoll Abschnitt 31). */
using Resolver = tflite::MicroMutableOpResolver<7>;

struct Impl {
    Resolver resolver;
    alignas(tflite::MicroInterpreter) uint8_t interp_mem[sizeof(tflite::MicroInterpreter)];
    tflite::MicroInterpreter *interp = nullptr;
    const tflite::Model *model = nullptr;
    bool used_fallback = false;
};

Impl g_impl;
mtb_ml_model_t g_model;
bool g_in_use = false;

bool build_interpreter(Impl &impl, uint8_t *arena, int arena_size) {
    if (impl.interp) { impl.interp->~MicroInterpreter(); impl.interp = nullptr; }
    impl.interp = new (impl.interp_mem) tflite::MicroInterpreter(impl.model, impl.resolver, arena, arena_size);
    return impl.interp->AllocateTensors() == kTfLiteOk;
}

} // namespace

extern "C" {

cy_rslt_t mtb_ml_model_init(const mtb_ml_model_bin_t *bin, const mtb_ml_model_buffer_t *buffer, mtb_ml_model_t **model_out) {
    if (g_in_use || !bin || !model_out) return -1;
    Impl &impl = g_impl;

    impl.model = tflite::GetModel(bin->model_bin);
    if (impl.model->version() != TFLITE_SCHEMA_VERSION) return -2;

    impl.resolver.AddReshape();
    impl.resolver.AddConv2D();
    impl.resolver.AddMaxPool2D();
    impl.resolver.AddUnidirectionalSequenceLSTM();
    impl.resolver.AddMean();
    impl.resolver.AddFullyConnected();
    impl.resolver.AddSoftmax();

    bool ok = false;
    if (buffer && buffer->tensor_arena && buffer->tensor_arena_size > 0) {
        ok = build_interpreter(impl, buffer->tensor_arena, buffer->tensor_arena_size);
    }
    if (!ok) {
        impl.used_fallback = true;
        ok = build_interpreter(impl, g_fallback_arena, kFallbackArenaSize);
    }
    if (!ok) return -3;

    TfLiteTensor *in  = impl.interp->input(0);
    TfLiteTensor *out = impl.interp->output(0);
    if (!in || !out || in->type != kTfLiteFloat32 || out->type != kTfLiteFloat32) return -4;

    g_model.impl        = &impl;
    g_model.input_size  = in->bytes / sizeof(float);
    g_model.output_size = out->bytes / sizeof(float);
    g_model.output      = out->data.f;
    g_in_use = true;
    *model_out = &g_model;
    return CY_RSLT_SUCCESS;
}

cy_rslt_t mtb_ml_model_run(mtb_ml_model_t *model, float *input) {
    if (!model || !model->impl) return -1;
    Impl &impl = *static_cast<Impl *>(model->impl);
    TfLiteTensor *in = impl.interp->input(0);
    memcpy(in->data.f, input, model->input_size * sizeof(float));
    if (impl.interp->Invoke() != kTfLiteOk) return -2;
    model->output = impl.interp->output(0)->data.f;
    return CY_RSLT_SUCCESS;
}

cy_rslt_t mtb_ml_model_deinit(mtb_ml_model_t *model) {
    if (!model || !model->impl) return -1;
    Impl &impl = *static_cast<Impl *>(model->impl);
    if (impl.interp) { impl.interp->~MicroInterpreter(); impl.interp = nullptr; }
    model->impl = nullptr;
    g_in_use = false;
    return CY_RSLT_SUCCESS;
}

int mtb_ml_model_arena_used(const mtb_ml_model_t *model) {
    if (!model) model = &g_model;
    if (!model->impl) return -1;
    Impl &impl = *static_cast<Impl *>(model->impl);
    int used = (int)impl.interp->arena_used_bytes();
    return impl.used_fallback ? -used : used;   /* negativ = Fallback-Arena in Benutzung */
}

} // extern "C"
