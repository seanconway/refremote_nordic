#include "buttons.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/*
 * PLAN.md §4.9: this board's job is to prove the *dongle's* central
 * scheduling with a second real connection, not to model a wrist remote —
 * "it does not need seven buttons or a wrist. One button covers all three
 * gestures, and a self-stimulus timer covers sustained load." The one
 * physical button below covers the human-driven case (press it and watch
 * B6 routing on the wire); the self-stimulus timer covers the "GREEN is
 * generating realistic uplink traffic unattended" case W6/W7 actually need.
 * Mapped to ADD_POINT — same as remote/src/buttons.c's button 1 — purely so
 * a physical press is visibly meaningful on the scoreboard during the
 * demonstration, not because the button token means anything on this board.
 *
 * State machine is remote/src/buttons.c's, reduced to one button and no
 * HOLD_REP (remote/BUILD_SPEC.md §4 restricts repeating gestures to
 * FORWARD/BACKWARD, neither of which this board maps).
 */
#define DEBOUNCE_MS       15
#define HOLD_THRESHOLD_MS 600

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

enum btn_state {
	BTN_IDLE = 0,
	BTN_DEBOUNCE,
	BTN_DOWN,  /* debounced, waiting for release (PRESS) or threshold (HOLD) */
	BTN_HELD,  /* HOLD already fired; only a release can follow */
};

static enum btn_state state = BTN_IDLE;
static struct gpio_callback gpio_cb;
static struct k_work_delayable timer;
static struct k_work edge_work;
static struct k_work_delayable stim_work;
static buttons_gesture_cb gesture_cb;
static struct k_work_q *buttons_workq;

static bool is_pressed(void)
{
	return gpio_pin_get_dt(&button) > 0;
}

static void emit(enum proto_gesture g)
{
	if (gesture_cb) {
		gesture_cb(PROTO_BTN_ADD_POINT, g);
	}
}

static void timer_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	switch (state) {
	case BTN_DEBOUNCE:
		if (!is_pressed()) {
			state = BTN_IDLE; /* bounced back up inside the window */
			return;
		}
		state = BTN_DOWN;
		k_work_reschedule_for_queue(buttons_workq, &timer,
					    K_MSEC(HOLD_THRESHOLD_MS - DEBOUNCE_MS));
		return;

	case BTN_DOWN:
		/* Threshold reached with the button still down: HOLD fires
		 * once, at the threshold — never on release (§4 detail 1). */
		emit(PROTO_GEST_HOLD);
		state = BTN_HELD;
		return;

	case BTN_IDLE:
	case BTN_HELD:
	default:
		return;
	}
}

static void edge_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	bool pressed = is_pressed();

	switch (state) {
	case BTN_IDLE:
		if (pressed) {
			state = BTN_DEBOUNCE;
			k_work_reschedule_for_queue(buttons_workq, &timer, K_MSEC(DEBOUNCE_MS));
		}
		return;

	case BTN_DEBOUNCE:
		return; /* timer_handler's own re-check owns this case */

	case BTN_DOWN:
		if (!pressed) {
			/* Released before the threshold: a genuine PRESS.
			 * This path is only reachable while HOLD has not
			 * fired — once it does, state is BTN_HELD, not
			 * BTN_DOWN (§4 detail 2: a HOLD never also emits a
			 * PRESS). */
			(void)k_work_cancel_delayable(&timer);
			emit(PROTO_GEST_PRESS);
			state = BTN_IDLE;
		}
		return;

	case BTN_HELD:
		if (!pressed) {
			state = BTN_IDLE;
		}
		return;
	}
}

static void gpio_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	k_work_submit_to_queue(buttons_workq, &edge_work);
}

/*
 * The self-stimulus timer. Fires a synthetic PRESS through the identical
 * gesture_cb() path a real debounced release would use — link_on_gesture()
 * cannot tell the difference, which is the point: this exercises the real
 * CTR-increment and RR_UPLINK-notify code, not a shortcut around it, so the
 * traffic it produces is what W6/W7 actually need to see. Runs on
 * buttons_workq, the same single-producer queue edge_work/timer use, so it
 * never races a real physical press.
 */
static void stim_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	emit(PROTO_GEST_PRESS);
	k_work_reschedule_for_queue(buttons_workq, &stim_work,
				    K_MSEC(CONFIG_REMOTE_DUMMY_STIM_INTERVAL_MS));
}

int buttons_init(buttons_gesture_cb cb, struct k_work_q *workq)
{
	int err;

	gesture_cb = cb;
	buttons_workq = workq;

	if (!gpio_is_ready_dt(&button)) {
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (err) {
		return err;
	}
	err = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
	if (err) {
		return err;
	}

	k_work_init_delayable(&timer, timer_handler);
	k_work_init(&edge_work, edge_work_handler);
	gpio_init_callback(&gpio_cb, gpio_isr, BIT(button.pin));
	err = gpio_add_callback(button.port, &gpio_cb);
	if (err) {
		return err;
	}

	k_work_init_delayable(&stim_work, stim_handler);
	k_work_reschedule_for_queue(buttons_workq, &stim_work,
				    K_MSEC(CONFIG_REMOTE_DUMMY_STIM_INTERVAL_MS));

	return 0;
}
