#include "engine.h"
#include "protocol.h"
#include "usb_link.h"
#include "indicator.h"

#include <zephyr/kernel.h>

#include <stdio.h>
#include <string.h>

#define FW_VERSION           "0.1.0"
#define CAPS                 0u

#define HEARTBEAT_PERIOD_MS  1000
#define SUPERVISION_MS       5000     /* §5.1 */
#define LINK_REEMIT_MS       10000    /* §3.2 */
#define CONFIRM_TIMEOUT_MS   500      /* §6 */
#define PENDING_SLOTS        8        /* §6: "8 entries is ample" */
#define TEST1_INTERVAL_MS    500
#define TEST2_INTERVAL_MS    200

/* ------------------------------------------------------------------------ */
/* State                                                                     */
/* ------------------------------------------------------------------------ */

static bool clock_running;
static bool supervision_suspended;
static uint16_t tx_seq;

struct pending {
	bool used;
	uint16_t seq;
	enum proto_remote src;
	int64_t deadline;
};

static struct pending pending_tbl[PENDING_SLOTS];

struct link_info {
	enum proto_link_state state;
	int32_t rssi;
	int32_t batt;
	bool has_batt;
};

static struct link_info links[PROTO_REMOTE_COUNT];

static struct k_work_delayable heartbeat_work;
static struct k_work_delayable supervision_work;
static struct k_work_delayable link_reemit_work;
static struct k_work_delayable test_work;

static uint8_t test_mode;
static uint8_t test1_index;
static uint32_t rng_state = 1u;

/* ------------------------------------------------------------------------ */
/* Send helpers                                                              */
/* ------------------------------------------------------------------------ */

static void send_hello(void)
{
	char buf[PROTO_MAX_LINE];

	if (proto_enc_hello(buf, sizeof(buf), FW_VERSION, CAPS) > 0) {
		usb_link_send(buf);
	}
}

static void send_link(enum proto_remote r)
{
	const struct link_info *l = &links[r];
	char buf[PROTO_MAX_LINE];

	if (proto_enc_link(buf, sizeof(buf), r, l->state, l->rssi,
			   l->batt, l->has_batt) > 0) {
		usb_link_send(buf);
	}
}

static void send_err(const char *text)
{
	char buf[PROTO_MAX_LINE];

	if (proto_enc_err(buf, sizeof(buf), text) > 0) {
		usb_link_send(buf);
	}
	indicator_error();
}

static void send_log(const char *text)
{
	char buf[PROTO_MAX_LINE];

	if (proto_enc_log(buf, sizeof(buf), text) > 0) {
		usb_link_send(buf);
	}
}

/* ------------------------------------------------------------------------ */
/* Pending confirmations (§6)                                                */
/*                                                                           */
/* Expiry is lazy: nothing observable happens when an entry times out — there */
/* is deliberately no failure haptic, because "no buzz means the point did    */
/* not land" is the rule the referee works to. So a sweep timer would buy     */
/* nothing over checking the deadline at lookup time.                         */
/* ------------------------------------------------------------------------ */

static void pending_add(uint16_t seq, enum proto_remote src)
{
	int64_t now = k_uptime_get();
	int slot = -1;
	int oldest_slot = 0;
	int64_t oldest = INT64_MAX;

	for (int i = 0; i < PENDING_SLOTS; i++) {
		if (pending_tbl[i].used && pending_tbl[i].deadline <= now) {
			pending_tbl[i].used = false;
		}
		if (!pending_tbl[i].used) {
			if (slot < 0) {
				slot = i;
			}
		} else if (pending_tbl[i].deadline < oldest) {
			oldest = pending_tbl[i].deadline;
			oldest_slot = i;
		}
	}

	if (slot < 0) {
		slot = oldest_slot;
	}

	pending_tbl[slot].used = true;
	pending_tbl[slot].seq = seq;
	pending_tbl[slot].src = src;
	pending_tbl[slot].deadline = now + CONFIRM_TIMEOUT_MS;
}

