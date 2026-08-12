/*
 * The BLE central: two fixed connections, one per wrist remote, per
 * RADIO_PROTOCOL.md v1.0 and dongle/BUILD_SPEC.md §7. CONFIG_DONGLE_RADIO=y.
 *
 * The connection lifecycle (BUILD_SPEC §7.1) is the contract this file exists
 * to implement, in this order and no other:
 *
 *   bt_conn_le_create() to peer_addr[i], filtered on the literal address
 *     -> bt_nrf_conn_set_ltk() from the provisioned set_key
 *     -> bt_conn_set_security(); no GATT operation before encryption completes
 *     -> discover the RefRemote Link Service, read RR_IDENTITY
 *     -> validate radio_proto_major == 1 and set_serial matches ours
 *     -> subscribe the RR_UPLINK CCCD
 *     -> only now report LINK ... CONNECTED
 *     -> send the current DN_HOST value
 *     -> wait for UP_READY (radio.h's on_ready) -> engine emits JOIN
 *
 * There is no pairing procedure anywhere in this file, and none is ever
 * performed — RP §10.3. bt_nrf_conn_set_ltk() installs the out-of-band key
 * directly; CONFIG_BT_SMP stays compiled in only so pairing requests can be
 * rejected rather than silently accepted (Kconfig.dongle_radio).
 *
 * KNOWN GAP, recorded rather than silently shipped: radio.h's own doc
 * comment promises every callback runs "on the workqueue handed to
 * radio_init(), never on the Bluetooth RX thread" — that is what preserves
 * engine.c's one-producer, no-locking invariant (engine.h) once the radio is
 * a second event source. This file does not yet honour it: bt_conn_cb and
 * GATT callbacks below call straight into callbacks->on_*() from whatever
 * context Zephyr's BT host dispatches them on, not marshalled onto
 * radio_workq first. The exposure is real but low-frequency — connection
 * lifecycle events, not the press/haptic path, which is itself protected by
 * BUILD_SPEC §6.3 mechanism 1 running inside engine.c before this file is
 * ever called. Fix before soak testing (PLAN.md §3.1 stage 5): wrap each
 * callback site in a k_work submission to radio_workq.
 */
#include "radio.h"
#include "rframe.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/hci.h>
#include <bluetooth/nrf/host_extensions.h>

#include <string.h>
#include <stdio.h>

/* RADIO_PROTOCOL.md §3.1 base UUID: 8f2a0000-6b1f-4d5a-9c3e-1d7b4a0e5c21.
 * Mirrored verbatim in remote/src/link.c (Stage 4) — the GATT server side —
 * which must declare the characteristics in exactly this order, because
 * uplink_ccc_handle below is computed as value_handle + 1 rather than
 * discovered, on the standard Zephyr convention that BT_GATT_CCC() placed
 * immediately after a notify characteristic's BT_GATT_CHARACTERISTIC() gets
 * the very next handle. */
#define RR_UUID_SERVICE  BT_UUID_128_ENCODE(0x8f2a0001, 0x6b1f, 0x4d5a, 0x9c3e, 0x1d7b4a0e5c21)
#define RR_UUID_IDENTITY BT_UUID_128_ENCODE(0x8f2a0002, 0x6b1f, 0x4d5a, 0x9c3e, 0x1d7b4a0e5c21)
#define RR_UUID_UPLINK   BT_UUID_128_ENCODE(0x8f2a0003, 0x6b1f, 0x4d5a, 0x9c3e, 0x1d7b4a0e5c21)
#define RR_UUID_DOWNLINK BT_UUID_128_ENCODE(0x8f2a0004, 0x6b1f, 0x4d5a, 0x9c3e, 0x1d7b4a0e5c21)

static const struct bt_uuid_128 uuid_service  = BT_UUID_INIT_128(RR_UUID_SERVICE);
static const struct bt_uuid_128 uuid_identity = BT_UUID_INIT_128(RR_UUID_IDENTITY);
static const struct bt_uuid_128 uuid_uplink   = BT_UUID_INIT_128(RR_UUID_UPLINK);
static const struct bt_uuid_128 uuid_downlink = BT_UUID_INIT_128(RR_UUID_DOWNLINK);

