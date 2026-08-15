/*
 * BLE peripheral. See link.h and dongle/BUILD_SPEC.md §7.1 for the
 * connection lifecycle this file implements the peripheral half of:
 *
 *   advertise, filtered to the dongle's address only
 *     -> connected: install set_key as LTK via bt_nrf_conn_set_ltk(),
 *        then wait — the peripheral never raises security itself
 *     -> security_changed: LED_LINK's radio-up half goes true
 *     -> RR_UPLINK CCCD written: first notification allowed is UP_READY
 *     -> RR_DOWNLINK writes decoded and dispatched to haptic.c/indicators.c
 *
 * THREADING: same discipline as dongle/src/radio_ble.c, same reason — this
 * board has its own single cooperative workqueue (main.c's remote_q, shared
 * by buttons.c/haptic.c/indicators.c), and the invariant only holds if the
 * BT host thread is the only other source of events and it never touches
 * that state directly either. bt_conn_cb and the GATT server callbacks below
 * copy out the (small, short-lived) data they were handed into a struct
 * link_evt, push it onto link_evtq, and submit the single static
 * link_evt_work to link_workq; link_evt_work drains the queue and does the
 * actual state mutation and haptic.c/indicators.c calls there. The one
 * exception is identity_read_cb: it only ever serves identity_buf, filled
 * once at boot and never written again, so there is nothing to race and
 * nothing to defer — and its ATT read response has to be synchronous anyway.
 */
#include "link.h"
#include "haptic.h"
#include "indicators.h"
#include "rframe.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/att.h>
#include <bluetooth/nrf/host_extensions.h>

#include <string.h>

#define FW_MAJOR 0
#define FW_MINOR 1
#define FW_PATCH 0

/* RADIO_PROTOCOL.md §3.1 base UUID. Byte-identical to
 * dongle/src/radio_ble.c's copy by necessity — the two are separate Zephyr
 * applications with no shared build, so there is no compiler to catch a
 * drift here. Same hazard PROTOCOL.md's hard-link story warns about
 * (CLAUDE.md §2), watched by hand rather than by tooling for now. */
#define RR_UUID_SERVICE  BT_UUID_128_ENCODE(0x8f2a0001, 0x6b1f, 0x4d5a, 0x9c3e, 0x1d7b4a0e5c21)
#define RR_UUID_IDENTITY BT_UUID_128_ENCODE(0x8f2a0002, 0x6b1f, 0x4d5a, 0x9c3e, 0x1d7b4a0e5c21)
#define RR_UUID_UPLINK   BT_UUID_128_ENCODE(0x8f2a0003, 0x6b1f, 0x4d5a, 0x9c3e, 0x1d7b4a0e5c21)
#define RR_UUID_DOWNLINK BT_UUID_128_ENCODE(0x8f2a0004, 0x6b1f, 0x4d5a, 0x9c3e, 0x1d7b4a0e5c21)

/* §8.3: this board is bus-powered with no battery, so battery_pct has
 * nothing true to report. Fixed and obviously synthetic rather than a
 * plausible, drifting value — the same trap CONFIG_DONGLE_FAKE_LINK set on
 * the dongle (deleted, PLAN.md §4.11), wearing different clothes and with
 * none of the visibility: there is no Kconfig symbol whose name gives it
 * away, only this comment. */
#define SYNTHETIC_BATTERY_PCT 42u
#define TELEMETRY_INTERVAL_MS 10000u

static const struct bt_uuid_128 uuid_service  = BT_UUID_INIT_128(RR_UUID_SERVICE);
static const struct bt_uuid_128 uuid_identity = BT_UUID_INIT_128(RR_UUID_IDENTITY);
static const struct bt_uuid_128 uuid_uplink   = BT_UUID_INIT_128(RR_UUID_UPLINK);
static const struct bt_uuid_128 uuid_downlink = BT_UUID_INIT_128(RR_UUID_DOWNLINK);

