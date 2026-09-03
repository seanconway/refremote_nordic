#include "haptic.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

/* PROTO_WF_COUNT as a sentinel: "nothing currently rendering". Shared by
 * both backends below -- the BEAT-vs-TAP priority rule (RP §8.1: "A BEAT
 * arriving while a TAP renders is dropped; the TAP is never truncated")
 * is app-level policy, identical regardless of what actually renders it. */
static enum proto_waveform active = PROTO_WF_COUNT;
static uint8_t haptic_scale = 100;
static struct k_work_q *haptic_workq;

#ifdef CONFIG_REMOTE_HAPTIC_PROXY_LED

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

static uint8_t pulses_left;
static bool in_gap;
static struct k_work_delayable phase_work;

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

#else /* !CONFIG_REMOTE_HAPTIC_PROXY_LED -- real DRV2605L library playback */

#include "drv2605.h"

/*
 * Waveform -> ROM library effect index (datasheets/drv2605l.pdf §12.1.2,
 * TS2200 library). A starting point to bench-verify by feel, not a final
 * assignment — see PLAN.md's entry on this change for the full reasoning.
 * BEAT is deliberately the *same click family as TAP, at a fixed weaker
 * strength* (effect 6 vs effect 5, "Sharp Click" at 30% vs 60%) rather
 * than an unrelated effect: BUILD_SPEC.md §7.3 requires BEAT to be
 * "distinctly weaker... not different in principle, different in
 * sensation", which picking two strength variants of one effect satisfies
 * by construction, the same way the PWM proxy's shared base_duty_pct did.
 */
static const uint8_t effect_single[PROTO_WF_COUNT] = {
	[PROTO_WF_TAP]    = 5,  /* Sharp Click - 60% */
	[PROTO_WF_BEAT]   = 6,  /* Sharp Click - 30% */
	[PROTO_WF_WARN]   = 7,  /* Soft Bump - 100% -- different character than Click, unmistakable */
	[PROTO_WF_BUZZ]   = 47, /* Buzz 1 - 100% */
	[PROTO_WF_LONG]   = 14, /* Strong Buzz - 100% */
	[PROTO_WF_DOUBLE] = 10, /* Double Click - 100%, correct spacing built in */
	[PROTO_WF_TRIPLE] = 0,  /* not used -- see triple_seq below */
};

/* No dedicated "triple" entry found in the library list, so TRIPLE chains
 * the same click three times via WAV_FRM_SEQ (drv2605_play_sequence()) --
 * the hardware feature DOUBLE could also use, just not needed there since
 * effect 10 already encodes correct double-click spacing. */
static const uint8_t triple_seq[3] = { 1, 1, 1 };

/*
 * Estimated render duration per waveform, ms — for priority-window
 * bookkeeping only (deciding whether a BEAT arrives "while a TAP renders"),
 * not for driving the motor: the DRV2605L renders each effect's timing
 * itself once triggered. Estimates, not measured — bench-verify and
 * shorten/lengthen once the real effects are felt.
 */
static const uint16_t est_duration_ms[PROTO_WF_COUNT] = {
	[PROTO_WF_TAP]    = 100,
	[PROTO_WF_BEAT]   = 100,
	[PROTO_WF_WARN]   = 300,
	[PROTO_WF_BUZZ]   = 150,
	[PROTO_WF_LONG]   = 600,
	[PROTO_WF_DOUBLE] = 300,
	[PROTO_WF_TRIPLE] = 450,
};

static struct k_work_delayable active_timeout_work;

static void active_timeout_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	active = PROTO_WF_COUNT;
}

void haptic_render(enum proto_waveform w, uint8_t ttl_4ms)
{
	ARG_UNUSED(ttl_4ms); /* see haptic.h: not enforced here, by design */

	if (w >= PROTO_WF_COUNT) {
		return;
	}

	/* Same rule, same reasoning as the PWM backend above — RP §8.1. */
	if (active == PROTO_WF_TAP && w == PROTO_WF_BEAT) {
		return;
	}

	(void)k_work_cancel_delayable(&active_timeout_work);
	active = w;

	/*
	 * No explicit stop before playing: retriggering GO (inside
	 * drv2605_play_sequence()) while something is still rendering
	 * interrupts and restarts it, which is exactly "a frame arriving
	 * mid-render wins and restarts the motor" (RP §8.1) — the DRV2605L
	 * already does the thing this rule asks for.
	 */
	if (w == PROTO_WF_TRIPLE) {
		(void)drv2605_play_sequence(triple_seq, ARRAY_SIZE(triple_seq));
	} else {
		uint8_t one = effect_single[w];

		(void)drv2605_play_sequence(&one, 1);
	}

	k_work_reschedule_for_queue(haptic_workq, &active_timeout_work,
				    K_MSEC(est_duration_ms[w]));
}

void haptic_set_scale(uint8_t scale)
{
	haptic_scale = scale > 100u ? 100u : scale;

	/*
	 * NOT YET APPLIED. The obvious mechanism -- scale OD_CLAMP at
	 * runtime -- is wrong here, not just unimplemented: OD_CLAMP is a
	 * calibration INPUT (drv2605.c's OD_CLAMP_TARGET), and the datasheet
	 * is explicit that changing it invalidates A_CAL_BEMF until
	 * calibration reruns. Rewriting it on every DN_CONFIG would silently
	 * run a match on stale BEMF calibration. Real amplitude control for
	 * library effects needs a per-tier effect selection (the same
	 * "different index, not a scaled shared parameter" approach already
	 * used for BEAT vs TAP above), which needs the felt bench pass this
	 * whole waveform table is waiting on. Tracked, not silently dropped.
	 */
}

int haptic_init(struct k_work_q *workq)
{
	/* drv2605_init() already ran in main.c, before this — nothing to
	 * open here. */
	haptic_workq = workq;
	k_work_init_delayable(&active_timeout_work, active_timeout_handler);
	return 0;
}

#endif /* CONFIG_REMOTE_HAPTIC_PROXY_LED */
