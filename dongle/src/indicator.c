#include "indicator.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/* Board aliases: led0 = D1 (green), led1 = D2 (red). Both GPIO_ACTIVE_LOW,
 * which gpio_pin_set_dt() handles for us — 1 means "lit". */
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

/* Pattern steps alternate on/off durations in milliseconds, starting with on. */
static const uint16_t pat_heartbeat[] = { 30 };
static const uint16_t pat_confirm[]   = { 40, 60, 40 };
static const uint16_t pat_expire[]    = { 500 };
static const uint16_t pat_error[]     = { 150 };

struct blinker {
	const struct gpio_dt_spec *led;
	struct k_work_delayable work;
	const uint16_t *steps;
	size_t count;
	size_t idx;
};

static struct blinker green;
static struct blinker red;

static void blinker_step(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct blinker *b = CONTAINER_OF(dwork, struct blinker, work);

	if (b->idx >= b->count) {
		(void)gpio_pin_set_dt(b->led, 0);
		return;
	}

	(void)gpio_pin_set_dt(b->led, (b->idx % 2u == 0u) ? 1 : 0);
	k_work_reschedule(&b->work, K_MSEC(b->steps[b->idx]));
	b->idx++;
}

/* Last pattern wins — a new request restarts the sequence rather than queueing. */
static void blinker_start(struct blinker *b, const uint16_t *steps, size_t count)
{
	b->steps = steps;
	b->count = count;
	b->idx = 0;
	k_work_reschedule(&b->work, K_NO_WAIT);
}

static int blinker_init(struct blinker *b, const struct gpio_dt_spec *led)
{
	int err;

	if (!gpio_is_ready_dt(led)) {
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(led, GPIO_OUTPUT_INACTIVE);
	if (err != 0) {
		return err;
	}

	b->led = led;
	k_work_init_delayable(&b->work, blinker_step);
	return 0;
}

int indicator_init(void)
{
	int err = blinker_init(&green, &led_green);

	if (err != 0) {
		return err;
	}
	return blinker_init(&red, &led_red);
}

void indicator_heartbeat(void)
{
	blinker_start(&green, pat_heartbeat, ARRAY_SIZE(pat_heartbeat));
}

void indicator_confirm(void)
{
	blinker_start(&green, pat_confirm, ARRAY_SIZE(pat_confirm));
}

void indicator_expire(void)
{
	blinker_start(&red, pat_expire, ARRAY_SIZE(pat_expire));
}

void indicator_error(void)
{
	blinker_start(&red, pat_error, ARRAY_SIZE(pat_error));
}