static uint8_t identity_buf[20];
static struct bt_conn *active_conn;
static bool uplink_subscribed;
static bool booted_once;
static uint8_t next_ctr;
static struct bt_nrf_ltk set_ltk;
static bt_addr_le_t dongle_addr;
static struct k_work_q *link_workq;
static struct k_work_delayable telemetry_work;

/* ------------------------------------------------------------------------ */
/* BT-thread -> link_workq event marshaling                                  */
/*                                                                            */
/* See this file's header comment. Sized for 8 pending events: connection-
 * lifecycle ones are inherently one-in-flight-at-a-time, and a downlink
 * write burst is bounded by the controller's own connection event cadence.
 * A full queue drops the event and counts it — fail closed, CLAUDE.md §4.4.
 */
enum link_evt_type {
	LINK_EVT_CONNECTED,
	LINK_EVT_DISCONNECTED,
	LINK_EVT_SECURITY_CHANGED,
	LINK_EVT_CCC_CHANGED,
	LINK_EVT_DOWNLINK_WRITE,
};

struct link_evt {
	enum link_evt_type type;
	struct bt_conn *conn; /* ref'd by the shim, unref'd once the dispatcher
			       * has run the handler; NULL for CCC/downlink
			       * events, which need no conn identification —
			       * this board has exactly one connection */
	union {
		struct {
			uint8_t err;
		} connected;
		struct {
			bt_security_t level;
			enum bt_security_err err;
		} security;
		struct {
			uint16_t value;
		} ccc;
		struct {
			uint8_t data[RFRAME_MAX_LEN];
			uint16_t length;
		} downlink;
	};
};

K_MSGQ_DEFINE(link_evtq, sizeof(struct link_evt), 8, 4);
static struct k_work link_evt_work;
static uint32_t link_evt_drops;

static void link_evt_post(const struct link_evt *evt)
{
	if (k_msgq_put(&link_evtq, evt, K_NO_WAIT) != 0) {
		link_evt_drops++;
		if (evt->conn != NULL) {
			bt_conn_unref(evt->conn);
		}
		return;
	}
	(void)k_work_submit_to_queue(link_workq, &link_evt_work);
}

/* ------------------------------------------------------------------------ */
/* GATT attribute callbacks                                                  */
/* ------------------------------------------------------------------------ */

static ssize_t identity_read_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				void *buf, uint16_t len, uint16_t offset)
{
	/* No marshaling: identity_buf is written once at boot (link_init())
	 * and never again, so there is nothing here that races with
	 * link_workq, and an ATT read has to answer synchronously regardless. */
	ARG_UNUSED(conn);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, identity_buf,
				 sizeof(identity_buf));
}

static void send_uplink(const uint8_t *buf, int len);

/*
 * RP §7.2: UP_READY is the first uplink frame after subscription, whenever
 * the remote holds no indicator state — which is always true right here,
 * since this fires once per fresh subscription and the remote never retains
 * indicator state across one (§7.1 of this doc). ctr_base is "the value CTR
 * will take on the next uplink frame" (RP §7.2) — that is next_ctr's value
 * *after* this frame's own increment, i.e. next_ctr as read below,
 * post-increment.
 */
static void handle_ccc_changed(uint16_t value)
{
	uint8_t buf[RFRAME_MAX_LEN];
	uint8_t ctr;
	enum rframe_ready_reason reason;
	int n;

	uplink_subscribed = (value == BT_GATT_CCC_NOTIFY);
	if (!uplink_subscribed) {
		return;
	}

	ctr = next_ctr++;
	reason = booted_once ? RFRAME_READY_RECONNECT : RFRAME_READY_BOOT;
	n = rframe_enc_up_ready(buf, sizeof(buf), ctr, next_ctr, reason);
	booted_once = true;

	send_uplink(buf, n);
}

