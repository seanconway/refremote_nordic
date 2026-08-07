/*
 * Wire protocol implementation. See protocol.h for the no-Zephyr-dependencies
 * rule — only the C standard library may be included here.
 */
#include "protocol.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Name tables                                                               */
/* ------------------------------------------------------------------------ */

static const char *const action_names[PROTO_ACT_COUNT] = {
	[PROTO_ACT_TOGGLE_TIMER] = "TOGGLE_TIMER",
	[PROTO_ACT_ADD_POINT]    = "ADD_POINT",
	[PROTO_ACT_REMOVE_POINT] = "REMOVE_POINT",
	[PROTO_ACT_TIME_UP]      = "TIME_UP",
	[PROTO_ACT_TIME_DOWN]    = "TIME_DOWN",
	[PROTO_ACT_PERIOD_UP]    = "PERIOD_UP",
	[PROTO_ACT_PERIOD_DOWN]  = "PERIOD_DOWN",
};

static const char *const remote_names[PROTO_REMOTE_COUNT] = {
	[PROTO_REMOTE_RED]   = "RED",
	[PROTO_REMOTE_GREEN] = "GREEN",
};

static const char *const link_state_names[] = {
	[PROTO_LINK_CONNECTED]    = "CONNECTED",
	[PROTO_LINK_CONNECTING]   = "CONNECTING",
	[PROTO_LINK_DISCONNECTED] = "DISCONNECTED",
};

const char *proto_action_name(enum proto_action a)
{
	return (a < PROTO_ACT_COUNT) ? action_names[a] : "?";
}

const char *proto_remote_name(enum proto_remote r)
{
	return (r < PROTO_REMOTE_COUNT) ? remote_names[r] : "?";
}

const char *proto_link_state_name(enum proto_link_state s)
{
	return (s <= PROTO_LINK_DISCONNECTED) ? link_state_names[s] : "?";
}

uint16_t proto_next_seq(uint16_t seq)
{
	return (uint16_t)((seq + 1u) % PROTO_SEQ_MODULO);
}

/* ------------------------------------------------------------------------ */
/* Line assembler                                                            */
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
/* Parsing                                                                   */
/* ------------------------------------------------------------------------ */

static void parse_clock(char *rest, struct proto_msg *out)
{
	char *tok[PROTO_MAX_TOKENS];

	if (split_tokens(rest, tok, PROTO_MAX_TOKENS) != 1) {
		invalid(out, "wrong arg count");
		return;
	}
	if (strcmp(tok[0], "RUN") == 0) {
		out->type = PROTO_CLOCK;
		out->clock.run = true;
	} else if (strcmp(tok[0], "STOP") == 0) {
		out->type = PROTO_CLOCK;
		out->clock.run = false;
	} else {
		invalid(out, "bad mode");
	}
}

static void parse_confirm(char *rest, struct proto_msg *out)
{
	char *tok[PROTO_MAX_TOKENS];
	uint32_t seq;

	if (split_tokens(rest, tok, PROTO_MAX_TOKENS) != 1) {
		invalid(out, "wrong arg count");
		return;
	}
	if (!parse_uint(tok[0], &seq)) {
		invalid(out, "bad seq");
		return;
	}
	if (seq >= PROTO_SEQ_MODULO) {
		invalid(out, "seq out of range");
		return;
	}
	out->type = PROTO_CONFIRM;
	out->confirm.seq = (uint16_t)seq;
}

