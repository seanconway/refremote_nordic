#include "indicators.h"
#include "haptic.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>
#include <string.h>

/* §7.1/FS §10: each indicator is three PWM channels, R/G/B, wired common-
 * anode (bench-confirmed by diode test) — anode to VDD, GPIO sinks each
 * cathode through its own resistor. Active-low polarity is handled entirely
 * in the devicetree (nordic,invert + PWM_POLARITY_INVERTED, mirroring
 * pwm_led0's already-proven setup), so nothing here needs to know about it.
 */
struct rgb_channels {
	const struct pwm_dt_spec r;
	const struct pwm_dt_spec g;
	const struct pwm_dt_spec b;
};

static const struct rgb_channels f1_ch = {
	PWM_DT_SPEC_GET(DT_ALIAS(f1r)), PWM_DT_SPEC_GET(DT_ALIAS(f1g)), PWM_DT_SPEC_GET(DT_ALIAS(f1b)),
};
static const struct rgb_channels f2_ch = {
	PWM_DT_SPEC_GET(DT_ALIAS(f2r)), PWM_DT_SPEC_GET(DT_ALIAS(f2g)), PWM_DT_SPEC_GET(DT_ALIAS(f2b)),
};
static const struct rgb_channels link_ch = {
	PWM_DT_SPEC_GET(DT_ALIAS(linkr)), PWM_DT_SPEC_GET(DT_ALIAS(linkg)), PWM_DT_SPEC_GET(DT_ALIAS(linkb)),
};
static const struct rgb_channels pwr_ch = {
	PWM_DT_SPEC_GET(DT_ALIAS(pwrr)), PWM_DT_SPEC_GET(DT_ALIAS(pwrg)), PWM_DT_SPEC_GET(DT_ALIAS(pwrb)),
};

/* Shared by LED_LINK (§10.2) and LED_PWR (§10.1) — both are now 3-state
 * red/yellow/green ladders over the same three colours, not independent
 * palettes. */
static const uint8_t COLOR_RED[3]    = { 255, 0, 0 };
static const uint8_t COLOR_YELLOW[3] = { 255, 200, 0 };
static const uint8_t COLOR_GREEN[3]  = { 0, 255, 0 };

/* Mid-green until DN_SIMSOC says otherwise — "default to the green range,"
 * not 0, which would misrender as a real low-battery state before any test
 * value has ever been set. */
#define BATTERY_PCT_DEFAULT 80u

static uint8_t led_brightness = 100; /* DN_CONFIG default until told otherwise, matches haptic.c's haptic_scale */
static uint8_t battery_pct = BATTERY_PCT_DEFAULT;

static enum proto_ind_mode f1_mode, f2_mode;
static uint8_t f1_rgb[3], f2_rgb[3];
static bool radio_up, host_up;

/* §7.2: "link-lost also drives a repeating double buzz... a remote-local
 * behaviour with no representation on either protocol." The interval is not
 * pinned by any spec document; 2 s is chosen so the buzz is noticeable
 * without competing with real acknowledgement taps for attention. */
#define LOST_LINK_BUZZ_MS 2000

static struct k_work_delayable lost_link_work;
static struct k_work_q *ind_workq;

static uint32_t chan_pulse_ns(const struct pwm_dt_spec *ch, uint8_t level)
{
	/* level (0-255) scaled by led_brightness (0-100), same period*duty/100
	 * idiom haptic.c's start_pulse() uses. */
	uint32_t duty_of_255 = ((uint32_t)level * led_brightness) / 100u;

	return (uint32_t)(((uint64_t)ch->period * duty_of_255) / 255u);
}

static void render(const struct rgb_channels *ch, enum proto_ind_mode mode, const uint8_t rgb[3])
{
	uint8_t r = mode == PROTO_IND_SOLID ? rgb[0] : 0;
	uint8_t g = mode == PROTO_IND_SOLID ? rgb[1] : 0;
	uint8_t b = mode == PROTO_IND_SOLID ? rgb[2] : 0;

	(void)pwm_set_pulse_dt(&ch->r, chan_pulse_ns(&ch->r, r));
	(void)pwm_set_pulse_dt(&ch->g, chan_pulse_ns(&ch->g, g));
	(void)pwm_set_pulse_dt(&ch->b, chan_pulse_ns(&ch->b, b));
}

