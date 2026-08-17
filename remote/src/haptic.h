/*
 * Waveform table, ttl check, rendering — remote/BUILD_SPEC.md §7.3. Two
 * back ends behind CONFIG_REMOTE_HAPTIC_PROXY_LED: the original PWM/LED1
 * proxy (y, brightness stands in for amplitude on a board with no motor),
 * and real DRV2605L library-effect playback (n, §13's intended product
 * path). The waveform TABLE — which waveform is a distinct sensation, and
 * that BEAT is distinctly weaker than TAP — is what both back ends
 * implement identically in their own terms; only the rendering mechanism
 * differs.
 */
#ifndef REMOTE_HAPTIC_H_
#define REMOTE_HAPTIC_H_

#include "protocol.h"

#include <stdint.h>

struct k_work_q;

/* Renders on workq — the same queue buttons.c and link.c use, so a GATT
 * write callback arriving on the BT RX thread and a button edge never touch
 * haptic.c's render state from two different contexts at once. */
int haptic_init(struct k_work_q *workq);

/*
 * The remote renders; it decides nothing (RP §8.1). ttl_4ms is accepted for
 * completeness but not enforced as a deadline: remote/BUILD_SPEC.md §7.3
 * explicitly permits this — "where the determination cannot be made
 * confidently, render" — and this board has no reliable way to measure how
 * long a Write Without Response sat queued before delivery. The guarantee
 * that matters rests on the dongle's mechanism 1 (engine.c), which refuses
 * to hand over a frame whose budget is already spent; this is a completeness
 * note, not a silently-skipped requirement.
 */
void haptic_render(enum proto_waveform w, uint8_t ttl_4ms);

/* DN_CONFIG's haptic_scale, 0-100. Must preserve the BEAT:TAP ratio (RP
 * §7.3) — implemented by scaling every waveform's table amplitude by the
 * same factor rather than by a single shared parameter. */
void haptic_set_scale(uint8_t haptic_scale);

#endif /* REMOTE_HAPTIC_H_ */
