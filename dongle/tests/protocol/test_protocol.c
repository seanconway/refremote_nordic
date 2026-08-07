/*
 * Host unit tests for protocol.c — PROTOCOL.md §10, plus the encoder
 * round-trips and the two places where the scoreboard app parses more
 * strictly than the prose spec reads.
 *
 * Deliberately framework-free and Zephyr-free: `make && ./test_protocol`.
 *
 * T3, T4 and T5 are the ones that matter. Chunk boundaries are the only real
 * hazard left on this link, and they are the reason the line assembler exists.
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
/* §10 — assembler test cases                                                */
/* ------------------------------------------------------------------------ */

static void t1_plain_line(void)
{
	struct proto_asm a;
	struct collector c = { 0 };

	proto_asm_init(&a);
	feed_str(&a, "EVT ADD_POINT RED 17\n", &c);

	CHECK(c.count == 1, "expected 1 line, got %zu", c.count);
	CHECK(strcmp(c.lines[0], "EVT ADD_POINT RED 17") == 0,
	      "got \"%s\"", c.lines[0]);
}

static void t2_crlf_stripped(void)
{
	struct proto_asm a;
	struct collector c = { 0 };

	proto_asm_init(&a);
	feed_str(&a, "EVT ADD_POINT RED 17\r\n", &c);

	CHECK(c.count == 1, "expected 1 line, got %zu", c.count);
	CHECK(strcmp(c.lines[0], "EVT ADD_POINT RED 17") == 0,
	      "trailing \\r not stripped: \"%s\"", c.lines[0]);
}

static void t3_one_byte_per_read(void)
{
	struct proto_asm a;
	struct collector c = { 0 };

	proto_asm_init(&a);
	feed_str_bytewise(&a, "EVT ADD_POINT RED 17\n", &c);

	CHECK(c.count == 1, "expected 1 line, got %zu", c.count);
	CHECK(strcmp(c.lines[0], "EVT ADD_POINT RED 17") == 0,
	      "got \"%s\"", c.lines[0]);
}

static void t4_three_lines_one_read(void)
{
	struct proto_asm a;
	struct collector c = { 0 };

	proto_asm_init(&a);
	feed_str(&a,
		 "EVT ADD_POINT RED 17\n"
		 "EVT TOGGLE_TIMER GREEN 18\n"
		 "EVT PERIOD_UP RED 19\n",
		 &c);

	CHECK(c.count == 3, "expected 3 lines, got %zu", c.count);
	CHECK(strcmp(c.lines[0], "EVT ADD_POINT RED 17") == 0, "line 0 wrong");
	CHECK(strcmp(c.lines[1], "EVT TOGGLE_TIMER GREEN 18") == 0, "line 1 wrong");
	CHECK(strcmp(c.lines[2], "EVT PERIOD_UP RED 19") == 0, "line 2 wrong");
}

static void t5_split_mid_token(void)
{
	struct proto_asm a;
	struct collector c = { 0 };

	proto_asm_init(&a);
	feed_str(&a, "EVT ADD_POI", &c);
	CHECK(c.count == 0, "emitted a line before its terminator");
	feed_str(&a, "NT RED 17\n", &c);

	CHECK(c.count == 1, "expected 1 line, got %zu", c.count);
	CHECK(strcmp(c.lines[0], "EVT ADD_POINT RED 17") == 0,
	      "got \"%s\"", c.lines[0]);
}

static void t6_unknown_keyword(void)
{
	struct proto_msg m;
	char line[] = "BOGUS FOO BAR";

	proto_parse(line, &m);
	CHECK(m.type == PROTO_UNKNOWN, "expected UNKNOWN, got %d", m.type);
}

static void t7_missing_args(void)
{
	struct proto_msg m;
	char line[] = "EVT ADD_POINT";

	proto_parse(line, &m);
	CHECK(m.type == PROTO_INVALID, "expected INVALID, got %d", m.type);
}

