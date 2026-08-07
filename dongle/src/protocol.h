/*
 * Wire protocol for the dongle <-> scoreboard link. See PROTOCOL.md v2.0.
 *
 * This file and protocol.c are deliberately FREE OF ZEPHYR DEPENDENCIES
 * (no k_*, no LOG_*, no devicetree) so the same source compiles under host
 * gcc for unit tests. See PROTOCOL.md §11. Keep it that way.
 */
#ifndef DONGLE_PROTOCOL_H_
#define DONGLE_PROTOCOL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Protocol version reported in HELLO. Major mismatch makes the app refuse to
 * operate; minor mismatch only warns (PROTOCOL.md §8). */
#define PROTO_VERSION       "2.0"

#define PROTO_MAX_LINE      120u                    /* §2.1, includes terminator */
#define PROTO_MAX_CONTENT   (PROTO_MAX_LINE - 1u)   /* usable payload */
#define PROTO_ASM_BUF_SIZE  128u                    /* §11: fixed buffer */
#define PROTO_SEQ_MODULO    1000u                   /* §3.1: seq wraps 0-999 */
#define PROTO_MAX_TOKENS    8u

enum proto_remote {
	PROTO_REMOTE_RED = 0,
	PROTO_REMOTE_GREEN,
	PROTO_REMOTE_COUNT
};

/* §3.1 — seven actions covering the ten original requirements. */
enum proto_action {
	PROTO_ACT_TOGGLE_TIMER = 0,
	PROTO_ACT_ADD_POINT,
	PROTO_ACT_REMOVE_POINT,
	PROTO_ACT_TIME_UP,
	PROTO_ACT_TIME_DOWN,
	PROTO_ACT_PERIOD_UP,
	PROTO_ACT_PERIOD_DOWN,
	PROTO_ACT_COUNT
};

enum proto_link_state {
	PROTO_LINK_CONNECTED = 0,
	PROTO_LINK_CONNECTING,
	PROTO_LINK_DISCONNECTED
};

enum proto_type {
	/* Unknown keyword -> ignore silently (§2.2). This is what lets a v2.1
	 * peer add messages without breaking us. */
	PROTO_UNKNOWN = 0,
	/* Known keyword, bad arguments -> ignore silently, log locally (§2.2). */
	PROTO_INVALID,

	/* App -> Dongle (§4) */
	PROTO_CLOCK,
	PROTO_EXPIRE,
	PROTO_CONFIRM,
	PROTO_PING,
	PROTO_INFO,
	PROTO_ECHO,
	PROTO_TEST,

	/* Dongle -> App (§3). Parsed only so the host tests can round-trip
	 * encode->parse; the firmware never receives these. */
	PROTO_HELLO,
	PROTO_EVT,
	PROTO_LINK,
	PROTO_PONG,
	PROTO_LOG,
	PROTO_ERR
};

struct proto_msg {
	enum proto_type type;
	/* Set when type == PROTO_INVALID; a short static string for local logging. */
	const char *invalid_reason;

	union {
		struct {
			bool run;               /* CLOCK RUN vs CLOCK STOP */
		} clock;
		struct {
			uint16_t seq;
		} confirm;
		struct {
			uint32_t mode;
		} test;
		struct {
			/* Points into the caller's line buffer — valid only until
			 * that buffer is reused. */
			const char *text;
		} text;                         /* ECHO / LOG / ERR */
		struct {
			enum proto_action action;
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
		struct {
			const char *proto;
			const char *fw;
			uint32_t caps;
		} hello;
	};
};

/* ------------------------------------------------------------------------ */
/* Line assembler                                                            */
/* ------------------------------------------------------------------------ */

struct proto_asm {
	char buf[PROTO_ASM_BUF_SIZE];
	size_t len;
	/* Once an unterminated run crosses the length budget we drop everything
	 * until its terminator, so a garbage run cannot be stitched onto the
	 * line that follows it (§2.2). */
	bool discarding;
};

/* Receives one complete line, NUL-terminated, with \r and \n already stripped.
 * The buffer is mutable and is reused after the callback returns. */
typedef void (*proto_line_fn)(char *line, void *user);

void proto_asm_init(struct proto_asm *a);

/*
 * Feeds an arbitrary chunk of received bytes. A line may span several calls,
 * and one call may contain several lines plus a partial. This is the failure
 * that actually bites on a byte stream — never assume one read is one line.
 */
void proto_asm_feed(struct proto_asm *a, const uint8_t *data, size_t len,
		    proto_line_fn cb, void *user);

/* ------------------------------------------------------------------------ */
/* Parsing                                                                   */
/* ------------------------------------------------------------------------ */

/*
 * Parses one assembled line. DESTRUCTIVE: tokenises in place by writing NULs,
 * and `text`/`proto`/`fw` fields in the result point into `line`.
 */
void proto_parse(char *line, struct proto_msg *out);

const char *proto_action_name(enum proto_action a);
const char *proto_remote_name(enum proto_remote r);
const char *proto_link_state_name(enum proto_link_state s);

/* ------------------------------------------------------------------------ */
/* Encoding (Dongle -> App)                                                  */
/*                                                                           */
/* Each writes a NUL-terminated line WITHOUT the trailing \n — the transport  */
/* appends the terminator. Returns the length written, or -1 if the result    */
/* would not fit in `cap` or would exceed PROTO_MAX_CONTENT.                  */
/* ------------------------------------------------------------------------ */

int proto_enc_hello(char *out, size_t cap, const char *fw, uint32_t caps);
int proto_enc_evt(char *out, size_t cap, enum proto_action a,
		  enum proto_remote src, uint16_t seq);
/*
 * NOTE: when state is CONNECTED the app REQUIRES rssi — it rejects
 * "LINK RED CONNECTED" outright even though §3.2 reads as though rssi is
 * optional. Always pass a real value. `has_batt` false omits the battery field.
 */
int proto_enc_link(char *out, size_t cap, enum proto_remote r,
		   enum proto_link_state st, int32_t rssi,
		   int32_t batt, bool has_batt);
int proto_enc_pong(char *out, size_t cap);
int proto_enc_echo(char *out, size_t cap, const char *text);
int proto_enc_log(char *out, size_t cap, const char *text);
int proto_enc_err(char *out, size_t cap, const char *text);

/* §3.1 — sequence numbers wrap at PROTO_SEQ_MODULO. */
uint16_t proto_next_seq(uint16_t seq);

#ifdef __cplusplus
}
#endif

#endif /* DONGLE_PROTOCOL_H_ */
