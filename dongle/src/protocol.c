/*
 * Wire protocol implementation, PROTOCOL.md v3.0. See protocol.h for the
 * no-Zephyr-dependencies rule — only the C standard library may be included
 * here.
 */
#include "protocol.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Name tables                                                               */
/*                                                                           */
/* These are the wire spelling of every enum, and they are the one place a    */
/* typo produces a line the app silently ignores rather than an error anybody */
/* sees. The host suite round-trips all of them for exactly that reason.      */
/* ------------------------------------------------------------------------ */

static const char *const button_names[PROTO_BTN_COUNT] = {
	[PROTO_BTN_ADD_POINT]    = "ADD_POINT",
	[PROTO_BTN_TOGGLE_CLOCK] = "TOGGLE_CLOCK",
	[PROTO_BTN_REMOVE_POINT] = "REMOVE_POINT",
	[PROTO_BTN_FORWARD]      = "FORWARD",
	[PROTO_BTN_BACKWARD]     = "BACKWARD",
	[PROTO_BTN_F1]           = "F1",
	[PROTO_BTN_F2]           = "F2",
};

static const char *const gesture_names[PROTO_GEST_COUNT] = {
	[PROTO_GEST_PRESS]    = "PRESS",
	[PROTO_GEST_HOLD]     = "HOLD",
	[PROTO_GEST_HOLD_REP] = "HOLD_REP",
};

static const char *const remote_names[PROTO_REMOTE_COUNT] = {
	[PROTO_REMOTE_RED]   = "RED",
	[PROTO_REMOTE_GREEN] = "GREEN",
};

static const char *const target_names[PROTO_TGT_COUNT] = {
	[PROTO_TGT_RED]   = "RED",
	[PROTO_TGT_GREEN] = "GREEN",
	[PROTO_TGT_BOTH]  = "BOTH",
};

static const char *const waveform_names[PROTO_WF_COUNT] = {
	[PROTO_WF_TAP]    = "TAP",
	[PROTO_WF_BEAT]   = "BEAT",
	[PROTO_WF_WARN]   = "WARN",
	[PROTO_WF_BUZZ]   = "BUZZ",
	[PROTO_WF_LONG]   = "LONG",
	[PROTO_WF_DOUBLE] = "DOUBLE",
	[PROTO_WF_TRIPLE] = "TRIPLE",
};

static const char *const ind_mode_names[PROTO_IND_MODE_COUNT] = {
	[PROTO_IND_OFF]   = "OFF",
	[PROTO_IND_SOLID] = "SOLID",
};

static const char *const ind_colour_names[PROTO_COLOUR_COUNT] = {
	[PROTO_COLOUR_RED]    = "RED",
	[PROTO_COLOUR_GREEN]  = "GREEN",
	[PROTO_COLOUR_BLUE]   = "BLUE",
	[PROTO_COLOUR_YELLOW] = "YELLOW",
};

static const char *const link_state_names[PROTO_LINK_STATE_COUNT] = {
	[PROTO_LINK_CONNECTED]    = "CONNECTED",
	[PROTO_LINK_CONNECTING]   = "CONNECTING",
	[PROTO_LINK_DISCONNECTED] = "DISCONNECTED",
};

const char *proto_button_name(enum proto_button b)
{
	return (b < PROTO_BTN_COUNT) ? button_names[b] : "?";
}

const char *proto_gesture_name(enum proto_gesture g)
{
	return (g < PROTO_GEST_COUNT) ? gesture_names[g] : "?";
}

const char *proto_remote_name(enum proto_remote r)
{
	return (r < PROTO_REMOTE_COUNT) ? remote_names[r] : "?";
}

const char *proto_target_name(enum proto_target t)
{
	return (t < PROTO_TGT_COUNT) ? target_names[t] : "?";
}

const char *proto_waveform_name(enum proto_waveform w)
{
	return (w < PROTO_WF_COUNT) ? waveform_names[w] : "?";
}

const char *proto_ind_mode_name(enum proto_ind_mode m)
{
	return (m < PROTO_IND_MODE_COUNT) ? ind_mode_names[m] : "?";
}

const char *proto_ind_colour_name(enum proto_ind_colour c)
{
	return (c < PROTO_COLOUR_COUNT) ? ind_colour_names[c] : "?";
}

const char *proto_link_state_name(enum proto_link_state s)
{
	return (s < PROTO_LINK_STATE_COUNT) ? link_state_names[s] : "?";
}