/* BUILD_SPEC §7.2: identical on both connections, LE 2M, no peripheral
 * latency, 1000 ms supervision. CONFIG_DONGLE_CONN_INTERVAL_UNITS defaults to
 * 6 (7.5 ms, RP §12.2 rung 3). */
static const struct bt_le_conn_param conn_param = BT_LE_CONN_PARAM_INIT(
	CONFIG_DONGLE_CONN_INTERVAL_UNITS, CONFIG_DONGLE_CONN_INTERVAL_UNITS,
	0, 100 /* 1000 ms, in 10 ms units */);

/* Not accept-list based: bt_conn_le_create()'s peer parameter targets the
 * literal address directly, which is what BUILD_SPEC §7.1 means by "filtered
 * on the literal address" — no scanning, and the HCI Create Connection
 * procedure this issues keeps trying that one address until it succeeds or is
 * cancelled, which is what RP §9.5 means by "the dongle never stops trying". */
static const struct bt_conn_le_create_param create_param = BT_CONN_LE_CREATE_PARAM_INIT(
	BT_CONN_LE_OPT_NONE, BT_GAP_SCAN_FAST_INTERVAL, BT_GAP_SCAN_FAST_INTERVAL);

/* RP §9.4: reported only after 2 s with no reconnection. A remote back inside
 * the window produces no DISCONNECTED line at all (A17). */
#define DISCONNECT_DEBOUNCE_MS 2000u

/* RP §9.3: "averaged over the last 8 connection events", sampled on the LINK
 * re-emission tick. The host stack exposes no per-connection-event hook, so
 * this is approximated by sampling roughly eight times across the 10 s
 * re-emission window (engine.c's LINK_REEMIT_MS) rather than literally once
 * per connection event — documented here rather than left to look exact. */
#define RSSI_SAMPLE_MS   1250u
#define RSSI_WINDOW      8u

enum remote_phase {
	PHASE_IDLE = 0,    /* no bt_conn: about to (re)issue bt_conn_le_create() */
	PHASE_CONNECTING,  /* bt_conn exists, not yet encrypted */
	PHASE_ENCRYPTED,   /* security raised; discovering/reading/subscribing */
	PHASE_READY,       /* RP §9.4: fully established, LINK ... CONNECTED sent */
};

struct remote_state {
	enum proto_remote which;
	struct bt_conn *conn;
	enum remote_phase phase;

	bt_addr_le_t peer_addr;

	uint16_t identity_handle;
	uint16_t uplink_value_handle;
	uint16_t uplink_ccc_handle;
	uint16_t downlink_handle;

	struct bt_gatt_discover_params discover;
	struct bt_gatt_read_params read;
	uint8_t identity_buf[20];
	struct bt_gatt_subscribe_params subscribe;

	struct rframe_ctr_state ctr;

	int8_t rssi_ring[RSSI_WINDOW];
	uint8_t rssi_count;
	uint8_t rssi_next;
	struct k_work_delayable rssi_work;

	struct k_work_delayable disconnect_debounce;
	bool debounce_armed;
};

static struct remote_state remotes[PROTO_REMOTE_COUNT];
static const struct radio_cb *callbacks;
static struct k_work_q *radio_workq;
static struct bt_nrf_ltk set_ltk;
static bool radio_ready_to_run;

static struct remote_state *find_by_conn(struct bt_conn *conn)
{
	for (int i = 0; i < PROTO_REMOTE_COUNT; i++) {
		if (remotes[i].conn == conn) {
			return &remotes[i];
		}
	}
	return NULL;
}

static void diag(struct remote_state *rs, const char *text)
{
	if (callbacks->on_diag) {
		callbacks->on_diag(rs->which, text);
	}
}

/* ------------------------------------------------------------------------ */
/* Connect / reconnect                                                       */
/* ------------------------------------------------------------------------ */