/* FS §10.2: red (radio down) / yellow (radio up, host down) / green (both
 * up). Always SOLID — there is no OFF state for this indicator any more. */
static void update_link(void)
{
	bool up = radio_up && host_up;
	const uint8_t *color = !radio_up ? COLOR_RED : host_up ? COLOR_GREEN : COLOR_YELLOW;

	render(&link_ch, PROTO_IND_SOLID, color);

	if (up) {
		(void)k_work_cancel_delayable(&lost_link_work);
	} else {
		/* Idempotent: reschedule is safe even if already pending.
		 * Still fires for yellow, not just red — §11's "Link lost"
		 * buzz means "not fully connected end to end," unchanged
		 * from the two-state version this replaces. */
		k_work_reschedule_for_queue(ind_workq, &lost_link_work,
					    K_MSEC(LOST_LINK_BUZZ_MS));
	}
}

/* FS §10.1: red 0-33%, yellow 33-66%, green 66-100%. Always SOLID — a
 * battery reading always has some value. */
static void update_battery(void)
{
	const uint8_t *color = battery_pct < 33u ? COLOR_RED
			      : battery_pct < 66u ? COLOR_YELLOW
			      : COLOR_GREEN;

	render(&pwr_ch, PROTO_IND_SOLID, color);
}

static void lost_link_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!(radio_up && host_up)) {
		haptic_render(PROTO_WF_DOUBLE, 0);
		k_work_reschedule_for_queue(ind_workq, &lost_link_work,
					    K_MSEC(LOST_LINK_BUZZ_MS));
	}
}

void indicators_set(enum proto_ind_mode new_f1_mode, const uint8_t new_f1_rgb[3],
		    enum proto_ind_mode new_f2_mode, const uint8_t new_f2_rgb[3])
{
	f1_mode = new_f1_mode;
	f2_mode = new_f2_mode;
	memcpy(f1_rgb, new_f1_rgb, sizeof(f1_rgb));
	memcpy(f2_rgb, new_f2_rgb, sizeof(f2_rgb));

	render(&f1_ch, f1_mode, f1_rgb);
	render(&f2_ch, f2_mode, f2_rgb);
}

void indicators_set_brightness(uint8_t brightness)
{
	led_brightness = brightness > 100u ? 100u : brightness;

	/* Brightness applies to whatever is already showing, so every
	 * rendered indicator needs a re-render at the new level. */
	render(&f1_ch, f1_mode, f1_rgb);
	render(&f2_ch, f2_mode, f2_rgb);
	update_link();
	update_battery();
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

void indicators_set_battery_pct(uint8_t pct)
{
	battery_pct = pct > 100u ? 100u : pct;
	update_battery();
}

int indicators_init(struct k_work_q *workq)
{
	const struct pwm_dt_spec *all[] = {
		&f1_ch.r, &f1_ch.g, &f1_ch.b,
		&f2_ch.r, &f2_ch.g, &f2_ch.b,
		&link_ch.r, &link_ch.g, &link_ch.b,
		&pwr_ch.r, &pwr_ch.g, &pwr_ch.b,
	};

	ind_workq = workq;

	for (size_t i = 0; i < ARRAY_SIZE(all); i++) {
		if (!pwm_is_ready_dt(all[i])) {
			return -ENODEV;
		}
		(void)pwm_set_pulse_dt(all[i], 0);
	}

	k_work_init_delayable(&lost_link_work, lost_link_handler);

	/* A16: on boot, assume DOWN until told otherwise. radio_up/host_up
	 * both default false above, so update_link() renders red and arms
	 * the buzz — exactly the state that is true at this moment. */
	update_link();
	update_battery();
	return 0;
}