uint16_t proto_next_seq(uint16_t seq)
{
	/* Natural uint16_t wrap: 65535 -> 0. v2.0's `% 1000` is gone with the
	 * narrow range it enforced (§5.3). */
	return (uint16_t)(seq + 1u);
}

/* ------------------------------------------------------------------------ */
/* Line assembler                                                            */
/*                                                                           */
/* Unchanged from v2.0 and deliberately so: framing was the one part of the   */
/* v1.0 design that earned its place, §15 lists it as retained, and it is     */
/* proven on hardware. Do not touch it while changing the layers above.       */
/* ------------------------------------------------------------------------ */

void proto_asm_init(struct proto_asm *a)
{
	a->len = 0;
	a->discarding = false;
}

void proto_asm_feed(struct proto_asm *a, const uint8_t *data, size_t len,
		    proto_line_fn cb, void *user)
{
	for (size_t i = 0; i < len; i++) {
		char ch = (char)data[i];

		if (a->discarding) {
			/* Resynchronise at the next terminator (§2.2). */
			if (ch == '\n') {
				a->discarding = false;
				a->len = 0;
			}
			continue;
		}

		if (ch == '\n') {
			size_t n = a->len;

			a->len = 0;
			/* Receivers must tolerate and strip a preceding \r (§2.1). */
			if (n > 0 && a->buf[n - 1] == '\r') {
				n--;
			}
			/* Empty lines are ignored (§2.1). */
			if (n > 0 && n <= PROTO_MAX_CONTENT) {
				a->buf[n] = '\0';
				if (cb != NULL) {
					cb(a->buf, user);
				}
			}
			continue;
		}

		a->buf[a->len++] = ch;
		if (a->len > PROTO_MAX_CONTENT) {
			/* Overlong without a terminator: discard and resync. */
			a->len = 0;
			a->discarding = true;
		}
	}
}

/* ------------------------------------------------------------------------ */
/* Parsing helpers                                                           */
/* ------------------------------------------------------------------------ */

/*
 * Splits on runs of spaces, ignoring empty tokens. Writes NULs in place.
 * Returns the total token count, which may exceed `max`; only the first
 * `max` pointers are stored, so callers must check the count before indexing.
 *
 * That the count is *true* rather than clamped is what makes an exact-count
 * check (`!= 4`) safe against a long line — see parse_evt and §14 T7.
 */
static size_t split_tokens(char *s, char *tok[], size_t max)
{
	size_t n = 0;

	while (*s != '\0') {
		while (*s == ' ') {
			s++;
		}
		if (*s == '\0') {
			break;
		}
		if (n < max) {
			tok[n] = s;
		}
		n++;
		while (*s != '\0' && *s != ' ') {
			s++;
		}
		if (*s == ' ') {
			*s = '\0';
			s++;
		}
	}

	return n;
}

/* Accepts only /^\d+$/ — matches the app's isDecimalInt(). */
static bool parse_uint(const char *s, uint32_t *out)
{
	uint32_t v = 0;

	if (*s == '\0') {
		return false;
	}
	for (const char *p = s; *p != '\0'; p++) {
		if (*p < '0' || *p > '9') {
			return false;
		}
		if (v > (UINT32_MAX - (uint32_t)(*p - '0')) / 10u) {
			return false;
		}
		v = v * 10u + (uint32_t)(*p - '0');
	}
	*out = v;
	return true;
}

/* Accepts only /^-?\d+$/ — matches the app's rssi check. */
static bool parse_int(const char *s, int32_t *out)
{
	bool neg = false;
	uint32_t v;

	if (*s == '-') {
		neg = true;
		s++;
	}
	if (!parse_uint(s, &v) || v > (uint32_t)INT32_MAX) {
		return false;
	}
	*out = neg ? -(int32_t)v : (int32_t)v;
	return true;
}

/*
 * §5.3: parse wide, range-check, then narrow.
 *
 * With seq living in a uint16_t the v2.0 test `seq >= PROTO_SEQ_MODULO` is
 * vacuous — every representable value is in range — so 65536 would arrive as 0
 * and be acted on as a legitimate acknowledgement of a different event. The
 * range check has to happen while the value is still wide. T11 pins it.
 */
static bool parse_seq(const char *s, uint16_t *out)
{
	uint32_t v;

	if (!parse_uint(s, &v) || v > PROTO_SEQ_MAX) {
		return false;
	}
	*out = (uint16_t)v;
	return true;
}

