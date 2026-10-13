/*
 * Anwesenheit per klassischem Bluetooth (BR/EDR) im HCI-Rohmodus, ohne App auf dem Handy.
 *
 * Jedes Handy hat eine feste klassische Bluetooth-Adresse. Das Board schickt alle PAGE_INTERVAL_MS an jede
 * Adresse aus CONFIG_HOUSE_PHONES (phones.conf, bis zu 8 Handys) einen Remote Name Request ("Paging").
 * Antwortet ein Handy, ist es in Reichweite und Bluetooth an; Status 0x04 (Page Timeout) heisst: weg oder
 * Bluetooth aus. Kopplungsanfragen werden negativ beantwortet, es bleibt bei einem kurzen Funkkontakt ohne
 * Dialog. Anwesend = mindestens ein Handy antwortet; abwesend = keines in FAIL_LIMIT Runden hintereinander.
 *
 * Warum Rohmodus: Zephyrs experimenteller klassischer Host friert bei der ersten erfolgreichen
 * Verbindung ein (Protokoll Abschnitt 66). Hier spricht die Anwendung den Controller direkt an.
 *
 * Konsole (J1, 115200), Zeilen passend zu tools/live_viewer.py (Modus Anwesenheit):
 *   AP ssid=BT-Paging ip=none kanal=0
 *   PAGE ok <ms> <adresse> Name: <name> | PAGE fail 0x<status> ... <adresse> | PAGE timeout <adresse>   je Versuch
 *   CLIENT join|leave <adresse> je Handy, PRAESENZ an|aus clients=<anzahl erreichbar> ..., P,<-50|-100>,<anzahl>
 */
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/hci_raw.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/buf.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/bluetooth.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include <stdlib.h>
#include "house.h"

/* ---- Einstellungen ---- */
#define MAX_PHONES        8
#define PAGE_INTERVAL_MS  CONFIG_HOUSE_PAGE_INTERVAL_MS   /* Abstand der Runden (eine Runde pagt alle Handys nacheinander) */
#define FAIL_LIMIT        CONFIG_HOUSE_FAIL_LIMIT         /* so viele Fehlrunden in Folge = dieses Handy abwesend */
#define PAGE_TIMEOUT_MS   CONFIG_HOUSE_PAGE_TIMEOUT_MS    /* Page-Timeout des Controllers (HCI Write Page Timeout) */
#define CONN_WAIT_MS      (PAGE_TIMEOUT_MS + 2000)        /* laenger als der Page-Timeout, sonst Abbruch durch uns */

struct phone { bt_addr_t addr; char str[18]; bool present; int fails; };
static struct phone phones[MAX_PHONES];
static int n_phones = 0;

static K_FIFO_DEFINE(rx_queue);

/* ---- Nachbildung zweier Host-Funktionen, die der Infineon-Treiber beim Firmware-Download braucht.
 * Im Rohmodus gibt es keinen Host; der Treiber ruft sie waehrend bt_hci_setup() auf. Semantik wie im Host:
 * alloc reserviert Platz fuer den Kommando-Header, send_sync setzt ihn, sendet und wartet auf
 * Command Complete oder Command Status mit passendem Opcode. ---- */
static uint8_t evt_wait(k_timeout_t timeout, uint8_t *out, uint8_t *outlen);

/* Im Rohmodus legt bt_buf_get_tx das H4-Typbyte (0x01) als erstes Datenbyte ab. Dahinter kommt der
 * Kommando-Header (Opcode, Laenge), den send_sync spaeter fuellt, danach die Parameter des Treibers. */
struct net_buf *bt_hci_cmd_alloc(k_timeout_t timeout)
{
	struct net_buf *buf = bt_buf_get_tx(BT_BUF_CMD, timeout, NULL, 0);
	if (buf) memset(net_buf_add(buf, sizeof(struct bt_hci_cmd_hdr)), 0, sizeof(struct bt_hci_cmd_hdr));
	return buf;
}

