#include "engine.h"
#include "protocol.h"
#include "usb_link.h"
#include "indicator.h"
#include "radio.h"
#include "provisioning_flash.h"

#include <zephyr/kernel.h>

#include <stdio.h>
#include <string.h>

#define FW_VERSION           "0.2.0"
#define CAPS                 0u

/* Reported in HELLO's <set> field only when provisioning is absent or
 * corrupt — BUILD_SPEC.md §9. Deliberately not a plausible serial: the app
 * displays this for the referee's pre-match check against the label on the
 * hardware (PROTOCOL.md §4.1, FS §12.3), and a placeholder that looked real
 * would pass that check silently. A live ERR NO_PROVISIONING already fired
 * at boot for the same condition (engine_start()), so this is what a human
 * sees on the screen, not the only signal of the fault. */
#define UNPROVISIONED_SET_SERIAL "RR-0000"

/* ------------------------------------------------------------------------ */
/* Timing constants — BUILD_SPEC §4.1.                                       */
/*                                                                           */
/* Every one of these is named here and nowhere else. None appears as a       */
/* literal at a call site.                                                    */
/* ------------------------------------------------------------------------ */

/* §8. Was 5000 at v2.0. The tightening is affordable now and was not before:
 * v2.0's dongle-local heartbeat meant a short timeout risked cutting off a
 * heartbeat that was still correct. In v3.0 the dongle generates nothing, so
 * the only thing this timeout controls is how long a dead link can go on
 * looking alive on the referee's wrist. */
#define SUPERVISION_MS       2500

/* §11. Was 500 at v2.0, when acknowledgement was a convenience rather than a
 * functional requirement of the scoring interface. */
#define ACK_WINDOW_MS        120

/* §11: the ERM's hard floor, and the reason a tap sent at T+110 ms is a tap
 * that arrives too late to be one. */
#define MOTOR_SPINUP_MS      20

#define LINK_REEMIT_MS       10000    /* §7  */
#define PENDING_SLOTS        8        /* §5.3 */
#define TEST1_INTERVAL_MS    500      /* §10.2 */
#define TEST2_INTERVAL_MS    200
#define TEST4_INTERVAL_MS    250

/* Half a beat period. Later than this and a heartbeat lands closer to the next
 * beat than to its own, which reports accrual timing that is not happening;
 * §9.3 would rather drop it. */
#define BEAT_TTL_MS          500

#define ENGINE_STACK_SIZE    2048
#define ENGINE_PRIORITY      K_PRIO_COOP(7)

/* ------------------------------------------------------------------------ */
/* State                                                                     */
/* ------------------------------------------------------------------------ */

static struct k_work_q engine_q;
static K_THREAD_STACK_DEFINE(engine_stack, ENGINE_STACK_SIZE);

static bool supervision_suspended;

/* Whether the scoreboard is present. Starts false and is raised by the first
 * line to arrive, not at boot — a dongle plugged into a laptop has no reason to
 * believe an app is there, and telling the remotes otherwise would render
 * LED_LINK lit over a link that does not exist. */
static bool host_up;

/* Set once at boot by engine_start() and never re-read from flash after —
 * BUILD_SPEC.md §9 defines no field procedure for re-provisioning, and a
 * record that changed under a running unit is not a case anything here needs
 * to handle. */
static bool provisioned;
static struct provisioning_record prov;

static uint16_t tx_seq;

struct pending {
	bool used;
	uint16_t seq;
	enum proto_remote src;   /* the wrist the tap must land on */
	int64_t born;            /* k_uptime_get() at emission */
};

static struct pending pending_tbl[PENDING_SLOTS];

struct link_info {
	enum proto_link_state state;
	int32_t rssi;
	int32_t batt;
	bool has_batt;
};

static struct link_info links[PROTO_REMOTE_COUNT];

/* §6.4: applied on receipt, persisted until reboot, re-sent to any remote that
 * connects. This is transport state, not match state — unlike indicator state
 * it has no scoreboard-side counterpart that could diverge from it. */
struct remote_cfg {
	uint8_t haptic;
	uint8_t bright;
};

static struct remote_cfg cfgs[PROTO_REMOTE_COUNT];

/* BUILD_SPEC §8. The console is disabled and a second CDC-ACM instance is
 * forbidden, so these reach a human only as LOG lines. */