/* 0-100, the range CFG uses for both of its scale factors (§6.4). */
static bool parse_pct(const char *s, uint8_t *out)
{
	uint32_t v;

	if (!parse_uint(s, &v) || v > 100u) {
		return false;
	}
	*out = (uint8_t)v;
	return true;
}

static bool lookup_name(const char *const *table, size_t count,
			const char *s, uint32_t *idx)
{
	for (size_t i = 0; i < count; i++) {
		if (table[i] != NULL && strcmp(table[i], s) == 0) {
			*idx = (uint32_t)i;
			return true;
		}
	}
	return false;
}

static void invalid(struct proto_msg *out, const char *reason)
{
	out->type = PROTO_INVALID;
	out->invalid_reason = reason;
}

/* ------------------------------------------------------------------------ */
/* App -> Dongle                                                             */
/* ------------------------------------------------------------------------ */

/* ACK <seq> [SILENT] — §5.3, §5.2 */
static void parse_ack(char *rest, struct proto_msg *out)
{
	char *tok[PROTO_MAX_TOKENS];
	size_t n = split_tokens(rest, tok, PROTO_MAX_TOKENS);
	uint16_t seq;

	if (n != 1u && n != 2u) {
		invalid(out, "wrong arg count");
		return;
	}
	if (!parse_seq(tok[0], &seq)) {
		invalid(out, "bad seq");
		return;
	}
	/* Only SILENT is legal here. Anything else is refused rather than
	 * treated as a plain ACK: guessing would fire a tap on an inert button,
	 * which FS §5.6 says is worse than silence. */
	if (n == 2u && strcmp(tok[1], "SILENT") != 0) {
		invalid(out, "bad flag");
		return;
	}

	out->type = PROTO_ACK;
	out->ack.seq = seq;
	out->ack.silent = (n == 2u);
}

/* STATE <remote> <f1> <f1colour> <f2> <f2colour> — §6 */
static void parse_state(char *rest, struct proto_msg *out)
{
	char *tok[PROTO_MAX_TOKENS];
	uint32_t remote, f1, f1c, f2, f2c;

	if (split_tokens(rest, tok, PROTO_MAX_TOKENS) != 5u) {
		invalid(out, "wrong arg count");
		return;
	}
	if (!lookup_name(remote_names, PROTO_REMOTE_COUNT, tok[0], &remote)) {
		invalid(out, "bad remote");
		return;
	}
	if (!lookup_name(ind_mode_names, PROTO_IND_MODE_COUNT, tok[1], &f1)) {
		invalid(out, "bad f1 mode");
		return;
	}
	if (!lookup_name(ind_colour_names, PROTO_COLOUR_COUNT, tok[2], &f1c)) {
		invalid(out, "bad f1 colour");
		return;
	}
	if (!lookup_name(ind_mode_names, PROTO_IND_MODE_COUNT, tok[3], &f2)) {
		invalid(out, "bad f2 mode");
		return;
	}
	if (!lookup_name(ind_colour_names, PROTO_COLOUR_COUNT, tok[4], &f2c)) {
		invalid(out, "bad f2 colour");
		return;
	}

	out->type = PROTO_STATE;
	out->state.remote = (enum proto_remote)remote;
	out->state.f1_mode = (enum proto_ind_mode)f1;
	out->state.f1_colour = (enum proto_ind_colour)f1c;
	out->state.f2_mode = (enum proto_ind_mode)f2;
	out->state.f2_colour = (enum proto_ind_colour)f2c;
}

/* HAP <target> <waveform> — §9 */
static void parse_hap(char *rest, struct proto_msg *out)
{
	char *tok[PROTO_MAX_TOKENS];
	uint32_t target, waveform;

	if (split_tokens(rest, tok, PROTO_MAX_TOKENS) != 2u) {
		invalid(out, "wrong arg count");
		return;
	}
	if (!lookup_name(target_names, PROTO_TGT_COUNT, tok[0], &target)) {
		invalid(out, "bad target");
		return;
	}
	if (!lookup_name(waveform_names, PROTO_WF_COUNT, tok[1], &waveform)) {
		invalid(out, "bad waveform");
		return;
	}

	out->type = PROTO_HAP;
	out->hap.target = (enum proto_target)target;
	out->hap.waveform = (enum proto_waveform)waveform;
}

