/*
 * Klatschen erkennen und damit LED1 ein- und ausschalten (CY8CKIT-062S2-AI, Arduino-Core).
 *
 * Ohne ML: Ein Klatscher ist ein kurzer, lauter Impuls mit steiler Flanke. Der Sketch misst
 * die Lautstaerke (RMS) in 8-ms-Bloecken, fuehrt ein langsames Mass fuer das Grundrauschen mit
 * und erkennt einen Klatscher, wenn
 *   1. die Lautstaerke ploetzlich weit ueber das Grundrauschen springt (Verhaeltnis >= ONSET_RATIO),
 *   2. der Block davor deutlich leiser war (steile Flanke, kein langsames Anschwellen),
 *   3. der Impuls mindestens MIN_CLAP_MS dauert und hoechstens MAX_CLAP_MS. Die Dauer wird relativ
 *      zur eigenen Spitze gemessen (Ende, wenn der Pegel unter END_FRACTION der Spitze faellt),
 *      damit Nachhall und Umgebungslaerm sie nicht verlaengern. Zusaetzlich muss die Spitze
 *      MIN_PEAK_RMS erreichen. Fingerschnipsen ist deutlich kuerzer als Klatschen.
 *   4. seit dem letzten Klatscher mindestens REFRACTORY_MS vergangen sind.
 *
 * LED1 wechselt bei jedem erkannten Klatscher den Zustand, LED2 blitzt 80 ms als Bestaetigung.
 * Konsole an J1 (115200 8N1): eine Zeile pro Klatscher ("KLATSCH ...") und pro verworfenem Impuls
 * ("VERWORFEN ..."), jeweils mit Dauer, Spitzenpegel und Verhaeltnis zum Grundrauschen. Mit
 * SERIAL_LEVELS 1 ausserdem 10x pro Sekunde "L,<max. RMS im 100-ms-Fenster>,<floor>".
 */
#include "PDM.h"

#define SAMPLE_RATE      16000
#define BLOCK            128      /* Samples pro Auswertungsblock = 8 ms, entspricht dem DMA-Puffer der Bibliothek */
#define RING             1024     /* Ringpuffer der Bibliothek in Samples */

#define ONSET_RATIO      6.0f     /* Block muss mindestens so viel lauter als das Grundrauschen sein */
#define QUIET_RATIO      2.0f     /* darunter gilt der Block als leise (fuer die Flanke und das Nachfuehren des Grundrauschens) */
#define END_FRACTION     0.10f    /* Impuls ist zu Ende, wenn der Pegel unter 10 % der Spitze faellt (-20 dB), unabhaengig vom Raum */
#define SUDDEN_FACTOR    3.0f     /* Block davor muss mindestens um diesen Faktor leiser gewesen sein */
#define MIN_RMS          300.0f   /* absolute Untergrenze (16-Bit-Skala), damit Stille nicht "klatscht" */
#define MIN_CLAP_MS      24       /* kuerzer = Schnipsen, Klicken, Knacken. Dauer jetzt bis 10 % der Spitze gemessen (kuerzer
                                     als frueher bis 2x Grundrauschen). Bloecke sind 8 ms, 24 heisst mindestens 3 Bloecke.
                                     Nach dem naechsten Messlauf anhand der Spalte "Abkl." nachjustieren. */
#define MIN_PEAK_RMS     1500.0f  /* leiser = kein Klatscher. Messung: Schnipsen 330 bis 3400, Klatschen 4100 bis 8700. */
#define MAX_CLAP_MS      150      /* laenger = kein Klatscher (Sprechen, Musik) */
#define ABORT_MS         400      /* so lange ueber 10 % der Spitze = Dauerlaerm, abbrechen */
#define REFRACTORY_MS    400      /* Totzeit nach einem Klatscher (Echo, Nachhall) */
#define FLOOR_ALPHA      0.02f    /* Zeitkonstante des Grundrauschens: ca. 50 Bloecke = 0,4 s */
#define SERIAL_LEVELS    1

static int32_t buf[RING];
static volatile bool dataReady = false;
static float noiseFloor = 500.0f;
static float prevRatio = 0.0f, prevRms = 0.0f;
static uint32_t endFloorMs = 0;   /* Zeitpunkt, an dem der Pegel zuletzt unter 2x Grundrauschen fiel (nur fuer das Protokoll) */
static float peakRms = 0.0f, peakRatio = 0.0f, windowMax = 0.0f;
static uint32_t peakZcr = 0;   /* Nulldurchgaenge im lautesten Block: hoch = viel Hochfrequenz (Schnipsen), niedrig = dumpf (Klatschen) */
static bool  inPulse = false;
static uint32_t pulseStartMs = 0, lastClapMs = 0, led2OffMs = 0, lastLevelMs = 0;
static uint32_t claps = 0, blocks = 0;
static bool led1 = false;

static void onPdm() { dataReady = true; }