static uint32_t c_evt_emitted;
static uint32_t c_radio_gap;
static uint32_t c_radio_dup;
static uint32_t c_presses_no_host;
static uint32_t c_presses_not_ready;
static uint32_t c_taps_late;
static uint32_t c_beat_drops;

static struct k_work_delayable supervision_work;
static struct k_work_delayable link_reemit_work;
static struct k_work_delayable test_work;
static struct k_work_delayable pending_sweep_work;

static uint8_t test_mode;
static uint8_t test_index;
static uint32_t rng_state = 1u;

static void engine_reschedule(struct k_work_delayable *w, k_timeout_t delay)
{
	(void)k_work_reschedule_for_queue(&engine_q, w, delay);
}

static bool valid_remote(enum proto_remote r)
{
	return r < PROTO_REMOTE_COUNT;
}

/* ------------------------------------------------------------------------ */
/* Send helpers                                                              */
/* ------------------------------------------------------------------------ */

static void send_hello(void)
{
	char buf[PROTO_MAX_LINE];
	const char *set_serial = provisioned ? prov.set_serial : UNPROVISIONED_SET_SERIAL;

	if (proto_enc_hello(buf, sizeof(buf), FW_VERSION, set_serial, CAPS) > 0) {
		(void)usb_link_send(buf);
	}
}

static void send_link(enum proto_remote r)
{
	const struct link_info *l = &links[r];
	char buf[PROTO_MAX_LINE];

	if (proto_enc_link(buf, sizeof(buf), r, l->state, l->rssi,
			   l->batt, l->has_batt) > 0) {
		(void)usb_link_send(buf);
	}
}

static void send_log(const char *text)
{
	char buf[PROTO_MAX_LINE];

	if (proto_enc_log(buf, sizeof(buf), text) > 0) {
		(void)usb_link_send(buf);
	}
}

static void send_err(const char *text)
{
	char buf[PROTO_MAX_LINE];

	if (proto_enc_err(buf, sizeof(buf), text) > 0) {
		(void)usb_link_send(buf);
	}
	indicator_error();
}

/*
 * Two lines rather than one because eight 32-bit counters plus labels can
 * exceed the 119-byte content limit, and proto_enc_log() refuses an over-long
 * line rather than truncating it — so a single line would silently report
 * nothing at exactly the moment the counters had something to say.
 */
static void send_counters(void)
{
	char text[PROTO_MAX_CONTENT];

	(void)snprintf(text, sizeof(text),
		       "counters evt=%u gap=%u dup=%u nohost=%u notready=%u",
		       (unsigned int)c_evt_emitted, (unsigned int)c_radio_gap,
		       (unsigned int)c_radio_dup, (unsigned int)c_presses_no_host,
		       (unsigned int)c_presses_not_ready);
	send_log(text);

#ifdef CONFIG_DONGLE_RADIO
	(void)snprintf(text, sizeof(text),
		       "counters late=%u txdrop=%u beatdrop=%u conn=%u",
		       (unsigned int)c_taps_late,
		       (unsigned int)usb_link_tx_drops(),
		       (unsigned int)c_beat_drops,
		       (unsigned int)CONFIG_DONGLE_CONN_INTERVAL_UNITS);
#else
	/* A build with no radio says so on the wire, so any latency figure
	 * recorded from this session is attributable to a known configuration
	 * rather than to a rung somebody has to remember. */
	(void)snprintf(text, sizeof(text),
		       "counters late=%u txdrop=%u beatdrop=%u conn=none-noradio",
		       (unsigned int)c_taps_late,
		       (unsigned int)usb_link_tx_drops(),
		       (unsigned int)c_beat_drops);
#endif
	send_log(text);
}

/* ------------------------------------------------------------------------ */
/* Pending acknowledgements (§5.3, §11)                                      */
/*                                                                           */
/* v2.0 expired entries lazily, purging as add and take walked the table, on  */
/* the argument that nothing observable happens on expiry so a sweep timer    */
/* bought nothing. THAT ARGUMENT DOES NOT SURVIVE THE WINDOW SHRINKING TO     */
/* 120 ms. Lazily, an entry only expires when a later EVT or ACK touches the  */
/* table, so on an idle link a stale entry can still match an ACK arriving    */
/* long after its deadline — and firing a tap the referee cannot account for  */
/* is the single worst outcome §11 identifies.                                */
/* ------------------------------------------------------------------------ */