/* CFG <target> <haptic> <bright> — §6.4 */
static void parse_cfg(char *rest, struct proto_msg *out)
{
	char *tok[PROTO_MAX_TOKENS];
	uint32_t target;
	uint8_t haptic, bright;

	if (split_tokens(rest, tok, PROTO_MAX_TOKENS) != 3u) {
		invalid(out, "wrong arg count");
		return;
	}
	if (!lookup_name(target_names, PROTO_TGT_COUNT, tok[0], &target)) {
		invalid(out, "bad target");
		return;
	}
	if (!parse_pct(tok[1], &haptic)) {
		invalid(out, "bad haptic");
		return;
	}
	if (!parse_pct(tok[2], &bright)) {
		invalid(out, "bad bright");
		return;
	}

	out->type = PROTO_CFG;
	out->cfg.target = (enum proto_target)target;
	out->cfg.haptic = haptic;
	out->cfg.bright = bright;
}

/* SIMSOC <target> <pct> — §6.5. Bench-only simulated LED_PWR state of charge. */
static void parse_simsoc(char *rest, struct proto_msg *out)
{
	char *tok[PROTO_MAX_TOKENS];
	uint32_t target;
	uint8_t pct;

	if (split_tokens(rest, tok, PROTO_MAX_TOKENS) != 2u) {
		invalid(out, "wrong arg count");
		return;
	}
	if (!lookup_name(target_names, PROTO_TGT_COUNT, tok[0], &target)) {
		invalid(out, "bad target");
		return;
	}
	if (!parse_pct(tok[1], &pct)) {
		invalid(out, "bad pct");
		return;
	}

	out->type = PROTO_SIMSOC;
	out->simsoc.target = (enum proto_target)target;
	out->simsoc.pct = pct;
}

static void parse_test(char *rest, struct proto_msg *out)
{
	char *tok[PROTO_MAX_TOKENS];
	uint32_t mode;

	if (split_tokens(rest, tok, PROTO_MAX_TOKENS) != 1u) {
		invalid(out, "wrong arg count");
		return;
	}
	if (!parse_uint(tok[0], &mode)) {
		invalid(out, "bad mode");
		return;
	}
	out->type = PROTO_TEST;
	out->test.mode = mode;
}

/* ------------------------------------------------------------------------ */
/* Dongle -> App, parsed for encoder round-trips only                        */
/* ------------------------------------------------------------------------ */

/* EVT <button> <gesture> <src> <seq> — §5 */
static void parse_evt(char *rest, struct proto_msg *out)
{
	char *tok[PROTO_MAX_TOKENS];
	uint32_t button, gesture, src;
	uint16_t seq;

	/*
	 * Exactly four. Not `>= 4`.
	 *
	 * `EVT ADD_POINT RED 17` is the v2.0 shape, and it is what a partially
	 * updated dongle or a v2.0 unit in a mixed fleet puts on the wire. Read
	 * as a press it produces a score with no gesture and nothing to tell it
	 * apart from a real one. §14 T7.
	 */
	if (split_tokens(rest, tok, PROTO_MAX_TOKENS) != 4u) {
		invalid(out, "wrong arg count");
		return;
	}
	if (!lookup_name(button_names, PROTO_BTN_COUNT, tok[0], &button)) {
		invalid(out, "bad button");
		return;
	}
	if (!lookup_name(gesture_names, PROTO_GEST_COUNT, tok[1], &gesture)) {
		invalid(out, "bad gesture");
		return;
	}
	if (!lookup_name(remote_names, PROTO_REMOTE_COUNT, tok[2], &src)) {
		invalid(out, "bad src");
		return;
	}
	if (!parse_seq(tok[3], &seq)) {
		invalid(out, "bad seq");
		return;
	}

	out->type = PROTO_EVT;
	out->evt.button = (enum proto_button)button;
	out->evt.gesture = (enum proto_gesture)gesture;
	out->evt.src = (enum proto_remote)src;
	out->evt.seq = seq;
}

