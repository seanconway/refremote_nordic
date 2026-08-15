/*
 * Host unit tests for common/rframe.c — RADIO_PROTOCOL.md §4-§6 and the W0
 * conformance cases dongle/BUILD_SPEC.md §11 names for Stage 3: A1-A7, A11,
 * A20 "at codec level". Deliberately framework-free: `make check`.
 *
 * A20 (an inert press produces no downlink frame at all) has no assertion
 * here on purpose: the codec offers no way to *construct* a silent tap in
 * the first place — there is no frame type for it — so the case is verified
 * by the vocabulary's shape rather than by a runtime check. It is re-verified
 * end-to-end at Stage 4 by counting frames on the air.
 */
#include "rframe.h"

#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

#define CHECK(cond, ...)                                                     \
	do {                                                                  \
		checks++;                                                     \
		if (!(cond)) {                                                \
			failures++;                                           \
			printf("  FAIL %s:%d: ", __func__, __LINE__);         \
			printf(__VA_ARGS__);                                  \
			printf("\n");                                         \
		}                                                             \
	} while (0)

/* ------------------------------------------------------------------------ */
/* Round trips — one per frame type                                         */
/* ------------------------------------------------------------------------ */

static void round_trip_up_input(void)
{
	uint8_t buf[RFRAME_MAX_LEN];
	struct rframe_msg msg;

	int n = rframe_enc_up_input(buf, sizeof(buf), 7, PROTO_BTN_BACKWARD,
				    PROTO_GEST_HOLD_REP);

	CHECK(n == 4, "got %d", n);
	CHECK(buf[0] == RFRAME_UP_INPUT, "type 0x%02X", buf[0]);
	CHECK(buf[2] == 0x05, "button wire byte 0x%02X", buf[2]); /* BACKWARD is 5th, §5.3 */
	CHECK(buf[3] == 0x03, "gesture wire byte 0x%02X", buf[3]); /* HOLD_REP */

	enum rframe_decode_status st = rframe_decode(buf, (size_t)n, &msg);

	CHECK(st == RFRAME_OK, "status %d", st);
	CHECK(msg.type == RFRAME_UP_INPUT, "type %d", msg.type);
	CHECK(msg.ctr == 7, "ctr %u", msg.ctr);
	CHECK(msg.up_input.button == PROTO_BTN_BACKWARD, "button %d", msg.up_input.button);
	CHECK(msg.up_input.gesture == PROTO_GEST_HOLD_REP, "gesture %d", msg.up_input.gesture);
}

static void round_trip_up_ready(void)
{
	uint8_t buf[RFRAME_MAX_LEN];
	struct rframe_msg msg;

	int n = rframe_enc_up_ready(buf, sizeof(buf), 0, 42, RFRAME_READY_RECONNECT);

	CHECK(n == 4, "got %d", n);

	enum rframe_decode_status st = rframe_decode(buf, (size_t)n, &msg);

	CHECK(st == RFRAME_OK, "status %d", st);
	CHECK(msg.type == RFRAME_UP_READY, "type %d", msg.type);
	CHECK(msg.up_ready.ctr_base == 42, "ctr_base %u", msg.up_ready.ctr_base);
	CHECK(msg.up_ready.reason == RFRAME_READY_RECONNECT, "reason %d", msg.up_ready.reason);
}

static void round_trip_up_telemetry(void)
{
	uint8_t buf[RFRAME_MAX_LEN];
	struct rframe_msg msg;

	int n = rframe_enc_up_telemetry(buf, sizeof(buf), 3, 87,
					RFRAME_TELEMETRY_CHARGING, 5);

	CHECK(n == 6, "got %d", n);
	CHECK(buf[5] == 0, "reserved byte %u", buf[5]);

	enum rframe_decode_status st = rframe_decode(buf, (size_t)n, &msg);

	CHECK(st == RFRAME_OK, "status %d", st);
	CHECK(msg.up_telemetry.battery_pct == 87, "batt %u", msg.up_telemetry.battery_pct);
	CHECK(msg.up_telemetry.flags == RFRAME_TELEMETRY_CHARGING, "flags 0x%02X",
	      msg.up_telemetry.flags);
	CHECK(msg.up_telemetry.dn_lost == 5, "dn_lost %u", msg.up_telemetry.dn_lost);
}

