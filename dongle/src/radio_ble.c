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
 * THE SHARED INITIATING SLOT: Zephyr's host allows exactly one outstanding
 * bt_conn_le_create() system-wide — conn_le_create_common_checks() rejects a
 * second call with -EALREADY regardless of which peer address it targets
 * (zephyr/subsys/bluetooth/host/conn.c). Two remotes both wanting to connect
 * at once (boot, or both dropping together) therefore cannot both search
 * concurrently, and a search against an address that never advertises would
 * otherwise hold the slot forever (RP §9.5 "the dongle never stops trying"),
 * permanently starving the other remote even if it is present and
 * advertising correctly. connect_owner/connect_yield below share that one
 * slot fairly: only the remote actually running un-contested (the other one
 * already connected, or not yet wanting a turn) gets to search unbounded, as
 * RP §9.5 always intended; two remotes contending for the slot at once get
 * bounded turns instead, handed off via bt_conn_disconnect() on the
 * not-yet-connected bt_conn — which cancels the pending HCI Create
 * Connection rather than tearing down a live link, so it never touches the
 * OTHER remote's own (possibly already-established) connection. See
 * start_connect(), connect_yield_handler(), advance_connect().
 *
 * THREADING: every bt_conn_cb and every GATT client callback below runs on
 * whatever thread Zephyr's BT host dispatches it on, never on radio_workq —
 * and radio.h's own doc comment promises callbacks reach the application "on
 * the workqueue handed to radio_init(), never on the Bluetooth RX thread",
 * because that is what preserves engine.c's one-producer, no-locking
 * invariant (engine.h) once the radio is a second event source. None of the
 * callbacks below touch struct remote_state, or call callbacks->on_*(),
 * directly: each copies out the small, short-lived data it was handed into a
 * struct bt_evt, pushes it onto bt_evtq, and submits the single static
 * bt_evt_work to radio_workq. bt_evt_work drains the queue and does the
 * actual state mutation and callbacks->on_*() calls there — the same
 * "copy in the callback, do the work on the queue" split usb_link.c already
 * uses for the USB RX path (its rx_rb + rx_work), so radio_workq ends up
 * exactly as single-producer as engine_q already assumes.
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
 * cancelled. Per-remote this still runs unbounded, matching RP §9.5's "the
 * dongle never stops trying" — but only one such search may be outstanding
 * system-wide (see the header comment), so a search is only ever cancelled
 * to hand the shared slot to a remote that is also waiting, never to give up
 * on it. */
static const struct bt_conn_le_create_param create_param = BT_CONN_LE_CREATE_PARAM_INIT(
	BT_CONN_LE_OPT_NONE, BT_GAP_SCAN_FAST_INTERVAL, BT_GAP_SCAN_FAST_INTERVAL);

/* RP §9.4: reported only after 2 s with no reconnection. A remote back inside
 * the window produces no DISCONNECTED line at all (A17). */
#define DISCONNECT_DEBOUNCE_MS 2000u

/* How long a remote may hold the shared initiating slot (header comment)
 * while the other remote is also waiting for a turn, before yielding it.
 * Generous relative to the remote's own 100 ms undirected advertising
 * interval — a present, correctly-advertising remote should connect well
 * inside this window most turns — while still short enough that a genuinely
 * absent remote's turn doesn't leave the present one waiting long. */
#define CONNECT_YIELD_MS 4000u

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

	/* Rare path only: bt_conn_le_create() itself failing synchronously
	 * (e.g. real -ENOMEM). Deliberately separate from disconnect_debounce
	 * above, which start_connect() used to (mis)use for this — that timer
	 * only ever reports DISCONNECTED (disconnect_debounce_handler), it
	 * never re-attempts the connection, so the retry it promised never
	 * actually happened. */
	struct k_work_delayable retry_work;
};

static struct remote_state remotes[PROTO_REMOTE_COUNT];
static const struct radio_cb *callbacks;
static struct k_work_q *radio_workq;
static struct bt_nrf_ltk set_ltk;
static bool radio_ready_to_run;