/* LINK <remote> <state> [rssi] [batt] — §7 */
static void parse_link(char *rest, struct proto_msg *out)
{
	char *tok[PROTO_MAX_TOKENS];
	size_t n = split_tokens(rest, tok, PROTO_MAX_TOKENS);
	uint32_t remote, state;

	if (n < 2u || n > 4u) {
		invalid(out, "wrong arg count");
		return;
	}
	if (!lookup_name(remote_names, PROTO_REMOTE_COUNT, tok[0], &remote)) {
		invalid(out, "bad remote");
		return;
	}
	if (!lookup_name(link_state_names, PROTO_LINK_STATE_COUNT, tok[1], &state)) {
		invalid(out, "bad state");
		return;
	}

	out->link.remote = (enum proto_remote)remote;
	out->link.state = (enum proto_link_state)state;
	out->link.has_rssi = false;
	out->link.has_batt = false;

	if (out->link.state != PROTO_LINK_CONNECTED) {
		if (n != 2u) {
			invalid(out, "rssi/batt only valid when CONNECTED");
			return;
		}
		out->type = PROTO_LINK;
		return;
	}

	/*
	 * §7 makes rssi mandatory when CONNECTED, and the app's parser enforces
	 * it. Accepting the short form here would let firmware emit a line the
	 * app drops, presenting as a signal indicator that silently never
	 * updates — the worst class of bug in a system whose safety argument is
	 * that failures are visible.
	 */
	if (n < 3u) {
		invalid(out, "missing rssi for CONNECTED");
		return;
	}
	if (!parse_int(tok[2], &out->link.rssi)) {
		invalid(out, "bad rssi");
		return;
	}
	out->link.has_rssi = true;

	if (n == 4u) {
		uint32_t batt;

		if (!parse_uint(tok[3], &batt) || batt > 100u) {
			invalid(out, "bad batt");
			return;
		}
		out->link.batt = (int32_t)batt;
		out->link.has_batt = true;
	}

	out->type = PROTO_LINK;
}

/* HELLO <proto> <fw> <set> <caps> — §4.1 */
static void parse_hello(char *rest, struct proto_msg *out)
{
	char *tok[PROTO_MAX_TOKENS];
	uint32_t caps;

	if (split_tokens(rest, tok, PROTO_MAX_TOKENS) != 4u) {
		invalid(out, "wrong arg count");
		return;
	}
	if (strlen(tok[2]) > PROTO_SET_SERIAL_MAX) {
		invalid(out, "set serial too long");
		return;
	}
	if (!parse_uint(tok[3], &caps)) {
		invalid(out, "bad caps");
		return;
	}

	out->type = PROTO_HELLO;
	out->hello.proto = tok[0];
	out->hello.fw = tok[1];
	out->hello.set = tok[2];
	out->hello.caps = caps;
}

/* JOIN <remote> — §6.3 */
static void parse_join(char *rest, struct proto_msg *out)
{
	char *tok[PROTO_MAX_TOKENS];
	uint32_t remote;

	if (split_tokens(rest, tok, PROTO_MAX_TOKENS) != 1u) {
		invalid(out, "wrong arg count");
		return;
	}
	if (!lookup_name(remote_names, PROTO_REMOTE_COUNT, tok[0], &remote)) {
		invalid(out, "bad remote");
		return;
	}

	out->type = PROTO_JOIN;
	out->join.remote = (enum proto_remote)remote;
}

/* ------------------------------------------------------------------------ */

static void parse_no_args(const char *rest, struct proto_msg *out,
			  enum proto_type type)
{
	if (*rest != '\0') {
		invalid(out, "unexpected args");
		return;
	}
	out->type = type;
}

static void parse_text(const char *raw, struct proto_msg *out,
		       enum proto_type type)
{
	if (*raw == '\0') {
		invalid(out, "missing text");
		return;
	}
	out->type = type;
	out->text.text = raw;
}

