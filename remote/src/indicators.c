#include "indicators.h"
#include "haptic.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/* §2: LED 2/3/4 render mode only — OFF/SOLID — never colour. */
static const struct gpio_dt_spec led_f1   = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led_f2   = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);
static const struct gpio_dt_spec led_link = GPIO_DT_SPEC_GET(DT_ALIAS(led3), gpios);

static bool radio_up;
static bool host_up;

/* §7.2: "link-lost also drives a repeating double buzz... a remote-local
 * behaviour with no representation on either protocol." The interval is not
 * pinned by any spec document; 2 s is chosen so the buzz is noticeable
 * without competing with real acknowledgement taps for attention. */
#define LOST_LINK_BUZZ_MS 2000

static struct k_work_delayable lost_link_work;
static struct k_work_q *ind_workq;

static void lost_link_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!(radio_up && host_up)) {
		haptic_render(PROTO_WF_DOUBLE, 0);
		k_work_reschedule_for_queue(ind_workq, &lost_link_work,
					    K_MSEC(LOST_LINK_BUZZ_MS));
	}
}

static void update_link(void)
{
	bool up = radio_up && host_up;

	(void)gpio_pin_set_dt(&led_link, up ? 1 : 0);

	if (up) {
		(void)k_work_cancel_delayable(&lost_link_work);
	} else {
		/* Idempotent: reschedule is safe even if already pending. */
		k_work_reschedule_for_queue(ind_workq, &lost_link_work,
					    K_MSEC(LOST_LINK_BUZZ_MS));
	}
}

void indicators_set(enum proto_ind_mode f1_mode, const uint8_t f1_rgb[3],
		    enum proto_ind_mode f2_mode, const uint8_t f2_rgb[3])
{
	ARG_UNUSED(f1_rgb); /* colour not rendered on this board — §1 */
	ARG_UNUSED(f2_rgb);

	(void)gpio_pin_set_dt(&led_f1, f1_mode == PROTO_IND_SOLID ? 1 : 0);
	(void)gpio_pin_set_dt(&led_f2, f2_mode == PROTO_IND_SOLID ? 1 : 0);
}

void indicators_set_brightness(uint8_t led_brightness)
{
	/* GPIO on/off LEDs have no brightness to scale — §7.3's requirement is
	 * about the haptic amplitude ratio, which lives in haptic.c. Recorded
	 * here rather than silently ignored: real RGB hardware (R3/R4) is
	 * where this becomes a real PWM level. */
	ARG_UNUSED(led_brightness);
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
	const struct gpio_dt_spec *leds[] = { &led_f1, &led_f2, &led_link };

	ind_workq = workq;

	for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
		int err;

		if (!gpio_is_ready_dt(leds[i])) {
			return -ENODEV;
		}
		err = gpio_pin_configure_dt(leds[i], GPIO_OUTPUT_INACTIVE);
		if (err) {
			return err;
		}
	}

	k_work_init_delayable(&lost_link_work, lost_link_handler);

	/* A16: on boot, assume DOWN until told otherwise. radio_up/host_up
	 * both default false above, so update_link() renders link-lost and
	 * arms the buzz — exactly the state that is true at this moment. */
	update_link();
	return 0;
}
