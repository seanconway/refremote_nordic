#include "haptic.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>

/* §2: the haptic proxy drives pwm_led0 (LED 1 / P0.13) only. led0's plain
 * gpio-leds node is deliberately unused — driving the same physical pin from
 * both drivers at once is a conflict that does not announce itself. */
static const struct pwm_dt_spec pwm = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led0));

/*
 * Base amplitudes and timing. RP §9/remote/BUILD_SPEC.md §7.3 pin only the
 * *character* of each waveform ("single short tap", "two pulses", ...) and
 * the one hard numeric requirement — BEAT distinctly weaker than TAP — not
 * exact millisecond values, so the pulse/gap durations below are an
 * implementation choice, pinned here rather than left implicit. Amplitude is
 * a base duty percentage, scaled by haptic_set_scale() at render time so the
 * BEAT:TAP ratio survives DN_CONFIG unchanged.
 */
struct pulse_shape {
	uint8_t base_duty_pct;
	uint16_t pulse_ms;
	uint16_t gap_ms;
	uint8_t pulse_count;
};

static const struct pulse_shape shapes[PROTO_WF_COUNT] = {
	[PROTO_WF_TAP]    = { 100, 40,  0,  1 },
	[PROTO_WF_BEAT]   = { 25,  40,  0,  1 }, /* distinctly weaker than TAP */
	[PROTO_WF_WARN]   = { 100, 150, 0,  1 },
	[PROTO_WF_BUZZ]   = { 100, 80,  0,  1 },
	[PROTO_WF_LONG]   = { 100, 400, 0,  1 },
	[PROTO_WF_DOUBLE] = { 100, 40,  60, 2 },
	[PROTO_WF_TRIPLE] = { 100, 40,  60, 3 },
};

/* PROTO_WF_COUNT as a sentinel: "nothing currently rendering". */
static enum proto_waveform active = PROTO_WF_COUNT;
static uint8_t pulses_left;
static bool in_gap;
static struct k_work_delayable phase_work;
static uint8_t haptic_scale = 100;
static struct k_work_q *haptic_workq;

static void pwm_off(void)
{
	(void)pwm_set_pulse_dt(&pwm, 0);
}

static void start_pulse(void)
{
	const struct pulse_shape *s = &shapes[active];
	uint32_t duty = ((uint32_t)s->base_duty_pct * haptic_scale) / 100u;
	uint32_t pulse_ns = (uint32_t)(((uint64_t)pwm.period * duty) / 100u);

	(void)pwm_set_pulse_dt(&pwm, pulse_ns);
	in_gap = false;
	k_work_reschedule_for_queue(haptic_workq, &phase_work, K_MSEC(s->pulse_ms));
}

static void phase_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	const struct pulse_shape *s = &shapes[active];

	if (!in_gap) {
		pwm_off();
		pulses_left--;
		if (pulses_left == 0) {
			active = PROTO_WF_COUNT;
			return;
		}
		in_gap = true;
		k_work_reschedule_for_queue(haptic_workq, &phase_work, K_MSEC(s->gap_ms));
		return;
	}

	start_pulse();
}

void haptic_render(enum proto_waveform w, uint8_t ttl_4ms)
{
	ARG_UNUSED(ttl_4ms); /* see haptic.h: not enforced here, by design */

	if (w >= PROTO_WF_COUNT) {
		return;
	}

	/* A10: a BEAT arriving while a TAP renders is dropped; the TAP is
	 * never interrupted or truncated. Every other case — including a TAP
	 * arriving mid-BEAT — wins and restarts (RP §8.1). */
	if (active == PROTO_WF_TAP && w == PROTO_WF_BEAT) {
		return;
	}

	(void)k_work_cancel_delayable(&phase_work);
	active = w;
	pulses_left = shapes[w].pulse_count;
	in_gap = false;
	start_pulse();
}

void haptic_set_scale(uint8_t scale)
{
	haptic_scale = scale > 100u ? 100u : scale;
}

int haptic_init(struct k_work_q *workq)
{
	haptic_workq = workq;

	if (!pwm_is_ready_dt(&pwm)) {
		return -ENODEV;
	}
	k_work_init_delayable(&phase_work, phase_handler);
	pwm_off();
	return 0;
}