int bt_hci_cmd_send_sync(uint16_t opcode, struct net_buf *buf, struct net_buf **rsp)
{
	if (!buf) { buf = bt_hci_cmd_alloc(K_FOREVER); if (!buf) return -ENOBUFS; }
	struct bt_hci_cmd_hdr *hdr = (struct bt_hci_cmd_hdr *)&buf->data[1];   /* hinter dem H4-Typbyte */
	hdr->opcode = sys_cpu_to_le16(opcode);
	hdr->param_len = buf->len - 1 - sizeof(*hdr);
	int err = bt_send(buf);
	if (err) { printk("  send_sync 0x%04x: bt_send %d\n", opcode, err); return err; }
	uint8_t ev[64], n;
	for (int i = 0; i < 50; i++) {
		uint8_t code = evt_wait(K_MSEC(2000), ev, &n);
		if (code == 0) { printk("  send_sync 0x%04x: kein Ereignis (Timeout)\n", opcode); return -EIO; }
		if (opcode == BT_HCI_OP_RESET) printk("  send_sync 0x%04x: Ereignis 0x%02x len %u\n", opcode, code, n);
		if (code == BT_HCI_EVT_CMD_COMPLETE && n >= 4 && sys_get_le16(&ev[1]) == opcode) {
			if (rsp) *rsp = NULL;
			return ev[3] == 0 ? 0 : -EIO;
		}
		if (code == BT_HCI_EVT_CMD_STATUS && n >= 4 && sys_get_le16(&ev[2]) == opcode) {
			if (rsp) *rsp = NULL;
			return ev[0] == 0 ? 0 : -EIO;
		}
	}
	return -ETIMEDOUT;
}

static int addr_from_str(const char *str, bt_addr_t *addr)
{
	/* "AA:BB:CC:DD:EE:FF" -> val[5]..val[0] (Little Endian wie im HCI) */
	if (strlen(str) != 17) return -1;
	for (int i = 0; i < 6; i++) {
		char h[3] = { str[i * 3], str[i * 3 + 1], 0 };
		if (i < 5 && str[i * 3 + 2] != ':') return -1;
		addr->val[5 - i] = (uint8_t)strtoul(h, NULL, 16);
	}
	return 0;
}
static bool present = false;
static uint32_t attempts = 0;

static int hci_cmd(uint16_t opcode, const void *params, uint8_t len)
{
	struct net_buf *buf = bt_buf_get_tx(BT_BUF_CMD, K_FOREVER, NULL, 0);
	struct bt_hci_cmd_hdr *hdr = net_buf_add(buf, sizeof(*hdr));
	hdr->opcode = sys_cpu_to_le16(opcode);
	hdr->param_len = len;
	if (len) net_buf_add_mem(buf, params, len);
	return bt_send(buf);
}

static void neg_reply(uint16_t opcode, const bt_addr_t *addr)
{
	hci_cmd(opcode, addr, sizeof(*addr));
}

/* Ereignisse abholen; liefert Ereigniscode oder 0 bei Timeout. Payload wird nach *out kopiert. */
static uint8_t evt_wait(k_timeout_t timeout, uint8_t *out, uint8_t *outlen)
{
	struct net_buf *buf = k_fifo_get(&rx_queue, timeout);
	if (!buf) return 0;
	uint8_t code = 0;
	if (buf->len >= 1 + sizeof(struct bt_hci_evt_hdr) && net_buf_pull_u8(buf) == BT_HCI_H4_EVT) {
		struct bt_hci_evt_hdr *hdr = net_buf_pull_mem(buf, sizeof(*hdr));
		code = hdr->evt;
		uint8_t n = buf->len < 64 ? buf->len : 64;
		memcpy(out, buf->data, n); *outlen = n;
	}
	net_buf_unref(buf);
	return code;
}

/* Sicherheitsanfragen des Gegenuebers waehrend des kurzen Kontakts: alle ablehnen */
static void handle_security_event(uint8_t code, const uint8_t *p)
{
	bt_addr_t a; memcpy(&a, p, 6);
	if (code == BT_HCI_EVT_LINK_KEY_REQ)      neg_reply(BT_HCI_OP_LINK_KEY_NEG_REPLY, &a);
	else if (code == BT_HCI_EVT_PIN_CODE_REQ) neg_reply(BT_HCI_OP_PIN_CODE_NEG_REPLY, &a);
	else if (code == BT_HCI_EVT_IO_CAPA_REQ) {
		struct { bt_addr_t bdaddr; uint8_t reason; } __packed r = { .reason = 0x18 /* pairing not allowed */ };
		memcpy(&r.bdaddr, &a, 6);
		hci_cmd(BT_OP(BT_OGF_LINK_CTRL, 0x0034) /* IO Capability Request Negative Reply */, &r, sizeof(r));
	}
}

