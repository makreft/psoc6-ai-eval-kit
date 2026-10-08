/*
 * Gestenerkennung mit dem IMU des CY8CKIT-062S2-AI (Arduino-Core, ohne ModusToolbox).
 *
 * Portierung von Infineons mtb-example-ml-deepcraft-deploy-motion:
 *   - BMI270 ueber I2C (Standard-Wire, P0_2/P0_3), 50 Hz, 8 g, 500 dps, wie im Original
 *   - model.c / model.h unveraendert aus dem Infineon-Beispiel (DEEPCRAFT Studio, Feb. 2025)
 *   - mtb_ml_model.h / mtb_ml_shim.cpp bilden Infineons ML-Zwischenschicht auf TensorFlow Lite Micro nach
 *
 * Konsole: J1 (KitProg3-UART), 115200 8N1. Pro Inferenz eine Zeile mit allen Scores,
 * dazu (STREAM_RAW_FRAMES) pro Sensor-Frame eine Zeile "D,ax,ay,az,gx,gy,gz" fuer den Live-Viewer.
 * LED1 leuchtet bei "shaking", LED2 bei "circle".
 */
#include <Wire.h>
#include <SparkFun_BMI270_Arduino_Library.h>
/* model.h ist C99 und benutzt 'restrict'; dieser Sketch ist C++, dort heisst das __restrict. */
#ifndef restrict
#define restrict __restrict
#endif
/* model.h hat keine extern-"C"-Klammer, model.c wird aber als C uebersetzt. */
extern "C" {
#include "model.h"
}
#include "mtb_ml_model.h"

#define IMU_FREQ_HZ        50          /* wie IMU_FREQ im Original          */
#define IMU_ACCEL_RANGE_G  8           /* wie IMU_ACCEL_RANGE_G             */
#define IMU_GYRO_RANGE_DPS 500         /* wie IMU_GYRO_RANGE_DPS            */
#define GRAVITY_EARTH      9.80665f    /* Modell erwartet Beschleunigung in m/s^2 */
#define STREAM_RAW_FRAMES  1           /* 1: jeden Sensor-Frame als Zeile "D,ax,ay,az,gx,gy,gz" ausgeben (fuer tools/live_viewer.py) */

static BMI270 imu;
static const char *labels[] = IMAI_DATA_OUT_SYMBOLS;
static float frame[IMAI_DATA_IN_COUNT];
static float scores[IMAI_DATA_OUT_COUNT];
static const uint32_t periodUs = 1000000UL / IMU_FREQ_HZ;
static uint32_t nextSampleUs = 0;
static uint32_t frames = 0, inferences = 0;

static void halt(const char *msg, int code) {
    Serial.print("FEHLER: "); Serial.print(msg); Serial.print(" (Code "); Serial.print(code); Serial.println(")");
    for (;;) { digitalWrite(LED1, HIGH); digitalWrite(LED2, HIGH); delay(100); digitalWrite(LED1, LOW); digitalWrite(LED2, LOW); delay(100); }
}

void setup() {
    pinMode(LED1, OUTPUT); pinMode(LED2, OUTPUT);
    digitalWrite(LED1, LOW); digitalWrite(LED2, LOW);
    Serial.begin(115200);
    delay(1500);
    Serial.println();
    Serial.println("DEEPCRAFT IMU Model Example (Arduino-Portierung)");

    Wire.begin();
    Wire.setClock(400000);
    int8_t err = imu.beginI2C(BMI2_I2C_PRIM_ADDR, Wire);
    if (err != BMI2_OK) halt("BMI270 nicht gefunden an 0x68", err);

    bmi2_sens_config cfg[2];
    cfg[0].type = BMI2_ACCEL;
    cfg[0].cfg.acc.odr         = BMI2_ACC_ODR_50HZ;
    cfg[0].cfg.acc.range       = BMI2_ACC_RANGE_8G;
    cfg[0].cfg.acc.bwp         = BMI2_ACC_NORMAL_AVG4;
    cfg[0].cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;
    cfg[1].type = BMI2_GYRO;
    cfg[1].cfg.gyr.odr         = BMI2_GYR_ODR_50HZ;
    cfg[1].cfg.gyr.range       = BMI2_GYR_RANGE_500;
    cfg[1].cfg.gyr.bwp         = BMI2_GYR_NORMAL_MODE;
    cfg[1].cfg.gyr.noise_perf  = BMI2_POWER_OPT_MODE;
    cfg[1].cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;
    cfg[1].cfg.gyr.ois_range   = BMI2_GYR_OIS_250;
    err = imu.setConfigs(cfg, 2);
    if (err != BMI2_OK) halt("BMI270-Konfiguration fehlgeschlagen", err);
    Serial.println("BMI270 bereit: 50 Hz, 8 g, 500 dps");

    int r = IMAI_init();
    if (r != IMAI_RET_SUCCESS) halt("IMAI_init fehlgeschlagen", r);
    int arena = mtb_ml_model_arena_used(NULL);
    Serial.print("Modell geladen, Arena-Verbrauch: ");
    if (arena < 0) { Serial.print(-arena); Serial.println(" Bytes (interne Fallback-Arena, 16 KB reichten nicht)"); }
    else           { Serial.print(arena);  Serial.println(" Bytes (Arena aus model.c)"); }
    Serial.println("Board schuetteln oder kreisen. Ausgabe pro Inferenz:");

    nextSampleUs = micros();
}

void loop() {
    /* Abtastung auf 50 Hz takten */
    while ((int32_t)(micros() - nextSampleUs) < 0) { }
    nextSampleUs += periodUs;

    if (imu.getSensorData() != BMI2_OK) return;
    frame[0] = imu.data.accelX * GRAVITY_EARTH;   /* g -> m/s^2 */
    frame[1] = imu.data.accelY * GRAVITY_EARTH;
    frame[2] = imu.data.accelZ * GRAVITY_EARTH;
    frame[3] = imu.data.gyroX;                    /* bereits dps */
    frame[4] = imu.data.gyroY;
    frame[5] = imu.data.gyroZ;
    frames++;

#if STREAM_RAW_FRAMES
    /* Kompakte Rohdatenzeile, 50 pro Sekunde: m/s^2 und dps mit zwei Nachkommastellen */
    Serial.print("D,");
    for (int i = 0; i < IMAI_DATA_IN_COUNT; i++) { Serial.print(frame[i], 2); Serial.print(i < IMAI_DATA_IN_COUNT - 1 ? ',' : '\n'); }
#endif

    int r = IMAI_enqueue(frame);
    if (r != IMAI_RET_SUCCESS) halt("IMAI_enqueue", r);

    r = IMAI_dequeue(scores);
    if (r == IMAI_RET_NODATA) return;
    if (r != IMAI_RET_SUCCESS) halt("IMAI_dequeue", r);
    inferences++;

    int best = 0;
    for (int i = 1; i < IMAI_DATA_OUT_COUNT; i++) if (scores[i] > scores[best]) best = i;
    digitalWrite(LED1, best == 2 ? HIGH : LOW);   /* shaking */
    digitalWrite(LED2, best == 1 ? HIGH : LOW);   /* circle  */

    Serial.print('#'); Serial.print(inferences); Serial.print(' ');
    for (int i = 0; i < IMAI_DATA_OUT_COUNT; i++) {
        Serial.print(labels[i]); Serial.print('='); Serial.print(scores[i], 2); Serial.print(' ');
    }
    Serial.print("-> "); Serial.println(labels[best]);
}
