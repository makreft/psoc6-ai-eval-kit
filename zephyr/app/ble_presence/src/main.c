/*
 * Bluetooth-Anwesenheit: Das Board scannt passiv nach BLE-Advertisements und schaltet LED1,
 * solange das Handy in der Naehe ist. Das Handy muss nichts verbinden, es muss nur senden:
 *   - iBeacon mit der unten eingestellten UUID (Beacon-App auf dem Handy), oder
 *   - ein Advertisement mit dem eingestellten Geraetenamen.
 *
 * Konsole (J1, 115200), gleiches Zeilenformat wie Sketch 06 fuer tools/live_viewer.py:
 *   AP ssid=BLE-Scan ip=none kanal=0          einmal beim Start (Viewer-Hinweiszeile)
 *   ADV <adresse> <rssi> <art> [name]          jede neue/seltene Adresse, zum Identifizieren (max. 1 je Adresse je 10 s)
 *   CLIENT join|leave <adresse>                Ziel erstmals gesehen / Ziel verloren
 *   PRAESENZ an|aus clients=<n> rssi=<dBm> <adresse>
 *   P,<rssi gefiltert oder -100>,<0|1>         jede Sekunde
 */
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include <stdio.h>

/* ---- Einstellungen ---- */
static const uint8_t TARGET_UUID[16] = { 0xA1, 0xE7, 0xA1, 0x4B, 0x17, 0x00, 0x4A, 0x1C, 0x9E, 0x5A, 0x0C, 0x62, 0x00, 0x00, 0xC1, 0x47 };
static const char *TARGET_NAME = "AI-Eval-Kit-Handy";     /* optional: Geraetename im Advertisement */
#define RSSI_ON_DBM      (-85)      /* ab hier gilt "in der Naehe" (Einschalten) */
#define RSSI_OFF_DBM     (-92)      /* darunter gilt "weg" (Ausschalten), Hysterese */
#define LOST_TIMEOUT_MS  8000       /* ohne Advertisement so lange: weg */
#define EMA_ALPHA        0.3f       /* Glaettung der Signalstaerke */

static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);   /* LED1 = P5.3 */
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_NODELABEL(user_led1), gpios);   /* LED2 = P5.4, im Board-DTS ohne Alias */

static struct {
	bool seen;
	bt_addr_le_t addr;
	float rssi_ema;
	int8_t rssi_last;
	int64_t last_seen;
	bool present;
} target;

/* kleine Tabelle fuer die ADV-Ausgabe (Ratenbegrenzung je Adresse) */
#define SEEN_MAX 24
static struct { bt_addr_le_t addr; int64_t last_print; } seen[SEEN_MAX];
static int seen_n;

struct parse_ctx { bool match; bool apple; bool ibeacon; char name[32]; };

static bool parse_cb(struct bt_data *data, void *user_data)
{
	struct parse_ctx *c = user_data;
	switch (data->type) {
	case BT_DATA_NAME_COMPLETE:
	case BT_DATA_NAME_SHORTENED: {
		size_t n = MIN(data->data_len, sizeof(c->name) - 1);
		memcpy(c->name, data->data, n); c->name[n] = 0;
		if (TARGET_NAME[0] && strcmp(c->name, TARGET_NAME) == 0) c->match = true;
		break;
	}
	case BT_DATA_MANUFACTURER_DATA:
		if (data->data_len >= 2 && data->data[0] == 0x4C && data->data[1] == 0x00) {
			c->apple = true;
			/* iBeacon: 4C 00 02 15 <16 Byte UUID> <major> <minor> <txpower> */
			if (data->data_len >= 25 && data->data[2] == 0x02 && data->data[3] == 0x15) {
				c->ibeacon = true;
				if (memcmp(&data->data[4], TARGET_UUID, 16) == 0) c->match = true;
			}
		}
		break;
	default:
		break;
	}
	return true;
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type, struct net_buf_simple *ad)
{
	struct parse_ctx c = { 0 };
	uint8_t raw[31]; uint8_t rawlen = ad->len < sizeof(raw) ? ad->len : sizeof(raw);
	memcpy(raw, ad->data, rawlen);               /* vor dem Parsen sichern, bt_data_parse verbraucht den Puffer */
	bt_data_parse(ad, parse_cb, &c);
	int64_t now = k_uptime_get();

	/* Uebersicht aller Sender, begrenzt auf eine Zeile je Adresse je 10 s */
	int idx = -1;
	for (int i = 0; i < seen_n; i++) if (bt_addr_le_eq(&seen[i].addr, addr)) { idx = i; break; }
	if (idx < 0 && seen_n < SEEN_MAX) { idx = seen_n++; bt_addr_le_copy(&seen[idx].addr, addr); seen[idx].last_print = 0; }
	if (idx >= 0 && now - seen[idx].last_print >= 10000) {
		seen[idx].last_print = now;
		char s[BT_ADDR_LE_STR_LEN]; bt_addr_le_to_str(addr, s, sizeof(s));
		printk("ADV %s %d %s%s%s%s", s, rssi, c.match ? "ZIEL" : c.ibeacon ? "ibeacon" : c.apple ? "apple" : "other",
		       c.name[0] ? " " : "", c.name, adv_type == BT_GAP_ADV_TYPE_SCAN_RSP ? " (scan-rsp)" : "");
		if (rssi > -60) {   /* starke Sender: rohe Bytes zum Identifizieren des eigenen Handys */
			printk(" raw=");
			for (uint16_t i = 0; i < rawlen; i++) printk("%02x", raw[i]);
		}
		printk("\n");
	}

	if (!c.match) return;
	if (!target.seen) {
		target.seen = true; target.rssi_ema = rssi;
		char s[BT_ADDR_LE_STR_LEN]; bt_addr_le_to_str(addr, s, sizeof(s));
		printk("CLIENT join %s\n", s);
	} else {
		target.rssi_ema += EMA_ALPHA * (rssi - target.rssi_ema);
	}
	bt_addr_le_copy(&target.addr, addr);
	target.rssi_last = rssi;
	target.last_seen = now;
}