void setup() {
    pinMode(LED1, OUTPUT); pinMode(LED2, OUTPUT);
    digitalWrite(LED1, LOW); digitalWrite(LED2, LOW);
    Serial.begin(115200);
    delay(1500);
    Serial.println();
    Serial.println("Klatsch-Schalter: Klatschen schaltet LED1 um");
    PDM.setBufferSize(RING);
    if (PDM.begin(1, SAMPLE_RATE) != 1) {
        Serial.println("FEHLER: PDM-Mikrofon konnte nicht gestartet werden");
        for (;;) { digitalWrite(LED2, !digitalRead(LED2)); delay(100); }
    }
    PDM.onReceive(onPdm);
    Serial.println("Mikrofon laeuft: 16 kHz mono, Bloecke von 8 ms");
}

static void reject(uint32_t dur, uint32_t decay, const char *why) {
    Serial.print("VERWORFEN  Dauer "); Serial.print(dur); Serial.print(" ms  Peak "); Serial.print(peakRms, 0);
    Serial.print("  x"); Serial.print(peakRatio, 1); Serial.print("  Grund: "); Serial.print(why);
    Serial.print("  ZCR "); Serial.print(peakZcr); Serial.print("  Abkl. "); Serial.print(decay); Serial.println(" ms");
}

static void processBlock(const int32_t *s, uint32_t n) {
    /* Gleichanteil entfernen und RMS bilden */
    int64_t sum = 0;
    for (uint32_t i = 0; i < n; i++) sum += s[i];
    float mean = (float)sum / n, acc = 0.0f;
    for (uint32_t i = 0; i < n; i++) { float d = s[i] - mean; acc += d * d; }
    float rms = sqrtf(acc / n);
    uint32_t zcr = 0;
    for (uint32_t i = 1; i < n; i++) if (((s[i] - mean) < 0) != ((s[i - 1] - mean) < 0)) zcr++;
    blocks++;

    float ratio = rms / noiseFloor;
    uint32_t now = millis();

    if (!inPulse) {
        /* Grundrauschen nur nachfuehren, solange nichts Lautes passiert (sonst zieht der Klatscher es hoch) */
        if (ratio < ONSET_RATIO) noiseFloor += FLOOR_ALPHA * (rms - noiseFloor);
        if (noiseFloor < 50.0f) noiseFloor = 50.0f;

        bool loud   = ratio >= ONSET_RATIO && rms >= MIN_RMS;
        bool sudden = prevRms * SUDDEN_FACTOR <= rms;     /* relativ zum Block davor, nicht zum Grundrauschen */
        bool free   = now - lastClapMs >= REFRACTORY_MS;
        if (loud && sudden && free) { inPulse = true; pulseStartMs = now; peakRms = rms; peakRatio = ratio; peakZcr = zcr; endFloorMs = 0; }
    } else {
        uint32_t dur = now - pulseStartMs;
        if (rms > peakRms) { peakRms = rms; peakZcr = zcr; }
        if (ratio > peakRatio) peakRatio = ratio;
        if (ratio < QUIET_RATIO && endFloorMs == 0) endFloorMs = now;
        if (rms < peakRms * END_FRACTION || ratio < QUIET_RATIO) {
            /* Impuls zu Ende (unter 10 % der Spitze oder unter 2x Grundrauschen): Dauer und Spitze entscheiden */
            inPulse = false;
            uint32_t decay = endFloorMs ? endFloorMs - pulseStartMs : dur;   /* Abklingzeit bis 2x Grundrauschen, nur Protokoll */
            if (dur < MIN_CLAP_MS)            reject(dur, decay, "zu kurz (Schnipsen, Klicken)");
            else if (peakRms < MIN_PEAK_RMS)  reject(dur, decay, "zu leise");
            else if (dur > MAX_CLAP_MS)       reject(dur, decay, "zu lang");
            else {
                claps++; lastClapMs = now;
                led1 = !led1; digitalWrite(LED1, led1 ? HIGH : LOW);
                digitalWrite(LED2, HIGH); led2OffMs = now + 80;
                Serial.print("KLATSCH #"); Serial.print(claps); Serial.print("  Dauer "); Serial.print(dur); Serial.print(" ms  Peak "); Serial.print(peakRms, 0);
                Serial.print("  x"); Serial.print(peakRatio, 1); Serial.print("  LED1 "); Serial.print(led1 ? "an" : "aus");
                Serial.print("  ZCR "); Serial.print(peakZcr); Serial.print("  Abkl. "); Serial.print(decay); Serial.println(" ms");
            }
        } else if (dur > ABORT_MS) {
            inPulse = false;   /* Dauerlaerm, abbrechen und Grundrauschen wieder nachfuehren */
            reject(dur, dur, "Dauerlaerm");
        }
    }
    prevRatio = ratio; prevRms = rms;

#if SERIAL_LEVELS
    if (rms > windowMax) windowMax = rms;
    if (now - lastLevelMs >= 100) { lastLevelMs = now; Serial.print("L,"); Serial.print(windowMax, 0); Serial.print(','); Serial.println(noiseFloor, 0); windowMax = 0.0f; }
#endif
}

void loop() {
    if (led2OffMs && (int32_t)(millis() - led2OffMs) >= 0) { digitalWrite(LED2, LOW); led2OffMs = 0; }
    if (!dataReady) return;
    dataReady = false;
    while (PDM.available() >= BLOCK) {
        uint32_t n = PDM.read(buf, BLOCK);
        if (n == 0) break;
        processBlock(buf, n);
    }
}
