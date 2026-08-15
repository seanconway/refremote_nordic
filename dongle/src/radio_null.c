/*
 * The no-radio implementation. CONFIG_DONGLE_RADIO=n.
 *
 * Both remotes are DISCONNECTED and stay that way; the downlink is rendered on
 * the dongle's two LEDs. Everything above the seam — the parser, the pending
 * table, supervision, the TEST modes, the whole wire layer — runs exactly as it
 * does with a radio, which is the point.
 *
 * THIS REPLACES CONFIG_DONGLE_FAKE_LINK, WHICH WAS DELETED RATHER THAN
 * DEFAULTED OFF. The fake reported both remotes CONNECTED with a fixed RSSI and
 * a fixed battery percentage, so the scoreboard's signal and battery indicators
 * could be exercised before the radio existed. That was reasonable then and is
 * a standing hazard now: it fabricates precisely the values a link test is
 * trying to measure, and it does so plausibly. A Kconfig default is no
 * protection when the failure mode is forgetting to change it — a forgotten `y`
 * produces a passing test. See PLAN.md §4.11 and BUILD_SPEC §3.1.
 *
 * This file tells the truth instead: nothing is connected, because nothing is.
 */
#include "radio.h"
#include "indicator.h"

#include <zephyr/kernel.h>

int radio_init(const struct radio_cb *cb, struct k_work_q *workq,
	       const struct provisioning_record *prov)
{
	/*
	 * Neither is stored, and that is not an oversight.
	 *
	 * radio_null never originates anything: there is no remote to press a
	 * button, come ready, or report telemetry, and link state never changes
	 * from the DISCONNECTED the engine already holds at boot. Keeping a
	 * pointer we would never call would only invite someone to call it.
	 */
	ARG_UNUSED(cb);
	ARG_UNUSED(workq);
	ARG_UNUSED(prov);
	return 0;
}

int radio_send_haptic(enum proto_remote r, enum proto_waveform w,
		      uint8_t ttl_4ms)
{
	/*
	 * The ttl is discarded because there is no transmission to outlive.
	 * Deadline enforcement that matters happens above this seam — the
	 * engine refuses to hand over a tap whose budget is already spent
	 * (BUILD_SPEC §6.3 mechanism 1), and that check is exercised in this
	 * configuration exactly as it is with a radio.
	 */
	ARG_UNUSED(ttl_4ms);

	indicator_haptic(r, w);
	return 0;
}

int radio_send_indicator(enum proto_remote r, const struct indicator_state *s)
{
	/*
	 * Two LEDs cannot render four indicators and their colours. What they
	 * can render honestly is whether this remote is holding *any* app-owned
	 * state, which is enough to tell an idempotent re-assertion from a
	 * change, and that is what STATE is on the bench for.
	 */
	indicator_base(r, s->f1_mode != PROTO_IND_OFF || s->f2_mode != PROTO_IND_OFF);
	return 0;
}

int radio_send_config(enum proto_remote r, uint8_t haptic, uint8_t bright)
{
	/*
	 * Nothing to apply. The board LEDs are driven as GPIO, not PWM, so
	 * there is no brightness to scale and no motor to quieten — and a
	 * stand-in that *appeared* to honour CFG would be the FAKE_LINK mistake
	 * in a different costume. The engine still persists the values and
	 * re-sends them on JOIN, which is the part that has to be right.
	 */
	ARG_UNUSED(r);
	ARG_UNUSED(haptic);
	ARG_UNUSED(bright);
	return 0;
}

int radio_send_simsoc(enum proto_remote r, uint8_t pct)
{
	/*
	 * Nothing to apply, same reasoning as radio_send_config() above:
	 * there is no LED_PWR on this board's two-GPIO indicator stand-in for
	 * a simulated value to drive, and a stand-in that pretended to render
	 * it would be the FAKE_LINK mistake again.
	 */
	ARG_UNUSED(r);
	ARG_UNUSED(pct);
	return 0;
}

int radio_send_host(enum proto_remote r, bool up)
{
	/*
	 * A15 — instructing both remotes to render link-lost on supervision
	 * expiry — is the one supervision obligation with no executable
	 * reference anywhere, because the emulator models no radio. It cannot
	 * be verified here either; it is verified at Stage 4 by watching
	 * LED_LINK on the DK. Recorded so nobody reads a green no-radio run as
	 * covering it.
	 */
	ARG_UNUSED(r);
	ARG_UNUSED(up);
	return 0;
}

bool radio_is_ready(enum proto_remote r)
{
	ARG_UNUSED(r);
	return false;
}

int8_t radio_rssi(enum proto_remote r)
{
	ARG_UNUSED(r);
	return 0;
}
