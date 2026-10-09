/* Hauslogik: verbindet Anwesenheit (Bluetooth) und Klatschen (Audio) zu einem Lichtschalter.
 *   LED1 = Licht (per Klatschen umschaltbar, geht aus, wenn das Handy das Haus verlaesst)
 *   LED2 = Anwesenheit (an, solange das Handy erreichbar ist) */
#ifndef HOUSE_H
#define HOUSE_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
void house_init(void);
void house_set_presence(bool present);   /* vom Bluetooth-Thread */
bool house_toggle_light(void);           /* vom Audio-Thread bei Klatschen; liefert neuen Lichtzustand */
bool house_light(void);
bool house_present(void);
void bt_presence_thread(void *a, void *b, void *c);
void clap_thread(void *a, void *b, void *c);
#ifdef __cplusplus
}
#endif
#endif