static bool pending_take(uint16_t seq, enum proto_remote *src)
{
	int64_t now = k_uptime_get();

	for (int i = 0; i < PENDING_SLOTS; i++) {
		if (!pending_tbl[i].used) {
			continue;
		}
		if (pending_tbl[i].deadline <= now) {
			pending_tbl[i].used = false;
			continue;
		}
		if (pending_tbl[i].seq == seq) {
			pending_tbl[i].used = false;
			*src = pending_tbl[i].src;
			return true;
		}
	}

	return false;
}

/* ------------------------------------------------------------------------ */
/* Events                                                                    */
/* ------------------------------------------------------------------------ */

static void send_evt(enum proto_action action, enum proto_remote src)
{
	char buf[PROTO_MAX_LINE];
	uint16_t seq = tx_seq;

	tx_seq = proto_next_seq(tx_seq);

	/* Only these two are confirmed (§6) — the app sends CONFIRM for nothing
	 * else, so registering the rest would just age out of the table. */
	if (action == PROTO_ACT_ADD_POINT || action == PROTO_ACT_REMOVE_POINT) {
		pending_add(seq, src);
	}

	if (proto_enc_evt(buf, sizeof(buf), action, src, seq) > 0) {
		usb_link_send(buf);
	}
}

/* ------------------------------------------------------------------------ */
/* Clock and heartbeat (§5)                                                  */
/* ------------------------------------------------------------------------ */

static void heartbeat_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!clock_running) {
		return;
	}

	/* TODO(BLE): send the short haptic pulse to each connected remote. */
	indicator_heartbeat();

	k_work_reschedule(&heartbeat_work, K_MSEC(HEARTBEAT_PERIOD_MS));
}

static void clock_set(bool run)
{
	/* Idempotent: CLOCK RUN while already running must not restart or
	 * double the timer phase (§5). */
	if (run == clock_running) {
		return;
	}

	clock_running = run;

	if (run) {
		k_work_reschedule(&heartbeat_work, K_NO_WAIT);
	} else {
		(void)k_work_cancel_delayable(&heartbeat_work);
	}
}

/* ------------------------------------------------------------------------ */
/* Link supervision (§5.1) — the primary fail-safe                           */
/* ------------------------------------------------------------------------ */

static void supervision_kick(void)
{
	if (supervision_suspended) {
		return;
	}
	k_work_reschedule(&supervision_work, K_MSEC(SUPERVISION_MS));
}

static void supervision_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (supervision_suspended) {
		return;
	}

	/* Only meaningful if the clock is running — otherwise an idle bench
	 * session would emit APP_TIMEOUT every five seconds forever. */
	if (clock_running) {
		clock_set(false);
		send_err("APP_TIMEOUT");
	}
}

/* ------------------------------------------------------------------------ */
/* LINK re-emission (§3.2)                                                   */
/* ------------------------------------------------------------------------ */

static void link_reemit_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	for (int r = 0; r < PROTO_REMOTE_COUNT; r++) {
		if (links[r].state == PROTO_LINK_CONNECTED) {
			send_link((enum proto_remote)r);
		}
	}

	k_work_reschedule(&link_reemit_work, K_MSEC(LINK_REEMIT_MS));
}

/* ------------------------------------------------------------------------ */
/* TEST modes (§7.2)                                                         */
/* ------------------------------------------------------------------------ */

static uint32_t rng_next(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

static void test_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (test_mode == 1u) {
		if (test1_index >= PROTO_ACT_COUNT) {
			test_mode = 0u;
			return;
		}
		send_evt((enum proto_action)test1_index,
			 (test1_index % 2u == 0u) ? PROTO_REMOTE_RED
						  : PROTO_REMOTE_GREEN);
		test1_index++;
		k_work_reschedule(&test_work, K_MSEC(TEST1_INTERVAL_MS));
		return;
	}

	if (test_mode == 2u) {
		send_evt((enum proto_action)(rng_next() % PROTO_ACT_COUNT),
			 (enum proto_remote)(rng_next() % PROTO_REMOTE_COUNT));
		k_work_reschedule(&test_work, K_MSEC(TEST2_INTERVAL_MS));
	}
}

