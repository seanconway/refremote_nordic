/*
 * Bench stand-in for the remote haptics and indicators, on the dongle's own
 * indicator LED.
 *
 * ONE BLUE LAMP, ON THE GREEN CHANNEL. The devicetree presents two nodes,
 * led0 (P0.06) and led1 (P0.08), aliased led0-green and led1-red. Those alias
 * names are copied from the NORDIC nRF52840 Dongle, which has a real RGB part;
 * this board has two blue parts and only one of them is fitted. MEASURED
 * 2026-08-11: the fitted one is on P0.06 — the GREEN channel — which is the
 * opposite of Raytac's own pin table. BOARD.md §2 carries the evidence, and
 * exists because this fact was derived from the alias names twice and got wrong
 * both times.
 *
 * One channel per remote — led0/P0.06 is GREEN, led1/P0.08 is RED — with two
 * layers on each: a steady *base* level asserted by STATE, and a transient
 * *haptic* pattern that plays over it and restores the base afterwards.
 *
 * WHAT THIS CANNOT SHOW, stated here because a stand-in that looks complete is
 * worse than one that obviously is not:
 *
 *  - ANYTHING ADDRESSED TO RED. That channel drives the empty footprint, so
 *    HAP RED, STATE RED and indicator_error() are all silent. Note the
 *    consequence for debugging: on this board "no blink" NEVER means "no
 *    error" — errors leave on the wire as ERR lines and nowhere else.
 *  - Colour, as STATE means it. STATE carries an RGB triple per indicator;
 *    what is here is one bit per remote, and it is blue whatever was asked for.
 *
 * What it CAN show, unexpectedly, is per-remote routing — precisely BECAUSE one
 * lamp is missing. A firmware ignoring the target would light P0.06 for HAP RED
 * as well as for HAP GREEN; one blinks and one does not. Confirmed 2026-08-11.
 *  - Which of F1 and F2 is lit. The base is the disjunction of the two.
 *  - The one amplitude that is a requirement. PROTOCOL.md §9.1 makes BEAT
 *    "unmistakably weaker than TAP on the wrist, through a strap, in motion" —
 *    an amplitude difference. A GPIO LED has no amplitude, so BEAT is rendered
 *    as a shorter flash, which is a different distinction wearing the same
 *    name. Whether one ERM can deliver the real separation is an open item
 *    (PLAN.md R3/R4) and nothing observed here speaks to it.
 *
 * This is the routing and timing of the haptic path made visible, and no more
 * than that.
 */
#ifndef DONGLE_INDICATOR_H_
#define DONGLE_INDICATOR_H_

#include "protocol.h"

#include <stdbool.h>

int indicator_init(void);

/* Steady level for one remote, from STATE. Survives haptic patterns. */
void indicator_base(enum proto_remote r, bool lit);

/* Transient pattern for one remote, from a downlink haptic command. */
void indicator_haptic(enum proto_remote r, enum proto_waveform w);

/* Fault indication, not tied to a remote. Accompanies every ERR line. */
void indicator_error(void);

#endif /* DONGLE_INDICATOR_H_ */
