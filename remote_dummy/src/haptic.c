#include "haptic.h"

#include <zephyr/kernel.h>

/*
 * True no-op, deliberately. This board has no ERM and no LED budget left to
 * proxy one: the RGB LED renders DN_INDICATOR colour (indicators.c, the
 * whole reason PLAN.md §4.9 chose this board for bring-up over the spare
 * MDBT50Q-CX-40), and the plain green LED renders LED_LINK. There is no
 * third surface to borrow, unlike the DK's dedicated LED1/pwm_led0.
 *
 * The protocol contract this satisfies is "consume the downlink" (PLAN.md
 * §2.2's S8), not "render it" — a DN_HAPTIC arriving here is accepted,
 * decoded, and discarded, exactly as link.c's own comments already assume
 * (it never inspects what haptic_render()/haptic_set_scale() actually do).
 * Consequence, recorded rather than silently true: the link-lost double
 * buzz indicators.c would otherwise trigger via haptic_render() has nowhere
 * to render on this board either. Real haptic rendering is M5's question
 * (S15/S16), on the DK, which has the LED to spare.
 */
void haptic_render(enum proto_waveform w, uint8_t ttl_4ms)
{
	ARG_UNUSED(w);
	ARG_UNUSED(ttl_4ms);
}

void haptic_set_scale(uint8_t scale)
{
	ARG_UNUSED(scale);
}

int haptic_init(struct k_work_q *workq)
{
	ARG_UNUSED(workq);
	return 0;
}
