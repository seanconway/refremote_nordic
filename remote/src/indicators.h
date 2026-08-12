/*
 * LED_F1, LED_F2, LED_LINK — remote/BUILD_SPEC.md §7.1, §7.2. Mode only:
 * this board's LEDs are single-colour, so the RGB fields of DN_INDICATOR are
 * accepted and stored but not rendered. Read them back over RTT if the
 * colour needs checking (§1's table).
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
 * identical frame must change nothing and re-trigger nothing (A11) — this is
 * naturally true here because the GPIO write is idempotent, not because of
 * any explicit comparison against the previous frame. */
void indicators_set(enum proto_ind_mode f1_mode, const uint8_t f1_rgb[3],
		    enum proto_ind_mode f2_mode, const uint8_t f2_rgb[3]);

void indicators_set_brightness(uint8_t led_brightness);

/*
 * LED_LINK is a conjunction the remote computes, never a value it is told
 * directly (§7.2): radio_up is what this board's own BLE connection state
 * says; host_up is DN_HOST, the half of the path the remote cannot see for
 * itself. On boot both default to false (A16) — link-lost until proven
 * otherwise, never "connected" pending contact.
 */
void indicators_set_radio_up(bool up);
void indicators_set_host_up(bool up);

#endif /* REMOTE_INDICATORS_H_ */