static void round_trip_up_diag(void)
{
	uint8_t buf[RFRAME_MAX_LEN];
	struct rframe_msg msg;
	const uint8_t payload[] = { 0x11, 0x22, 0x33 };

	int n = rframe_enc_up_diag(buf, sizeof(buf), 9, payload, sizeof(payload));

	CHECK(n == 5, "got %d", n);

	enum rframe_decode_status st = rframe_decode(buf, (size_t)n, &msg);

	CHECK(st == RFRAME_OK, "status %d", st);
	CHECK(msg.up_diag.len == 3, "len %zu", msg.up_diag.len);
	CHECK(memcmp(msg.up_diag.data, payload, 3) == 0, "payload mismatch");
}

static void round_trip_up_diag_empty(void)
{
	uint8_t buf[RFRAME_MAX_LEN];
	struct rframe_msg msg;

	int n = rframe_enc_up_diag(buf, sizeof(buf), 1, NULL, 0);

	CHECK(n == 2, "got %d", n);

	enum rframe_decode_status st = rframe_decode(buf, (size_t)n, &msg);

	CHECK(st == RFRAME_OK, "status %d", st);
	CHECK(msg.up_diag.len == 0, "len %zu", msg.up_diag.len);
	CHECK(msg.up_diag.data == NULL, "data should be NULL when len == 0");
}

static void up_diag_over_budget_rejected(void)
{
	uint8_t buf[RFRAME_MAX_LEN];
	uint8_t payload[19]; /* one more than the 18-byte payload budget */

	memset(payload, 0xAA, sizeof(payload));

	int n = rframe_enc_up_diag(buf, sizeof(buf), 1, payload, sizeof(payload));

	CHECK(n == -1, "got %d", n);
}

static void round_trip_dn_haptic(void)
{
	uint8_t buf[RFRAME_MAX_LEN];
	struct rframe_msg msg;

	int n = rframe_enc_dn_haptic(buf, sizeof(buf), 200, PROTO_WF_TRIPLE, 0);

	CHECK(n == 4, "got %d", n);
	CHECK(buf[2] == 0x07, "waveform wire byte 0x%02X", buf[2]); /* TRIPLE, §5.4 */

	enum rframe_decode_status st = rframe_decode(buf, (size_t)n, &msg);

	CHECK(st == RFRAME_OK, "status %d", st);
	CHECK(msg.dn_haptic.waveform == PROTO_WF_TRIPLE, "waveform %d", msg.dn_haptic.waveform);
	CHECK(msg.dn_haptic.ttl_4ms == 0, "ttl %u", msg.dn_haptic.ttl_4ms);
}

static void round_trip_dn_indicator(void)
{
	uint8_t buf[RFRAME_MAX_LEN];
	struct rframe_msg msg;
	const uint8_t f1[3] = { 10, 20, 30 };
	const uint8_t f2[3] = { 40, 50, 60 };

	int n = rframe_enc_dn_indicator(buf, sizeof(buf), 1, PROTO_IND_SOLID, f1,
					PROTO_IND_OFF, f2);

	CHECK(n == 10, "got %d", n);

	enum rframe_decode_status st = rframe_decode(buf, (size_t)n, &msg);

	CHECK(st == RFRAME_OK, "status %d", st);
	CHECK(msg.dn_indicator.f1_mode == PROTO_IND_SOLID, "f1_mode %d", msg.dn_indicator.f1_mode);
	CHECK(memcmp(msg.dn_indicator.f1_rgb, f1, 3) == 0, "f1_rgb mismatch");
	CHECK(msg.dn_indicator.f2_mode == PROTO_IND_OFF, "f2_mode %d", msg.dn_indicator.f2_mode);
	CHECK(memcmp(msg.dn_indicator.f2_rgb, f2, 3) == 0, "f2_rgb mismatch");
}

/* A11: re-encoding an unchanged DN_INDICATOR must produce a byte-identical
 * frame, so a receiver comparing frames sees no change and re-triggers
 * nothing — the encoder is a pure function of its arguments, holding no
 * hidden state that could make two "identical" calls diverge. */