static void pending_sweep_reschedule(void)
{
	int64_t now = k_uptime_get();
	int64_t earliest = INT64_MAX;
	int64_t due;

	for (int i = 0; i < PENDING_SLOTS; i++) {
		if (pending_tbl[i].used && pending_tbl[i].born < earliest) {
			earliest = pending_tbl[i].born;
		}
	}

	if (earliest == INT64_MAX) {
		(void)k_work_cancel_delayable(&pending_sweep_work);
		return;
	}

	due = earliest + ACK_WINDOW_MS - now;
	engine_reschedule(&pending_sweep_work, K_MSEC(due > 0 ? due : 0));
}

static void pending_sweep_handler(struct k_work *work)
{
	int64_t now = k_uptime_get();

	ARG_UNUSED(work);

	for (int i = 0; i < PENDING_SLOTS; i++) {
		if (pending_tbl[i].used &&
		    now - pending_tbl[i].born >= ACK_WINDOW_MS) {
			pending_tbl[i].used = false;
		}
	}

	pending_sweep_reschedule();
}

static void pending_add(uint16_t seq, enum proto_remote src)
{
	int slot = -1;
	int oldest = 0;

	for (int i = 0; i < PENDING_SLOTS; i++) {
		if (!pending_tbl[i].used) {
			slot = i;
			break;
		}
		if (pending_tbl[i].born < pending_tbl[oldest].born) {
			oldest = i;
		}
	}

	/* On overflow evict the oldest: no expiry event, no tap. Eight entries
	 * is the steady-state capacity of a 120 ms window at any press rate a
	 * pair of hands can produce. */
	if (slot < 0) {
		slot = oldest;
	}

	pending_tbl[slot].used = true;
	pending_tbl[slot].seq = seq;
	pending_tbl[slot].src = src;
	pending_tbl[slot].born = k_uptime_get();

	pending_sweep_reschedule();
}

static bool pending_take(uint16_t seq, enum proto_remote *src, int64_t *born)
{
	int64_t now = k_uptime_get();

	for (int i = 0; i < PENDING_SLOTS; i++) {
		if (!pending_tbl[i].used || pending_tbl[i].seq != seq) {
			continue;
		}

		pending_tbl[i].used = false;

		/* Expired between the last sweep and this ACK. Treated exactly
		 * as an unknown seq: the entry goes, the tap does not fire. */
		if (now - pending_tbl[i].born >= ACK_WINDOW_MS) {
			pending_sweep_reschedule();
			return false;
		}

		*src = pending_tbl[i].src;
		*born = pending_tbl[i].born;
		pending_sweep_reschedule();
		return true;
	}

	return false;
}

/*
 * Deadline mechanism 1 (BUILD_SPEC §6.3): do not hand over a frame that is
 * already late. This needs no controller feature and it catches the case that
 * actually happens — an app stall, a USB stall, a browser that lost the
 * foreground.
 */
static void send_tap(enum proto_remote src, int64_t born)
{
	int64_t remaining = ACK_WINDOW_MS - (k_uptime_get() - born) - MOTOR_SPINUP_MS;
	int64_t units;

	if (remaining <= 0) {
		c_taps_late++;
		/* Silence, not a failure haptic. No tap invokes a rule the
		 * referee already has — press again — and the second press
		 * carries a new seq the app applies as a genuinely new event.
		 * A late tap arriving during that next press is read as
		 * acknowledgement of *it*, so the referee stops pressing with a
		 * point still missing. Silence is recoverable; a misattributed
		 * tap is not, and afterwards it looks like referee error. */
		send_log("tap withheld: acknowledgement budget already spent");
		return;
	}

	units = remaining / 4;
	if (units < 1) {
		units = 1;
	} else if (units > 255) {
		units = 255;
	}

	/* Routed by src, never broadcast. A broadcast tap is indistinguishable
	 * from a correctly routed one whenever only one remote is being
	 * watched — which is every bench test until a second remote exists —
	 * and it is wrong in every real match. */
	(void)radio_send_haptic(src, PROTO_WF_TAP, (uint8_t)units);
}