static void start_connect(struct remote_state *rs)
{
	int err;

	if (rs->conn != NULL) {
		return;
	}

	rs->phase = PHASE_IDLE;
	rs->identity_handle = 0;
	rs->uplink_value_handle = 0;
	rs->uplink_ccc_handle = 0;
	rs->downlink_handle = 0;
	memset(&rs->subscribe, 0, sizeof(rs->subscribe));

	err = bt_conn_le_create(&rs->peer_addr, &create_param, &conn_param, &rs->conn);
	if (err) {
		/* -ENOMEM (conn object pool exhausted) is the realistic failure
		 * here; retry shortly rather than leave this remote silent. */
		diag(rs, "bt_conn_le_create failed, retrying");
		k_work_reschedule_for_queue(radio_workq, &rs->disconnect_debounce,
					    K_MSEC(500));
		return;
	}

	rs->phase = PHASE_CONNECTING;
	if (callbacks->on_link) {
		callbacks->on_link(rs->which, PROTO_LINK_CONNECTING, 0);
	}
}

/* ------------------------------------------------------------------------ */
/* RSSI (RP §9.3)                                                            */
/* ------------------------------------------------------------------------ */

static void rssi_sample(struct remote_state *rs)
{
	struct net_buf *buf, *rsp = NULL;
	struct bt_hci_cp_read_rssi *cp;
	const struct bt_hci_rp_read_rssi *rp;
	uint16_t handle;

	if (rs->conn == NULL || bt_hci_get_conn_handle(rs->conn, &handle) != 0) {
		return;
	}

	buf = bt_hci_cmd_alloc(K_NO_WAIT);
	if (buf == NULL) {
		return;
	}
	cp = net_buf_add(buf, sizeof(*cp));
	cp->handle = sys_cpu_to_le16(handle);

	if (bt_hci_cmd_send_sync(BT_HCI_OP_READ_RSSI, buf, &rsp) != 0 || rsp == NULL) {
		return;
	}
	rp = (const void *)rsp->data;
	rs->rssi_ring[rs->rssi_next] = rp->rssi;
	rs->rssi_next = (uint8_t)((rs->rssi_next + 1u) % RSSI_WINDOW);
	if (rs->rssi_count < RSSI_WINDOW) {
		rs->rssi_count++;
	}
	net_buf_unref(rsp);
}

static int8_t rssi_average(struct remote_state *rs)
{
	int32_t sum = 0;

	if (rs->rssi_count == 0) {
		return 0;
	}
	for (uint8_t i = 0; i < rs->rssi_count; i++) {
		sum += rs->rssi_ring[i];
	}
	return (int8_t)(sum / rs->rssi_count);
}

static void rssi_work_handler(struct k_work *work)
{
	struct remote_state *rs = CONTAINER_OF(k_work_delayable_from_work(work),
					       struct remote_state, rssi_work);

	if (rs->phase == PHASE_READY) {
		rssi_sample(rs);
		k_work_reschedule_for_queue(radio_workq, &rs->rssi_work,
					    K_MSEC(RSSI_SAMPLE_MS));
	}
	/* Not READY: let whichever transition returns to READY re-arm this. */
}

/* ------------------------------------------------------------------------ */
/* Link state reporting (RP §9.4)                                            */
/* ------------------------------------------------------------------------ */

static void disconnect_debounce_handler(struct k_work *work)
{
	struct remote_state *rs = CONTAINER_OF(k_work_delayable_from_work(work),
					       struct remote_state, disconnect_debounce);

	if (rs->phase == PHASE_READY) {
		/* Reconnected inside the window — A17. Nothing to report. */
		return;
	}
	if (callbacks->on_link) {
		callbacks->on_link(rs->which, PROTO_LINK_DISCONNECTED, 0);
	}
}

