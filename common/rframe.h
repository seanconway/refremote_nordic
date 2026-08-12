/*
 * RADIO_PROTOCOL.md v1.0 — the dongle <-> remote frame codec, both directions,
 * plus the CTR arithmetic of §4.2 and §6.1 step 3.
 *
 * NO ZEPHYR DEPENDENCIES, same rule and same reason as protocol.h and
 * provisioning.h: dongle/BUILD_SPEC.md §2.1 requires this so that
 * tests/rframe/ builds and runs on a host with nothing but gcc, before either
 * end's radio code exists to link against it. Permitted: <stdint.h>,
 * <stddef.h>, <stdbool.h>, <string.h>, <stdio.h>.
 *
 * This file shares protocol.h's button/gesture/remote/waveform/indicator
 * vocabulary rather than inventing a second one — radio.h's whole point is
 * that the engine never acquires a second name for something the wire
 * protocol already names, and a frame codec with its own enums would be that
 * mistake one file over. The wire values in RADIO_PROTOCOL.md §5 are 1-based
 * (0x01 is the first button, first gesture, first waveform); protocol.h's
 * enums are 0-based. The +/-1 translation happens once, here, so nobody
 * downstream has to remember it.
 */
#ifndef COMMON_RFRAME_H_
#define COMMON_RFRAME_H_

#include "protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* RP §4.3: the ATT value budget, and therefore a hard ceiling on every frame
 * this codec produces or accepts. Do not raise this to fit a new field —
 * §4.3 is explicit that the ceiling and the latency floor are the same
 * constraint seen from two directions. */
#define RFRAME_MAX_LEN        20u
#define RFRAME_HEADER_LEN     2u   /* TYPE, CTR — RP §4.1 */

/* RP §6.1 step 3: "Duplicate (<= last, within a window of 8)". The document
 * does not say whether the 8 counts `last` itself; this reader treats `last`
 * and the 8 values before it (9 in total) as duplicates, which is the more
 * conservative reading — it never mistakes a stale replay for a fresh gap.
 * Pinned here rather than left to drift between dongle and remote firmware,
 * the same reasoning as the CRC32 variant in provisioning.h. */
#define RFRAME_CTR_DUP_WINDOW 8u

/* RP §5.1/§5.2. Values are the wire TYPE byte, not sequential — the high bit
 * is the direction split (uplink 0x0N, downlink 0x8N) and RP §4.1 requires an
 * unrecognised TYPE to be ignored silently, which is why decode reports it as
 * its own status rather than folding it into a generic parse failure. */
enum rframe_type {
	RFRAME_UP_INPUT     = 0x01,
	RFRAME_UP_READY     = 0x02,
	RFRAME_UP_TELEMETRY = 0x03,
	RFRAME_UP_DIAG      = 0x04,

	RFRAME_DN_HAPTIC    = 0x81,
	RFRAME_DN_INDICATOR = 0x82,
	RFRAME_DN_CONFIG    = 0x83,
	RFRAME_DN_HOST      = 0x84,
};

/* RP §7.2 byte 3. */
enum rframe_ready_reason {
	RFRAME_READY_BOOT      = 0x01,
	RFRAME_READY_RECONNECT = 0x02,
};

/* RP §5.6 byte 3 bitfield. Bits 2-7 are reserved and must be ignored on
 * decode, not rejected — the same forward-compatibility spirit as caps in
 * RR_IDENTITY (RP §3.2). */
#define RFRAME_TELEMETRY_CHARGING (1u << 0)
#define RFRAME_TELEMETRY_LOW_BATT (1u << 1)

/*
 * RP §4.1 draws two different outcomes that must not be conflated:
 *
 *   unknown TYPE        -> ignore silently, never counted, never logged
 *   known TYPE, wrong length or field -> ignore, count, log one line
 *
 * Collapsing these into one "parse failed" status would lose the distinction
 * a caller needs in order to implement A2 and A3 differently.
 */
enum rframe_decode_status {
	RFRAME_OK = 0,
	RFRAME_ERR_UNKNOWN_TYPE,  /* A2 */
	RFRAME_ERR_LENGTH,        /* A3 */
	RFRAME_ERR_FIELD,         /* A1: known type, an enum byte outside its range */
};

/*
 * A decoded frame. All direction-specific members are named for the frame
 * they hold; `type` says which member of the union is valid. `up_diag.data`
 * points into the caller's buffer and is valid only as long as that buffer
 * is.
 */
struct rframe_msg {
	enum rframe_type type;
	uint8_t ctr;

	union {
		struct {
			enum proto_button button;
			enum proto_gesture gesture;
		} up_input;

		struct {
			uint8_t ctr_base;
			enum rframe_ready_reason reason;
		} up_ready;