/* ------------------------------------------------------------------------ */
/* Event emission                                                            */
/* ------------------------------------------------------------------------ */

/*
 * The single point where a seq is allocated. Both origins funnel through it,
 * and they are separate functions rather than one function with an `is_test`
 * flag — a boolean parameter naming the caller is the version of this that gets
 * passed wrong once, silently, in a rebase.
 */
static void emit_evt(enum proto_button b, enum proto_gesture g,
		     enum proto_remote src)
{
	char buf[PROTO_MAX_LINE];
	uint16_t seq = tx_seq;

	tx_seq = proto_next_seq(tx_seq);
	c_evt_emitted++;
	pending_add(seq, src);

	if (proto_enc_evt(buf, sizeof(buf), b, g, src, seq) > 0) {
		(void)usb_link_send(buf);
	}
}

/*
 * From the radio. §12's guarantee — no EVT is ever assigned a seq for a press
 * that was lost — is what makes a gap in seq mean USB loss and never radio
 * loss, and it holds only if a press from a remote that is not established is
 * refused a number rather than given one.
 */
static void on_radio_input(enum proto_remote src, enum proto_button b,
			   enum proto_gesture g)
{
	if (!valid_remote(src) || b >= PROTO_BTN_COUNT || g >= PROTO_GEST_COUNT) {
		return;
	}
	if (!radio_is_ready(src)) {
		c_presses_not_ready++;
		return;
	}

	/* §7.5: dropped, never queued. A press applied minutes later is a wrong
	 * score with no visible cause, and it arrives with no acknowledgement
	 * tap to warn anyone it happened. The referee already knows before
	 * pressing, because DN_HOST DOWN has put the remote into link-lost
	 * rendering. */
	if (!host_up) {
		c_presses_no_host++;
		return;
	}

	emit_evt(b, g, src);
}

/*
 * From a TEST mode. The connectivity checks above are deliberately bypassed:
 * test events are synthetic stimulus and are not claims about any remote.
 *
 * This is the trap the radio seam sets. Under radio_null nothing is ever ready,
 * so applying the radio path's check here would silently disable every TEST
 * mode — the entire stimulus the no-radio configuration exists to provide.
 */
static void emit_test_evt(enum proto_button b, enum proto_gesture g,
			  enum proto_remote src)
{
	emit_evt(b, g, src);
}

/* ------------------------------------------------------------------------ */
/* Radio callbacks                                                           */
/* ------------------------------------------------------------------------ */

static void on_radio_ready(enum proto_remote src)
{
	char buf[PROTO_MAX_LINE];

	if (!valid_remote(src)) {
		return;
	}

	/* A remote that has just come up holds no config either (§6.4), and it
	 * cannot see the USB half of the path (§7.4). Both are re-asserted
	 * before JOIN so the scoreboard's STATE reply lands on a remote that is
	 * already configured. */
	(void)radio_send_config(src, cfgs[src].haptic, cfgs[src].bright);
	(void)radio_send_host(src, host_up);

	if (proto_enc_join(buf, sizeof(buf), src) > 0) {
		(void)usb_link_send(buf);
	}
}

static void on_radio_link(enum proto_remote src, enum proto_link_state st,
			  int8_t rssi)
{
	if (!valid_remote(src) || st >= PROTO_LINK_STATE_COUNT) {
		return;
	}

	links[src].state = st;
	links[src].rssi = rssi;
	if (st != PROTO_LINK_CONNECTED) {
		/* A battery reading from a remote that is gone is a stale
		 * reading presented as a live one. */
		links[src].has_batt = false;
	}

	/* Already debounced by the radio layer (§7): what arrives here is
	 * settled, so it goes straight out. */
	send_link(src);
}

static void on_radio_telemetry(enum proto_remote src, uint8_t batt_pct,
			       uint8_t flags)
{
	ARG_UNUSED(flags);

	if (!valid_remote(src) || batt_pct > 100u) {
		return;
	}

	links[src].batt = batt_pct;
	links[src].has_batt = true;
}

static void on_radio_diag(enum proto_remote src, const char *text)
{
	char t[PROTO_MAX_CONTENT];

	(void)snprintf(t, sizeof(t), "%s: %s", proto_remote_name(src), text);
	send_log(t);
}

