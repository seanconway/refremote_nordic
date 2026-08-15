/*
 * PROTOCOL.md v3.0 — the dongle ↔ scoreboard wire contract.
 *
 * NO ZEPHYR DEPENDENCIES. Nothing from zephyr/, no k_* call, no CONFIG_*, no
 * devicetree, no BUILD_ASSERT. Permitted: <stdint.h>, <stddef.h>, <stdbool.h>,
 * <string.h>, <stdio.h>.
 *
 * That rule is what lets tests/protocol/ build and run on a host machine with
 * nothing but gcc — no board, no SDK, no flash cycle. It is worth more than it
 * costs: the parser is the one part of the firmware where a mistake is silent
 * on both ends, and it is the one part that can be exhaustively tested in a
 * second. See dongle/BUILD_SPEC.md §2.1.
 *
 * Framing (§2) is unchanged from v2.0 and must stay that way. Everything above
 * it changed: buttons and gestures replace officiating actions, the dongle-side
 * clock is gone, and CONFIRM became ACK with a 120 ms window.
 */
#ifndef DONGLE_PROTOCOL_H_
#define DONGLE_PROTOCOL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROTO_VERSION       "3.0"

/* §2.1: 120 bytes including the terminator. */
#define PROTO_MAX_LINE      120u
#define PROTO_MAX_CONTENT   (PROTO_MAX_LINE - 1u)

/* Slack over PROTO_MAX_LINE so an overlong run is detected inside the buffer
 * rather than at its edge. */
#define PROTO_ASM_BUF_SIZE  128u

/* §5.3: seq is 0-65535, wrapping. Widened from v2.0's 0-999 because hold-repeat
 * at 150 ms wraps a 1000-entry space in about 2.5 minutes — close enough to a
 * match that a delayed duplicate and a genuine new event could collide on the
 * same number. */
#define PROTO_SEQ_MAX       65535u

/* The longest message takes 5 arguments (STATE). One spare catches an over-long
 * line without the tokenizer having to grow; split_tokens() returns the true
 * count regardless, which is what makes exact-count checks safe. */
#define PROTO_MAX_TOKENS    8u

/* §4.1: [A-Z0-9-]{1,16} */
#define PROTO_SET_SERIAL_MAX 16u

enum proto_remote {
	PROTO_REMOTE_RED = 0,
	PROTO_REMOTE_GREEN,
	PROTO_REMOTE_COUNT
};

/*
 * §5.1 — the seven buttons, named by *position*, not by function.
 *
 * The order here is the order TEST 1 and TEST 4 sweep in, so it is observable
 * on the wire and worth keeping aligned with the table in §5.1 and with the
 * app's BUTTONS array.
 *
 * F1 and F2 are deliberately unnamed: their meaning is a riding-time clock, an
 * advantage counter, a caution counter or a pending-choice flag depending on a
 * ruleset the dongle has never heard of. v2.0's TIME_UP / TIME_DOWN /
 * PERIOD_UP / PERIOD_DOWN encoded officiating meaning on the wire and are gone.
 */
enum proto_button {
	PROTO_BTN_ADD_POINT = 0,
	PROTO_BTN_TOGGLE_CLOCK,
	PROTO_BTN_REMOVE_POINT,
	PROTO_BTN_FORWARD,
	PROTO_BTN_BACKWARD,
	PROTO_BTN_F1,
	PROTO_BTN_F2,
	PROTO_BTN_COUNT
};

/*
 * §5.1 — classified in remote firmware, never negotiated over a link.
 *
 * HOLD_REP is emitted only for FORWARD and BACKWARD. That is a rule for the
 * *transmitter*, so it is enforced where events are generated (engine.c's TEST
 * modes, and the remote firmware) rather than in the parser: a receiver that
 * refused the combination would be rejecting a line it is supposed to be able
 * to read back.
 */
enum proto_gesture {
	PROTO_GEST_PRESS = 0,
	PROTO_GEST_HOLD,
	PROTO_GEST_HOLD_REP,
	PROTO_GEST_COUNT
};

/*
 * §6.4, §9 — CFG and HAP address one remote or both.
 *
 * A separate enum from proto_remote because BOTH is not a remote and there is
 * nowhere to put it in a two-entry list. RED and GREEN share the numbering so
 * expanding BOTH is a loop over proto_remote, not a translation table.
 */
enum proto_target {
	PROTO_TGT_RED = 0,
	PROTO_TGT_GREEN,
	PROTO_TGT_BOTH,
	PROTO_TGT_COUNT
};

/* §9. BEAT must be unmistakably weaker than TAP on the wrist (§9.1); that is a
 * property of the remote's waveform tables, not of this enum. */
enum proto_waveform {
	PROTO_WF_TAP = 0,
	PROTO_WF_BEAT,
	PROTO_WF_WARN,
	PROTO_WF_BUZZ,
	PROTO_WF_LONG,
	PROTO_WF_DOUBLE,
	PROTO_WF_TRIPLE,
	PROTO_WF_COUNT
};

/* §6.2. OFF and SOLID only — there is exactly one blinking indicator in the
 * system and it is remote-local, so no blink mode exists here to be misused. */
enum proto_ind_mode {
	PROTO_IND_OFF = 0,
	PROTO_IND_SOLID,
	PROTO_IND_MODE_COUNT
};

enum proto_link_state {
	PROTO_LINK_CONNECTED = 0,
	PROTO_LINK_CONNECTING,
	PROTO_LINK_DISCONNECTED,
	PROTO_LINK_STATE_COUNT
};