static void handle_test(uint32_t mode)
{
	switch (mode) {
	case 0u:
		test_mode = 0u;
		(void)k_work_cancel_delayable(&test_work);
		supervision_suspended = false;
		supervision_kick();
		send_log("TEST 0: stopped, supervision active");
		break;
	case 1u:
		test_mode = 1u;
		test1_index = 0u;
		k_work_reschedule(&test_work, K_NO_WAIT);
		break;
	case 2u:
		test_mode = 2u;
		k_work_reschedule(&test_work, K_NO_WAIT);
		break;
	case 3u:
		supervision_suspended = true;
		(void)k_work_cancel_delayable(&supervision_work);
		send_log("TEST 3: supervision suspended (bench mode)");
		break;
	default:
		send_log("unsupported TEST mode");
		break;
	}
}

/* ------------------------------------------------------------------------ */
/* Line dispatch                                                             */
/* ------------------------------------------------------------------------ */

void engine_on_line(char *line)
{
	struct proto_msg msg;
	char buf[PROTO_MAX_LINE];
	enum proto_remote src;

	/* Any line from the app is proof of life, whatever it says (§5.1). */
	supervision_kick();

	proto_parse(line, &msg);

	switch (msg.type) {
	case PROTO_INFO:
		send_hello();
		send_link(PROTO_REMOTE_RED);
		send_link(PROTO_REMOTE_GREEN);
		break;

	case PROTO_PING:
		if (proto_enc_pong(buf, sizeof(buf)) > 0) {
			usb_link_send(buf);
		}
		break;

	case PROTO_ECHO:
		if (proto_enc_echo(buf, sizeof(buf), msg.text.text) > 0) {
			usb_link_send(buf);
		}
		break;

	case PROTO_CLOCK:
		clock_set(msg.clock.run);
		break;

	case PROTO_EXPIRE:
		/* Independent of CLOCK STOP — the app sends both, in whichever
		 * order suits it (§4). */
		indicator_expire();
		break;

	case PROTO_CONFIRM:
		/* Unknown or already-confirmed seq: ignore silently (§6). */
		if (pending_take(msg.confirm.seq, &src)) {
			/* TODO(BLE): route the double pulse to `src` only. */
			indicator_confirm();
		}
		break;

	case PROTO_TEST:
		handle_test(msg.test.mode);
		break;

	case PROTO_INVALID: {
		char text[64];

		(void)snprintf(text, sizeof(text), "ignored malformed line: %s",
			       msg.invalid_reason ? msg.invalid_reason : "?");
		send_log(text);
		break;
	}

	default:
		/* Unknown keyword, or a Dongle->App message we should never
		 * receive: ignore silently (§2.2). */
		break;
	}
}

/* ------------------------------------------------------------------------ */

void engine_init(void)
{
	k_work_init_delayable(&heartbeat_work, heartbeat_handler);
	k_work_init_delayable(&supervision_work, supervision_handler);
	k_work_init_delayable(&link_reemit_work, link_reemit_handler);
	k_work_init_delayable(&test_work, test_handler);

	rng_state = (uint32_t)k_uptime_get() | 1u;

	for (int r = 0; r < PROTO_REMOTE_COUNT; r++) {
#ifdef CONFIG_DONGLE_FAKE_LINK
		links[r].state = PROTO_LINK_CONNECTED;
		links[r].rssi = (r == PROTO_REMOTE_RED) ? -50 : -58;
		links[r].batt = (r == PROTO_REMOTE_RED) ? 92 : 77;
		links[r].has_batt = true;
#else
		links[r].state = PROTO_LINK_DISCONNECTED;
		links[r].has_batt = false;
#endif
	}

	/*
	 * Best-effort only. At boot the host has not enumerated and opened the
	 * port yet, so these bytes are very likely to go nowhere — which is
	 * fine, because the handshake is driven by the app sending INFO (§8).
	 */
	send_hello();

	supervision_kick();
	k_work_reschedule(&link_reemit_work, K_MSEC(LINK_REEMIT_MS));
}