static void dn_indicator_encode_is_idempotent(void)
{
	uint8_t a[RFRAME_MAX_LEN], b[RFRAME_MAX_LEN];
	const uint8_t rgb[3] = { 1, 2, 3 };

	int na = rframe_enc_dn_indicator(a, sizeof(a), 5, PROTO_IND_SOLID, rgb,
					 PROTO_IND_SOLID, rgb);
	int nb = rframe_enc_dn_indicator(b, sizeof(b), 5, PROTO_IND_SOLID, rgb,
					 PROTO_IND_SOLID, rgb);

	CHECK(na == nb, "%d vs %d", na, nb);
	CHECK(memcmp(a, b, (size_t)na) == 0, "identical calls produced different bytes");
}

static void round_trip_dn_config(void)
{
	uint8_t buf[RFRAME_MAX_LEN];
	struct rframe_msg msg;

	int n = rframe_enc_dn_config(buf, sizeof(buf), 2, 80, 60);

	CHECK(n == 4, "got %d", n);

	enum rframe_decode_status st = rframe_decode(buf, (size_t)n, &msg);

	CHECK(st == RFRAME_OK, "status %d", st);
	CHECK(msg.dn_config.haptic_scale == 80, "haptic_scale %u", msg.dn_config.haptic_scale);
	CHECK(msg.dn_config.led_brightness == 60, "led_brightness %u", msg.dn_config.led_brightness);
}

static void round_trip_dn_host(void)
{
	uint8_t buf[RFRAME_MAX_LEN];
	struct rframe_msg msg;

	int n = rframe_enc_dn_host(buf, sizeof(buf), 4, true);

	CHECK(n == 3, "got %d", n);

	enum rframe_decode_status st = rframe_decode(buf, (size_t)n, &msg);

	CHECK(st == RFRAME_OK, "status %d", st);
	CHECK(msg.dn_host.up == true, "up %d", msg.dn_host.up);

	n = rframe_enc_dn_host(buf, sizeof(buf), 4, false);
	st = rframe_decode(buf, (size_t)n, &msg);
	CHECK(st == RFRAME_OK, "status %d", st);
	CHECK(msg.dn_host.up == false, "up %d", msg.dn_host.up);
}

static void round_trip_dn_simsoc(void)
{
	uint8_t buf[RFRAME_MAX_LEN];
	struct rframe_msg msg;

	int n = rframe_enc_dn_simsoc(buf, sizeof(buf), 4, 42);

	CHECK(n == 3, "got %d", n);

	enum rframe_decode_status st = rframe_decode(buf, (size_t)n, &msg);

	CHECK(st == RFRAME_OK, "status %d", st);
	CHECK(msg.type == RFRAME_DN_SIMSOC, "type %d", msg.type);
	CHECK(msg.dn_simsoc.pct == 42u, "pct %d", msg.dn_simsoc.pct);
}

static void a_field_dn_simsoc_out_of_range_rejected(void)
{
	uint8_t buf[RFRAME_MAX_LEN] = { RFRAME_DN_SIMSOC, 4, 101 };
	struct rframe_msg msg;

	enum rframe_decode_status st = rframe_decode(buf, 3, &msg);

	CHECK(st == RFRAME_ERR_FIELD, "status %d", st);
}

static void dn_simsoc_enc_rejects_out_of_range(void)
{
	uint8_t buf[RFRAME_MAX_LEN];

	int n = rframe_enc_dn_simsoc(buf, sizeof(buf), 4, 101);

	CHECK(n == -1, "got %d", n);
}

/* ------------------------------------------------------------------------ */
/* Conformance cases                                                         */
/* ------------------------------------------------------------------------ */

/* A1: UP_INPUT with an unknown button or gesture value. */
static void a1_unknown_button_rejected(void)
{
	uint8_t buf[4] = { RFRAME_UP_INPUT, 0, 0x08 /* one past F2 */, 0x01 };
	struct rframe_msg msg;

	enum rframe_decode_status st = rframe_decode(buf, sizeof(buf), &msg);

	CHECK(st == RFRAME_ERR_FIELD, "got %d", st);
}