static void handle_downlink_write(const uint8_t *data, uint16_t length)
{
	struct rframe_msg msg;
	enum rframe_decode_status st;

	st = rframe_decode(data, length, &msg);
	switch (st) {
	case RFRAME_ERR_UNKNOWN_TYPE:
	case RFRAME_ERR_LENGTH:
	case RFRAME_ERR_FIELD:
		return; /* A1/A2/A3: ignore. No LOG channel exists on this board. */
	default:
		break;
	}

	switch (msg.type) {
	case RFRAME_DN_HAPTIC:
		haptic_render(msg.dn_haptic.waveform, msg.dn_haptic.ttl_4ms);
		break;
	case RFRAME_DN_INDICATOR:
		indicators_set(msg.dn_indicator.f1_mode, msg.dn_indicator.f1_rgb,
			       msg.dn_indicator.f2_mode, msg.dn_indicator.f2_rgb);
		break;
	case RFRAME_DN_CONFIG:
		haptic_set_scale(msg.dn_config.haptic_scale);
		indicators_set_brightness(msg.dn_config.led_brightness);
		break;
	case RFRAME_DN_HOST:
		indicators_set_host_up(msg.dn_host.up);
		break;
	case RFRAME_DN_SIMSOC:
		indicators_set_battery_pct(msg.dn_simsoc.pct);
		break;
	default:
		break;
	}
}

static void uplink_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	struct link_evt evt = { .type = LINK_EVT_CCC_CHANGED };

	ARG_UNUSED(attr);
	evt.ccc.value = value;
	link_evt_post(&evt);
}

static ssize_t downlink_write_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				 const void *buf, uint16_t len, uint16_t offset,
				 uint8_t flags)
{
	struct link_evt evt = { .type = LINK_EVT_DOWNLINK_WRITE };

	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	evt.downlink.length = MIN(len, sizeof(evt.downlink.data));
	memcpy(evt.downlink.data, buf, evt.downlink.length);
	link_evt_post(&evt);

	/* Write Without Response: this return value is not used to build an
	 * ATT reply (there isn't one), only to satisfy the callback's
	 * signature — the actual decode+dispatch happens in
	 * handle_downlink_write() above, on link_workq. */
	return len;
}

/* RR_UPLINK's value has no direct read/write; NOTIFY only (RP §3.3). */
BT_GATT_SERVICE_DEFINE(rr_svc,
	BT_GATT_PRIMARY_SERVICE(&uuid_service.uuid),

	BT_GATT_CHARACTERISTIC(&uuid_identity.uuid, BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ_ENCRYPT,
			       identity_read_cb, NULL, NULL),

	BT_GATT_CHARACTERISTIC(&uuid_uplink.uuid, BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE, NULL, NULL, NULL),
	BT_GATT_CCC(uplink_ccc_changed,
		   BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),

	BT_GATT_CHARACTERISTIC(&uuid_downlink.uuid, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE_ENCRYPT,
			       NULL, downlink_write_cb, NULL),
);

/* Index of the RR_UPLINK *value* attribute within rr_svc.attrs[], counting
 * from 0: [0] primary service, [1] identity decl, [2] identity value,
 * [3] uplink decl, [4] uplink value, [5] uplink CCC, [6] downlink decl,
 * [7] downlink value. dongle/src/radio_ble.c assumes ccc_handle ==
 * value_handle + 1 for exactly this reason — [5] follows [4] directly. */
#define RR_UPLINK_VALUE_ATTR (&rr_svc.attrs[4])

/* ------------------------------------------------------------------------ */
/* Uplink                                                                    */
/* ------------------------------------------------------------------------ */

/*
 * The one choke point every uplink frame passes through. CTR advances here
 * unconditionally — remote/BUILD_SPEC.md §6.2: "CTR advances for every
 * classified press, including presses that cannot be delivered" — so a press
 * made while disconnected still consumes a CTR value, which is what lets the
 * dongle see it as a radio_gap on reconnection (A18) instead of nothing at
 * all.
 */
static void send_uplink(const uint8_t *buf, int len)
{
	if (len < 0) {
		return;
	}
	if (active_conn == NULL || !uplink_subscribed) {
		return; /* dropped; the CTR consumed above is the only trace */
	}
	(void)bt_gatt_notify(active_conn, RR_UPLINK_VALUE_ATTR, buf, (uint16_t)len);
}

