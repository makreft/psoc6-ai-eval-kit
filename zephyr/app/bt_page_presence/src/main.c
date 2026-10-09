/*
 * Anwesenheit per klassischem Bluetooth (BR/EDR), ohne App auf dem Handy.
 *
 * Jedes Handy hat eine feste klassische Bluetooth-Adresse (Android: Einstellungen > Ueber das Telefon >
 * Status). Das Board versucht alle PAGE_INTERVAL_MS eine Verbindung auf ACL-Ebene zu dieser Adresse
 * ("Paging"). Kommt sie zustande, ist das Handy in Reichweite und Bluetooth ist an; die Verbindung wird
 * sofort wieder getrennt. Keine Kopplung, kein Dialog am Handy. Schlaegt das Paging FAIL_LIMIT mal in
 * Folge fehl (Handy weg oder Bluetooth aus), gilt "abwesend" und LED1 geht aus.
 *
 * Konsole (J1, 115200), Zeilen wie bei den anderen Anwesenheits-Firmwares fuer tools/live_viewer.py:
 *   AP ssid=BT-Paging ip=none kanal=0
 *   CLIENT join|leave <adresse>, PRAESENZ an|aus clients=<n> ..., P,<-50 wenn erreichbar sonst -100>,<0|1>
 *   PAGE ok <ms>  |  PAGE fail <err>   je Versuch
 */
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <string.h>

/* ---- Einstellungen ---- */
#define PHONE_BT_ADDR     "11:22:33:44:55:66"   /* klassische Bluetooth-Adresse des Handys (Platzhalter, bis Marco sie nennt) */
#define PAGE_INTERVAL_MS  15000                 /* Abstand der Versuche */
#define FAIL_LIMIT        2                     /* so viele Fehlversuche in Folge = abwesend */

static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_NODELABEL(user_led1), gpios);

static bt_addr_t phone;
static K_SEM_DEFINE(page_done, 0, 1);
static volatile int page_result = -1;
static int64_t page_t0;
static bool present = false;
static int fails = 0, oks = 0;
static uint32_t attempts = 0;

static void connected(struct bt_conn *conn, uint8_t err)
{
	struct bt_conn_info info;
	if (bt_conn_get_info(conn, &info) == 0 && info.type != BT_CONN_TYPE_BR) return;
	page_result = err;
	if (err == 0) {
		printk("PAGE ok %lld ms\n", k_uptime_get() - page_t0);
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	} else {
		printk("PAGE fail 0x%02x\n", err);
	}
	k_sem_give(&page_done);
}

static void disconnected(struct bt_conn *conn, uint8_t reason) { (void)conn; (void)reason; }

BT_CONN_CB_DEFINE(conn_callbacks) = { .connected = connected, .disconnected = disconnected };

static void set_present(bool on)
{
	if (on == present) return;
	present = on;
	gpio_pin_set_dt(&led1, on ? 1 : 0);
	if (on) printk("CLIENT join %s\nPRAESENZ an  clients=1  rssi=-50 dBm  %s\n", PHONE_BT_ADDR, PHONE_BT_ADDR);
	else    printk("CLIENT leave %s\nPRAESENZ aus  clients=0\n", PHONE_BT_ADDR);
}

static bool page_once(void)
{
	attempts++;
	page_result = -1; page_t0 = k_uptime_get();
	struct bt_conn *conn = bt_conn_create_br(&phone, BT_BR_CONN_PARAM_DEFAULT);
	if (!conn) { printk("PAGE fail (create)\n"); return false; }
	k_sem_take(&page_done, K_MSEC(12000));
	bt_conn_unref(conn);
	return page_result == 0;
}

int main(void)
{
	gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);
	printk("\nBluetooth-Anwesenheit per Paging (Zephyr, BR/EDR)\n");
	if (bt_addr_from_str(PHONE_BT_ADDR, &phone)) { printk("FEHLER: Adresse ungueltig\n"); return 0; }
	int err = bt_enable(NULL);
	if (err) { printk("FEHLER: bt_enable %d\n", err); for (;;) { gpio_pin_toggle_dt(&led2); k_msleep(100); } }
	printk("Bluetooth bereit (BR/EDR + LE)\n");
	gpio_pin_set_dt(&led2, 1);
	printk("AP ssid=BT-Paging ip=none kanal=0\n");
	printk("Ziel: %s, alle %d s ein Verbindungsversuch, %d Fehlversuche = abwesend\n", PHONE_BT_ADDR, PAGE_INTERVAL_MS / 1000, FAIL_LIMIT);

	int64_t next = k_uptime_get();
	for (;;) {
		int64_t now = k_uptime_get();
		if (now >= next) {
			next = now + PAGE_INTERVAL_MS;
			bool ok = page_once();
			if (ok) { oks++; fails = 0; set_present(true); }
			else { fails++; oks = 0; if (fails >= FAIL_LIMIT) set_present(false); }
		}
		printk("P,%d,%d\n", present ? -50 : -100, present ? 1 : 0);
		k_msleep(1000);
	}
	return 0;
}