static const struct radio_cb radio_callbacks = {
	.on_input     = on_radio_input,
	.on_ready     = on_radio_ready,
	.on_link      = on_radio_link,
	.on_telemetry = on_radio_telemetry,
	.on_diag      = on_radio_diag,
};

/* ------------------------------------------------------------------------ */
/* Link supervision (§8) — the primary fail-safe                             */
/* ------------------------------------------------------------------------ */

static void supervision_arm(void)
{
	if (supervision_suspended) {
		return;
	}
	engine_reschedule(&supervision_work, K_MSEC(SUPERVISION_MS));
}

static void supervision_kick(void)
{
	if (!host_up) {
		host_up = true;
		for (int r = 0; r < PROTO_REMOTE_COUNT; r++) {
			(void)radio_send_host((enum proto_remote)r, true);
		}
	}
	supervision_arm();
}

/*
 * Re-derived rather than edited. The v2.0 handler stopped the clock and emitted
 * APP_TIMEOUT only `if (clock_running)`, so an idle bench did not spam
 * timeouts; at v3.0 there is no clock, so that gate has nothing left to test
 * and keeping its shape would have hidden step 2 below.
 */
static void supervision_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (supervision_suspended) {
		return;
	}

	host_up = false;
	send_err("APP_TIMEOUT");

	/*
	 * §8 states this in prose — instruct both remotes to render link-lost —
	 * and it is the one supervision requirement with no executable
	 * reference anywhere, because the emulator models no radio. It is
	 * RADIO_PROTOCOL.md §14 A15, and §9.2 calls it the case most likely to
	 * be missed: every part of the radio looks healthy while it happens.
	 */
	for (int r = 0; r < PROTO_REMOTE_COUNT; r++) {
		(void)radio_send_host((enum proto_remote)r, false);
	}

	/* Deliberately not re-armed. Recovery is the next line from the app,
	 * which re-arms it and raises DN_HOST again. */
}

/* ------------------------------------------------------------------------ */
/* LINK re-emission (§7)                                                     */
/* ------------------------------------------------------------------------ */

static void link_reemit_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	/* Only for connected remotes: a DISCONNECTED line every 10 s would be
	 * noise on a link whose state has not changed since boot. */
	for (int r = 0; r < PROTO_REMOTE_COUNT; r++) {
		if (links[r].state == PROTO_LINK_CONNECTED) {
			send_link((enum proto_remote)r);
		}
	}

	engine_reschedule(&link_reemit_work, K_MSEC(LINK_REEMIT_MS));
}

/* ------------------------------------------------------------------------ */
/* TEST modes (§10.2)                                                        */
/* ------------------------------------------------------------------------ */

/*
 * The TEST 4 sweep. SIXTEEN PER REMOTE, NOT TWENTY-ONE.
 *
 * Seven buttons take PRESS and seven take HOLD, but §5.1 emits HOLD_REP only
 * for FORWARD and BACKWARD: 7 + 7 + 2. A 21-event sweep would emit HOLD_REP on
 * five buttons that can never repeat in the field, exercising an app path
 * against traffic no remote will ever send. PROTOCOL.md §10.2 said 21 and has
 * been corrected; the assertion below is what stops it coming back.
 */
static const struct {
	enum proto_button btn;
	enum proto_gesture gest;
} sweep[] = {
	{ PROTO_BTN_ADD_POINT,    PROTO_GEST_PRESS },
	{ PROTO_BTN_ADD_POINT,    PROTO_GEST_HOLD },
	{ PROTO_BTN_TOGGLE_CLOCK, PROTO_GEST_PRESS },
	{ PROTO_BTN_TOGGLE_CLOCK, PROTO_GEST_HOLD },
	{ PROTO_BTN_REMOVE_POINT, PROTO_GEST_PRESS },
	{ PROTO_BTN_REMOVE_POINT, PROTO_GEST_HOLD },
	{ PROTO_BTN_FORWARD,      PROTO_GEST_PRESS },
	{ PROTO_BTN_FORWARD,      PROTO_GEST_HOLD },
	{ PROTO_BTN_FORWARD,      PROTO_GEST_HOLD_REP },
	{ PROTO_BTN_BACKWARD,     PROTO_GEST_PRESS },
	{ PROTO_BTN_BACKWARD,     PROTO_GEST_HOLD },
	{ PROTO_BTN_BACKWARD,     PROTO_GEST_HOLD_REP },
	{ PROTO_BTN_F1,           PROTO_GEST_PRESS },
	{ PROTO_BTN_F1,           PROTO_GEST_HOLD },
	{ PROTO_BTN_F2,           PROTO_GEST_PRESS },
	{ PROTO_BTN_F2,           PROTO_GEST_HOLD },
};