		struct {
			uint8_t battery_pct;
			uint8_t flags;
			uint8_t dn_lost;
		} up_telemetry;

		struct {
			const uint8_t *data; /* NULL when len == 0 */
			size_t len;          /* 0-18 */
		} up_diag;

		struct {
			enum proto_waveform waveform;
			uint8_t ttl_4ms;
		} dn_haptic;

		struct {
			enum proto_ind_mode f1_mode;
			uint8_t f1_rgb[3];
			enum proto_ind_mode f2_mode;
			uint8_t f2_rgb[3];
		} dn_indicator;

		struct {
			uint8_t haptic_scale;
			uint8_t led_brightness;
		} dn_config;

		struct { bool up; } dn_host;
	};
};

/* Decodes one complete frame value, exactly as ATT hands it over — there is
 * no line-assembly problem here (RP §4.1: "a frame arrives complete or not
 * at all"), so unlike proto_parse() this never mutates `buf` and never spans
 * more than one call. */
enum rframe_decode_status rframe_decode(const uint8_t *buf, size_t len,
					struct rframe_msg *out);

/* ------------------------------------------------------------------------ */
/* Encoding                                                                  */
/* ------------------------------------------------------------------------ */

/* All return the byte count written, or -1 if it would exceed cap. Refused,
 * never truncated — same discipline as proto_enc_*(), and for the same
 * reason: a truncated frame corrupts the receiver's field layout, and there
 * is no delimiter here to resynchronise on. */

int rframe_enc_up_input(uint8_t *out, size_t cap, uint8_t ctr,
			enum proto_button b, enum proto_gesture g);
int rframe_enc_up_ready(uint8_t *out, size_t cap, uint8_t ctr,
			uint8_t ctr_base, enum rframe_ready_reason reason);
int rframe_enc_up_telemetry(uint8_t *out, size_t cap, uint8_t ctr,
			    uint8_t battery_pct, uint8_t flags, uint8_t dn_lost);
int rframe_enc_up_diag(uint8_t *out, size_t cap, uint8_t ctr,
		       const uint8_t *data, size_t len);

int rframe_enc_dn_haptic(uint8_t *out, size_t cap, uint8_t ctr,
			 enum proto_waveform w, uint8_t ttl_4ms);
int rframe_enc_dn_indicator(uint8_t *out, size_t cap, uint8_t ctr,
			    enum proto_ind_mode f1_mode, const uint8_t f1_rgb[3],
			    enum proto_ind_mode f2_mode, const uint8_t f2_rgb[3]);
int rframe_enc_dn_config(uint8_t *out, size_t cap, uint8_t ctr,
			 uint8_t haptic_scale, uint8_t led_brightness);
int rframe_enc_dn_host(uint8_t *out, size_t cap, uint8_t ctr, bool up);

/* ------------------------------------------------------------------------ */
/* CTR arithmetic — RP §4.2, §6.1 step 3                                     */
/* ------------------------------------------------------------------------ */

enum rframe_ctr_result {
	RFRAME_CTR_EXPECTED,   /* ctr == last + 1 (mod 256): accept */
	RFRAME_CTR_DUPLICATE,  /* within RFRAME_CTR_DUP_WINDOW behind last: discard */
	RFRAME_CTR_GAP,        /* ahead of last + 1: accept, *out_gap frames missing */
};

/* One instance per connection (i.e. per remote at the dongle, or the single
 * uplink-from-dongle counter at a remote — RP §4.2 makes CTR per-direction,
 * per-connection). Deliberately just the one field: there is no "primed"
 * state to track separately from `last`, because RP §7.2 guarantees a
 * remote's first uplink frame after subscription is always UP_READY, which
 * rebaselines before rframe_ctr_accept() is ever called for that connection. */
struct rframe_ctr_state {
	uint8_t last;
};

void rframe_ctr_init(struct rframe_ctr_state *s);

/* A7: UP_READY's ctr_base rebaselines the counter without producing a gap.
 * Call this for a decoded UP_READY frame instead of routing its ctr_base
 * through rframe_ctr_accept() — the whole point is that a reboot or a
 * reconnect must not present as however many frames were "missed" against
 * the old baseline. */
void rframe_ctr_rebaseline(struct rframe_ctr_state *s, uint8_t ctr_base);

/* A4-A6: classify and, for EXPECTED and GAP, accept (advance `last`).
 * *out_gap is written only when the result is RFRAME_CTR_GAP; may be NULL. */
enum rframe_ctr_result rframe_ctr_accept(struct rframe_ctr_state *s, uint8_t ctr,
					 uint8_t *out_gap);

const char *rframe_type_name(enum rframe_type t);

#endif /* COMMON_RFRAME_H_ */
