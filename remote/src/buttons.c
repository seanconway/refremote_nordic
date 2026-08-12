#include "buttons.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/* remote/BUILD_SPEC.md §4: fixed in firmware, not configurable anywhere. */
#define DEBOUNCE_MS      15
#define HOLD_THRESHOLD_MS 600
#define HOLD_REPEAT_MS   150

/* §2: gesture and semantic coverage, not button coverage. */
struct button_def {
	const struct gpio_dt_spec spec;
	enum proto_button button;
	bool repeating;   /* HOLD_REP only for FORWARD/BACKWARD; only FORWARD exists here */
};

static const struct button_def defs[] = {
	{ GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios), PROTO_BTN_ADD_POINT,    false },
	{ GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios), PROTO_BTN_TOGGLE_CLOCK, false },
	{ GPIO_DT_SPEC_GET(DT_ALIAS(sw2), gpios), PROTO_BTN_FORWARD,      true  },
	{ GPIO_DT_SPEC_GET(DT_ALIAS(sw3), gpios), PROTO_BTN_F1,           false },
};

#define BUTTON_COUNT (sizeof(defs) / sizeof(defs[0]))

enum btn_state {
	BTN_IDLE = 0,
	BTN_DEBOUNCE,
	BTN_DOWN,     /* debounced, waiting for release (PRESS) or threshold (HOLD) */
	BTN_HELD,     /* HOLD already fired; repeating buttons re-arm the timer */
};

struct button_runtime {
	enum btn_state state;
	struct gpio_callback gpio_cb;
	struct k_work_delayable timer;
	uint8_t index;
};

static struct button_runtime runtimes[BUTTON_COUNT];
static buttons_gesture_cb gesture_cb;
static struct k_work_q *buttons_workq;

static bool is_pressed(uint8_t i)
{
	return gpio_pin_get_dt(&defs[i].spec) > 0;
}

static void emit(uint8_t i, enum proto_gesture g)
{
	if (gesture_cb) {
		gesture_cb(defs[i].button, g);
	}
}

/*
 * One state machine per button, all timing driven by k_work_delayable on
 * buttons_workq rather than by raw GPIO ISR context — the same one-producer
 * discipline dongle/src/engine.c uses, now extended to a second event source.
 */
static void timer_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct button_runtime *rt = CONTAINER_OF(dwork, struct button_runtime, timer);
	uint8_t i = rt->index;

	switch (rt->state) {
	case BTN_DEBOUNCE:
		if (!is_pressed(i)) {
			/* Bounced back up inside the debounce window. */
			rt->state = BTN_IDLE;
			return;
		}
		rt->state = BTN_DOWN;
		k_work_reschedule_for_queue(buttons_workq, &rt->timer,
					    K_MSEC(HOLD_THRESHOLD_MS - DEBOUNCE_MS));
		return;

	case BTN_DOWN:
		/* Threshold reached with the button still down: HOLD fires
		 * once, at the threshold — never on release (§4 detail 1). */
		emit(i, PROTO_GEST_HOLD);
		rt->state = BTN_HELD;
		if (defs[i].repeating) {
			k_work_reschedule_for_queue(buttons_workq, &rt->timer,
						    K_MSEC(HOLD_REPEAT_MS));
		}
		return;

	case BTN_HELD:
		/* Only reached for a repeating button still down. */
		emit(i, PROTO_GEST_HOLD_REP);
		k_work_reschedule_for_queue(buttons_workq, &rt->timer,
					    K_MSEC(HOLD_REPEAT_MS));
		return;

	case BTN_IDLE:
	default:
		return;
	}
}

static void edge_work_handler(struct k_work *work);

struct edge_work_item {
	struct k_work work;
	uint8_t index;
};

static struct edge_work_item edge_items[BUTTON_COUNT];

static void gpio_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(pins);

	struct button_runtime *rt = CONTAINER_OF(cb, struct button_runtime, gpio_cb);

	/* Marshal onto buttons_workq immediately; nothing below this point
	 * runs in interrupt context. */
	k_work_submit_to_queue(buttons_workq, &edge_items[rt->index].work);
}

static void edge_work_handler(struct k_work *work)
{
	struct edge_work_item *item = CONTAINER_OF(work, struct edge_work_item, work);
	uint8_t i = item->index;
	struct button_runtime *rt = &runtimes[i];
	bool pressed = is_pressed(i);

	switch (rt->state) {
	case BTN_IDLE:
		if (pressed) {
			rt->state = BTN_DEBOUNCE;
			k_work_reschedule_for_queue(buttons_workq, &rt->timer,
						    K_MSEC(DEBOUNCE_MS));
		}
		return;

	case BTN_DEBOUNCE:
		/* Handled by timer_handler's re-check; nothing to do here. */
		return;

	case BTN_DOWN:
		if (!pressed) {
			/* Released before the threshold: a genuine PRESS.
			 * §4 detail 2: a HOLD never also emits a PRESS, and
			 * this path is only reached when HOLD has not fired. */
			(void)k_work_cancel_delayable(&rt->timer);
			emit(i, PROTO_GEST_PRESS);
			rt->state = BTN_IDLE;
		}
		return;

	case BTN_HELD:
		if (!pressed) {
			(void)k_work_cancel_delayable(&rt->timer);
			rt->state = BTN_IDLE;
		}
		return;

	default:
		return;
	}
}

int buttons_init(buttons_gesture_cb cb, struct k_work_q *workq)
{
	gesture_cb = cb;
	buttons_workq = workq;

	for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
		int err;

		if (!gpio_is_ready_dt(&defs[i].spec)) {
			return -ENODEV;
		}
		err = gpio_pin_configure_dt(&defs[i].spec, GPIO_INPUT);
		if (err) {
			return err;
		}
		err = gpio_pin_interrupt_configure_dt(&defs[i].spec, GPIO_INT_EDGE_BOTH);
		if (err) {
			return err;
		}

		runtimes[i].index = i;
		runtimes[i].state = BTN_IDLE;
		k_work_init_delayable(&runtimes[i].timer, timer_handler);
		gpio_init_callback(&runtimes[i].gpio_cb, gpio_isr, BIT(defs[i].spec.pin));
		err = gpio_add_callback(defs[i].spec.port, &runtimes[i].gpio_cb);
		if (err) {
			return err;
		}

		edge_items[i].index = i;
		k_work_init(&edge_items[i].work, edge_work_handler);
	}

	return 0;
}