void proto_parse(char *line, struct proto_msg *out)
{
	char *kw;
	char *raw;   /* remainder verbatim — ECHO must reply identically (§5.7) */
	char *args;  /* remainder with leading spaces skipped, for tokenising   */

	memset(out, 0, sizeof(*out));
	out->type = PROTO_UNKNOWN;

	while (*line == ' ') {
		line++;
	}
	if (*line == '\0') {
		return;
	}

	kw = line;
	while (*line != '\0' && *line != ' ') {
		line++;
	}
	if (*line == ' ') {
		*line = '\0';
		line++;
	}

	/*
	 * Two views of the same remainder, and the distinction is load-bearing.
	 *
	 * `raw` starts immediately after the single separator space, so ECHO
	 * returns runs of spaces intact. The emulator collapses them with
	 * args.join(' '), which contradicts §3's "identical text" — the firmware
	 * has the raw line and does not have to. BUILD_SPEC §5.7 records this as
	 * an expected difference in the wire-log diff, not a defect.
	 */
	raw = line;
	args = raw;
	while (*args == ' ') {
		args++;
	}

	/* App -> Dongle */
	if (strcmp(kw, "ACK") == 0) {
		parse_ack(args, out);
	} else if (strcmp(kw, "STATE") == 0) {
		parse_state(args, out);
	} else if (strcmp(kw, "HAP") == 0) {
		parse_hap(args, out);
	} else if (strcmp(kw, "CFG") == 0) {
		parse_cfg(args, out);
	} else if (strcmp(kw, "SIMSOC") == 0) {
		parse_simsoc(args, out);
	} else if (strcmp(kw, "PING") == 0) {
		parse_no_args(args, out, PROTO_PING);
	} else if (strcmp(kw, "INFO") == 0) {
		parse_no_args(args, out, PROTO_INFO);
	} else if (strcmp(kw, "ECHO") == 0) {
		parse_text(raw, out, PROTO_ECHO);
	} else if (strcmp(kw, "TEST") == 0) {
		parse_test(args, out);
	/* Dongle -> App, parsed for round-trip tests only */
	} else if (strcmp(kw, "HELLO") == 0) {
		parse_hello(args, out);
	} else if (strcmp(kw, "EVT") == 0) {
		parse_evt(args, out);
	} else if (strcmp(kw, "LINK") == 0) {
		parse_link(args, out);
	} else if (strcmp(kw, "JOIN") == 0) {
		parse_join(args, out);
	} else if (strcmp(kw, "PONG") == 0) {
		parse_no_args(args, out, PROTO_PONG);
	} else if (strcmp(kw, "LOG") == 0) {
		parse_text(raw, out, PROTO_LOG);
	} else if (strcmp(kw, "ERR") == 0) {
		parse_text(raw, out, PROTO_ERR);
	}
	/*
	 * else: unknown keyword -> PROTO_UNKNOWN, ignore silently (§2.2). This
	 * is what lets v3.1 add messages without breaking a v3.0 peer, and it is
	 * also where v2.0's CLOCK, EXPIRE and CONFIRM now land — deleted rather
	 * than deprecated, because a dongle that still answered CLOCK would hold
	 * a clock FS §6.2 forbids it to have.
	 */
}

/* ------------------------------------------------------------------------ */
/* Encoding                                                                  */
/* ------------------------------------------------------------------------ */

static int enc_result(size_t cap, int n)
{
	if (n < 0 || (size_t)n >= cap || (size_t)n > PROTO_MAX_CONTENT) {
		return -1;
	}
	return n;
}

int proto_enc_hello(char *out, size_t cap, const char *fw, const char *set,
		    uint32_t caps)
{
	return enc_result(cap, snprintf(out, cap, "HELLO %s %s %s %u",
					PROTO_VERSION, fw, set,
					(unsigned int)caps));
}

int proto_enc_evt(char *out, size_t cap, enum proto_button b,
		  enum proto_gesture g, enum proto_remote src, uint16_t seq)
{
	return enc_result(cap, snprintf(out, cap, "EVT %s %s %s %u",
					proto_button_name(b),
					proto_gesture_name(g),
					proto_remote_name(src),
					(unsigned int)seq));
}

int proto_enc_link(char *out, size_t cap, enum proto_remote r,
		   enum proto_link_state st, int32_t rssi,
		   int32_t batt, bool has_batt)
{
	const char *rn = proto_remote_name(r);
	const char *sn = proto_link_state_name(st);

	if (st != PROTO_LINK_CONNECTED) {
		return enc_result(cap, snprintf(out, cap, "LINK %s %s", rn, sn));
	}
	/* rssi is never omitted when CONNECTED — see parse_link. */
	if (!has_batt) {
		return enc_result(cap, snprintf(out, cap, "LINK %s %s %d",
						rn, sn, (int)rssi));
	}
	return enc_result(cap, snprintf(out, cap, "LINK %s %s %d %d",
					rn, sn, (int)rssi, (int)batt));
}

int proto_enc_join(char *out, size_t cap, enum proto_remote r)
{
	return enc_result(cap, snprintf(out, cap, "JOIN %s",
					proto_remote_name(r)));
}

int proto_enc_pong(char *out, size_t cap)
{
	return enc_result(cap, snprintf(out, cap, "PONG"));
}

int proto_enc_echo(char *out, size_t cap, const char *text)
{
	return enc_result(cap, snprintf(out, cap, "ECHO %s", text));
}

int proto_enc_log(char *out, size_t cap, const char *text)
{
	return enc_result(cap, snprintf(out, cap, "LOG %s", text));
}

int proto_enc_err(char *out, size_t cap, const char *text)
{
	return enc_result(cap, snprintf(out, cap, "ERR %s", text));
}
