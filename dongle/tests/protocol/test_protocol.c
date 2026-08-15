/*
 * Host unit tests for protocol.c — PROTOCOL.md v3.0 §14 (T1–T16), plus the
 * encoder round-trips and the places where the scoreboard app parses more
 * strictly than the prose spec reads.
 *
 * Deliberately framework-free and Zephyr-free: `make check`.
 *
 * T3, T4, T5 and T9c are the ones that matter for framing. Chunk boundaries
 * are the only real hazard left on this link, and they are the reason the line
 * assembler exists at all.
 *
 * T7 is the one that matters for the v2.0 → v3.0 migration: a three-field
 * `EVT` must be refused, never read as a gestureless press.
 */
#include "../../src/protocol.h"

#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

#define CHECK(cond, ...)                                                       \
	do {                                                                   \
		checks++;                                                      \
		if (!(cond)) {                                                 \
			failures++;                                            \
			printf("  FAIL %s:%d: ", __func__, __LINE__);          \
			printf(__VA_ARGS__);                                   \
			printf("\n");                                          \
		}                                                              \
	} while (0)

/* Parses a string literal without the caller needing a mutable copy —
 * proto_parse() writes NULs in place, so every fixture needs its own array. */
#define PARSE(dst, literal)                                                    \
	char dst##_buf[] = literal;                                            \
	proto_parse(dst##_buf, &dst)

/* ------------------------------------------------------------------------ */
/* Line collector                                                            */
/* ------------------------------------------------------------------------ */

#define MAX_COLLECTED 16

struct collector {
	char lines[MAX_COLLECTED][PROTO_ASM_BUF_SIZE];
	size_t count;
};

static void collect(char *line, void *user)
{
	struct collector *c = user;

	if (c->count < MAX_COLLECTED) {
		snprintf(c->lines[c->count], sizeof(c->lines[0]), "%s", line);
		c->count++;
	}
}

static void feed_str(struct proto_asm *a, const char *s, struct collector *c)
{
	proto_asm_feed(a, (const uint8_t *)s, strlen(s), collect, c);
}

/* Feeds one byte per call, simulating the worst-case read() granularity. */
static void feed_str_bytewise(struct proto_asm *a, const char *s,
			      struct collector *c)
{
	for (const char *p = s; *p != '\0'; p++) {
		proto_asm_feed(a, (const uint8_t *)p, 1u, collect, c);
	}
}

/* ------------------------------------------------------------------------ */
/* §14 — the assembler cases                                                 */
/* ------------------------------------------------------------------------ */

static void t1_plain_line(void)
{
	struct proto_asm a;
	struct collector c = { 0 };

	proto_asm_init(&a);
	feed_str(&a, "EVT ADD_POINT PRESS RED 17\n", &c);

	CHECK(c.count == 1, "expected 1 line, got %zu", c.count);
	CHECK(c.count == 1 && strcmp(c.lines[0], "EVT ADD_POINT PRESS RED 17") == 0,
	      "got \"%s\"", c.count ? c.lines[0] : "(none)");
}

static void t2_crlf_stripped(void)
{
	struct proto_asm a;
	struct collector c = { 0 };

	proto_asm_init(&a);
	feed_str(&a, "EVT ADD_POINT PRESS RED 17\r\n", &c);

	CHECK(c.count == 1, "expected 1 line, got %zu", c.count);
	CHECK(c.count == 1 && strcmp(c.lines[0], "EVT ADD_POINT PRESS RED 17") == 0,
	      "trailing \\r not stripped: \"%s\"", c.count ? c.lines[0] : "(none)");
}

static void t3_one_byte_per_read(void)
{
	struct proto_asm a;
	struct collector c = { 0 };

	proto_asm_init(&a);
	feed_str_bytewise(&a, "EVT ADD_POINT PRESS RED 17\n", &c);

	CHECK(c.count == 1, "expected 1 line, got %zu", c.count);
	CHECK(c.count == 1 && strcmp(c.lines[0], "EVT ADD_POINT PRESS RED 17") == 0,
	      "got \"%s\"", c.count ? c.lines[0] : "(none)");
}

static void t4_three_lines_one_read(void)
{
	struct proto_asm a;
	struct collector c = { 0 };

	proto_asm_init(&a);
	feed_str(&a,
		 "EVT ADD_POINT PRESS RED 17\n"
		 "EVT TOGGLE_CLOCK HOLD GREEN 18\n"
		 "EVT BACKWARD HOLD_REP RED 19\n",
		 &c);

	CHECK(c.count == 3, "expected 3 lines, got %zu", c.count);
	if (c.count == 3) {
		CHECK(strcmp(c.lines[0], "EVT ADD_POINT PRESS RED 17") == 0, "line 0 wrong");
		CHECK(strcmp(c.lines[1], "EVT TOGGLE_CLOCK HOLD GREEN 18") == 0, "line 1 wrong");
		CHECK(strcmp(c.lines[2], "EVT BACKWARD HOLD_REP RED 19") == 0, "line 2 wrong");
	}
}

static void t5_split_mid_token(void)
{
	struct proto_asm a;
	struct collector c = { 0 };

	proto_asm_init(&a);
	feed_str(&a, "EVT ADD_PO", &c);
	CHECK(c.count == 0, "emitted a line before its terminator");
	feed_str(&a, "INT PRESS RED 17\n", &c);

	CHECK(c.count == 1, "expected 1 line, got %zu", c.count);
	CHECK(c.count == 1 && strcmp(c.lines[0], "EVT ADD_POINT PRESS RED 17") == 0,
	      "got \"%s\"", c.count ? c.lines[0] : "(none)");
}

static void t6_unknown_keyword(void)
{
	struct proto_msg m;

	PARSE(m, "BOGUS FOO BAR");
	CHECK(m.type == PROTO_UNKNOWN, "expected UNKNOWN, got %d", m.type);
}

/*
 * The v2.0-compatibility case, and the single most important one in the
 * rewrite. `EVT ADD_POINT RED 17` is what a partially-updated dongle, or a
 * v2.0 unit in a mixed fleet, puts on the wire. Read as a press it produces a
 * score with no gesture and nothing to tell it apart from a real one.
 *
 * The token count must be checked exactly. A `>= 4` test would accept the
 * five-field form below, and a `>= 3` test would accept the v2.0 form itself.
 */
static void t7_v2_shape_rejected(void)
{
	struct proto_msg m3, m2, m5;

	PARSE(m3, "EVT ADD_POINT RED 17");
	CHECK(m3.type == PROTO_INVALID, "v2.0 three-field EVT must be INVALID, got %d",
	      m3.type);

	PARSE(m2, "EVT ADD_POINT");
	CHECK(m2.type == PROTO_INVALID, "two-field EVT must be INVALID, got %d", m2.type);

	PARSE(m5, "EVT ADD_POINT PRESS RED 17 EXTRA");
	CHECK(m5.type == PROTO_INVALID, "five-field EVT must be INVALID, got %d", m5.type);
}

static void t8_bad_seq(void)
{
	struct proto_msg m, mb, mg;

	PARSE(m, "EVT ADD_POINT PRESS RED xyz");
	CHECK(m.type == PROTO_INVALID, "non-numeric seq must be INVALID, got %d", m.type);

	PARSE(mb, "EVT SIDEWAYS PRESS RED 17");
	CHECK(mb.type == PROTO_INVALID, "unknown button must be INVALID, got %d", mb.type);

	PARSE(mg, "EVT ADD_POINT SQUEEZE RED 17");
	CHECK(mg.type == PROTO_INVALID, "unknown gesture must be INVALID, got %d", mg.type);
}

static void t9_overlong_discarded(void)
{
	struct proto_asm a;
	struct collector c = { 0 };
	char junk[201];

	memset(junk, 'x', sizeof(junk) - 1);
	junk[sizeof(junk) - 1] = '\0';

	proto_asm_init(&a);
	feed_str(&a, junk, &c);
	CHECK(c.count == 0, "overlong run should emit nothing");

	/* The terminator of the junk run, then a good line. */
	feed_str(&a, "\nEVT ADD_POINT PRESS RED 17\n", &c);

	CHECK(c.count == 1, "expected 1 line after resync, got %zu", c.count);
	CHECK(c.count == 1 && strcmp(c.lines[0], "EVT ADD_POINT PRESS RED 17") == 0,
	      "junk was stitched onto the next line: \"%s\"",
	      c.count ? c.lines[0] : "(none)");
}

/*
 * The discard state must survive a chunk boundary. A split between reads
 * carries no information about the stream, so it cannot act as a resync
 * point — only a terminator can. If this regresses, the tail of a garbage run
 * is emitted as a line of its own, and a tail beginning at a keyword boundary
 * would parse as a real message.
 *
 * The scoreboard app had exactly this bug; both sides are pinned by this test
 * and its counterpart, protocol.test.js "T9c".
 */
static void t9c_discard_survives_chunk_boundary(void)
{
	struct proto_asm a;
	struct collector c = { 0 };
	char junk[101];

	memset(junk, 'x', sizeof(junk) - 1);
	junk[sizeof(junk) - 1] = '\0';

	proto_asm_init(&a);
	feed_str(&a, junk, &c);          /* 100 bytes: under budget so far */
	feed_str(&a, junk, &c);          /* 200 total: budget blown mid-chunk */
	CHECK(c.count == 0, "overlong run should emit nothing");

	feed_str(&a, "TAIL\nEVT ADD_POINT PRESS RED 17\n", &c);

	CHECK(c.count == 1, "expected 1 line, got %zu", c.count);
	CHECK(c.count == 1 && strcmp(c.lines[0], "EVT ADD_POINT PRESS RED 17") == 0,
	      "junk tail leaked as a line: \"%s\"", c.count ? c.lines[0] : "(none)");
}

static void t10_empty_lines_ignored(void)
{
	struct proto_asm a;
	struct collector c = { 0 };

	proto_asm_init(&a);
	feed_str(&a, "\n\n\nEVT ADD_POINT PRESS RED 17\n", &c);

	CHECK(c.count == 1, "expected 1 line, got %zu", c.count);
	CHECK(c.count == 1 && strcmp(c.lines[0], "EVT ADD_POINT PRESS RED 17") == 0,
	      "got \"%s\"", c.count ? c.lines[0] : "(none)");
}

/*
 * seq is 0–65535 now, and it lives in a uint16_t. The v2.0 range check
 * (`seq >= PROTO_SEQ_MODULO`) becomes vacuous once the field can hold every
 * value in range, so the literal 65536 has to be caught in a wider
 * intermediate before it is narrowed — otherwise it silently becomes 0.
 */
static void t11_seq_range(void)
{
	struct proto_msg lo, hi, over, huge;

	PARSE(lo, "ACK 0");
	CHECK(lo.type == PROTO_ACK && lo.ack.seq == 0u, "ACK 0");

	PARSE(hi, "ACK 65535");
	CHECK(hi.type == PROTO_ACK && hi.ack.seq == 65535u, "ACK 65535 is in range");

	PARSE(over, "ACK 65536");
	CHECK(over.type == PROTO_INVALID, "ACK 65536 must be INVALID, got %d (seq %u)",
	      over.type, (unsigned)over.ack.seq);

	PARSE(huge, "ACK 4294967296");
	CHECK(huge.type == PROTO_INVALID, "a seq wider than uint32 must be INVALID");
}

static void t12_state_parsed(void)
{
	struct proto_msg m, lower;

	PARSE(m, "STATE RED SOLID 00A0FF OFF 000000");
	CHECK(m.type == PROTO_STATE, "expected STATE, got %d", m.type);
	if (m.type == PROTO_STATE) {
		CHECK(m.state.remote == PROTO_REMOTE_RED, "remote");
		CHECK(m.state.f1_mode == PROTO_IND_SOLID, "f1 mode");
		CHECK(m.state.f1_rgb[0] == 0x00 && m.state.f1_rgb[1] == 0xA0 &&
		      m.state.f1_rgb[2] == 0xFF, "f1 rgb %02X%02X%02X",
		      m.state.f1_rgb[0], m.state.f1_rgb[1], m.state.f1_rgb[2]);
		CHECK(m.state.f2_mode == PROTO_IND_OFF, "f2 mode");
	}

	/* Case-insensitive on receive; the app emits upper case. */
	PARSE(lower, "STATE GREEN OFF 000000 SOLID c2f000");
	CHECK(lower.type == PROTO_STATE &&
	      lower.state.f2_rgb[0] == 0xC2 && lower.state.f2_rgb[1] == 0xF0 &&
	      lower.state.f2_rgb[2] == 0x00, "lower-case hex must parse");
}

/*
 * Five hex characters must invalidate the whole line. The failure mode of a
 * digit-accumulating parser is to accept 00A0F as 0x00A0F and render a colour
 * that is wrong but plausible, so the length is checked before the digits.
 */
static void t13_state_hex_strict(void)
{
	struct proto_msg m5, m7, mx, mode, argc;

	PARSE(m5, "STATE RED SOLID 00A0F OFF 000000");
	CHECK(m5.type == PROTO_INVALID, "5-char hex must be INVALID, got %d", m5.type);

	PARSE(m7, "STATE RED SOLID 00A0FFF OFF 000000");
	CHECK(m7.type == PROTO_INVALID, "7-char hex must be INVALID, got %d", m7.type);

	PARSE(mx, "STATE RED SOLID 00A0FG OFF 000000");
	CHECK(mx.type == PROTO_INVALID, "non-hex digit must be INVALID, got %d", mx.type);

	PARSE(mode, "STATE RED BLINK 00A0FF OFF 000000");
	CHECK(mode.type == PROTO_INVALID, "BLINK is not a mode in v3.0");

	PARSE(argc, "STATE RED SOLID 00A0FF OFF");
	CHECK(argc.type == PROTO_INVALID, "STATE needs exactly 5 arguments");
}

/*
 * §7 makes rssi mandatory when CONNECTED, and the app's parser enforces it.
 * A dongle emitting the short form would present as a signal indicator that
 * silently never updates, so our own parser must reject it too — that keeps
 * the failure a test failure rather than a field mystery.
 */
static void t14_link_requires_rssi(void)
{
	struct proto_msg short_form, full, disc, extra;

	PARSE(short_form, "LINK RED CONNECTED");
	CHECK(short_form.type == PROTO_INVALID,
	      "LINK CONNECTED without rssi must be rejected, as the app does");

	PARSE(full, "LINK RED CONNECTED -52 87");
	CHECK(full.type == PROTO_LINK && full.link.has_rssi && full.link.rssi == -52 &&
	      full.link.has_batt && full.link.batt == 87, "LINK RED CONNECTED -52 87");

	PARSE(disc, "LINK GREEN DISCONNECTED");
	CHECK(disc.type == PROTO_LINK && disc.link.state == PROTO_LINK_DISCONNECTED,
	      "LINK GREEN DISCONNECTED");

	PARSE(extra, "LINK GREEN DISCONNECTED -52");
	CHECK(extra.type == PROTO_INVALID, "rssi is only meaningful when CONNECTED");
}

static void t15_unknown_waveform(void)
{
	struct proto_msg spin, ok, both, tgt, argc;

	PARSE(spin, "HAP BOTH SPIN");
	CHECK(spin.type == PROTO_INVALID, "unknown waveform must be INVALID, got %d",
	      spin.type);

	PARSE(ok, "HAP RED BEAT");
	CHECK(ok.type == PROTO_HAP && ok.hap.target == PROTO_TGT_RED &&
	      ok.hap.waveform == PROTO_WF_BEAT, "HAP RED BEAT");

	PARSE(both, "HAP BOTH LONG");
	CHECK(both.type == PROTO_HAP && both.hap.target == PROTO_TGT_BOTH &&
	      both.hap.waveform == PROTO_WF_LONG, "HAP BOTH LONG");

	PARSE(tgt, "HAP BLUE TAP");
	CHECK(tgt.type == PROTO_INVALID, "unknown target must be INVALID");

	PARSE(argc, "HAP TAP");
	CHECK(argc.type == PROTO_INVALID, "HAP needs exactly 2 arguments");
}

/*
 * T16 is a deduplication case, and deduplication is deliberately *not* the
 * parser's job — it belongs to the pending table in engine.c (BUILD_SPEC §6.2)
 * and to the app's reducer. What the parser owes the layer above is that it
 * carries no state between calls, so two identical lines produce two identical
 * results and the caller is the only place a duplicate can be noticed.
 *
 * Pinning it here is what makes it safe for engine.c to dedupe on seq alone.
 */
static void t16_parser_is_stateless(void)
{
	struct proto_msg first, second;

	PARSE(first, "EVT ADD_POINT PRESS RED 17");
	PARSE(second, "EVT ADD_POINT PRESS RED 17");

	CHECK(first.type == PROTO_EVT && second.type == PROTO_EVT,
	      "both parses must succeed");
	CHECK(first.evt.seq == second.evt.seq && first.evt.src == second.evt.src &&
	      first.evt.button == second.evt.button &&
	      first.evt.gesture == second.evt.gesture,
	      "the parser must not dedupe or otherwise carry state between lines");
}

/* ------------------------------------------------------------------------ */
/* App -> Dongle messages (what the firmware actually consumes)              */
/* ------------------------------------------------------------------------ */

static void ack_and_silent(void)
{
	struct proto_msg plain, silent, bad, argc;

	PARSE(plain, "ACK 17");
	CHECK(plain.type == PROTO_ACK && plain.ack.seq == 17u && !plain.ack.silent,
	      "ACK 17 is a full acknowledgement");

	/* SILENT is the inert-button case: the event is consumed and nothing at
	 * all goes on the air. Losing the flag would fire a tap on a dead
	 * control, which FS §5.6 says is worse than silence. */
	PARSE(silent, "ACK 17 SILENT");
	CHECK(silent.type == PROTO_ACK && silent.ack.seq == 17u && silent.ack.silent,
	      "ACK 17 SILENT must set the silent flag");

	PARSE(bad, "ACK 17 QUIET");
	CHECK(bad.type == PROTO_INVALID, "only SILENT is a legal second argument");

	PARSE(argc, "ACK");
	CHECK(argc.type == PROTO_INVALID, "ACK needs a seq");
}

static void cfg_parsed(void)
{
	struct proto_msg m, hi, argc;

	PARSE(m, "CFG BOTH 80 60");
	CHECK(m.type == PROTO_CFG && m.cfg.target == PROTO_TGT_BOTH &&
	      m.cfg.haptic == 80u && m.cfg.bright == 60u, "CFG BOTH 80 60");

	PARSE(hi, "CFG RED 101 50");
	CHECK(hi.type == PROTO_INVALID, "CFG values are 0-100");

	PARSE(argc, "CFG BOTH 80");
	CHECK(argc.type == PROTO_INVALID, "CFG needs exactly 3 arguments");
}

static void simsoc_parsed(void)
{
	struct proto_msg m, hi, argc;

	PARSE(m, "SIMSOC RED 42");
	CHECK(m.type == PROTO_SIMSOC && m.simsoc.target == PROTO_TGT_RED &&
	      m.simsoc.pct == 42u, "SIMSOC RED 42");

	PARSE(hi, "SIMSOC GREEN 101");
	CHECK(hi.type == PROTO_INVALID, "SIMSOC pct is 0-100");

	PARSE(argc, "SIMSOC BOTH");
	CHECK(argc.type == PROTO_INVALID, "SIMSOC needs exactly 2 arguments");
}

static void simple_messages(void)
{
	struct proto_msg ping, ping_args, info, test3, test_bad, join;

	PARSE(ping, "PING");
	CHECK(ping.type == PROTO_PING, "PING");

	PARSE(ping_args, "PING extra");
	CHECK(ping_args.type == PROTO_INVALID, "PING with args must be rejected");

	PARSE(info, "INFO");
	CHECK(info.type == PROTO_INFO, "INFO");

	PARSE(test3, "TEST 3");
	CHECK(test3.type == PROTO_TEST && test3.test.mode == 3u, "TEST 3");

	PARSE(test_bad, "TEST x");
	CHECK(test_bad.type == PROTO_INVALID, "TEST with a non-numeric mode");

	PARSE(join, "JOIN RED");
	CHECK(join.type == PROTO_JOIN && join.join.remote == PROTO_REMOTE_RED, "JOIN RED");
}

/*
 * The v2.0 keywords are gone, and they must be gone from the parser too. A
 * dongle that still answered CLOCK would keep a clock the functional spec
 * forbids it to have (FS §6.2), and the failure would be invisible: the app
 * simply never sends the line.
 */
static void v2_keywords_are_unknown(void)
{
	struct proto_msg clock, expire, confirm;

	PARSE(clock, "CLOCK RUN");
	CHECK(clock.type == PROTO_UNKNOWN, "CLOCK is not a v3.0 keyword, got %d",
	      clock.type);

	PARSE(expire, "EXPIRE");
	CHECK(expire.type == PROTO_UNKNOWN, "EXPIRE is not a v3.0 keyword, got %d",
	      expire.type);

	PARSE(confirm, "CONFIRM 17");
	CHECK(confirm.type == PROTO_UNKNOWN, "CONFIRM is not a v3.0 keyword, got %d",
	      confirm.type);
}

/*
 * §5.7: ECHO replies with the text verbatim, runs of spaces included. The
 * emulator reconstructs it with args.join(' ') and collapses them; the
 * firmware has the raw line and does not have to. That difference is expected
 * in the wire-log diff and is recorded in BUILD_SPEC §5.7.
 */
static void echo_is_verbatim(void)
{
	struct proto_msg m, runs, empty;

	PARSE(m, "ECHO hello world");
	CHECK(m.type == PROTO_ECHO && strcmp(m.text.text, "hello world") == 0,
	      "got \"%s\"", m.type == PROTO_ECHO ? m.text.text : "(not ECHO)");

	PARSE(runs, "ECHO a  b");
	CHECK(runs.type == PROTO_ECHO && strcmp(runs.text.text, "a  b") == 0,
	      "runs of spaces must survive: \"%s\"",
	      runs.type == PROTO_ECHO ? runs.text.text : "(not ECHO)");

	PARSE(empty, "ECHO");
	CHECK(empty.type == PROTO_INVALID, "ECHO with no text");
}

static void extra_whitespace_tolerated(void)
{
	struct proto_msg m;

	/* The app filters empty tokens before dispatching, so we must too. */
	PARSE(m, "  ACK   17  ");
	CHECK(m.type == PROTO_ACK && m.ack.seq == 17u,
	      "runs of spaces should parse like a single separator, got %d", m.type);
}

/* ------------------------------------------------------------------------ */
/* Encoders                                                                  */
/* ------------------------------------------------------------------------ */

static void encoders_roundtrip(void)
{
	char buf[PROTO_MAX_LINE];
	struct proto_msg m;
	int n;

	n = proto_enc_hello(buf, sizeof(buf), "0.2.0", "RR-0147", 0u);
	CHECK(n > 0 && strcmp(buf, "HELLO 3.0 0.2.0 RR-0147 0") == 0,
	      "HELLO: \"%s\"", buf);
	proto_parse(buf, &m);
	CHECK(m.type == PROTO_HELLO, "HELLO must round-trip");

	n = proto_enc_evt(buf, sizeof(buf), PROTO_BTN_ADD_POINT, PROTO_GEST_PRESS,
			  PROTO_REMOTE_RED, 17u);
	CHECK(n > 0 && strcmp(buf, "EVT ADD_POINT PRESS RED 17") == 0,
	      "EVT: \"%s\"", buf);

	n = proto_enc_evt(buf, sizeof(buf), PROTO_BTN_BACKWARD, PROTO_GEST_HOLD_REP,
			  PROTO_REMOTE_GREEN, 65535u);
	CHECK(n > 0 && strcmp(buf, "EVT BACKWARD HOLD_REP GREEN 65535") == 0,
	      "EVT: \"%s\"", buf);

	n = proto_enc_join(buf, sizeof(buf), PROTO_REMOTE_GREEN);
	CHECK(n > 0 && strcmp(buf, "JOIN GREEN") == 0, "JOIN: \"%s\"", buf);

	n = proto_enc_pong(buf, sizeof(buf));
	CHECK(n > 0 && strcmp(buf, "PONG") == 0, "PONG: \"%s\"", buf);

	n = proto_enc_err(buf, sizeof(buf), "APP_TIMEOUT");
	CHECK(n > 0 && strcmp(buf, "ERR APP_TIMEOUT") == 0, "ERR: \"%s\"", buf);
}

/* Every EVT the dongle can emit must survive its own parser. The button and
 * gesture name tables are the one place a typo produces a line the app quietly
 * ignores rather than an error anybody sees. */
static void every_evt_roundtrips(void)
{
	char buf[PROTO_MAX_LINE];

	/* The loop counters are enum-typed, not int: gcc picks an unsigned
	 * underlying type for these enums, so an int counter compared against
	 * m.evt.button trips -Wsign-compare under -Werror. */
	for (enum proto_button b = 0; b < PROTO_BTN_COUNT; b++) {
		for (enum proto_gesture g = 0; g < PROTO_GEST_COUNT; g++) {
			struct proto_msg m;

			int n = proto_enc_evt(buf, sizeof(buf), b, g,
					      PROTO_REMOTE_RED, 1u);
			CHECK(n > 0, "encode failed for button %d gesture %d",
			      (int)b, (int)g);

			proto_parse(buf, &m);
			CHECK(m.type == PROTO_EVT && m.evt.button == b && m.evt.gesture == g,
			      "round-trip failed for button %d gesture %d",
			      (int)b, (int)g);
		}
	}
}

static void link_encoding(void)
{
	char buf[PROTO_MAX_LINE];

	(void)proto_enc_link(buf, sizeof(buf), PROTO_REMOTE_RED,
			     PROTO_LINK_CONNECTED, -52, 87, true);
	CHECK(strcmp(buf, "LINK RED CONNECTED -52 87") == 0, "LINK: \"%s\"", buf);

	(void)proto_enc_link(buf, sizeof(buf), PROTO_REMOTE_GREEN,
			     PROTO_LINK_DISCONNECTED, 0, 0, false);
	CHECK(strcmp(buf, "LINK GREEN DISCONNECTED") == 0, "LINK: \"%s\"", buf);

	/* rssi with no battery is legal — telemetry may not have arrived yet. */
	(void)proto_enc_link(buf, sizeof(buf), PROTO_REMOTE_RED,
			     PROTO_LINK_CONNECTED, -52, 0, false);
	CHECK(strcmp(buf, "LINK RED CONNECTED -52") == 0, "LINK: \"%s\"", buf);
}

static void seq_wraps_at_65536(void)
{
	CHECK(proto_next_seq(0u) == 1u, "0 -> 1");
	CHECK(proto_next_seq(999u) == 1000u, "999 -> 1000, not 0 (the v2.0 modulo)");
	CHECK(proto_next_seq(65534u) == 65535u, "65534 -> 65535");
	CHECK(proto_next_seq(65535u) == 0u, "65535 must wrap to 0");
}

static void oversized_encode_rejected(void)
{
	char buf[PROTO_MAX_LINE];
	char huge[200];

	memset(huge, 'z', sizeof(huge) - 1);
	huge[sizeof(huge) - 1] = '\0';

	CHECK(proto_enc_log(buf, sizeof(buf), huge) == -1,
	      "an over-long line must be refused, not truncated");
}

/* ------------------------------------------------------------------------ */

int main(void)
{
	struct {
		const char *name;
		void (*fn)(void);
	} tests[] = {
		{ "T1  plain line",              t1_plain_line },
		{ "T2  CRLF stripped",           t2_crlf_stripped },
		{ "T3  one byte per read",       t3_one_byte_per_read },
		{ "T4  three lines, one read",   t4_three_lines_one_read },
		{ "T5  split mid-token",         t5_split_mid_token },
		{ "T6  unknown keyword",         t6_unknown_keyword },
		{ "T7  v2.0 EVT shape rejected", t7_v2_shape_rejected },
		{ "T8  bad token rejected",      t8_bad_seq },
		{ "T9  overlong discarded",      t9_overlong_discarded },
		{ "T9c discard spans chunks",    t9c_discard_survives_chunk_boundary },
		{ "T10 empty lines ignored",     t10_empty_lines_ignored },
		{ "T11 seq range 0-65535",       t11_seq_range },
		{ "T12 STATE parsed",            t12_state_parsed },
		{ "T13 STATE hex is strict",     t13_state_hex_strict },
		{ "T14 LINK requires rssi",      t14_link_requires_rssi },
		{ "T15 unknown waveform",        t15_unknown_waveform },
		{ "T16 parser is stateless",     t16_parser_is_stateless },
		{ "ACK and SILENT",              ack_and_silent },
		{ "CFG parsed",                  cfg_parsed },
		{ "SIMSOC parsed",               simsoc_parsed },
		{ "simple messages",             simple_messages },
		{ "v2.0 keywords are unknown",   v2_keywords_are_unknown },
		{ "ECHO is verbatim",            echo_is_verbatim },
		{ "extra whitespace",            extra_whitespace_tolerated },
		{ "encoder round-trips",         encoders_roundtrip },
		{ "every EVT round-trips",       every_evt_roundtrips },
		{ "LINK encoding",               link_encoding },
		{ "seq wraps at 65536",          seq_wraps_at_65536 },
		{ "oversized encode rejected",   oversized_encode_rejected },
	};

	for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		int before = failures;

		tests[i].fn();
		printf("%s %s\n", failures == before ? "ok  " : "FAIL", tests[i].name);
	}

	printf("\n%d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