enum proto_type {
	PROTO_UNKNOWN = 0,   /* unrecognised keyword — ignore silently (§2.2) */
	PROTO_INVALID,       /* recognised keyword, unusable arguments        */

	/* App -> Dongle */
	PROTO_ACK,
	PROTO_STATE,
	PROTO_HAP,
	PROTO_CFG,
	PROTO_SIMSOC,
	PROTO_PING,
	PROTO_INFO,
	PROTO_ECHO,
	PROTO_TEST,

	/* Dongle -> App. Parsed only so encoders can be round-tripped in the
	 * host suite; the firmware never receives these. */
	PROTO_HELLO,
	PROTO_EVT,
	PROTO_LINK,
	PROTO_JOIN,
	PROTO_PONG,
	PROTO_LOG,
	PROTO_ERR
};

/*
 * A parsed line. All `const char *` members point into the caller's line
 * buffer, which proto_parse() has written NULs into — they are valid only until
 * that buffer is reused.
 */
struct proto_msg {
	enum proto_type type;
	const char *invalid_reason;   /* set only when type == PROTO_INVALID */

	union {
		struct {
			uint16_t seq;
			bool silent;  /* §5.2: inert button — nothing on the air */
		} ack;

		struct {
			enum proto_remote remote;
			enum proto_ind_mode f1_mode;
			uint8_t f1_rgb[3];
			enum proto_ind_mode f2_mode;
			uint8_t f2_rgb[3];
		} state;

		struct {
			enum proto_target target;
			enum proto_waveform waveform;
		} hap;

		struct {
			enum proto_target target;
			uint8_t haptic;   /* 0-100 */
			uint8_t bright;   /* 0-100 */
		} cfg;

		/* SIMSOC <target> <pct> — PROTOCOL.md §6.5. Bench-only. */
		struct {
			enum proto_target target;
			uint8_t pct;      /* 0-100 */
		} simsoc;

		struct { uint32_t mode; } test;

		/* ECHO / LOG / ERR: the remainder of the line, verbatim. */
		struct { const char *text; } text;

		struct {
			enum proto_button button;
			enum proto_gesture gesture;
			enum proto_remote src;
			uint16_t seq;
		} evt;

		struct {
			enum proto_remote remote;
			enum proto_link_state state;
			int32_t rssi;
			int32_t batt;
			bool has_rssi;
			bool has_batt;
		} link;

		struct { enum proto_remote remote; } join;

		struct {
			const char *proto;
			const char *fw;
			const char *set;
			uint32_t caps;
		} hello;
	};
};

/* ------------------------------------------------------------------------ */
/* Line assembler (§2.2)                                                     */
/* ------------------------------------------------------------------------ */

/*
 * `discarding` is the whole point of this being a struct rather than a local.
 * Resynchronisation happens at the next \n, which may be several reads away, so
 * the discard state has to survive a chunk boundary — a split between reads is
 * an artifact of the transport and carries no information about the stream.
 * Test T9c pins this.
 */
struct proto_asm {
	char buf[PROTO_ASM_BUF_SIZE];
	size_t len;
	bool discarding;
};

/* Receives one complete, NUL-terminated line. The buffer is reused after the
 * callback returns. */
typedef void (*proto_line_fn)(char *line, void *user);

void proto_asm_init(struct proto_asm *a);
void proto_asm_feed(struct proto_asm *a, const uint8_t *data, size_t len,
		    proto_line_fn cb, void *user);

/* ------------------------------------------------------------------------ */
/* Parsing                                                                   */
/* ------------------------------------------------------------------------ */

/* Writes NULs into `line` in place. */
void proto_parse(char *line, struct proto_msg *out);

const char *proto_button_name(enum proto_button b);
const char *proto_gesture_name(enum proto_gesture g);
const char *proto_remote_name(enum proto_remote r);
const char *proto_target_name(enum proto_target t);
const char *proto_waveform_name(enum proto_waveform w);
const char *proto_ind_mode_name(enum proto_ind_mode m);
const char *proto_link_state_name(enum proto_link_state s);

/* ------------------------------------------------------------------------ */
/* Encoding                                                                  */
/* ------------------------------------------------------------------------ */

/* All return the byte count written (excluding the terminator, which the
 * transport appends), or -1 if the line would exceed PROTO_MAX_CONTENT.
 * Refused, never truncated: a truncated line corrupts the receiver's framing. */

int proto_enc_hello(char *out, size_t cap, const char *fw, const char *set,
		    uint32_t caps);
int proto_enc_evt(char *out, size_t cap, enum proto_button b,
		  enum proto_gesture g, enum proto_remote src, uint16_t seq);
int proto_enc_link(char *out, size_t cap, enum proto_remote r,
		   enum proto_link_state st, int32_t rssi,
		   int32_t batt, bool has_batt);
int proto_enc_join(char *out, size_t cap, enum proto_remote r);
int proto_enc_pong(char *out, size_t cap);
int proto_enc_echo(char *out, size_t cap, const char *text);
int proto_enc_log(char *out, size_t cap, const char *text);
int proto_enc_err(char *out, size_t cap, const char *text);

/* Post-increment wrap. Natural uint16_t overflow — 65535 -> 0 — which is why
 * there is no modulo here any more. The app handles the discontinuity with a
 * wrap heuristic, so seq is never reset on reconnect. */
uint16_t proto_next_seq(uint16_t seq);

#endif /* DONGLE_PROTOCOL_H_ */