/* Page-Timeout des Controllers setzen (Einheit 0,625 ms); Standard waere 0x2000 = 5,12 s */
static int hci_set_page_timeout(uint32_t ms)
{
	uint16_t v = sys_cpu_to_le16((uint16_t)(ms * 1000 / 625));
	struct net_buf *buf = bt_hci_cmd_alloc(K_FOREVER);
	if (!buf) return -ENOBUFS;
	net_buf_add_mem(buf, &v, sizeof(v));
	return bt_hci_cmd_send_sync(BT_OP(BT_OGF_BASEBAND, 0x0018), buf, NULL);
}

static int hci_reset(void)
{
	uint8_t ev[64], n;
	hci_cmd(BT_HCI_OP_RESET, NULL, 0);
	for (int i = 0; i < 10; i++) {
		uint8_t code = evt_wait(K_MSEC(2000), ev, &n);
		if (code == 0) return -1;
		if (code == BT_HCI_EVT_CMD_COMPLETE && n >= 4 && sys_get_le16(&ev[1]) == BT_HCI_OP_RESET) return ev[3] == 0 ? 0 : -2;
	}
	return -3;
}

/* Ein Paging-Versuch per Remote Name Request: true = Handy erreichbar.
 * Das Kommando pagt die Adresse und holt nur den Geraetenamen, ohne ACL-Datenverbindung und ohne
 * Kopplung (so arbeitet auch "hcitool name"). Antwort: Remote Name Request Complete (0x07) mit
 * Status 0 (da) oder 0x04 Page Timeout (weg oder Bluetooth aus). */
#define OP_REMOTE_NAME_REQ        BT_OP(BT_OGF_LINK_CTRL, 0x0019)
#define OP_REMOTE_NAME_REQ_CANCEL BT_OP(BT_OGF_LINK_CTRL, 0x001a)
#define EVT_REMOTE_NAME_COMPLETE  0x07

static bool page_once(struct phone *ph)
{
	attempts++;
	struct { bt_addr_t bdaddr; uint8_t pscan_rep_mode; uint8_t reserved; uint16_t clock_offset; } __packed cp = { .pscan_rep_mode = 0x01 };
	bt_addr_copy(&cp.bdaddr, &ph->addr);
	int64_t t0 = k_uptime_get();
	hci_cmd(OP_REMOTE_NAME_REQ, &cp, sizeof(cp));

	uint8_t ev[64], n; int result = -1;
	int64_t deadline = t0 + CONN_WAIT_MS;
	while (k_uptime_get() < deadline) {
		int64_t left = deadline - k_uptime_get();
		uint8_t code = evt_wait(K_MSEC(left > 0 ? left : 1), ev, &n);
		if (code == 0) continue;                       /* Nicht-Ereignis (z. B. ACL) oder Timeout der Wartezeit */
		if (code == BT_HCI_EVT_CMD_STATUS && n >= 4) {
			if (ev[0] != 0) { printk("PAGE fail (cmd status 0x%02x) %s\n", ev[0], ph->str); return false; }
		} else if (code == EVT_REMOTE_NAME_COMPLETE && n >= 7) {
			result = ev[0];
			if (result == 0) { ev[n < 64 ? n : 63] = 0; printk("PAGE ok %lld ms  %s  Name: %s\n", k_uptime_get() - t0, ph->str, (const char *)&ev[7]); }
			else printk("PAGE fail 0x%02x nach %lld ms  %s\n", result, k_uptime_get() - t0, ph->str);
			break;
		} else if (code == BT_HCI_EVT_LINK_KEY_REQ || code == BT_HCI_EVT_PIN_CODE_REQ || code == BT_HCI_EVT_IO_CAPA_REQ) {
			handle_security_event(code, ev);
		}
	}
	if (result < 0) {
		printk("PAGE timeout  %s\n", ph->str);
		hci_cmd(OP_REMOTE_NAME_REQ_CANCEL, &ph->addr, sizeof(ph->addr));
		for (int i = 0; i < 5; i++) { uint8_t c = evt_wait(K_MSEC(1000), ev, &n); if (c == EVT_REMOTE_NAME_COMPLETE || c == 0) break; }
		return false;
	}
	return result == 0;
}

static int n_present(void) { int n = 0; for (int i = 0; i < n_phones; i++) if (phones[i].present) n++; return n; }