void link_on_gesture(enum proto_button b, enum proto_gesture g)
{
	uint8_t buf[RFRAME_MAX_LEN];
	uint8_t ctr = next_ctr++;
	int n = rframe_enc_up_input(buf, sizeof(buf), ctr, b, g);

	send_uplink(buf, n);
}

/* §6.1: "sent on connection, on any battery_pct change of more than one
 * point, on any flags change, and otherwise every 10 s." The synthetic value
 * never changes, so only the connection-time send and the 10 s floor apply
 * here — there is no change-triggered path to implement. */
static void telemetry_handler(struct k_work *work)
{
	uint8_t buf[RFRAME_MAX_LEN];
	uint8_t ctr;
	int n;

	ARG_UNUSED(work);

	ctr = next_ctr++;
	n = rframe_enc_up_telemetry(buf, sizeof(buf), ctr, SYNTHETIC_BATTERY_PCT, 0, 0);
	send_uplink(buf, n);

	k_work_reschedule_for_queue(link_workq, &telemetry_work,
				    K_MSEC(TELEMETRY_INTERVAL_MS));
}

/* ------------------------------------------------------------------------ */
/* Connection lifecycle                                                      */
/* ------------------------------------------------------------------------ */

static void start_advertising(void);

static void handle_connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		start_advertising();
		return;
	}

	active_conn = bt_conn_ref(conn);
	uplink_subscribed = false;

	/*
	 * BUILD_SPEC §7.1/RP §10.3: the LTK is installed here but security is
	 * never raised from this side. "Peripherals should wait for the
	 * central to trigger encryption" (host_extensions.h) — the dongle
	 * calls bt_conn_set_security() once it has done the same on its end.
	 */
	(void)bt_nrf_conn_set_ltk(conn, &set_ltk, true);

	/* §6.1: "sent on connection". Dropped silently if this fires before
	 * the dongle has subscribed — send_uplink()'s ordinary not-subscribed
	 * path, no special case needed. */
	k_work_reschedule_for_queue(link_workq, &telemetry_work, K_NO_WAIT);
}

static void handle_disconnected(struct bt_conn *conn)
{
	if (active_conn == conn) {
		bt_conn_unref(active_conn);
		active_conn = NULL;
	}
	uplink_subscribed = false;
	indicators_set_radio_up(false);
	(void)k_work_cancel_delayable(&telemetry_work);
	start_advertising();
}

static void handle_security_changed(bt_security_t level, enum bt_security_err err)
{
	/* A12 from this side: an encryption failure just means the link never
	 * becomes usable — GATT_PERM_*_ENCRYPT already refuses every
	 * characteristic below L4, so there is nothing further to refuse. */
	indicators_set_radio_up(err == BT_SECURITY_ERR_SUCCESS && level >= BT_SECURITY_L4);
}

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	struct link_evt evt = { .type = LINK_EVT_CONNECTED, .conn = bt_conn_ref(conn) };

	evt.connected.err = err;
	link_evt_post(&evt);
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	struct link_evt evt = { .type = LINK_EVT_DISCONNECTED, .conn = bt_conn_ref(conn) };

	ARG_UNUSED(reason);
	link_evt_post(&evt);
}

static void on_security_changed(struct bt_conn *conn, bt_security_t level,
				enum bt_security_err err)
{
	struct link_evt evt = { .type = LINK_EVT_SECURITY_CHANGED, .conn = bt_conn_ref(conn) };

	evt.security.level = level;
	evt.security.err = err;
	link_evt_post(&evt);
}

static struct bt_conn_cb conn_callbacks = {
	.connected = on_connected,
	.disconnected = on_disconnected,
	.security_changed = on_security_changed,
};

/* ------------------------------------------------------------------------ */
/* link_evt dispatcher                                                       */
/* ------------------------------------------------------------------------ */