BUILD_ASSERT(ARRAY_SIZE(sweep) == 16,
	     "TEST 4 sweeps 16 events per remote, 32 total (PROTOCOL.md §10.2)");

static uint32_t rng_next(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

static enum proto_gesture random_gesture(enum proto_button b)
{
	uint32_t g = rng_next() % (uint32_t)PROTO_GEST_COUNT;

	/* Same rule as the sweep, for the same reason. */
	if (g == (uint32_t)PROTO_GEST_HOLD_REP &&
	    b != PROTO_BTN_FORWARD && b != PROTO_BTN_BACKWARD) {
		g = (uint32_t)PROTO_GEST_PRESS;
	}

	return (enum proto_gesture)g;
}

static void test_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	switch (test_mode) {
	case 1u:
		if (test_index >= PROTO_BTN_COUNT) {
			test_mode = 0u;
			return;
		}
		emit_test_evt((enum proto_button)test_index, PROTO_GEST_PRESS,
			      (test_index % 2u == 0u) ? PROTO_REMOTE_RED
						      : PROTO_REMOTE_GREEN);
		test_index++;
		engine_reschedule(&test_work, K_MSEC(TEST1_INTERVAL_MS));
		return;

	case 2u: {
		enum proto_button b =
			(enum proto_button)(rng_next() % (uint32_t)PROTO_BTN_COUNT);

		emit_test_evt(b, random_gesture(b),
			      (enum proto_remote)(rng_next() %
						  (uint32_t)PROTO_REMOTE_COUNT));
		engine_reschedule(&test_work, K_MSEC(TEST2_INTERVAL_MS));
		return;
	}

	case 4u: {
		size_t i;
		enum proto_remote r;

		if (test_index >= 2u * ARRAY_SIZE(sweep)) {
			test_mode = 0u;
			send_log("TEST 4: complete, 32 events");
			return;
		}

		i = test_index % ARRAY_SIZE(sweep);
		r = (test_index < ARRAY_SIZE(sweep)) ? PROTO_REMOTE_RED
						     : PROTO_REMOTE_GREEN;
		emit_test_evt(sweep[i].btn, sweep[i].gest, r);
		test_index++;
		engine_reschedule(&test_work, K_MSEC(TEST4_INTERVAL_MS));
		return;
	}

	default:
		return;
	}
}

static void handle_test(uint32_t mode)
{
	/* Starting any mode implicitly stops the previous one. */
	test_mode = 0u;
	test_index = 0u;
	(void)k_work_cancel_delayable(&test_work);

	switch (mode) {
	case 0u:
		supervision_suspended = false;
		supervision_arm();
		send_log("TEST 0: stopped, supervision active");
		break;

	case 1u:
	case 2u:
	case 4u:
		test_mode = (uint8_t)mode;
		engine_reschedule(&test_work, K_NO_WAIT);
		break;

	case 3u:
		/* A standing trap: left on, every supervision test passes for
		 * the wrong reason. The reply exists so a bench session can
		 * confirm TEST 0 took effect before trusting one. */
		supervision_suspended = true;
		(void)k_work_cancel_delayable(&supervision_work);
		send_log("TEST 3: suspended until TEST 0");
		break;

	default:
		/* LOG and ERR are the only diagnostic channel this firmware
		 * has — the console is disabled and a second CDC-ACM instance
		 * is forbidden — so an unsupported mode is noted there rather
		 * than absorbed silently. LOG is never semantic (§3), so this
		 * is not an answer, it is a note. */
		send_log("unsupported TEST mode");
		break;
	}
}

/* ------------------------------------------------------------------------ */
/* Target expansion (§6.4, §9)                                               */
/* ------------------------------------------------------------------------ */