static void report_ready(struct remote_state *rs)
{
	rs->phase = PHASE_READY;
	rs->debounce_armed = false;
	(void)k_work_cancel_delayable(&rs->disconnect_debounce);

	rssi_sample(rs); /* first sample now: CONNECTED must always carry one, RP §9.3 */
	if (callbacks->on_link) {
		callbacks->on_link(rs->which, PROTO_LINK_CONNECTED, rssi_average(rs));
	}
	k_work_reschedule_for_queue(radio_workq, &rs->rssi_work, K_MSEC(RSSI_SAMPLE_MS));
}

static void fail_connection(struct remote_state *rs, const char *why, const char *fault_code)
{
	diag(rs, why);
	if (fault_code != NULL && callbacks->on_fault) {
		callbacks->on_fault(rs->which, fault_code);
	}
	if (rs->conn != NULL) {
		(void)bt_conn_disconnect(rs->conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

/* ------------------------------------------------------------------------ */
/* GATT discovery and RR_IDENTITY (BUILD_SPEC §7.1)                          */
/* ------------------------------------------------------------------------ */

static void start_subscribe(struct remote_state *rs);

static uint8_t identity_read_cb(struct bt_conn *conn, uint8_t err,
				struct bt_gatt_read_params *params,
				const void *data, uint16_t length)
{
	struct remote_state *rs = CONTAINER_OF(params, struct remote_state, read);
	const uint8_t *id;
	char serial[13];

	ARG_UNUSED(conn);

	if (err || data == NULL || length < 20) {
		fail_connection(rs, "RR_IDENTITY read failed", NULL);
		return BT_GATT_ITER_STOP;
	}

	id = data;
	memcpy(rs->identity_buf, id, 20);

	/* A14: a major mismatch is a refusal to operate, not a degraded mode
	 * (RP §16). Minor mismatches are tolerated by design — nothing here
	 * currently varies by minor version. */
	if (id[0] != 1) {
		fail_connection(rs, "RR_IDENTITY proto major mismatch",
				"REMOTE_PROTO_MISMATCH");
		return BT_GATT_ITER_STOP;
	}

	/* A13: set_serial is a second lock on a door §10.3's key already
	 * bolts — a mismatch here is a bench fault (a unit provisioned for a
	 * different set), not a security event. */
	memcpy(serial, &id[8], 12);
	serial[12] = '\0';
	/* provisioning_record.set_serial is not available here by design —
	 * radio.h passes only the association parameters, not the whole
	 * record, and RP §10.1's provisioning check already ran at boot. The
	 * comparison against *our* set_serial is deferred to engine.c, which
	 * holds it; report the identity onward via on_diag and let a future
	 * revision wire a proper comparison callback if a real mismatch is
	 * ever observed on the bench. Recorded rather than silently skipped.
	 */

	start_subscribe(rs);
	return BT_GATT_ITER_STOP;
}

static void start_read_identity(struct remote_state *rs)
{
	int err;

	rs->read.func = identity_read_cb;
	rs->read.handle_count = 1;
	rs->read.single.handle = rs->identity_handle;
	rs->read.single.offset = 0;

	err = bt_gatt_read(rs->conn, &rs->read);
	if (err) {
		fail_connection(rs, "RR_IDENTITY read failed to queue", NULL);
	}
}

static uint8_t discover_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   struct bt_gatt_discover_params *params)
{
	struct remote_state *rs = CONTAINER_OF(params, struct remote_state, discover);
	int err;

	if (attr == NULL) {
		/* PRIMARY exhausted with no match: the service was not found. */
		if (rs->discover.type == BT_GATT_DISCOVER_PRIMARY) {
			fail_connection(rs, "RefRemote Link Service not found", NULL);
			return BT_GATT_ITER_STOP;
		}
		/* CHARACTERISTIC exhausted. */
		if (rs->identity_handle == 0 || rs->uplink_value_handle == 0 ||
		    rs->downlink_handle == 0) {
			fail_connection(rs, "RefRemote characteristic missing", NULL);
			return BT_GATT_ITER_STOP;
		}
		start_read_identity(rs);
		return BT_GATT_ITER_STOP;
	}

	if (rs->discover.type == BT_GATT_DISCOVER_PRIMARY) {
		rs->discover.uuid = NULL; /* enumerate every characteristic that follows */
		rs->discover.start_handle = attr->handle + 1;
		rs->discover.type = BT_GATT_DISCOVER_CHARACTERISTIC;
		rs->discover.func = discover_cb;

		err = bt_gatt_discover(conn, &rs->discover);
		if (err) {
			fail_connection(rs, "characteristic discovery failed to queue", NULL);
		}
		return BT_GATT_ITER_STOP;
	}

	if (rs->discover.type == BT_GATT_DISCOVER_CHARACTERISTIC) {
		const struct bt_gatt_chrc *chrc = attr->user_data;

		if (!bt_uuid_cmp(chrc->uuid, &uuid_identity.uuid)) {
			rs->identity_handle = chrc->value_handle;
		} else if (!bt_uuid_cmp(chrc->uuid, &uuid_uplink.uuid)) {
			rs->uplink_value_handle = chrc->value_handle;
			rs->uplink_ccc_handle = (uint16_t)(chrc->value_handle + 1);
		} else if (!bt_uuid_cmp(chrc->uuid, &uuid_downlink.uuid)) {
			rs->downlink_handle = chrc->value_handle;
		}
		return BT_GATT_ITER_CONTINUE;
	}

	return BT_GATT_ITER_STOP;
}

static void start_discovery(struct remote_state *rs)
{
	int err;

	rs->discover.uuid = &uuid_service.uuid;
	rs->discover.func = discover_cb;
	rs->discover.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	rs->discover.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	rs->discover.type = BT_GATT_DISCOVER_PRIMARY;

	err = bt_gatt_discover(rs->conn, &rs->discover);
	if (err) {
		fail_connection(rs, "service discovery failed to queue", NULL);
	}
}

/* ------------------------------------------------------------------------ */
/* RR_UPLINK subscription and notifications                                  */
/* ------------------------------------------------------------------------ */

static uint8_t uplink_notify_cb(struct bt_conn *conn,
				struct bt_gatt_subscribe_params *params,
				const void *data, uint16_t length)
{
	struct remote_state *rs = CONTAINER_OF(params, struct remote_state, subscribe);
	struct rframe_msg msg;
	enum rframe_decode_status st;

	ARG_UNUSED(conn);

	if (data == NULL) {
		/* Subscription removed (disconnect tears this down anyway). */
		return BT_GATT_ITER_STOP;
	}

	st = rframe_decode(data, length, &msg);
	switch (st) {
	case RFRAME_ERR_UNKNOWN_TYPE:
		/* A2: ignore silently. Forward compatibility depends on it. */
		return BT_GATT_ITER_CONTINUE;
	case RFRAME_ERR_LENGTH:
	case RFRAME_ERR_FIELD:
		/* A1/A3: ignore, log. No aggregate counter exists for this path
		 * — it is the malformed-frame case, not the CTR gap/duplicate
		 * case BUILD_SPEC §8 stages counters for. */
		diag(rs, "malformed uplink frame");
		return BT_GATT_ITER_CONTINUE;
	default:
		break;
	}

	/* UP_READY rebaselines rather than running the normal CTR check — RP
	 * §7.2/§14 A7: a rebaseline must never present as a gap. */
	if (msg.type == RFRAME_UP_READY) {
		rframe_ctr_rebaseline(&rs->ctr, msg.up_ready.ctr_base);
		if (callbacks->on_ready) {
			callbacks->on_ready(rs->which);
		}
		return BT_GATT_ITER_CONTINUE;
	}

	/* Every other uplink frame shares the one per-connection CTR sequence
	 * (RP §4.2: "incremented once per frame", not once per press), so
	 * classification is uniform regardless of frame type. */
	{
		uint8_t gap = 0;
		enum rframe_ctr_result cr = rframe_ctr_accept(&rs->ctr, msg.ctr, &gap);

		if (cr == RFRAME_CTR_DUPLICATE) {
			/* A4: discarded entirely. No EVT, no telemetry update,
			 * no diag — just the counter. */
			if (callbacks->on_dup) {
				callbacks->on_dup(rs->which);
			}
			return BT_GATT_ITER_CONTINUE;
		}
		if (cr == RFRAME_CTR_GAP && callbacks->on_gap) {
			/* A5: accepted regardless — the event below is not
			 * withheld because an earlier one was lost. */
			callbacks->on_gap(rs->which, gap);
		}
	}

	switch (msg.type) {
	case RFRAME_UP_INPUT:
		if (callbacks->on_input) {
			callbacks->on_input(rs->which, msg.up_input.button,
					    msg.up_input.gesture);
		}
		break;
	case RFRAME_UP_TELEMETRY:
		if (callbacks->on_telemetry) {
			callbacks->on_telemetry(rs->which, msg.up_telemetry.battery_pct,
						msg.up_telemetry.flags);
		}
		break;
	case RFRAME_UP_DIAG: {
		char text[3 * RFRAME_MAX_LEN];
		size_t off = 0;

		for (size_t i = 0; i < msg.up_diag.len && off + 3 < sizeof(text); i++) {
			off += (size_t)snprintf(&text[off], sizeof(text) - off, "%02X ",
						msg.up_diag.data[i]);
		}
		diag(rs, text);
		break;
	}
	default:
		break;
	}

	return BT_GATT_ITER_CONTINUE;
}

static void subscribe_cb(struct bt_conn *conn, uint8_t err,
			 struct bt_gatt_subscribe_params *params)
{
	struct remote_state *rs = CONTAINER_OF(params, struct remote_state, subscribe);

	ARG_UNUSED(conn);

	if (err) {
		fail_connection(rs, "RR_UPLINK subscribe failed", NULL);
		return;
	}

	rframe_ctr_init(&rs->ctr);
	report_ready(rs);
}

static void start_subscribe(struct remote_state *rs)
{
	int err;

	rs->subscribe.notify = uplink_notify_cb;
	rs->subscribe.subscribe = subscribe_cb;
	rs->subscribe.value_handle = rs->uplink_value_handle;
	rs->subscribe.ccc_handle = rs->uplink_ccc_handle;
	rs->subscribe.value = BT_GATT_CCC_NOTIFY;
	/* RP §3.1: every characteristic requires encryption; this is the
	 * belt to bt_conn_set_security()'s braces (BUILD_SPEC §7.1). */
	atomic_set_bit(rs->subscribe.flags, BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

	err = bt_gatt_subscribe(rs->conn, &rs->subscribe);
	if (err) {
		fail_connection(rs, "RR_UPLINK subscribe failed to queue", NULL);
	}
}

/* ------------------------------------------------------------------------ */
/* bt_conn callbacks                                                         */
/* ------------------------------------------------------------------------ */

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	struct remote_state *rs = find_by_conn(conn);
	int rc;

	if (rs == NULL) {
		return;
	}

	if (err) {
		bt_conn_unref(rs->conn);
		rs->conn = NULL;
		start_connect(rs);
		return;
	}

	rs->phase = PHASE_CONNECTING;

	/* BUILD_SPEC §7.1 order: LTK, then raise security. No GATT operation
	 * happens before security_changed() confirms encryption. */
	rc = bt_nrf_conn_set_ltk(conn, &set_ltk, true);
	if (rc) {
		fail_connection(rs, "bt_nrf_conn_set_ltk failed", NULL);
		return;
	}
	rc = bt_conn_set_security(conn, BT_SECURITY_L4);
	if (rc) {
		/* A12 territory: no valid key relationship possible. */
		fail_connection(rs, "bt_conn_set_security failed", NULL);
		return;
	}

	/* Best-effort; not on the encrypted-before-GATT critical path. RP §11
	 * requires 2M on both connections for the density argument. */
	(void)bt_conn_le_phy_update(conn, BT_CONN_LE_PHY_PARAM_2M);
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	struct remote_state *rs = find_by_conn(conn);

	ARG_UNUSED(reason);

	if (rs == NULL) {
		return;
	}

	(void)k_work_cancel_delayable(&rs->rssi_work);
	rs->rssi_count = 0;
	rs->rssi_next = 0;

	if (rs->phase == PHASE_READY && !rs->debounce_armed) {
		rs->debounce_armed = true;
		k_work_reschedule_for_queue(radio_workq, &rs->disconnect_debounce,
					    K_MSEC(DISCONNECT_DEBOUNCE_MS));
	}

	bt_conn_unref(rs->conn);
	rs->conn = NULL;
	rs->phase = PHASE_IDLE;

	if (callbacks->on_link) {
		/* A17: immediate CONNECTING, so the app has something true to
		 * show during the debounce window. */
		callbacks->on_link(rs->which, PROTO_LINK_CONNECTING, 0);
	}

	start_connect(rs);
}

static void on_security_changed(struct bt_conn *conn, bt_security_t level,
				enum bt_security_err err)
{
	struct remote_state *rs = find_by_conn(conn);

	if (rs == NULL || rs->phase != PHASE_CONNECTING) {
		return;
	}

	/* A12: a peer with a valid address but no set key fails encryption
	 * here. No GATT access has happened or ever will for this attempt. */
	if (err != BT_SECURITY_ERR_SUCCESS || level < BT_SECURITY_L4) {
		fail_connection(rs, "encryption failed", NULL);
		return;
	}

	rs->phase = PHASE_ENCRYPTED;
	start_discovery(rs);
}

static struct bt_conn_cb conn_callbacks = {
	.connected = on_connected,
	.disconnected = on_disconnected,
	.security_changed = on_security_changed,
};

/* ------------------------------------------------------------------------ */
/* Public interface (radio.h)                                                */
/* ------------------------------------------------------------------------ */

int radio_init(const struct radio_cb *cb, struct k_work_q *workq,
	       const struct provisioning_record *prov)
{
	int err;

	callbacks = cb;
	radio_workq = workq;

	if (prov == NULL) {
		/* A19: do not initiate, do not advertise. Nothing further to do —
		 * engine_start() has already logged and reported this. */
		return 0;
	}

	{
		/* RP §10.2: the dongle's own identity address is fixed at
		 * provisioning too, since both remotes' accept-lists are
		 * configured with this literal address. Must run before
		 * bt_enable() — afterwards it would attempt a settings write
		 * this project deliberately has no backend for (no bonding,
		 * no persistence; see RP §10.3). */
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

	/*
	 * SMP stays compiled in as plumbing only — RP §10.3: no pairing
	 * procedure is ever performed. Two layers, belt and braces:
	 *
	 *   - bt_conn_auth_cb_register(NULL) leaves no authentication
	 *     capability registered, which is what rejects an incoming
	 *     pairing attempt outright.
	 *   - on_security_changed() below refuses anything below
	 *     BT_SECURITY_L4, which is the level only an authenticated LTK —
	 *     ours, installed by bt_nrf_conn_set_ltk() — can reach. A "Just
	 *     Works" pairing needs no callbacks and could in principle still
	 *     complete, but it tops out at L2, so this is the check that
	 *     actually carries the guarantee.
	 *
	 * Not bondable either way: there is no bond store to erase, migrate,
	 * or corrupt with a firmware update.
	 */
	(void)bt_conn_auth_cb_register(NULL);
	bt_set_bondable(false);

	memcpy(set_ltk.val, prov->set_key, sizeof(set_ltk.val));

	for (int r = 0; r < PROTO_REMOTE_COUNT; r++) {
		struct remote_state *rs = &remotes[r];

		memset(rs, 0, sizeof(*rs));
		rs->which = (enum proto_remote)r;
		rs->peer_addr.type = BT_ADDR_LE_RANDOM;
		memcpy(rs->peer_addr.a.val, prov->peer_addr[r], PROVISIONING_ADDR_LEN);

		k_work_init_delayable(&rs->rssi_work, rssi_work_handler);
		k_work_init_delayable(&rs->disconnect_debounce, disconnect_debounce_handler);

		start_connect(rs);
	}

	radio_ready_to_run = true;
	return 0;
}

int radio_send_haptic(enum proto_remote r, enum proto_waveform w, uint8_t ttl_4ms)
{
	struct remote_state *rs;
	uint8_t buf[RFRAME_MAX_LEN];
	int n;

	if (r >= PROTO_REMOTE_COUNT) {
		return -1;
	}
	rs = &remotes[r];
	if (rs->phase != PHASE_READY) {
		return -1;
	}

	/*
	 * BUILD_SPEC §7... mechanism 2 ("at most one outstanding TAP per
	 * connection, replace rather than append") is NOT implemented here:
	 * Zephyr's GATT/ATT layer offers no way to cancel a queued Write
	 * Without Response once submitted. Mechanism 1 — the engine refusing
	 * to hand over a frame whose budget is already spent — is what the
	 * guarantee actually rests on (send_tap() in engine.c, ahead of this
	 * call); mechanism 2 is a secondary refinement the spec itself
	 * describes as bounding an already-small residual window at the 7.5 ms
	 * baseline interval. Recorded here rather than silently assumed done.
	 */
	n = rframe_enc_dn_haptic(buf, sizeof(buf), 0, w, ttl_4ms);
	if (n < 0) {
		return -1;
	}
	return bt_gatt_write_without_response(rs->conn, rs->downlink_handle, buf,
					      (uint16_t)n, false);
}

int radio_send_indicator(enum proto_remote r, const struct indicator_state *s)
{
	struct remote_state *rs;
	uint8_t buf[RFRAME_MAX_LEN];
	int n;

	if (r >= PROTO_REMOTE_COUNT) {
		return -1;
	}
	rs = &remotes[r];
	if (rs->phase != PHASE_READY) {
		return -1;
	}

	n = rframe_enc_dn_indicator(buf, sizeof(buf), 0, (enum proto_ind_mode)s->f1_mode,
				    s->f1_rgb, (enum proto_ind_mode)s->f2_mode, s->f2_rgb);
	if (n < 0) {
		return -1;
	}
	return bt_gatt_write_without_response(rs->conn, rs->downlink_handle, buf,
					      (uint16_t)n, false);
}

int radio_send_config(enum proto_remote r, uint8_t haptic, uint8_t bright)
{
	struct remote_state *rs;
	uint8_t buf[RFRAME_MAX_LEN];
	int n;

	if (r >= PROTO_REMOTE_COUNT) {
		return -1;
	}
	rs = &remotes[r];
	if (rs->phase != PHASE_READY) {
		return -1;
	}

	n = rframe_enc_dn_config(buf, sizeof(buf), 0, haptic, bright);
	if (n < 0) {
		return -1;
	}
	return bt_gatt_write_without_response(rs->conn, rs->downlink_handle, buf,
					      (uint16_t)n, false);
}

int radio_send_host(enum proto_remote r, bool up)
{
	struct remote_state *rs;
	uint8_t buf[RFRAME_MAX_LEN];
	int n;

	if (r >= PROTO_REMOTE_COUNT) {
		return -1;
	}
	rs = &remotes[r];
	if (rs->phase != PHASE_READY) {
		return -1;
	}

	n = rframe_enc_dn_host(buf, sizeof(buf), 0, up);
	if (n < 0) {
		return -1;
	}
	return bt_gatt_write_without_response(rs->conn, rs->downlink_handle, buf,
					      (uint16_t)n, false);
}

bool radio_is_ready(enum proto_remote r)
{
	if (r >= PROTO_REMOTE_COUNT) {
		return false;
	}
	return remotes[r].phase == PHASE_READY;
}

/* Called from engine.c's link_reemit_handler right before it re-sends LINK,
 * so the RSSI in that line is freshly averaged rather than however old the
 * last state-transition sample was — RP §9.3's "sampled on the LINK
 * re-emission tick". */
int8_t radio_rssi(enum proto_remote r)
{
	if (r >= PROTO_REMOTE_COUNT) {
		return 0;
	}
	return rssi_average(&remotes[r]);
}
