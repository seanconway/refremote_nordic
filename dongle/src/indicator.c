#include "indicator.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/* Board aliases: led0 = D1 (green), led1 = D2 (red). Both GPIO_ACTIVE_LOW,
 * which gpio_pin_set_dt() handles for us — 1 means "lit". */
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

/*
 * Pattern steps alternate on/off durations in milliseconds, starting with on,
 * and the LED returns to its base level when the sequence runs out.
 *
 * The durations are chosen to be *distinguishable on a bench*, which is a
 * different requirement from the wrist waveforms they stand for — see the
 * header. BEAT being merely shorter than TAP is the clearest case: on the
 * wrist the distinction is amplitude, and it is load-bearing for repeated-press
 * scoring (§9.1). Nothing observed here validates it.
 */
static const uint16_t pat_tap[]    = { 40 };
static const uint16_t pat_beat[]   = { 12 };
static const uint16_t pat_warn[]   = { 200 };
static const uint16_t pat_buzz[]   = { 120 };
static const uint16_t pat_long[]   = { 500 };
static const uint16_t pat_double[] = { 60, 70, 60 };
static const uint16_t pat_triple[] = { 60, 70, 60, 70, 60 };
static const uint16_t pat_error[]  = { 150 };

struct waveform_pattern {
	const uint16_t *steps;
	size_t count;
};

#define PATTERN(p) { (p), ARRAY_SIZE(p) }

static const struct waveform_pattern waveforms[PROTO_WF_COUNT] = {
	[PROTO_WF_TAP]    = PATTERN(pat_tap),
	[PROTO_WF_BEAT]   = PATTERN(pat_beat),
	[PROTO_WF_WARN]   = PATTERN(pat_warn),
	[PROTO_WF_BUZZ]   = PATTERN(pat_buzz),
	[PROTO_WF_LONG]   = PATTERN(pat_long),
	[PROTO_WF_DOUBLE] = PATTERN(pat_double),
	[PROTO_WF_TRIPLE] = PATTERN(pat_triple),
};

struct blinker {
	const struct gpio_dt_spec *led;
	struct k_work_delayable work;
	const uint16_t *steps;
	size_t count;
	size_t idx;
	bool base;
};

static struct blinker leds[PROTO_REMOTE_COUNT];

/* Not a remote's LED: fault indication borrows red, because a fault is not
 * attributable to one wrist and the alternative is a third LED this board does
 * not have. */
static struct blinker *const fault_led = &leds[PROTO_REMOTE_RED];

static void blinker_step(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct blinker *b = CONTAINER_OF(dwork, struct blinker, work);

	if (b->idx >= b->count) {
		/* Restore the steady level rather than switching off: a haptic
		 * command must not clear indicator state the app asserted. */
		(void)gpio_pin_set_dt(b->led, b->base ? 1 : 0);
		return;
	}

	(void)gpio_pin_set_dt(b->led, (b->idx % 2u == 0u) ? 1 : 0);
	k_work_reschedule(&b->work, K_MSEC(b->steps[b->idx]));
	b->idx++;
}

/* Last pattern wins — a new request restarts the sequence rather than queueing.
 * Queueing would render a burst of taps as a longer buzz, which is the opposite
 * of what a referee counting acknowledgements needs. */
static void blinker_start(struct blinker *b, const struct waveform_pattern *p)
{
	if (p->steps == NULL || p->count == 0) {
		return;
	}
	b->steps = p->steps;
	b->count = p->count;
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
	b->base = false;
	k_work_init_delayable(&b->work, blinker_step);
	return 0;
}

int indicator_init(void)
{
	int err = blinker_init(&leds[PROTO_REMOTE_RED], &led_red);

	if (err != 0) {
		return err;
	}
	return blinker_init(&leds[PROTO_REMOTE_GREEN], &led_green);
}

void indicator_base(enum proto_remote r, bool lit)
{
	struct blinker *b;

	if (r >= PROTO_REMOTE_COUNT) {
		return;
	}
	b = &leds[r];
	b->base = lit;

	/* Only take the LED now if no pattern is running; otherwise the pattern
	 * will land on the new base when it finishes. */
	if (b->idx >= b->count) {
		(void)gpio_pin_set_dt(b->led, lit ? 1 : 0);
	}
}

void indicator_haptic(enum proto_remote r, enum proto_waveform w)
{
	if (r >= PROTO_REMOTE_COUNT || w >= PROTO_WF_COUNT) {
		return;
	}
	blinker_start(&leds[r], &waveforms[w]);
}

void indicator_error(void)
{
	static const struct waveform_pattern err_pattern = PATTERN(pat_error);

	blinker_start(fault_led, &err_pattern);
}
