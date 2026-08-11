/*
 * The engine's entire view of the radio. See dongle/BUILD_SPEC.md §3.
 *
 * Two implementations are selected at build time:
 *
 *   CONFIG_DONGLE_RADIO=n   radio_null.c   nothing connected, downlink on LEDs
 *   CONFIG_DONGLE_RADIO=y   radio_ble.c    BLE central, two remotes
 *
 * radio_null is not scaffolding to be deleted. It is a permanent build
 * configuration in which the whole wire layer runs — exercised by the TEST
 * modes — with no radio anywhere in the system, and it is what makes a radio
 * regression attributable: PLAN.md §3.10 lists eight ways adding a radio
 * degrades a working USB link without touching any USB code, and every one of
 * them is diagnosed by asking "does it still happen with the radio compiled
 * out?" A baseline you can re-run in thirty seconds answers that. A baseline
 * that was green as a milestone six weeks ago does not.
 *
 * The engine never sees a bt_conn, a GATT handle, or a frame. It sees two
 * remotes identified the way the wire protocol identifies them, because the
 * moment the engine knows about connections it acquires a second vocabulary for
 * the same thing and the two drift.
 */
#ifndef DONGLE_RADIO_H_
#define DONGLE_RADIO_H_

#include "protocol.h"

#include <stdbool.h>
#include <stdint.h>

/* Opaque here on purpose: radio.h stays free of <zephyr/kernel.h> so the
 * interface reads as a contract rather than as Zephyr plumbing. */
struct k_work_q;

/* The app-owned half of a remote's indicators (PROTOCOL.md §6). LED_PWR and
 * LED_LINK are remote-local and deliberately unreachable from here. */
struct indicator_state {
	uint8_t f1_mode;        /* enum proto_ind_mode */
	uint8_t f1_rgb[3];
	uint8_t f2_mode;
	uint8_t f2_rgb[3];
};

/*
 * Every callback is invoked on the workqueue handed to radio_init(), never on
 * the Bluetooth RX thread. That is what preserves the engine's one-producer,
 * no-locking invariant (engine.h) once a second source of events exists.
 */
struct radio_cb {
	/* A press. The engine assigns seq and emits EVT. */
	void (*on_input)(enum proto_remote src, enum proto_button b,
			 enum proto_gesture g);

	/* This remote holds no indicator state — drives JOIN. From UP_READY. */
	void (*on_ready)(enum proto_remote src);

	/* Already debounced per RADIO_PROTOCOL.md §9.4; the engine emits LINK
	 * verbatim. rssi is meaningful only when st is CONNECTED, where
	 * PROTOCOL.md §7 makes it mandatory. */
	void (*on_link)(enum proto_remote src, enum proto_link_state st,
			int8_t rssi);

	/* From UP_TELEMETRY. Feeds the batt field of LINK. */
	void (*on_telemetry)(enum proto_remote src, uint8_t batt_pct,
			     uint8_t flags);

	/* Freeform diagnostic destined for a LOG line. Never semantic. */
	void (*on_diag)(enum proto_remote src, const char *text);
};

int radio_init(const struct radio_cb *cb, struct k_work_q *workq);

/*
 * ttl_4ms is the frame's remaining useful life in 4 ms units; 0 means no
 * deadline.
 *
 * A negative return means the frame was not sent at all, and that is a normal
 * outcome rather than an error — BUILD_SPEC §6.3. Exceeding the acknowledgement
 * budget must degrade to silence, never to a late tap: no tap invokes a rule
 * the referee already has ("press again"), while a late tap arriving during the
 * next press is read as acknowledgement of *that* press.
 */
int radio_send_haptic(enum proto_remote r, enum proto_waveform w,
		      uint8_t ttl_4ms);
int radio_send_indicator(enum proto_remote r, const struct indicator_state *s);
int radio_send_config(enum proto_remote r, uint8_t haptic, uint8_t bright);

/* DN_HOST carries the half of the path the remote cannot see: the USB cable,
 * the browser tab, the laptop's sleep state, the app's own watchdog. */
int radio_send_host(enum proto_remote r, bool up);

/* True once the remote is fully established per RADIO_PROTOCOL.md §9.4 —
 * encrypted, identity validated, CCCD subscribed. Not merely "connected". */
bool radio_is_ready(enum proto_remote r);

#endif /* DONGLE_RADIO_H_ */