static void t8_bad_seq(void)
{
	struct proto_msg m;
	char line[] = "EVT ADD_POINT RED xyz";

	proto_parse(line, &m);
	CHECK(m.type == PROTO_INVALID, "expected INVALID, got %d", m.type);
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
	feed_str(&a, "\nEVT ADD_POINT RED 17\n", &c);

	CHECK(c.count == 1, "expected 1 line after resync, got %zu", c.count);
	CHECK(c.count == 1 && strcmp(c.lines[0], "EVT ADD_POINT RED 17") == 0,
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

	feed_str(&a, "TAIL\nEVT ADD_POINT RED 17\n", &c);

	CHECK(c.count == 1, "expected 1 line, got %zu", c.count);
	CHECK(c.count == 1 && strcmp(c.lines[0], "EVT ADD_POINT RED 17") == 0,
	      "junk tail leaked as a line: \"%s\"", c.count ? c.lines[0] : "(none)");
}

static void t10_empty_lines_ignored(void)
{
	struct proto_asm a;
	struct collector c = { 0 };

	proto_asm_init(&a);
	feed_str(&a, "\n\n\nEVT ADD_POINT RED 17\n", &c);

	CHECK(c.count == 1, "expected 1 line, got %zu", c.count);
	CHECK(strcmp(c.lines[0], "EVT ADD_POINT RED 17") == 0,
	      "got \"%s\"", c.lines[0]);
}

/* ------------------------------------------------------------------------ */
/* App -> Dongle parsing (what the firmware actually consumes)               */
/* ------------------------------------------------------------------------ */

static void app_to_dongle_messages(void)
{
	struct proto_msg m;

	char clock_run[] = "CLOCK RUN";
	proto_parse(clock_run, &m);
	CHECK(m.type == PROTO_CLOCK && m.clock.run, "CLOCK RUN");

	char clock_stop[] = "CLOCK STOP";
	proto_parse(clock_stop, &m);
	CHECK(m.type == PROTO_CLOCK && !m.clock.run, "CLOCK STOP");

	char clock_bad[] = "CLOCK SIDEWAYS";
	proto_parse(clock_bad, &m);
	CHECK(m.type == PROTO_INVALID, "CLOCK with bad mode");

	char ping[] = "PING";
	proto_parse(ping, &m);
	CHECK(m.type == PROTO_PING, "PING");

	char ping_args[] = "PING extra";
	proto_parse(ping_args, &m);
	CHECK(m.type == PROTO_INVALID, "PING with args must be rejected");

	char info[] = "INFO";
	proto_parse(info, &m);
	CHECK(m.type == PROTO_INFO, "INFO");

	char expire[] = "EXPIRE";
	proto_parse(expire, &m);
	CHECK(m.type == PROTO_EXPIRE, "EXPIRE");

	char confirm[] = "CONFIRM 17";
	proto_parse(confirm, &m);
	CHECK(m.type == PROTO_CONFIRM && m.confirm.seq == 17, "CONFIRM 17");

	char confirm_big[] = "CONFIRM 1000";
	proto_parse(confirm_big, &m);
	CHECK(m.type == PROTO_INVALID, "CONFIRM seq must be 0-999");

	char echo[] = "ECHO hello world";
	proto_parse(echo, &m);
	CHECK(m.type == PROTO_ECHO && strcmp(m.text.text, "hello world") == 0,
	      "ECHO should keep its text intact");

	char echo_empty[] = "ECHO";
	proto_parse(echo_empty, &m);
	CHECK(m.type == PROTO_INVALID, "ECHO with no text");

	char test3[] = "TEST 3";
	proto_parse(test3, &m);
	CHECK(m.type == PROTO_TEST && m.test.mode == 3u, "TEST 3");
}

static void extra_whitespace_tolerated(void)
{
	struct proto_msg m;
	char line[] = "  CLOCK   RUN  ";

	/* The app filters empty tokens before dispatching, so we must too. */
	proto_parse(line, &m);
	CHECK(m.type == PROTO_CLOCK && m.clock.run,
	      "runs of spaces should parse like a single separator");
}

/* ------------------------------------------------------------------------ */
/* Encoders, and the app's stricter-than-spec expectations                   */
/* ------------------------------------------------------------------------ */

static void encoders_roundtrip(void)
{
	char buf[PROTO_MAX_LINE];
	struct proto_msg m;
	int n;

	n = proto_enc_hello(buf, sizeof(buf), "0.1.0", 0u);
	CHECK(n > 0 && strcmp(buf, "HELLO 2.0 0.1.0 0") == 0, "HELLO: \"%s\"", buf);
	proto_parse(buf, &m);
	CHECK(m.type == PROTO_HELLO, "HELLO must round-trip");

	n = proto_enc_evt(buf, sizeof(buf), PROTO_ACT_ADD_POINT,
			  PROTO_REMOTE_RED, 17u);
	CHECK(n > 0 && strcmp(buf, "EVT ADD_POINT RED 17") == 0, "EVT: \"%s\"", buf);

	n = proto_enc_pong(buf, sizeof(buf));
	CHECK(n > 0 && strcmp(buf, "PONG") == 0, "PONG: \"%s\"", buf);

	n = proto_enc_err(buf, sizeof(buf), "APP_TIMEOUT");
	CHECK(n > 0 && strcmp(buf, "ERR APP_TIMEOUT") == 0, "ERR: \"%s\"", buf);
}

static void link_encoding_matches_app_parser(void)
{
	char buf[PROTO_MAX_LINE];
	struct proto_msg m;

	/*
	 * The app REQUIRES rssi whenever the state is CONNECTED — it rejects
	 * "LINK RED CONNECTED" outright (protocol.js parseLink), even though
	 * §3.2 reads as though rssi is optional. Firmware must never emit the
	 * short form for a connected remote.
	 */
	(void)proto_enc_link(buf, sizeof(buf), PROTO_REMOTE_RED,
			     PROTO_LINK_CONNECTED, -52, 87, true);
	CHECK(strcmp(buf, "LINK RED CONNECTED -52 87") == 0, "LINK: \"%s\"", buf);

	(void)proto_enc_link(buf, sizeof(buf), PROTO_REMOTE_GREEN,
			     PROTO_LINK_DISCONNECTED, 0, 0, false);
	CHECK(strcmp(buf, "LINK GREEN DISCONNECTED") == 0, "LINK: \"%s\"", buf);

	/* rssi with no battery is legal. */
	(void)proto_enc_link(buf, sizeof(buf), PROTO_REMOTE_RED,
			     PROTO_LINK_CONNECTED, -52, 0, false);
	CHECK(strcmp(buf, "LINK RED CONNECTED -52") == 0, "LINK: \"%s\"", buf);

	/* The short CONNECTED form the app rejects must also fail our parser,
	 * so this stays a test failure rather than a field mystery. */
	char short_form[] = "LINK RED CONNECTED";
	proto_parse(short_form, &m);
	CHECK(m.type == PROTO_INVALID,
	      "LINK CONNECTED without rssi must be rejected, as the app does");
}

static void seq_wraps_at_1000(void)
{
	CHECK(proto_next_seq(0u) == 1u, "0 -> 1");
	CHECK(proto_next_seq(998u) == 999u, "998 -> 999");
	CHECK(proto_next_seq(999u) == 0u, "999 must wrap to 0");
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
		{ "T7  missing args",            t7_missing_args },
		{ "T8  bad seq",                 t8_bad_seq },
		{ "T9  overlong discarded",      t9_overlong_discarded },
		{ "T9c discard spans chunks",    t9c_discard_survives_chunk_boundary },
		{ "T10 empty lines ignored",     t10_empty_lines_ignored },
		{ "app->dongle messages",        app_to_dongle_messages },
		{ "extra whitespace",            extra_whitespace_tolerated },
		{ "encoder round-trips",         encoders_roundtrip },
		{ "LINK matches app parser",     link_encoding_matches_app_parser },
		{ "seq wraps at 1000",           seq_wraps_at_1000 },
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