static void set_present(bool on, int n)
{
	if (on == target.present) return;
	target.present = on;
	gpio_pin_set_dt(&led1, on ? 1 : 0);
	char s[BT_ADDR_LE_STR_LEN] = "-";
	if (target.seen) bt_addr_le_to_str(&target.addr, s, sizeof(s));
	if (on) printk("PRAESENZ an  clients=%d  rssi=%d dBm  %s\n", n, (int)target.rssi_ema, s);
	else    printk("PRAESENZ aus  clients=%d\n", n);
}

int main(void)
{
	gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);
	printk("\nBluetooth-Anwesenheit (Zephyr) auf dem CY8CKIT-062S2-AI\n");

	/* BT_DEVICE_WAKE (P3.5) hoch halten, damit der BT-Kern nicht im Schlaf bleibt; HOST_WAKE (P4.0) als Eingang */
	const struct device *prt3 = DEVICE_DT_GET(DT_NODELABEL(gpio_prt3));
	const struct device *prt4 = DEVICE_DT_GET(DT_NODELABEL(gpio_prt4));
	if (device_is_ready(prt3)) gpio_pin_configure(prt3, 5, GPIO_OUTPUT_ACTIVE);
	if (device_is_ready(prt4)) gpio_pin_configure(prt4, 0, GPIO_INPUT);

	int err = bt_enable(NULL);
	if (err) { printk("FEHLER: bt_enable %d\n", err); for (;;) { gpio_pin_toggle_dt(&led2); k_msleep(100); } }
	printk("Bluetooth bereit\n");

	struct bt_le_scan_param param = {
		.type = BT_LE_SCAN_TYPE_ACTIVE,          /* aktiv: Scan-Antworten anfordern, Android legt Herstellerdaten und Namen oft dorthin */
		.options = BT_LE_SCAN_OPT_NONE,          /* Duplikate melden, damit RSSI laufend aktualisiert wird */
		.interval = 0x0060,                      /* 60 ms */
		.window = 0x0050,                        /* 50 ms, fast dauernd auf Empfang */
	};
	/* Hinweis: Extended Scanning (CONFIG_BT_EXT_ADV) lehnt der CYW43439-Controller ab (bt_le_scan_start -5).
	 * Deshalb klassisches Scannen; das Handy muss klassisch (legacy) senden. */
	err = bt_le_scan_start(&param, scan_cb);
	if (err) { printk("FEHLER: bt_le_scan_start %d\n", err); for (;;) { gpio_pin_toggle_dt(&led2); k_msleep(100); } }
	gpio_pin_set_dt(&led2, 1);                   /* LED2 an = Scanner laeuft */
	printk("AP ssid=BLE-Scan ip=none kanal=0\n");
	printk("Scanne passiv. Ziel: iBeacon-UUID oder Name \"%s\". LED1 = anwesend.\n", TARGET_NAME);

	for (;;) {
		k_msleep(1000);
		int64_t now = k_uptime_get();
		bool fresh = target.seen && (now - target.last_seen) < LOST_TIMEOUT_MS;
		if (target.seen && !fresh && target.present) {
			char s[BT_ADDR_LE_STR_LEN]; bt_addr_le_to_str(&target.addr, s, sizeof(s));
			printk("CLIENT leave %s\n", s);
		}
		bool on;
		if (!fresh) on = false;
		else if (target.present) on = target.rssi_ema >= RSSI_OFF_DBM;
		else on = target.rssi_ema >= RSSI_ON_DBM;
		set_present(on, fresh ? 1 : 0);
		if (!fresh && target.seen && !target.present) target.seen = false;   /* naechstes Sehen gilt wieder als join */
		printk("P,%d,%d\n", fresh ? (int)target.rssi_ema : -100, target.present ? 1 : 0);
	}
	return 0;
}
