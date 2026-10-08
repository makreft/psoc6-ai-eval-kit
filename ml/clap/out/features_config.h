/* Automatisch erzeugt von ml/clap/features.py. Nicht von Hand aendern. */
#define FEAT_RATE 16000
#define FEAT_FRAME 256
#define FEAT_N_BANDS 32
#define FEAT_N_FRAMES 32
#define FEAT_ONSET_FRAME 8
#define FEAT_EPS 1e-07f
#define FEAT_DYN_RANGE 5.0f
static const uint16_t FEAT_BAND_EDGES[FEAT_N_BANDS + 1] = {2, 3, 4, 5, 6, 8, 9, 11, 12, 14, 16, 18, 20, 22, 25, 28, 30, 34, 37, 40, 44, 48, 53, 58, 63, 68, 74, 80, 87, 95, 102, 111, 120};
#define MODEL_IN_SCALE 0.003921568859368563f
#define MODEL_IN_ZERO (-128)
#define MODEL_OUT_SCALE 0.00390625f
#define MODEL_OUT_ZERO (-128)
static const char *MODEL_CLASSES[] = {"other", "clap", "snap"};