/* A13 (RADIO_PROTOCOL.md §3.2, §10.2): the dongle's own set_serial, held here
 * so handle_identity_read() has something to compare RR_IDENTITY against.
 * Copied from prov->set_serial in radio_init() — radio.h passes the whole
 * record there, not just the association parameters, precisely so this
 * comparison is possible. Wire-format width (12 bytes, NUL-padded within
 * them), not the +1 caller convenience byte of struct provisioning_record. */
static uint8_t own_set_serial[PROVISIONING_SERIAL_LEN];

/* The shared initiating slot (header comment): the remote currently holding
 * the one system-wide outstanding bt_conn_le_create(), or NULL if neither is
 * searching right now. Only ever written on radio_workq. */
static struct remote_state *connect_owner;
static struct k_work_delayable connect_yield;

static struct remote_state *find_by_conn(struct bt_conn *conn)
{
	for (int i = 0; i < PROTO_REMOTE_COUNT; i++) {
		if (remotes[i].conn == conn) {
			return &remotes[i];
		}
	}
	return NULL;
}

/* PROTO_REMOTE_COUNT is fixed at 2 (protocol.h) — the whole provisioning
 * record shape (PROVISIONING_PEER_COUNT) already assumes exactly two, so a
 * direct RED<->GREEN swap is as general as this file needs to be. */
static struct remote_state *other_remote(const struct remote_state *rs)
{
	return &remotes[(rs->which == PROTO_REMOTE_RED) ? PROTO_REMOTE_GREEN
							 : PROTO_REMOTE_RED];
}

static void diag(struct remote_state *rs, const char *text)
{
	if (callbacks->on_diag) {
		callbacks->on_diag(rs->which, text);
	}
}

/* ------------------------------------------------------------------------ */
/* BT-thread -> radio_workq event marshaling                                 */
/*                                                                            */
/* Sized for 8 pending events: generous relative to how these actually
 * arrive. The connection-lifecycle ones are inherently one-in-flight-at-a-
 * time per remote — each step only produces its callback after the previous
 * one's handler (below) issued the next request — and even a receive burst
 * on RR_UPLINK's notify path is bounded by the controller's own connection
 * event cadence at 7.5 ms. A full queue drops the event and counts it rather
 * than block the BT host thread — fail closed, CLAUDE.md §4.4.
 *
 * BT_EVT_CONNECTED/DISCONNECTED/SECURITY_CHANGED carry a ref'd conn rather
 * than a resolved remote_state *: resolving which remote a bt_conn belongs to
 * means comparing against remotes[i].conn, and that field is now written
 * exclusively on radio_workq, so the comparison itself has to happen there
 * too rather than back on the BT thread. The GATT client callbacks
 * (discover/read/subscribe) skip that lookup entirely — their params structs
 * are embedded fields of struct remote_state, so CONTAINER_OF gives the
 * correct remote_state * directly, and that pointer is static-array memory,
 * always valid to hand across threads by value.
 */
enum bt_evt_type {
	BT_EVT_CONNECTED,
	BT_EVT_DISCONNECTED,
	BT_EVT_SECURITY_CHANGED,
	BT_EVT_DISCOVER,
	BT_EVT_IDENTITY_READ,
	BT_EVT_SUBSCRIBED,
	BT_EVT_NOTIFY,
};

struct bt_evt {
	enum bt_evt_type type;
	struct bt_conn *conn;     /* ref'd by the shim, unref'd once the dispatcher
				   * has run the handler; NULL for the
				   * CONTAINER_OF-resolved events below */
	struct remote_state *rs;  /* set instead of conn for discover/identity/
				   * subscribe/notify events */
	union {
		struct {
			uint8_t err;
		} connected;
		struct {
			bt_security_t level;
			enum bt_security_err err;
		} security;
		struct {
			enum bt_gatt_discover_type phase;
			bool found;
			uint16_t handle;
			int which; /* CHARACTERISTIC phase only: 0=unmatched,
				    * 1=identity, 2=uplink, 3=downlink — the
				    * UUID compare itself runs synchronously in
				    * discover_cb against const tables, so only
				    * the result needs to cross threads */
		} discover;
		struct {
			uint8_t err;
			uint8_t data[20];
			uint16_t length;
		} identity;
		struct {
			uint8_t err;
		} subscribed;
		struct {
			uint8_t data[RFRAME_MAX_LEN];
			uint16_t length;
		} notify;
	};
};

