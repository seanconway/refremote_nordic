#include "ind_colour.h"

/*
 * RED/GREEN/BLUE are full-saturation primaries — each lights exactly one PWM
 * channel, so per-channel drive imbalance between the R/G/B chips (measured
 * on the bench: the red chip reads visibly dimmer than green at equal PWM
 * duty) has nothing to blend against and cannot desaturate them.
 *
 * YELLOW is the one colour that mixes channels, which is exactly where that
 * imbalance shows up: a naive (255, 255, 0) rendered close to green, because
 * green's chip outputs more light per unit duty than red's does at the same
 * setting. Deliberately R-heavy/G-light rather than a textbook yellow, so it
 * reads as amber rather than drifting toward green on this hardware. If a
 * future board's channels are better balanced, this is the one entry to
 * revisit — RED/GREEN/BLUE should not need to change.
 */
static const uint8_t TABLE[PROTO_COLOUR_COUNT][3] = {
	[PROTO_COLOUR_RED]    = { 255, 0, 0 },
	[PROTO_COLOUR_GREEN]  = { 0, 255, 0 },
	[PROTO_COLOUR_BLUE]   = { 0, 0, 255 },
	[PROTO_COLOUR_YELLOW] = { 255, 60, 0 },
};

void ind_colour_rgb(enum proto_ind_colour c, uint8_t rgb_out[3])
{
	if (c >= PROTO_COLOUR_COUNT) {
		rgb_out[0] = 0;
		rgb_out[1] = 0;
		rgb_out[2] = 0;
		return;
	}
	rgb_out[0] = TABLE[c][0];
	rgb_out[1] = TABLE[c][1];
	rgb_out[2] = TABLE[c][2];
}
