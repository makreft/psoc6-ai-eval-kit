/*
 * Gemeinsame Firmware "house": Bluetooth-Anwesenheit (Paging, HCI-Rohmodus) und Klatsch-Erkennung
 * (Mikrofon, Merkmale, TensorFlow Lite Micro) laufen parallel in zwei Threads.
 *
 * Verhalten: Klatschen schaltet das Licht (LED1) um. Verlaesst das Handy das Haus (Bluetooth nicht mehr
 * erreichbar), geht das Licht aus. Kommt das Handy zurueck, bleibt das Licht aus, bis geklatscht wird.
 * LED2 zeigt die Anwesenheit. Konsole: Zeilen beider Teile (P,/PAGE/PRAESENZ und L,/MODELL/KLATSCH), dazu
 * "LICHT an|aus <grund>".
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include "house.h"

static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);       /* Licht */
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_NODELABEL(user_led1), gpios); /* Anwesenheit */
static K_MUTEX_DEFINE(lock);
static bool light = false, present = false;

void house_init(void)
{
	gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);
}

static void apply(const char *why)
{
	gpio_pin_set_dt(&led1, light ? 1 : 0);
	gpio_pin_set_dt(&led2, present ? 1 : 0);
	printk("LICHT %s  (%s)\n", light ? "an" : "aus", why);
}

void house_set_presence(bool p)
{
	k_mutex_lock(&lock, K_FOREVER);
	bool was = present; present = p;
	if (was && !p && light) { light = false; apply("Handy weg"); }
	else gpio_pin_set_dt(&led2, present ? 1 : 0);
	k_mutex_unlock(&lock);
}

bool house_toggle_light(void)
{
	k_mutex_lock(&lock, K_FOREVER);
	light = !light; apply("Klatschen");
	bool l = light;
	k_mutex_unlock(&lock);
	return l;
}

bool house_light(void) { return light; }
bool house_present(void) { return present; }

K_THREAD_STACK_DEFINE(audio_stack, 16384);
static struct k_thread audio_thread;

int main(void)
{
	printk("\nHouse: Bluetooth-Anwesenheit + Klatsch-Modell (Zephyr)\n");
	house_init();
	/* Audio-Thread (Prioritaet 5) hoeher als der Bluetooth-Thread im Hauptthread (Prioritaet 7, prj.conf),
	 * damit das Mikrofon-FIFO (256 Woerter = 16 ms) beim Firmware-Download und Paging nicht ueberlaeuft */
	k_thread_create(&audio_thread, audio_stack, K_THREAD_STACK_SIZEOF(audio_stack), clap_thread, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	k_thread_name_set(&audio_thread, "audio");
	bt_presence_thread(NULL, NULL, NULL);   /* laeuft im Hauptthread weiter */
	return 0;
}