K_MSGQ_DEFINE(bt_evtq, sizeof(struct bt_evt), 8, 4);
static struct k_work bt_evt_work;
static uint32_t bt_evt_drops;

static void bt_evt_post(const struct bt_evt *evt)
{
	if (k_msgq_put(&bt_evtq, evt, K_NO_WAIT) != 0) {
		bt_evt_drops++;
		if (evt->conn != NULL) {
			bt_conn_unref(evt->conn);
		}
		return;
	}
	(void)k_work_submit_to_queue(radio_workq, &bt_evt_work);
}

/* ------------------------------------------------------------------------ */
/* Connect / reconnect                                                       */
/* ------------------------------------------------------------------------ */

static void start_connect(struct remote_state *rs);

static void retry_work_handler(struct k_work *work)
{
	struct remote_state *rs = CONTAINER_OF(k_work_delayable_from_work(work),
					       struct remote_state, retry_work);

	start_connect(rs);
}

/* Entry point for "this remote wants a connection" from every caller:
 * radio_init()'s boot loop, handle_disconnected(), advance_connect(). Safe to
 * call on a remote that already has one (no-op) or one that cannot have the
 * shared initiating slot right now (no-op, waits for advance_connect() to
 * hand it over — see the header comment). */
static void start_connect(struct remote_state *rs)
{
	int err;

	if (rs->conn != NULL) {
		return;
	}

	if (connect_owner != NULL && connect_owner != rs) {
		/* The other remote is currently the one system-wide initiating
		 * attempt is allowed to belong to. Nothing to do here — its
		 * own connected callback (success or the cancelled-by-yield
		 * failure) or its yield timer is what calls advance_connect()
		 * and gets back to this remote. Calling bt_conn_le_create()
		 * here would just fail with -EALREADY. */
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
		/* Real resource failure (e.g. -ENOMEM) — the contention case
		 * above is what -EALREADY meant before this file tracked
		 * connect_owner itself, and that case now returns early
		 * without ever reaching bt_conn_le_create(). */
		diag(rs, "bt_conn_le_create failed, retrying");
		k_work_reschedule_for_queue(radio_workq, &rs->retry_work, K_MSEC(500));
		return;
	}

	connect_owner = rs;
	rs->phase = PHASE_CONNECTING;
	if (callbacks->on_link) {
		callbacks->on_link(rs->which, PROTO_LINK_CONNECTING, 0);
	}

	/* Always arm, even when the other remote isn't currently waiting: it
	 * might start waiting (mid-match disconnect) before this search
	 * resolves, and connect_yield_handler() re-checks contention fresh
	 * every time it fires rather than only at issue time, so an
	 * uncontested search just gets re-armed and keeps running unbounded
	 * (RP §9.5) instead of ever being cancelled. */
	k_work_reschedule_for_queue(radio_workq, &connect_yield, K_MSEC(CONNECT_YIELD_MS));
}

/* Fires periodically while connect_owner is mid-search. If the other remote
 * is still waiting for a turn, cancels this attempt so it gets one; if not,
 * just re-arms and keeps checking — the search itself is untouched and keeps
 * running unbounded, RP §9.5. */
static void connect_yield_handler(struct k_work *work)
{
	struct remote_state *owner = connect_owner;

	ARG_UNUSED(work);

	if (owner == NULL || owner->phase != PHASE_CONNECTING) {
		return; /* Already resolved by the time this fired. */
	}

	if (other_remote(owner)->conn != NULL) {
		/* Uncontested for now — keep searching, keep checking. */
		k_work_reschedule_for_queue(radio_workq, &connect_yield,
					    K_MSEC(CONNECT_YIELD_MS));
		return;
	}

	diag(owner, "yielding initiating slot for the other remote's turn");

	/* owner->conn is still BT_CONN_INITIATING (never reached CONNECTED),
	 * so this issues HCI LE Create Connection Cancel rather than tearing
	 * down a live link (zephyr/subsys/bluetooth/host/conn.c,
	 * bt_conn_disconnect()'s BT_CONN_INITIATING case) — there is nothing
	 * live to tear down yet, and the OTHER remote's connection (if it has
	 * one) is a completely separate bt_conn untouched by this call.
	 * handle_connected()'s err path is what actually clears connect_owner
	 * and calls advance_connect() once the cancel completes — not here,
	 * to keep exactly one place responsible for tearing down rs->conn. */
	(void)bt_conn_disconnect(owner->conn, BT_HCI_ERR_LOCALHOST_TERM_CONN);
}

