/*
 * Mikrofon als Rohaudio streamen (CY8CKIT-062S2-AI, Arduino-Core), fuer tools/record_audio.py.
 *
 * 16 kHz, Mono, 16 Bit. Pro 256 Samples (16 ms) ein Rahmen auf der KitProg3-Schnittstelle (J1):
 *   2 Byte Magic 0xA5 0x5A | 2 Byte Folgenummer (little endian) | 512 Byte PCM int16 LE | 2 Byte Pruefsumme
 * Pruefsumme = Summe aller Samples als uint16 (little endian). Der Empfaenger synchronisiert sich
 * ueber Magic plus Pruefsumme und erkennt verlorene Rahmen an der Folgenummer.
 *
 * Datenrate: 16000 * 2 Byte = 32 KB/s plus Rahmen, bei 1.000.000 Baud (ca. 100 KB/s) genug Luft.
 * Steuerung vom PC: '1' schaltet LED1 ein (Aufnahme laeuft), '0' aus, '?' gibt eine Statuszeile aus.
 */
#include "PDM.h"

#define SAMPLE_RATE   16000
#define FRAME         256
#define RING          2048
#define BAUD          1000000

static int32_t buf[FRAME];
static int16_t pcm[FRAME];
static uint16_t seq = 0;
static uint32_t frames = 0, dropped = 0;

void setup() {
    pinMode(LED1, OUTPUT); pinMode(LED2, OUTPUT);
    digitalWrite(LED1, LOW); digitalWrite(LED2, LOW);
    Serial.begin(BAUD);
    delay(300);
    PDM.setBufferSize(RING);
    if (PDM.begin(1, SAMPLE_RATE) != 1) {
        for (;;) { digitalWrite(LED2, !digitalRead(LED2)); delay(100); }
    }
    digitalWrite(LED2, HIGH);   /* LED2 dauerhaft an = Streaming laeuft */
}

void loop() {
    while (Serial.available()) {
        int c = Serial.read();
        if (c == '1') digitalWrite(LED1, HIGH);
        else if (c == '0') digitalWrite(LED1, LOW);
        else if (c == '?') { /* Statuszeile als Text, der Empfaenger verwirft sie beim Resync */
            Serial.print("\nSTATUS frames="); Serial.print(frames); Serial.print(" dropped="); Serial.print(dropped); Serial.print("\n");
        }
    }
    int avail = PDM.available();
    if (avail >= RING - FRAME) dropped++;          /* Ringpuffer fast voll: Senden kommt nicht hinterher */
    if (avail < FRAME) return;
    uint32_t n = PDM.read(buf, FRAME);
    if (n != FRAME) return;
    uint16_t sum = 0;
    for (uint32_t i = 0; i < FRAME; i++) { pcm[i] = (int16_t)buf[i]; sum += (uint16_t)pcm[i]; }
    uint8_t hdr[4] = { 0xA5, 0x5A, (uint8_t)(seq & 0xFF), (uint8_t)(seq >> 8) };
    Serial.write(hdr, 4);
    Serial.write((const uint8_t *)pcm, FRAME * 2);
    uint8_t tail[2] = { (uint8_t)(sum & 0xFF), (uint8_t)(sum >> 8) };
    Serial.write(tail, 2);
    seq++; frames++;
}
