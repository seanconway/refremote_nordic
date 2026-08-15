/*
 * The one RGB truth for `enum proto_ind_colour` (protocol.h), shared by every
 * indicator on every remote board — remote/src/indicators.c (LED_F1, LED_F2,
 * LED_LINK, LED_PWR) and remote_dummy/src/indicators.c (its one RGB LED) all
 * render from this table, so "yellow" is the same PWM duty wherever it's
 * asked for rather than four independent guesses.
 *
 * NO ZEPHYR DEPENDENCIES, same rule as rframe.h and provisioning.h: this is
 * plain colour arithmetic, not board wiring, so nothing here should ever need
 * a devicetree or a driver header.
 */
#ifndef COMMON_IND_COLOUR_H_
#define COMMON_IND_COLOUR_H_

#include "protocol.h"

#include <stdint.h>

/* Writes the 8-bit R/G/B triple for `c` into `rgb_out`. An out-of-range `c`
 * (should never happen past rframe_decode()'s own field validation) renders
 * as off rather than reading past the table. */
void ind_colour_rgb(enum proto_ind_colour c, uint8_t rgb_out[3]);

#endif /* COMMON_IND_COLOUR_H_ */
