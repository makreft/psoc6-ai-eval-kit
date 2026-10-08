/*
 * Anwesenheit per eigenem Access Point (CY8CKIT-062S2-AI, Arduino-Core).
 *
 * Das Board spannt ein WLAN auf. Ein Handy, das dieses Netz einmal gespeichert hat, verbindet sich
 * automatisch, sobald es in Reichweite ist, und meldet sich ab, wenn es den Bereich verlaesst.
 * Anwesend = mindestens ein Client angemeldet (optional: nur ein bestimmtes Geraet per MAC).
 * LED1 an = anwesend, LED2 an = Access Point laeuft.
 *
 * Die Arduino-WiFi-Bibliothek des Cores kann den AP starten, aber keine Clients auflisten. Dafuer
 * greift dieser Sketch auf die darunterliegende Schicht (cy_wcm und WHD) zu:
 *   cy_wcm_register_event_callback      -> Ereignisse STA_JOINED_SOFTAP / STA_LEFT_SOFTAP
 *   cy_wcm_get_associated_client_list   -> Liste der angemeldeten MAC-Adressen
 *   whd_wifi_get_ap_client_rssi         -> Signalstaerke je Client
 * Genau diese drei Dinge sollen spaeter als API in die WiFi-Bibliothek wandern (siehe Protokoll).
 *
 * Konsole (J1, 115200): "AP ssid=.. ip=.. kanal=..", "CLIENT join|leave <mac>", "PRAESENZ an|aus",
 * jede Sekunde "P,<rssi des staerksten Clients oder -100>,<anzahl clients>" fuer den Viewer.
 */
#include <WiFi.h>
#include "cy_wcm.h"
#include "whd_wifi_api.h"
/* In cy_wcm.h nicht deklariert, aber in der Bibliothek vorhanden (cy_wcm_internal.h / ipv4 helper) */
extern "C" cy_rslt_t cy_wcm_get_whd_interface(cy_wcm_interface_t interface_type, whd_interface_t *whd_iface);

#define AP_SSID        "AI-Eval-Kit"
#define AP_PASS        "aievalkit2026"     /* mindestens 8 Zeichen (WPA2) */
#define AP_CHANNEL     6
#define MAX_CLIENTS    8
#define POLL_MS        1000
#define GRACE_MS       0                   /* Karenzzeit nach Abmeldung, bevor LED1 ausgeht (0 = sofort) */
static const uint8_t PHONE_MAC[6] = {0, 0, 0, 0, 0, 0};   /* alles 0 = jedes Geraet zaehlt */

static volatile uint32_t evJoin = 0, evLeave = 0;
static cy_wcm_mac_t lastEventMac;
static whd_interface_t apIf = nullptr;
static bool present = false;
static uint32_t lastSeenMs = 0, lastPollMs = 0, lastStatusMs = 0;

static void macToStr(const uint8_t *m, char *out) { snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]); }
static bool macIsZero(const uint8_t *m) { for (int i = 0; i < 6; i++) if (m[i]) return false; return true; }
static bool macEq(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6) == 0; }

/* Laeuft im Thread des WLAN-Treibers: nur merken, Ausgabe im loop() */
static void onWcmEvent(cy_wcm_event_t event, cy_wcm_event_data_t *data) {
    if (event == CY_WCM_EVENT_STA_JOINED_SOFTAP) { memcpy(lastEventMac, data->sta_mac, 6); evJoin++; }
    else if (event == CY_WCM_EVENT_STA_LEFT_SOFTAP) { memcpy(lastEventMac, data->sta_mac, 6); evLeave++; }
}

void setup() {
    pinMode(LED1, OUTPUT); pinMode(LED2, OUTPUT); digitalWrite(LED1, LOW); digitalWrite(LED2, LOW);
    Serial.begin(115200); delay(1500);
    Serial.println(); Serial.println("Anwesenheit per Access Point");
    uint8_t st = WiFi.beginAP(AP_SSID, AP_PASS, AP_CHANNEL);
    if (st != WL_AP_LISTENING && st != WL_CONNECTED) {
        Serial.print("FEHLER: Access Point konnte nicht gestartet werden, Status "); Serial.println(st);
        for (;;) { digitalWrite(LED2, !digitalRead(LED2)); delay(100); }
    }
    digitalWrite(LED2, HIGH);
    if (cy_wcm_get_whd_interface(CY_WCM_INTERFACE_TYPE_AP, &apIf) != CY_RSLT_SUCCESS) apIf = nullptr;
    cy_wcm_register_event_callback(onWcmEvent);
    Serial.print("AP ssid="); Serial.print(AP_SSID); Serial.print(" ip="); Serial.print(WiFi.localIP());
    Serial.print(" kanal="); Serial.print(AP_CHANNEL); Serial.print(" passwort="); Serial.println(AP_PASS);
    Serial.println("Handy mit diesem WLAN verbinden. LED1 = anwesend.");
}

static int pollClients(int32_t *bestRssi, char *bestMacStr) {
    cy_wcm_mac_t list[MAX_CLIENTS]; memset(list, 0, sizeof(list));
    if (cy_wcm_get_associated_client_list(list, MAX_CLIENTS) != CY_RSLT_SUCCESS) return -1;
    int n = 0; *bestRssi = -100; bestMacStr[0] = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (macIsZero(list[i])) continue;
        if (!macIsZero(PHONE_MAC) && !macEq(list[i], PHONE_MAC)) continue;
        n++;
        int32_t rssi = -100;
        if (apIf) whd_wifi_get_ap_client_rssi(apIf, &rssi, (const whd_mac_t *)list[i]);
        if (rssi > *bestRssi) { *bestRssi = rssi; macToStr(list[i], bestMacStr); }
    }
    return n;
}

void loop() {
    uint32_t now = millis();
    static uint32_t seenJoin = 0, seenLeave = 0;
    char ms[18];
    if (evJoin != seenJoin)  { seenJoin = evJoin;   macToStr(lastEventMac, ms); Serial.print("CLIENT join ");  Serial.println(ms); }
    if (evLeave != seenLeave){ seenLeave = evLeave; macToStr(lastEventMac, ms); Serial.print("CLIENT leave "); Serial.println(ms); }

    if (now - lastPollMs >= POLL_MS) {
        lastPollMs = now;
        int32_t rssi; char best[18];
        int n = pollClients(&rssi, best);
        if (n > 0) lastSeenMs = now;
        bool nowPresent = (n > 0) || (lastSeenMs && now - lastSeenMs < GRACE_MS);
        if (nowPresent != present) {
            present = nowPresent; digitalWrite(LED1, present ? HIGH : LOW);
            Serial.print("PRAESENZ "); Serial.print(present ? "an" : "aus"); Serial.print("  clients="); Serial.print(n);
            if (n > 0) { Serial.print("  rssi="); Serial.print(rssi); Serial.print(" dBm  "); Serial.print(best); }
            Serial.println();
        }
        Serial.print("P,"); Serial.print(n > 0 ? rssi : -100); Serial.print(','); Serial.println(n < 0 ? 0 : n);
        if (now - lastStatusMs >= 10000) {
            lastStatusMs = now;
            Serial.print("STATUS clients="); Serial.print(n); if (n > 0) { Serial.print(" bester="); Serial.print(best); Serial.print(" rssi="); Serial.print(rssi); Serial.print(" dBm"); }
            Serial.println();
        }
    }
}