static void parse_test(char *rest, struct proto_msg *out)
{
	char *tok[PROTO_MAX_TOKENS];
	uint32_t mode;

	if (split_tokens(rest, tok, PROTO_MAX_TOKENS) != 1) {
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

static void parse_evt(char *rest, struct proto_msg *out)
{
	char *tok[PROTO_MAX_TOKENS];
	uint32_t action, src, seq;

	if (split_tokens(rest, tok, PROTO_MAX_TOKENS) != 3) {
		invalid(out, "wrong arg count");
		return;
	}
	if (!lookup_name(action_names, PROTO_ACT_COUNT, tok[0], &action)) {
		invalid(out, "bad action");
		return;
	}
	if (!lookup_name(remote_names, PROTO_REMOTE_COUNT, tok[1], &src)) {
		invalid(out, "bad src");
		return;
	}
	if (!parse_uint(tok[2], &seq)) {
		invalid(out, "bad seq");
		return;
	}
	if (seq >= PROTO_SEQ_MODULO) {
		invalid(out, "seq out of range");
		return;
	}
	out->type = PROTO_EVT;
	out->evt.action = (enum proto_action)action;
	out->evt.src = (enum proto_remote)src;
	out->evt.seq = (uint16_t)seq;
}

static void parse_link(char *rest, struct proto_msg *out)
{
	char *tok[PROTO_MAX_TOKENS];
	size_t n = split_tokens(rest, tok, PROTO_MAX_TOKENS);
	uint32_t remote, state;

	if (n < 2 || n > 4) {
		invalid(out, "wrong arg count");
		return;
	}
	if (!lookup_name(remote_names, PROTO_REMOTE_COUNT, tok[0], &remote)) {
		invalid(out, "bad remote");
		return;
	}
	if (!lookup_name(link_state_names, 3, tok[1], &state)) {
		invalid(out, "bad state");
		return;
	}

	out->link.remote = (enum proto_remote)remote;
	out->link.state = (enum proto_link_state)state;
	out->link.has_rssi = false;
	out->link.has_batt = false;

	if (out->link.state != PROTO_LINK_CONNECTED) {
		if (n != 2) {
			invalid(out, "rssi/batt only valid when CONNECTED");
			return;
		}
		out->type = PROTO_LINK;
		return;
	}

	if (n < 3) {
		invalid(out, "missing rssi for CONNECTED");
		return;
	}
	if (!parse_int(tok[2], &out->link.rssi)) {
		invalid(out, "bad rssi");
		return;
	}
	out->link.has_rssi = true;

	if (n == 4) {
		uint32_t batt;

		if (!parse_uint(tok[3], &batt)) {
			invalid(out, "bad batt");
			return;
		}
		if (batt > 100u) {
			invalid(out, "batt out of range");
			return;
		}
		out->link.batt = (int32_t)batt;
		out->link.has_batt = true;
	}

	out->type = PROTO_LINK;
}

static void parse_hello(char *rest, struct proto_msg *out)
{
	char *tok[PROTO_MAX_TOKENS];
	uint32_t caps;

	if (split_tokens(rest, tok, PROTO_MAX_TOKENS) != 3) {
		invalid(out, "wrong arg count");
		return;
	}
	if (!parse_uint(tok[2], &caps)) {
		invalid(out, "bad caps");
		return;
	}
	out->type = PROTO_HELLO;
	out->hello.proto = tok[0];
	out->hello.fw = tok[1];
	out->hello.caps = caps;
}

static void parse_no_args(char *rest, struct proto_msg *out, enum proto_type type)
{
	if (*rest != '\0') {
		invalid(out, "unexpected args");
		return;
	}
	out->type = type;
}

static void parse_text(char *rest, struct proto_msg *out, enum proto_type type)
{
	if (*rest == '\0') {
		invalid(out, "missing text");
		return;
	}
	out->type = type;
	out->text.text = rest;
}

void proto_parse(char *line, struct proto_msg *out)
{
	char *kw;
	char *rest;

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
	rest = line;
	if (*rest == ' ') {
		*rest = '\0';
		rest++;
	}
	while (*rest == ' ') {
		rest++;
	}

	/* App -> Dongle */
	if (strcmp(kw, "CLOCK") == 0) {
		parse_clock(rest, out);
	} else if (strcmp(kw, "EXPIRE") == 0) {
		parse_no_args(rest, out, PROTO_EXPIRE);
	} else if (strcmp(kw, "CONFIRM") == 0) {
		parse_confirm(rest, out);
	} else if (strcmp(kw, "PING") == 0) {
		parse_no_args(rest, out, PROTO_PING);
	} else if (strcmp(kw, "INFO") == 0) {
		parse_no_args(rest, out, PROTO_INFO);
	} else if (strcmp(kw, "ECHO") == 0) {
		parse_text(rest, out, PROTO_ECHO);
	} else if (strcmp(kw, "TEST") == 0) {
		parse_test(rest, out);
	/* Dongle -> App, parsed for round-trip tests only */
	} else if (strcmp(kw, "HELLO") == 0) {
		parse_hello(rest, out);
	} else if (strcmp(kw, "EVT") == 0) {
		parse_evt(rest, out);
	} else if (strcmp(kw, "LINK") == 0) {
		parse_link(rest, out);
	} else if (strcmp(kw, "PONG") == 0) {
		parse_no_args(rest, out, PROTO_PONG);
	} else if (strcmp(kw, "LOG") == 0) {
		parse_text(rest, out, PROTO_LOG);
	} else if (strcmp(kw, "ERR") == 0) {
		parse_text(rest, out, PROTO_ERR);
	}
	/* else: unknown keyword -> PROTO_UNKNOWN, ignore silently (§2.2) */
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

int proto_enc_hello(char *out, size_t cap, const char *fw, uint32_t caps)
{
	return enc_result(cap, snprintf(out, cap, "HELLO %s %s %u",
					PROTO_VERSION, fw, (unsigned int)caps));
}

int proto_enc_evt(char *out, size_t cap, enum proto_action a,
		  enum proto_remote src, uint16_t seq)
{
	return enc_result(cap, snprintf(out, cap, "EVT %s %s %u",
					proto_action_name(a),
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
	if (!has_batt) {
		return enc_result(cap, snprintf(out, cap, "LINK %s %s %d",
						rn, sn, (int)rssi));
	}
	return enc_result(cap, snprintf(out, cap, "LINK %s %s %d %d",
					rn, sn, (int)rssi, (int)batt));
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