/*
 * BOTH is expanded here into one call per remote. There is deliberately no
 * broadcast frame at the radio: RADIO_PROTOCOL.md §5 has none, and the
 * per-connection CTR a broadcast would need has nowhere to live.
 *
 * PROTO_TGT_RED/GREEN share numbering with proto_remote, so the expansion is
 * loop bounds rather than a translation table.
 */
static void target_bounds(enum proto_target t, int *first, int *last)
{
	if (t == PROTO_TGT_BOTH) {
		*first = 0;
		*last = PROTO_REMOTE_COUNT - 1;
	} else {
		*first = (int)t;
		*last = (int)t;
	}
}

/* ------------------------------------------------------------------------ */
/* Line dispatch                                                             */
/* ------------------------------------------------------------------------ */

void engine_on_line(char *line)
{
	struct proto_msg msg;
	char buf[PROTO_MAX_LINE];

	/*
	 * Re-armed BEFORE parsing, on any framed line including a malformed
	 * one. §8's "any received line" means any line the assembler completed,
	 * not any line that parsed — a peer sending garbage is a peer that is
	 * alive, and dropping the remotes into link-lost over a typo would be a
	 * fail-safe firing on the wrong evidence.
	 */
	supervision_kick();

	proto_parse(line, &msg);

	switch (msg.type) {
	case PROTO_INFO:
		send_hello();
		send_link(PROTO_REMOTE_RED);
		send_link(PROTO_REMOTE_GREEN);
		send_counters();
		break;

	case PROTO_PING:
		if (proto_enc_pong(buf, sizeof(buf)) > 0) {
			(void)usb_link_send(buf);
		}
		break;

	case PROTO_ECHO:
		if (proto_enc_echo(buf, sizeof(buf), msg.text.text) > 0) {
			(void)usb_link_send(buf);
		}
		break;

	case PROTO_ACK: {
		enum proto_remote src;
		int64_t born;

		/*
		 * Unknown, duplicate or already-expired: do nothing at all. No
		 * tap, no ERR, no LOG at error level. Acting on it would fire a
		 * tap the referee cannot attribute to any press they made.
		 *
		 * The app deliberately ACKs a duplicate EVT it dropped, because
		 * withholding the tap would make the referee press a third
		 * time. That lands here, on this path, and exactly one tap
		 * reaches the wrist.
		 */
		if (!pending_take(msg.ack.seq, &src, &born)) {
			break;
		}

		/*
		 * Inert (SILENT) and no-op are different and must not be
		 * flattened. Inert means the ruleset left the button
		 * unassigned: nothing goes on the air, because a rejection
		 * signal is more confusing than silence (FS §5.6). A no-op —
		 * REMOVE_POINT at the score floor — arrives as a plain ACK and
		 * gets the full tap, because a referee who feels nothing after
		 * a legitimate press will press again and double the score.
		 *
		 * From here the difference is exactly "tap or no tap", which is
		 * what makes it easy to collapse into one path. A20 verifies it
		 * by counting transmitted frames, not by watching an LED that
		 * was never going to light.
		 */
		if (msg.ack.silent) {
			break;
		}

		send_tap(src, born);
		break;
	}

	case PROTO_STATE: {
		struct indicator_state s;

		s.f1_mode = (uint8_t)msg.state.f1_mode;
		s.f2_mode = (uint8_t)msg.state.f2_mode;
		memcpy(s.f1_rgb, msg.state.f1_rgb, sizeof(s.f1_rgb));
		memcpy(s.f2_rgb, msg.state.f2_rgb, sizeof(s.f2_rgb));

		/*
		 * Relayed unconditionally, with NO dongle-side indicator cache.
		 *
		 * A cache would suppress redundant frames and the app already
		 * suppresses unchanged STATE lines at its end, so the
		 * temptation is real. It is refused because a cache is
		 * dongle-held state that can diverge from the scoreboard's —
		 * the FS §6.2 failure mode by name — and it diverges silently.
		 * Re-sending an unchanged 10-byte frame costs one frame.
		 * Holding a cache costs a class of bug.
		 *
		 * The emulator does hold one, for its remote mockups. On this
		 * point the emulator is not the authority.
		 */
		(void)radio_send_indicator(msg.state.remote, &s);
		break;
	}

	case PROTO_CFG: {
		int first, last;

		target_bounds(msg.cfg.target, &first, &last);
		for (int r = first; r <= last; r++) {
			cfgs[r].haptic = msg.cfg.haptic;
			cfgs[r].bright = msg.cfg.bright;
			(void)radio_send_config((enum proto_remote)r,
						msg.cfg.haptic, msg.cfg.bright);
		}
		break;
	}

	case PROTO_HAP: {
		int first, last;

		target_bounds(msg.hap.target, &first, &last);
		for (int r = first; r <= last; r++) {
			/* §9.3: BEAT is the only message on this link that may
			 * be dropped, it is never retried or queued, and its
			 * drops are counted separately — a BEAT drop is
			 * expected under load and a TAP drop is a defect, and
			 * one counter cannot say both. */
			uint8_t ttl = (msg.hap.waveform == PROTO_WF_BEAT)
				      ? (uint8_t)(BEAT_TTL_MS / 4) : 0u;

			if (radio_send_haptic((enum proto_remote)r,
					      msg.hap.waveform, ttl) < 0 &&
			    msg.hap.waveform == PROTO_WF_BEAT) {
				c_beat_drops++;
			}
		}
		break;
	}

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
		 * receive: ignore silently (§2.2). This is also where v2.0's
		 * CLOCK, EXPIRE and CONFIRM land. */
		break;
	}
}