static void a1_zero_button_rejected(void)
{
	/* 0x00 is not a defined button — the wire is 1-based. */
	uint8_t buf[4] = { RFRAME_UP_INPUT, 0, 0x00, 0x01 };
	struct rframe_msg msg;

	enum rframe_decode_status st = rframe_decode(buf, sizeof(buf), &msg);

	CHECK(st == RFRAME_ERR_FIELD, "got %d", st);
}

static void a1_unknown_gesture_rejected(void)
{
	uint8_t buf[4] = { RFRAME_UP_INPUT, 0, 0x01, 0x04 /* one past HOLD_REP */ };
	struct rframe_msg msg;

	enum rframe_decode_status st = rframe_decode(buf, sizeof(buf), &msg);

	CHECK(st == RFRAME_ERR_FIELD, "got %d", st);
}

/* A2: an unknown TYPE is ignored silently — the caller must be able to tell
 * this apart from a known type that failed to parse. */
static void a2_unknown_type_ignored(void)
{
	uint8_t buf[4] = { 0x55, 0, 0, 0 }; /* not any defined TYPE */
	struct rframe_msg msg;

	enum rframe_decode_status st = rframe_decode(buf, sizeof(buf), &msg);

	CHECK(st == RFRAME_ERR_UNKNOWN_TYPE, "got %d", st);
}

/* A3: a known TYPE with the wrong length. */
static void a3_wrong_length_rejected(void)
{
	uint8_t short_haptic[3] = { RFRAME_DN_HAPTIC, 0, 0x01 };
	uint8_t long_config[5] = { RFRAME_DN_CONFIG, 0, 50, 50, 0 };
	struct rframe_msg msg;

	CHECK(rframe_decode(short_haptic, sizeof(short_haptic), &msg) == RFRAME_ERR_LENGTH,
	      "DN_HAPTIC one byte short");
	CHECK(rframe_decode(long_config, sizeof(long_config), &msg) == RFRAME_ERR_LENGTH,
	      "DN_CONFIG one byte long");
}

/* A4: CTR equal to the last accepted value is a duplicate, not a new event.
 *
 * Fixture note: `s.last` is set directly rather than by priming through
 * rframe_ctr_accept(), because accept() on a freshly-init'd state (last == 0)
 * misclassifies any priming value within RFRAME_CTR_DUP_WINDOW of 0 going
 * backwards through the wrap — exactly the trap a6 below exists to catch.
 * Real callers never hit this: RP §7.2 guarantees the first frame on a
 * connection is always UP_READY, which calls rframe_ctr_rebaseline(), never
 * a raw accept(). Poking `last` here is fixture setup, not a claim about how
 * the state is reached in production. */
static void a4_duplicate_ctr_discarded(void)
{
	struct rframe_ctr_state s;
	uint8_t gap = 0xFF;

	rframe_ctr_init(&s);
	s.last = 1;

	enum rframe_ctr_result r = rframe_ctr_accept(&s, 1, &gap);

	CHECK(r == RFRAME_CTR_DUPLICATE, "got %d", r);
	CHECK(s.last == 1, "last moved on a duplicate: %u", s.last);
}

/* A5: CTR skipping three values is accepted, and the event is not withheld —
 * this test is about the classification; the caller is what does not
 * withhold the event, by treating GAP the same as EXPECTED for delivery. */
static void a5_gap_of_three_accepted(void)
{
	struct rframe_ctr_state s;
	uint8_t gap = 0xFF;

	rframe_ctr_init(&s);
	s.last = 10;

	enum rframe_ctr_result r = rframe_ctr_accept(&s, 14, &gap); /* 11,12,13 missing */

	CHECK(r == RFRAME_CTR_GAP, "got %d", r);
	CHECK(gap == 3, "got %u", gap);
	CHECK(s.last == 14, "last %u", s.last);
}

/* A6: CTR wrapping 255 -> 0 is consecutive, not a 255-frame gap.
 *
 * This is the case that exposed why a4/a5 cannot prime through accept():
 * seeding via accept(255) against a freshly-init'd state (last == 0) computes
 * back = (0 - 255) mod 256 == 1, which is inside the duplicate window, so the
 * "priming" call itself would misreport as a duplicate instead of establishing
 * last == 255. Setting `s.last` directly sidesteps that and tests only the
 * wrap arithmetic this case is actually about. */