static void link_evt_work_handler(struct k_work *work)
{
	struct link_evt evt;

	ARG_UNUSED(work);

	while (k_msgq_get(&link_evtq, &evt, K_NO_WAIT) == 0) {
		switch (evt.type) {
		case LINK_EVT_CONNECTED:
			handle_connected(evt.conn, evt.connected.err);
			break;
		case LINK_EVT_DISCONNECTED:
			handle_disconnected(evt.conn);
			break;
		case LINK_EVT_SECURITY_CHANGED:
			handle_security_changed(evt.security.level, evt.security.err);
			break;
		case LINK_EVT_CCC_CHANGED:
			handle_ccc_changed(evt.ccc.value);
			break;
		case LINK_EVT_DOWNLINK_WRITE:
			handle_downlink_write(evt.downlink.data, evt.downlink.length);
			break;
		}

		if (evt.conn != NULL) {
			bt_conn_unref(evt.conn);
		}
	}
}

/* ------------------------------------------------------------------------ */
/* Advertising — remote/BUILD_SPEC.md §8.2                                   */
/* ------------------------------------------------------------------------ */

/*
 * Simplified from §8.2's full table: this always advertises undirected,
 * connectable, filtered to the dongle's address, at 100 ms. The directed
 * fast-path for the first 2 s after a disconnect is not implemented —
 * documented as a deliberate scope cut for the first cut of this firmware,
 * not an oversight. It trades a faster reconnect for one less state machine;
 * nothing about W1-W5 depends on reconnect speed specifically.
 */
static void start_advertising(void)
{
	static const struct bt_le_adv_param adv_param = BT_LE_ADV_PARAM_INIT(
		BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_FILTER_CONN,
		160 /* 100 ms, 0.625 ms units */, 160, NULL);

	(void)bt_le_adv_stop();
	(void)bt_le_adv_start(&adv_param, NULL, 0, NULL, 0);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

int link_init(const struct provisioning_record *prov, struct k_work_q *workq)
{
	int err;

	link_workq = workq;
	k_work_init(&link_evt_work, link_evt_work_handler);

	memset(identity_buf, 0, sizeof(identity_buf));
	/* CONFIG_REMOTE_TEST_RADIO_PROTO_MAJOR defaults to 1, the real value —
	 * see remote/Kconfig. Only the A14 negative-case build ever sets it
	 * to anything else. */
	identity_buf[0] = CONFIG_REMOTE_TEST_RADIO_PROTO_MAJOR;
	identity_buf[1] = 0; /* radio_proto_minor */
	identity_buf[2] = (prov->role == PROVISIONING_ROLE_RED) ? 0x01 : 0x02;
	identity_buf[3] = FW_MAJOR;
	identity_buf[4] = FW_MINOR;
	identity_buf[5] = FW_PATCH;
	/* [6..7] caps, 0 */
	memcpy(&identity_buf[8], prov->set_serial, 12);

	memcpy(set_ltk.val, prov->set_key, sizeof(set_ltk.val));

	dongle_addr.type = BT_ADDR_LE_RANDOM;
	memcpy(dongle_addr.a.val, prov->peer_addr[0], PROVISIONING_ADDR_LEN);

	{
		bt_addr_le_t own_addr = { .type = BT_ADDR_LE_RANDOM };

		memcpy(own_addr.a.val, prov->own_addr, sizeof(own_addr.a.val));
		err = bt_id_create(&own_addr, NULL);
		if (err < 0) {
			return err;
		}
	}

	err = bt_enable(NULL);
	if (err) {
		return err;
	}

	bt_conn_cb_register(&conn_callbacks);
	/* Same belt-and-braces reasoning as dongle/src/radio_ble.c: no
	 * authentication capability registered, and not bondable either way. */
	(void)bt_conn_auth_cb_register(NULL);
	bt_set_bondable(false);

	err = bt_le_filter_accept_list_add(&dongle_addr);
	if (err) {
		return err;
	}

	k_work_init_delayable(&telemetry_work, telemetry_handler);

	start_advertising();
	return 0;
}
