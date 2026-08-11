/*
 * Bench stand-in for the remote haptics and indicators, on the dongle's own
 * indicator LED.
 *
 * ONE PHYSICAL LED, TWO DIES. The devicetree presents two nodes — led0 (D1,
 * P0.06, green) and led1 (D2, P0.08, red) — and reading that as two lamps is
 * the natural mistake. The board carries a single bi-colour package: two
 * independently drivable channels in one body, in one place. Confirmed by
 * inspection 2026-08-11 (PLAN.md §2.7).
 *
 * One die per remote — red is RED, green is GREEN — with two layers on each: a
 * steady *base* level asserted by STATE, and a transient *haptic* pattern that
 * plays over it and restores the base afterwards.
 *
 * WHAT THIS CANNOT SHOW, stated here because a stand-in that looks complete is
 * worse than one that obviously is not:
 *
 *  - Two remotes at once, legibly. Sharing one body means simultaneous
 *    activity mixes to amber rather than showing two lights. A HAP addressed
 *    to BOTH is therefore the ONE case that cannot distinguish correct
 *    per-remote routing from a firmware that ignores the target and drives
 *    both channels always. Address the remotes SEPARATELY and read the colour.
 *  - Colour, as STATE means it. STATE carries an RGB triple per indicator;
 *    what is here is one bit per remote, and the red/green distinction
 *    identifies the *remote*, not the indicator's colour.
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
