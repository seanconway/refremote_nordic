#include "indicators.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>

/*
 * This board has exactly two LED positions (confirmed against the installed
 * board devicetree, nrf52840dongle_nrf52840_common.dtsi — not read off alias
 * names alone, CLAUDE.md §6's standing trap): one plain green GPIO LED
 * (led0/led0-green, P0.06) and one RGB LED with all three channels on PWM
 * (pwm-led0/1/2 = red/green/blue, all on pwm0). PLAN.md §4.9 chose this
 * board for bring-up specifically because that RGB LED is "the only surface
 * before M5 that can render DN_INDICATOR colour at all."
 *
 * Only two indicator surfaces exist here, not the DK's four (LED_F1, LED_F2,
 * LED_LINK). LED_F2 is not rendered at all — recorded as a limitation, same
 * spirit as the dongle's own unfitted RED channel (dongle/BOARD.md §2):
 * "no colour" on this board never means "F2 has no state," only that this
 * board cannot show it.
 */
static const struct pwm_dt_spec rgb_r = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led0));
static const struct pwm_dt_spec rgb_g = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led1));
static const struct pwm_dt_spec rgb_b = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led2));
static const struct gpio_dt_spec led_link = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static bool radio_up;
static bool host_up;
static uint8_t f1_rgb_cache[3];
static enum proto_ind_mode f1_mode_cache;
static uint8_t brightness_pct = 100;

static void rgb_apply(void)
{
	uint32_t r = 0, g = 0, b = 0;

	if (f1_mode_cache == PROTO_IND_SOLID) {
		r = ((uint32_t)f1_rgb_cache[0] * brightness_pct) / 100u;
		g = ((uint32_t)f1_rgb_cache[1] * brightness_pct) / 100u;
		b = ((uint32_t)f1_rgb_cache[2] * brightness_pct) / 100u;
	}

	/* 8-bit channel value (0-255) scaled onto each PWM period. */
	(void)pwm_set_pulse_dt(&rgb_r, (uint32_t)(((uint64_t)rgb_r.period * r) / 255u));
	(void)pwm_set_pulse_dt(&rgb_g, (uint32_t)(((uint64_t)rgb_g.period * g) / 255u));
	(void)pwm_set_pulse_dt(&rgb_b, (uint32_t)(((uint64_t)rgb_b.period * b) / 255u));
}

static void update_link(void)
{
	/* Unlike the DK, there is no haptic proxy budget left on this board
	 * for the link-lost double buzz (remote/src/indicators.c's
	 * lost_link_work calls haptic_render(), which is a true no-op here —
	 * see haptic.c). LED_LINK's own on/off conjunction still renders
	 * correctly; only the buzz has nowhere to go. Recorded, not silently
	 * dropped, same discipline as every other unobservable surface in
	 * this project. */
	(void)gpio_pin_set_dt(&led_link, (radio_up && host_up) ? 1 : 0);
}

void indicators_set(enum proto_ind_mode f1_mode, const uint8_t f1_rgb[3],
		    enum proto_ind_mode f2_mode, const uint8_t f2_rgb[3])
{
	ARG_UNUSED(f2_mode); /* F2 unrenderable on this board — no LED left */
	ARG_UNUSED(f2_rgb);

	f1_mode_cache = f1_mode;
	f1_rgb_cache[0] = f1_rgb[0];
	f1_rgb_cache[1] = f1_rgb[1];
	f1_rgb_cache[2] = f1_rgb[2];
	rgb_apply();
}

void indicators_set_brightness(uint8_t led_brightness)
{
	brightness_pct = led_brightness > 100u ? 100u : led_brightness;
	rgb_apply();
}

void indicators_set_radio_up(bool up)
{
	radio_up = up;
	update_link();
}

void indicators_set_host_up(bool up)
{
	host_up = up;
	update_link();
}

int indicators_init(struct k_work_q *workq)
{
	ARG_UNUSED(workq); /* nothing timer-driven here, unlike the DK's buzz */

	if (!pwm_is_ready_dt(&rgb_r) || !pwm_is_ready_dt(&rgb_g) || !pwm_is_ready_dt(&rgb_b)) {
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&led_link)) {
		return -ENODEV;
	}
	if (gpio_pin_configure_dt(&led_link, GPIO_OUTPUT_INACTIVE) != 0) {
		return -EIO;
	}

	f1_mode_cache = PROTO_IND_OFF;
	rgb_apply();

	/* A16: on boot, assume DOWN until told otherwise. Both flags default
	 * false above, so this renders link-lost immediately — the state
	 * that is actually true at this moment. */
	update_link();
	return 0;
}