/* ------------------------------------------------------------------------ */

void engine_init(void)
{
	k_work_queue_init(&engine_q);
	k_work_queue_start(&engine_q, engine_stack,
			   K_THREAD_STACK_SIZEOF(engine_stack),
			   ENGINE_PRIORITY, NULL);

	k_work_init_delayable(&supervision_work, supervision_handler);
	k_work_init_delayable(&link_reemit_work, link_reemit_handler);
	k_work_init_delayable(&test_work, test_handler);
	k_work_init_delayable(&pending_sweep_work, pending_sweep_handler);

	for (int r = 0; r < PROTO_REMOTE_COUNT; r++) {
		/* DISCONNECTED is the truth at boot, and it stays the truth for
		 * the whole of a CONFIG_DONGLE_RADIO=n build. */
		links[r].state = PROTO_LINK_DISCONNECTED;
		links[r].has_batt = false;

		/* Full scale until the app says otherwise. The app re-sends CFG
		 * on every handshake (§4.2 step 3), so this is what a manual
		 * terminal session gets and nothing more. */
		cfgs[r].haptic = 100u;
		cfgs[r].bright = 100u;
	}
}

struct k_work_q *engine_workq(void)
{
	return &engine_q;
}

void engine_start(void)
{
	/* k_uptime_get() is still near zero here, so it would seed every boot
	 * identically; the cycle counter has actually moved. */
	rng_state = k_cycle_get_32() | 1u;

	/*
	 * Read before anything else touches the wire: HELLO's <set> field
	 * below, and — from Stage 3 — radio_init()'s association parameters,
	 * both come from this. BUILD_SPEC.md §9: on any failure the unit
	 * still runs the USB side of the protocol (there is no advertise or
	 * initiate to refuse yet with CONFIG_DONGLE_RADIO=n) but the fault is
	 * reported once, so a bench technician sees it rather than a dongle
	 * that looks fine and silently reports the wrong set.
	 */
	{
		enum provisioning_status st = provisioning_load(&prov);

		provisioned = (st == PROVISIONING_OK);
		if (!provisioned) {
			char text[48];

			(void)snprintf(text, sizeof(text), "unprovisioned: %s",
				       provisioning_status_str(st));
			send_log(text);
			send_err("NO_PROVISIONING");
		}
	}

	if (radio_init(&radio_callbacks, &engine_q) != 0) {
		send_err("RADIO_INIT_FAILED");
	}

	/*
	 * Best-effort only. At boot the host has not enumerated and opened the
	 * port yet, so these bytes are very likely to go nowhere — which is
	 * fine, because the handshake is driven by the app sending INFO (§4.2).
	 * It exists for the benefit of a human on a terminal.
	 */
	send_hello();

	/* Armed, not kicked: host_up must stay false until a line actually
	 * arrives, or the remotes would be told the scoreboard is present
	 * before one has said anything. */
	supervision_arm();
	engine_reschedule(&link_reemit_work, K_MSEC(LINK_REEMIT_MS));
}