static void a6_wrap_is_consecutive(void)
{
	struct rframe_ctr_state s;
	uint8_t gap = 0xFF;

	rframe_ctr_init(&s);
	s.last = 255;

	enum rframe_ctr_result r = rframe_ctr_accept(&s, 0, &gap);

	CHECK(r == RFRAME_CTR_EXPECTED, "got %d", r);
	CHECK(s.last == 0, "last %u", s.last);
}

/* A7: a reboot's UP_READY carries a reset ctr_base, and re-baselining must
 * not itself present as a gap against the old sequence. */
static void a7_rebaseline_logs_no_gap(void)
{
	struct rframe_ctr_state s;
	uint8_t gap = 0xFF;

	rframe_ctr_init(&s);
	rframe_ctr_accept(&s, 200, &gap); /* running normally: last = 200 */

	rframe_ctr_rebaseline(&s, 0); /* remote rebooted; will resume at CTR 0 */

	enum rframe_ctr_result r = rframe_ctr_accept(&s, 0, &gap);

	CHECK(r == RFRAME_CTR_EXPECTED, "rebaseline should make the reset value EXPECTED, got %d", r);
	CHECK(gap == 0, "no gap should be attributed to a reboot, got %u", gap);
}

/* The duplicate window's documented boundary (RP §6.1: "within a window of
 * 8"): back == 8 is still a duplicate; the classification does not silently
 * shift by one. */
static void duplicate_window_boundary(void)
{
	struct rframe_ctr_state s;
	uint8_t gap;

	rframe_ctr_init(&s);
	rframe_ctr_accept(&s, 100, &gap); /* last = 100 */

	enum rframe_ctr_result r = rframe_ctr_accept(&s, 92, &gap); /* back == 8 */

	CHECK(r == RFRAME_CTR_DUPLICATE, "back==8 should still be inside the window, got %d", r);
}

/* ------------------------------------------------------------------------ */

int main(void)
{
	struct {
		const char *name;
		void (*fn)(void);
	} tests[] = {
		{ "round trip: UP_INPUT",             round_trip_up_input },
		{ "round trip: UP_READY",             round_trip_up_ready },
		{ "round trip: UP_TELEMETRY",         round_trip_up_telemetry },
		{ "round trip: UP_DIAG",              round_trip_up_diag },
		{ "round trip: UP_DIAG (empty)",      round_trip_up_diag_empty },
		{ "UP_DIAG over payload budget",      up_diag_over_budget_rejected },
		{ "round trip: DN_HAPTIC",            round_trip_dn_haptic },
		{ "round trip: DN_INDICATOR",         round_trip_dn_indicator },
		{ "DN_INDICATOR encode is idempotent (A11)", dn_indicator_encode_is_idempotent },
		{ "round trip: DN_CONFIG",            round_trip_dn_config },
		{ "round trip: DN_HOST",              round_trip_dn_host },
		{ "round trip: DN_SIMSOC",            round_trip_dn_simsoc },
		{ "A1: DN_SIMSOC out-of-range rejected", a_field_dn_simsoc_out_of_range_rejected },
		{ "DN_SIMSOC encoder rejects out-of-range", dn_simsoc_enc_rejects_out_of_range },
		{ "A1: unknown button rejected",      a1_unknown_button_rejected },
		{ "A1: zero button rejected",         a1_zero_button_rejected },
		{ "A1: unknown gesture rejected",     a1_unknown_gesture_rejected },
		{ "A2: unknown type ignored",         a2_unknown_type_ignored },
		{ "A3: wrong length rejected",        a3_wrong_length_rejected },
		{ "A4: duplicate CTR discarded",      a4_duplicate_ctr_discarded },
		{ "A5: gap of three accepted",        a5_gap_of_three_accepted },
		{ "A6: wrap is consecutive",          a6_wrap_is_consecutive },
		{ "A7: rebaseline logs no gap",       a7_rebaseline_logs_no_gap },
		{ "duplicate window boundary",        duplicate_window_boundary },
	};

	for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		int before = failures;

		tests[i].fn();
		printf("%s %s\n", failures == before ? "ok  " : "FAIL", tests[i].name);
	}

	printf("\n%d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
