/*
 * LED_F1, LED_F2, LED_LINK, LED_PWR — remote/BUILD_SPEC.md §7.1, §7.2, FS
 * §10. Real RGB via PWM as of the M5 GPIO harness (PLAN.md S13/S14) — the
 * DN_INDICATOR colour fields are now actually rendered, not just stored.
 *
 * LED_PWR renders FS §10.1's 3-band colour ladder from whatever
 * indicators_set_battery_pct() was last told, driven by DN_SIMSOC
 * (RADIO_PROTOCOL.md §7.5) rather than a real fuel-gauge reading — this
 * board has no battery yet. DN_SIMSOC never touches UP_TELEMETRY
 * (link.c's SYNTHETIC_BATTERY_PCT stays exactly what it was, a fixed
 * placeholder for the app's own battery display), so the two synthetic
 * values can't be confused for each other: one feeds a real wire path
 * for testing this indicator, the other is a hardcoded constant nothing
 * downstream should trust. Real content arrives with the nPM1300-EK
 * (PLAN.md S17/S18).
 */
#ifndef REMOTE_INDICATORS_H_
#define REMOTE_INDICATORS_H_

#include "protocol.h"

#include <stdbool.h>
#include <stdint.h>

struct k_work_q;

/* workq: same one buttons.c, haptic.c and link.c use — the link-lost double
 * buzz below is timer-driven and calls into haptic_render(), so it must run
 * on the queue haptic.c expects callers from. */
int indicators_init(struct k_work_q *workq);

/* DN_INDICATOR: complete app-owned state, asserted whole (§7.1). Applying an
 * identical frame must change nothing and re-trigger nothing (A11) — true
 * here because every PWM pulse write is idempotent, not because of any
 * explicit comparison against the previous frame.
 *
 * Colour is one of the fixed PROTO_COLOUR_* palette (protocol.h), never an
 * RGB triple — the app picks which of red/green/blue/yellow, this board
 * decides what that looks like in PWM duty (common/ind_colour.c), same as
 * LED_LINK and LED_PWR already do. */
void indicators_set(enum proto_ind_mode f1_mode, enum proto_ind_colour f1_colour,
		    enum proto_ind_mode f2_mode, enum proto_ind_colour f2_colour);

/* DN_CONFIG's led_brightness (0-100), applied multiplicatively to whatever
 * colour is currently set on every rendered indicator — real PWM levels now
 * that real RGB hardware exists, per the comment this replaces. */
void indicators_set_brightness(uint8_t led_brightness);

/*
 * LED_LINK is a conjunction the remote computes, never a value it is told
 * directly (§7.2): radio_up is what this board's own BLE connection state
 * says; host_up is DN_HOST, the half of the path the remote cannot see for
 * itself. On boot both default to false (A16) — link-lost until proven
 * otherwise, never "connected" pending contact. Colour is fixed by FS §10.2
 * (blue solid when up), not carried on the wire.
 */
void indicators_set_radio_up(bool up);
void indicators_set_host_up(bool up);

/* DN_SIMSOC (RADIO_PROTOCOL.md §7.5): bench-only. Not real telemetry — see
 * the header comment. Renders FS §10.1's 3-band ladder on LED_PWR. */
void indicators_set_battery_pct(uint8_t pct);

#endif /* REMOTE_INDICATORS_H_ */