/* Called on radio_workq whenever a connection attempt for just_ended has
 * resolved — connected, failed, or was cancelled to yield — freeing (or not
 * needing) the shared initiating slot. Hands it to the other remote first if
 * that remote is still waiting (round-robin under contention); either way
 * also retries just_ended itself, which start_connect()'s rs->conn/
 * connect_owner guards make a safe no-op when it isn't the one that should
 * go next. */
static void advance_connect(struct remote_state *just_ended)
{
	start_connect(other_remote(just_ended));
	start_connect(just_ended);
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

static void start_read_identity(struct remote_state *rs)
{
	int err;

	rs->read.handle_count = 1;
	rs->read.single.handle = rs->identity_handle;
	rs->read.single.offset = 0;

	err = bt_gatt_read(rs->conn, &rs->read);
	if (err) {
		fail_connection(rs, "RR_IDENTITY read failed to queue", NULL);
	}
}

static void start_discovery(struct remote_state *rs)
{
	int err;

	rs->discover.uuid = &uuid_service.uuid;
	rs->discover.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	rs->discover.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	rs->discover.type = BT_GATT_DISCOVER_PRIMARY;

	err = bt_gatt_discover(rs->conn, &rs->discover);
	if (err) {
		fail_connection(rs, "service discovery failed to queue", NULL);
	}
}

/* Everything discover_cb/identity_read_cb/subscribe_cb/uplink_notify_cb used
 * to do inline now lives here, run only from bt_evt_work on radio_workq. */

static void handle_discover(struct remote_state *rs, enum bt_gatt_discover_type phase,
			    bool found, uint16_t handle, int which)
{
	int err;

	if (!found) {
		if (phase == BT_GATT_DISCOVER_PRIMARY) {
			fail_connection(rs, "RefRemote Link Service not found", NULL);
			return;
		}
		/* CHARACTERISTIC exhausted. */
		if (rs->identity_handle == 0 || rs->uplink_value_handle == 0 ||
		    rs->downlink_handle == 0) {
			fail_connection(rs, "RefRemote characteristic missing", NULL);
			return;
		}
		start_read_identity(rs);
		return;
	}

	if (phase == BT_GATT_DISCOVER_PRIMARY) {
		rs->discover.uuid = NULL; /* enumerate every characteristic that follows */
		rs->discover.start_handle = (uint16_t)(handle + 1);
		rs->discover.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(rs->conn, &rs->discover);
		if (err) {
			fail_connection(rs, "characteristic discovery failed to queue", NULL);
		}
		return;
	}

	/* CHARACTERISTIC: record a match, then step past it and keep looking.
	 * discover_cb always returns STOP (see its own comment), so each hop
	 * is a fresh bt_gatt_discover() issued from here rather than Zephyr's
	 * own CONTINUE-driven sweep — that is what lets the whole chain run
	 * on radio_workq instead of the BT host thread. */
	switch (which) {
	case 1:
		rs->identity_handle = handle;
		break;
	case 2:
		rs->uplink_value_handle = handle;
		rs->uplink_ccc_handle = (uint16_t)(handle + 1);
		break;
	case 3:
		rs->downlink_handle = handle;
		break;
	default:
		break;
	}

	rs->discover.start_handle = (uint16_t)(handle + 1);
	err = bt_gatt_discover(rs->conn, &rs->discover);
	if (err) {
		fail_connection(rs, "characteristic discovery failed to queue", NULL);
	}
}

static void handle_identity_read(struct remote_state *rs, uint8_t err,
				 const uint8_t *data, uint16_t length)
{
	if (err || length < 20) {
		fail_connection(rs, "RR_IDENTITY read failed", NULL);
		return;
	}

	memcpy(rs->identity_buf, data, 20);

	/* A14: a major mismatch is a refusal to operate, not a degraded mode
	 * (RP §16). Minor mismatches are tolerated by design — nothing here
	 * currently varies by minor version. */
	if (data[0] != 1) {
		fail_connection(rs, "RR_IDENTITY proto major mismatch",
				"REMOTE_PROTO_MISMATCH");
		return;
	}

	/* A13: set_serial is a second lock on a door §10.3's key already
	 * bolts — a mismatch here is a bench fault (a unit provisioned for a
	 * different set), not a security event. Compared as the raw 12-byte
	 * wire field, not as a NUL-terminated C string: a bench-fault serial
	 * that happens to lack a NUL within the field is exactly the kind of
	 * malformed input §10.2 exists to catch, not a case to give a free
	 * pass by stopping at the first NUL. */
	if (memcmp(&rs->identity_buf[8], own_set_serial, PROVISIONING_SERIAL_LEN) != 0) {
		fail_connection(rs, "RR_IDENTITY set_serial mismatch", "SET_MISMATCH");
		return;
	}

	start_subscribe(rs);
}

static void handle_subscribed(struct remote_state *rs, uint8_t err)
{
	if (err) {
		fail_connection(rs, "RR_UPLINK subscribe failed", NULL);
		return;
	}

	rframe_ctr_init(&rs->ctr);
	report_ready(rs);
}

static void handle_notify(struct remote_state *rs, const uint8_t *data, uint16_t length)
{
	struct rframe_msg msg;
	enum rframe_decode_status st;

	st = rframe_decode(data, length, &msg);
	switch (st) {
	case RFRAME_ERR_UNKNOWN_TYPE:
		/* A2: ignore silently. Forward compatibility depends on it. */
		return;
	case RFRAME_ERR_LENGTH:
	case RFRAME_ERR_FIELD:
		/* A1/A3: ignore, log. No aggregate counter exists for this path
		 * — it is the malformed-frame case, not the CTR gap/duplicate
		 * case BUILD_SPEC §8 stages counters for. */
		diag(rs, "malformed uplink frame");
		return;
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
		return;
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
			return;
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
}

static uint8_t discover_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   struct bt_gatt_discover_params *params)
{
	struct remote_state *rs = CONTAINER_OF(params, struct remote_state, discover);
	struct bt_evt evt = { .type = BT_EVT_DISCOVER, .rs = rs };

	ARG_UNUSED(conn);

	/* Reading rs->discover.type here, rather than writing it, is safe:
	 * nothing writes it except handle_discover() below, and that only
	 * runs after this exact callback has already returned — the chain is
	 * strictly one hop in flight at a time per remote. */
	evt.discover.phase = rs->discover.type;

	if (attr == NULL) {
		evt.discover.found = false;
	} else if (rs->discover.type == BT_GATT_DISCOVER_PRIMARY) {
		evt.discover.found = true;
		evt.discover.handle = attr->handle;
	} else {
		const struct bt_gatt_chrc *chrc = attr->user_data;

		evt.discover.found = true;
		evt.discover.handle = chrc->value_handle;
		if (!bt_uuid_cmp(chrc->uuid, &uuid_identity.uuid)) {
			evt.discover.which = 1;
		} else if (!bt_uuid_cmp(chrc->uuid, &uuid_uplink.uuid)) {
			evt.discover.which = 2;
		} else if (!bt_uuid_cmp(chrc->uuid, &uuid_downlink.uuid)) {
			evt.discover.which = 3;
		}
	}

	bt_evt_post(&evt);

	/* Always STOP: handle_discover() re-issues bt_gatt_discover() for the
	 * next hop itself, rather than relying on Zephyr's CONTINUE-driven
	 * sweep, so nothing here decides based on state only radio_workq now
	 * owns. */
	return BT_GATT_ITER_STOP;
}

static uint8_t identity_read_cb(struct bt_conn *conn, uint8_t err,
				struct bt_gatt_read_params *params,
				const void *data, uint16_t length)
{
	struct remote_state *rs = CONTAINER_OF(params, struct remote_state, read);
	struct bt_evt evt = { .type = BT_EVT_IDENTITY_READ, .rs = rs };

	ARG_UNUSED(conn);

	evt.identity.err = err;
	evt.identity.length = MIN(length, sizeof(evt.identity.data));
	if (data != NULL && evt.identity.length > 0) {
		memcpy(evt.identity.data, data, evt.identity.length);
	}

	bt_evt_post(&evt);
	return BT_GATT_ITER_STOP;
}

static uint8_t uplink_notify_cb(struct bt_conn *conn,
				struct bt_gatt_subscribe_params *params,
				const void *data, uint16_t length)
{
	struct remote_state *rs = CONTAINER_OF(params, struct remote_state, subscribe);
	struct bt_evt evt = { .type = BT_EVT_NOTIFY, .rs = rs };

	ARG_UNUSED(conn);

	if (data == NULL) {
		/* Subscription removed (disconnect tears this down anyway). */
		return BT_GATT_ITER_STOP;
	}

	evt.notify.length = MIN(length, sizeof(evt.notify.data));
	memcpy(evt.notify.data, data, evt.notify.length);

	bt_evt_post(&evt);
	return BT_GATT_ITER_CONTINUE;
}

static void subscribe_cb(struct bt_conn *conn, uint8_t err,
			 struct bt_gatt_subscribe_params *params)
{
	struct remote_state *rs = CONTAINER_OF(params, struct remote_state, subscribe);
	struct bt_evt evt = { .type = BT_EVT_SUBSCRIBED, .rs = rs };

	ARG_UNUSED(conn);

	evt.subscribed.err = err;
	bt_evt_post(&evt);
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

static void handle_connected(struct remote_state *rs, uint8_t err)
{
	int rc;

	/* Whatever this resolves to — connected or failed/cancelled — the
	 * shared initiating slot (header comment) is free again the instant
	 * this callback runs; the raw ACL connection either now exists or
	 * never will for this attempt. Only encryption/discovery/subscribe
	 * remain below, none of which touch the initiating slot. */
	if (connect_owner == rs) {
		connect_owner = NULL;
		(void)k_work_cancel_delayable(&connect_yield);
	}

	if (err) {
		bt_conn_unref(rs->conn);
		rs->conn = NULL;
		rs->phase = PHASE_IDLE;
		advance_connect(rs);
		return;
	}

	rs->phase = PHASE_CONNECTING;

	/* BUILD_SPEC §7.1 order: LTK, then raise security. No GATT operation
	 * happens before security_changed() confirms encryption. */
	rc = bt_nrf_conn_set_ltk(rs->conn, &set_ltk, true);
	if (rc) {
		fail_connection(rs, "bt_nrf_conn_set_ltk failed", NULL);
		return;
	}
	rc = bt_conn_set_security(rs->conn, BT_SECURITY_L4);
	if (rc) {
		/* A12 territory: no valid key relationship possible. */
		fail_connection(rs, "bt_conn_set_security failed", NULL);
		return;
	}

	/* Best-effort; not on the encrypted-before-GATT critical path. RP §11
	 * requires 2M on both connections for the density argument. */
	(void)bt_conn_le_phy_update(rs->conn, BT_CONN_LE_PHY_PARAM_2M);

	/* The slot freed above is this remote's own to give up now, since
	 * it's past needing it — let the other remote take a turn if it's
	 * been waiting. start_connect(rs) is a no-op (rs->conn is already
	 * set), so advance_connect() is safe to use here too rather than
	 * duplicating its logic. */
	advance_connect(rs);
}

static void handle_disconnected(struct remote_state *rs)
{
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

static void handle_security_changed(struct remote_state *rs, bt_security_t level,
				    enum bt_security_err err)
{
	if (rs->phase != PHASE_CONNECTING) {
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

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	struct bt_evt evt = { .type = BT_EVT_CONNECTED, .conn = bt_conn_ref(conn) };

	evt.connected.err = err;
	bt_evt_post(&evt);
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	struct bt_evt evt = { .type = BT_EVT_DISCONNECTED, .conn = bt_conn_ref(conn) };

	ARG_UNUSED(reason);
	bt_evt_post(&evt);
}

static void on_security_changed(struct bt_conn *conn, bt_security_t level,
				enum bt_security_err err)
{
	struct bt_evt evt = { .type = BT_EVT_SECURITY_CHANGED, .conn = bt_conn_ref(conn) };

	evt.security.level = level;
	evt.security.err = err;
	bt_evt_post(&evt);
}

static struct bt_conn_cb conn_callbacks = {
	.connected = on_connected,
	.disconnected = on_disconnected,
	.security_changed = on_security_changed,
};

/* ------------------------------------------------------------------------ */
/* bt_evt dispatcher                                                         */
/* ------------------------------------------------------------------------ */

static void bt_evt_work_handler(struct k_work *work)
{
	struct bt_evt evt;

	ARG_UNUSED(work);

	while (k_msgq_get(&bt_evtq, &evt, K_NO_WAIT) == 0) {
		struct remote_state *rs = evt.rs;

		if (evt.conn != NULL) {
			/* Only radio_workq writes remotes[i].conn now (see the
			 * marshaling section's comment above), so this
			 * comparison is safe here and would not have been on
			 * the BT thread. */
			rs = find_by_conn(evt.conn);
		}

		if (rs != NULL) {
			switch (evt.type) {
			case BT_EVT_CONNECTED:
				handle_connected(rs, evt.connected.err);
				break;
			case BT_EVT_DISCONNECTED:
				handle_disconnected(rs);
				break;
			case BT_EVT_SECURITY_CHANGED:
				handle_security_changed(rs, evt.security.level, evt.security.err);
				break;
			case BT_EVT_DISCOVER:
				handle_discover(rs, evt.discover.phase, evt.discover.found,
						evt.discover.handle, evt.discover.which);
				break;
			case BT_EVT_IDENTITY_READ:
				handle_identity_read(rs, evt.identity.err, evt.identity.data,
						     evt.identity.length);
				break;
			case BT_EVT_SUBSCRIBED:
				handle_subscribed(rs, evt.subscribed.err);
				break;
			case BT_EVT_NOTIFY:
				handle_notify(rs, evt.notify.data, evt.notify.length);
				break;
			}
		}

		if (evt.conn != NULL) {
			bt_conn_unref(evt.conn);
		}
	}
}

/* ------------------------------------------------------------------------ */
/* Public interface (radio.h)                                                */
/* ------------------------------------------------------------------------ */

int radio_init(const struct radio_cb *cb, struct k_work_q *workq,
	       const struct provisioning_record *prov)
{
	int err;

	callbacks = cb;
	radio_workq = workq;
	k_work_init(&bt_evt_work, bt_evt_work_handler);

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
	memcpy(own_set_serial, prov->set_serial, sizeof(own_set_serial));

	connect_owner = NULL;
	k_work_init_delayable(&connect_yield, connect_yield_handler);

	for (int r = 0; r < PROTO_REMOTE_COUNT; r++) {
		struct remote_state *rs = &remotes[r];

		memset(rs, 0, sizeof(*rs));
		rs->which = (enum proto_remote)r;
		rs->peer_addr.type = BT_ADDR_LE_RANDOM;
		memcpy(rs->peer_addr.a.val, prov->peer_addr[r], PROVISIONING_ADDR_LEN);

		rs->discover.func = discover_cb;
		rs->read.func = identity_read_cb;

		k_work_init_delayable(&rs->rssi_work, rssi_work_handler);
		k_work_init_delayable(&rs->disconnect_debounce, disconnect_debounce_handler);
		k_work_init_delayable(&rs->retry_work, retry_work_handler);
	}

	/* Both remotes are marked idle (memset above) before either calls
	 * start_connect() — RED's own call must see GREEN's real want-a-turn
	 * state, not a half-initialized one, for the very first contention
	 * check to be correct. */
	for (int r = 0; r < PROTO_REMOTE_COUNT; r++) {
		start_connect(&remotes[r]);
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