/* Zustand eines Handys setzen; die Gesamtanwesenheit (mindestens eines erreichbar) geht an die Hauslogik */
static void set_phone_present(struct phone *ph, bool on)
{
	if (on == ph->present) return;
	ph->present = on;
	printk("CLIENT %s %s\n", on ? "join" : "leave", ph->str);
	int n = n_present(); bool any = n > 0;
	if (any != present) { present = any; house_set_presence(any); }
	if (any) { const char *first = ""; for (int i = 0; i < n_phones; i++) if (phones[i].present) { first = phones[i].str; break; }
		printk("PRAESENZ an  clients=%d  rssi=-50 dBm  %s\n", n, first); }
	else printk("PRAESENZ aus  clients=0\n");
}

static int parse_phones(const char *list)
{
	char buf[MAX_PHONES * 20]; strncpy(buf, list, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
	for (char *tok = strtok(buf, ", "); tok && n_phones < MAX_PHONES; tok = strtok(NULL, ", ")) {
		struct phone *ph = &phones[n_phones];
		if (addr_from_str(tok, &ph->addr)) { printk("FEHLER: Adresse ungueltig: %s\n", tok); return -1; }
		strncpy(ph->str, tok, 17); ph->str[17] = 0; ph->present = false; ph->fails = 0; n_phones++;
	}
	return n_phones ? 0 : -1;
}


void bt_presence_thread(void *a, void *b, void *c)
{
	(void)a; (void)b; (void)c;
	printk("BT: Anwesenheit per Paging (HCI-Rohmodus)\n");
	if (parse_phones(CONFIG_HOUSE_PHONES)) { printk("FEHLER: keine gueltige Handy-Adresse (CONFIG_HOUSE_PHONES, siehe phones.conf.example)\n"); return; }
	int err = bt_enable_raw(&rx_queue);
	printk("bt_enable_raw -> %d\n", err);
	if (err) { for (;;) { k_msleep(100); } }
	/* Im Rohmodus ruft Zephyr die Treiber-Einrichtung (Firmware-Download, Baudraten) nicht auf: selbst machen */
	const struct device *hci = DEVICE_DT_GET(DT_CHOSEN(zephyr_bt_hci));
	struct bt_hci_setup_params sp = { 0 };
	err = bt_hci_setup(hci, &sp);
	printk("bt_hci_setup (Firmware-Download) -> %d\n", err);
	if (err) { for (;;) { k_msleep(100); } }
	err = hci_reset();
	printk("HCI Reset -> %d\n", err);
	if (err) { for (;;) { k_msleep(200); } }
	err = hci_set_page_timeout(PAGE_TIMEOUT_MS);
	printk("Page-Timeout %d ms -> %d\n", PAGE_TIMEOUT_MS, err);
	printk("Bluetooth bereit (Controller direkt)\n");
	printk("AP ssid=BT-Paging ip=none kanal=0\n");
	printk("Ziel: %d Handy(s), alle %d s eine Paging-Runde, Page-Timeout %d ms, %d Fehlrunden = abwesend\n", n_phones, PAGE_INTERVAL_MS / 1000, PAGE_TIMEOUT_MS, FAIL_LIMIT);
	for (int i = 0; i < n_phones; i++) printk("  Handy %d: %s\n", i + 1, phones[i].str);

	int64_t next = k_uptime_get();
	uint32_t ticks = 0;
	for (;;) {
		int64_t now = k_uptime_get();
		if (now >= next) {
			next = now + PAGE_INTERVAL_MS;
			for (int i = 0; i < n_phones; i++) {
				struct phone *ph = &phones[i];
				if (page_once(ph)) { ph->fails = 0; set_phone_present(ph, true); }
				else { ph->fails++; if (ph->fails >= FAIL_LIMIT) set_phone_present(ph, false); }
			}
		}
		/* zwischendurch eintreffende Ereignisse (z. B. verspaetete) leeren */
		uint8_t ev[64], n; while (evt_wait(K_NO_WAIT, ev, &n)) { }
		printk("P,%d,%d\n", present ? -50 : -100, n_present());
		/* AP-Zeile jede Minute wiederholen, damit ein spaeter gestarteter Viewer den Modus erkennt */
		if (++ticks % 60 == 0) printk("AP ssid=BT-Paging ip=none kanal=0\n");
		k_msleep(1000);
	}
}
