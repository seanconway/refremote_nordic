#include "rframe.h"

#include <string.h>

/* ------------------------------------------------------------------------ */
/* Decoding                                                                  */
/* ------------------------------------------------------------------------ */

enum rframe_decode_status rframe_decode(const uint8_t *buf, size_t len,
					struct rframe_msg *out)
{
	uint8_t type_byte;

	if (len < 1) {
		/* Nothing to classify — there is no TYPE byte to read. Nearest
		 * in spirit to A2: there is no known type to count a failure
		 * against. */
		return RFRAME_ERR_UNKNOWN_TYPE;
	}
	type_byte = buf[0];

	switch (type_byte) {
	case RFRAME_UP_INPUT: {
		uint8_t bw, gw;

		if (len != 4) {
			return RFRAME_ERR_LENGTH;
		}
		bw = buf[2];
		gw = buf[3];
		/* Wire values are 1-based (RP §5.3); PROTO_BTN_COUNT /
		 * PROTO_GEST_COUNT are the 0-based enum's size, so a wire byte
		 * of exactly that count is one past the last valid value. */
		if (bw < 1 || bw > PROTO_BTN_COUNT) {
			return RFRAME_ERR_FIELD;
		}
		if (gw < 1 || gw > PROTO_GEST_COUNT) {
			return RFRAME_ERR_FIELD;
		}
		out->type = RFRAME_UP_INPUT;
		out->ctr = buf[1];
		out->up_input.button = (enum proto_button)(bw - 1);
		out->up_input.gesture = (enum proto_gesture)(gw - 1);
		return RFRAME_OK;
	}

	case RFRAME_UP_READY: {
		uint8_t reason;

		if (len != 4) {
			return RFRAME_ERR_LENGTH;
		}
		reason = buf[3];
		if (reason != RFRAME_READY_BOOT && reason != RFRAME_READY_RECONNECT) {
			return RFRAME_ERR_FIELD;
		}
		out->type = RFRAME_UP_READY;
		out->ctr = buf[1];
		out->up_ready.ctr_base = buf[2];
		out->up_ready.reason = (enum rframe_ready_reason)reason;
		return RFRAME_OK;
	}

	case RFRAME_UP_TELEMETRY: {
		if (len != 6) {
			return RFRAME_ERR_LENGTH;
		}
		out->type = RFRAME_UP_TELEMETRY;
		out->ctr = buf[1];
		out->up_telemetry.battery_pct = buf[2];
		out->up_telemetry.flags = buf[3];
		out->up_telemetry.dn_lost = buf[4];
		/* buf[5] is reserved (RP §5.6) and deliberately not inspected. */
		return RFRAME_OK;
	}

	case RFRAME_UP_DIAG: {
		if (len < RFRAME_HEADER_LEN || len > RFRAME_MAX_LEN) {
			return RFRAME_ERR_LENGTH;
		}
		out->type = RFRAME_UP_DIAG;
		out->ctr = buf[1];
		out->up_diag.len = len - RFRAME_HEADER_LEN;
		out->up_diag.data = out->up_diag.len > 0 ? &buf[2] : NULL;
		return RFRAME_OK;
	}

	case RFRAME_DN_HAPTIC: {
		uint8_t ww;

		if (len != 4) {
			return RFRAME_ERR_LENGTH;
		}
		ww = buf[2];
		if (ww < 1 || ww > PROTO_WF_COUNT) {
			return RFRAME_ERR_FIELD;
		}
		out->type = RFRAME_DN_HAPTIC;
		out->ctr = buf[1];
		out->dn_haptic.waveform = (enum proto_waveform)(ww - 1);
		out->dn_haptic.ttl_4ms = buf[3];
		return RFRAME_OK;
	}

	case RFRAME_DN_INDICATOR: {
		uint8_t m1, m2, c1, c2;

		if (len != 6) {
			return RFRAME_ERR_LENGTH;
		}
		m1 = buf[2];
		c1 = buf[3];
		m2 = buf[4];
		c2 = buf[5];
		/* PROTO_IND_OFF/SOLID and PROTO_COLOUR_* are 0-based on the
		 * wire with no offset (RP §5.5), unlike button/gesture/
		 * waveform. */
		if (m1 > PROTO_IND_SOLID || m2 > PROTO_IND_SOLID) {
			return RFRAME_ERR_FIELD;
		}
		if (c1 >= PROTO_COLOUR_COUNT || c2 >= PROTO_COLOUR_COUNT) {
			return RFRAME_ERR_FIELD;
		}
		out->type = RFRAME_DN_INDICATOR;
		out->ctr = buf[1];
		out->dn_indicator.f1_mode = (enum proto_ind_mode)m1;
		out->dn_indicator.f1_colour = (enum proto_ind_colour)c1;
		out->dn_indicator.f2_mode = (enum proto_ind_mode)m2;
		out->dn_indicator.f2_colour = (enum proto_ind_colour)c2;
		return RFRAME_OK;
	}

	case RFRAME_DN_CONFIG: {
		if (len != 4) {
			return RFRAME_ERR_LENGTH;
		}
		out->type = RFRAME_DN_CONFIG;
		out->ctr = buf[1];
		out->dn_config.haptic_scale = buf[2];
		out->dn_config.led_brightness = buf[3];
		return RFRAME_OK;
	}

	case RFRAME_DN_HOST: {
		uint8_t st;

		if (len != 3) {
			return RFRAME_ERR_LENGTH;
		}
		st = buf[2];
		if (st > 1) {
			return RFRAME_ERR_FIELD;
		}
		out->type = RFRAME_DN_HOST;
		out->ctr = buf[1];
		out->dn_host.up = (st == 1);
		return RFRAME_OK;
	}

	case RFRAME_DN_SIMSOC: {
		uint8_t pct;

		if (len != 3) {
			return RFRAME_ERR_LENGTH;
		}
		pct = buf[2];
		if (pct > 100) {
			return RFRAME_ERR_FIELD;
		}
		out->type = RFRAME_DN_SIMSOC;
		out->ctr = buf[1];
		out->dn_simsoc.pct = pct;
		return RFRAME_OK;
	}

	/* Bench-only, RADIO_PROTOCOL.md's factory addendum. No payload beyond
	 * the header — see rframe.h's comment on why. */
	case RFRAME_DN_CAL_TRIGGER: {
		if (len != RFRAME_HEADER_LEN) {
			return RFRAME_ERR_LENGTH;
		}
		out->type = RFRAME_DN_CAL_TRIGGER;
		out->ctr = buf[1];
		return RFRAME_OK;
	}

	case RFRAME_DN_OTP_BURN: {
		if (len != RFRAME_HEADER_LEN) {
			return RFRAME_ERR_LENGTH;
		}
		out->type = RFRAME_DN_OTP_BURN;
		out->ctr = buf[1];
		return RFRAME_OK;
	}

	case RFRAME_UP_FACTORY_STATUS: {
		uint8_t state;

		if (len != 10) {
			return RFRAME_ERR_LENGTH;
		}
		state = buf[2];
		if (state > RFRAME_FACTORY_OTP_FAILED) {
			return RFRAME_ERR_FIELD;
		}
		out->type = RFRAME_UP_FACTORY_STATUS;
		out->ctr = buf[1];
		out->up_factory_status.state = (enum rframe_factory_state)state;
		out->up_factory_status.detail = buf[3];
		out->up_factory_status.diag_pass = (buf[4] != 0);
		out->up_factory_status.vdd_mv =
			(uint16_t)((uint16_t)buf[5] | ((uint16_t)buf[6] << 8));
		out->up_factory_status.a_cal_comp = buf[7];
		out->up_factory_status.a_cal_bemf = buf[8];
		out->up_factory_status.feedback_control = buf[9];
		return RFRAME_OK;
	}

	default:
		return RFRAME_ERR_UNKNOWN_TYPE;
	}
}

/* ------------------------------------------------------------------------ */
/* Encoding                                                                  */
/* ------------------------------------------------------------------------ */

int rframe_enc_up_input(uint8_t *out, size_t cap, uint8_t ctr,
			enum proto_button b, enum proto_gesture g)
{
	if (cap < 4) {
		return -1;
	}
	out[0] = RFRAME_UP_INPUT;
	out[1] = ctr;
	out[2] = (uint8_t)((uint8_t)b + 1u);
	out[3] = (uint8_t)((uint8_t)g + 1u);
	return 4;
}

int rframe_enc_up_ready(uint8_t *out, size_t cap, uint8_t ctr,
			uint8_t ctr_base, enum rframe_ready_reason reason)
{
	if (cap < 4) {
		return -1;
	}
	out[0] = RFRAME_UP_READY;
	out[1] = ctr;
	out[2] = ctr_base;
	out[3] = (uint8_t)reason;
	return 4;
}

int rframe_enc_up_telemetry(uint8_t *out, size_t cap, uint8_t ctr,
			    uint8_t battery_pct, uint8_t flags, uint8_t dn_lost)
{
	if (cap < 6) {
		return -1;
	}
	out[0] = RFRAME_UP_TELEMETRY;
	out[1] = ctr;
	out[2] = battery_pct;
	out[3] = flags;
	out[4] = dn_lost;
	out[5] = 0; /* reserved */
	return 6;
}

int rframe_enc_up_diag(uint8_t *out, size_t cap, uint8_t ctr,
		       const uint8_t *data, size_t len)
{
	if (len > RFRAME_MAX_LEN - RFRAME_HEADER_LEN) {
		return -1;
	}
	if (RFRAME_HEADER_LEN + len > cap) {
		return -1;
	}
	out[0] = RFRAME_UP_DIAG;
	out[1] = ctr;
	if (len > 0) {
		memcpy(&out[2], data, len);
	}
	return (int)(RFRAME_HEADER_LEN + len);
}

int rframe_enc_dn_haptic(uint8_t *out, size_t cap, uint8_t ctr,
			 enum proto_waveform w, uint8_t ttl_4ms)
{
	if (cap < 4) {
		return -1;
	}
	out[0] = RFRAME_DN_HAPTIC;
	out[1] = ctr;
	out[2] = (uint8_t)((uint8_t)w + 1u);
	out[3] = ttl_4ms;
	return 4;
}

int rframe_enc_dn_indicator(uint8_t *out, size_t cap, uint8_t ctr,
			    enum proto_ind_mode f1_mode, enum proto_ind_colour f1_colour,
			    enum proto_ind_mode f2_mode, enum proto_ind_colour f2_colour)
{
	if (cap < 6) {
		return -1;
	}
	out[0] = RFRAME_DN_INDICATOR;
	out[1] = ctr;
	out[2] = (uint8_t)f1_mode;
	out[3] = (uint8_t)f1_colour;
	out[4] = (uint8_t)f2_mode;
	out[5] = (uint8_t)f2_colour;
	return 6;
}

int rframe_enc_dn_config(uint8_t *out, size_t cap, uint8_t ctr,
			 uint8_t haptic_scale, uint8_t led_brightness)
{
	if (cap < 4) {
		return -1;
	}
	out[0] = RFRAME_DN_CONFIG;
	out[1] = ctr;
	out[2] = haptic_scale;
	out[3] = led_brightness;
	return 4;
}

int rframe_enc_dn_host(uint8_t *out, size_t cap, uint8_t ctr, bool up)
{
	if (cap < 3) {
		return -1;
	}
	out[0] = RFRAME_DN_HOST;
	out[1] = ctr;
	out[2] = up ? 1 : 0;
	return 3;
}

int rframe_enc_dn_simsoc(uint8_t *out, size_t cap, uint8_t ctr, uint8_t pct)
{
	if (cap < 3 || pct > 100) {
		return -1;
	}
	out[0] = RFRAME_DN_SIMSOC;
	out[1] = ctr;
	out[2] = pct;
	return 3;
}

int rframe_enc_dn_cal_trigger(uint8_t *out, size_t cap, uint8_t ctr)
{
	if (cap < RFRAME_HEADER_LEN) {
		return -1;
	}
	out[0] = RFRAME_DN_CAL_TRIGGER;
	out[1] = ctr;
	return RFRAME_HEADER_LEN;
}

int rframe_enc_dn_otp_burn(uint8_t *out, size_t cap, uint8_t ctr)
{
	if (cap < RFRAME_HEADER_LEN) {
		return -1;
	}
	out[0] = RFRAME_DN_OTP_BURN;
	out[1] = ctr;
	return RFRAME_HEADER_LEN;
}

int rframe_enc_up_factory_status(uint8_t *out, size_t cap, uint8_t ctr,
				 enum rframe_factory_state state, uint8_t detail,
				 bool diag_pass, uint16_t vdd_mv, uint8_t a_cal_comp,
				 uint8_t a_cal_bemf, uint8_t feedback_control)
{
	if (cap < 10) {
		return -1;
	}
	out[0] = RFRAME_UP_FACTORY_STATUS;
	out[1] = ctr;
	out[2] = (uint8_t)state;
	out[3] = detail;
	out[4] = diag_pass ? 1u : 0u;
	out[5] = (uint8_t)(vdd_mv & 0xFFu);
	out[6] = (uint8_t)(vdd_mv >> 8);
	out[7] = a_cal_comp;
	out[8] = a_cal_bemf;
	out[9] = feedback_control;
	return 10;
}

/* ------------------------------------------------------------------------ */
/* CTR arithmetic                                                            */
/* ------------------------------------------------------------------------ */

void rframe_ctr_init(struct rframe_ctr_state *s)
{
	s->last = 0;
}

void rframe_ctr_rebaseline(struct rframe_ctr_state *s, uint8_t ctr_base)
{
	/* ctr_base is "the value CTR will take on the next uplink frame" (RP
	 * §7.2), so set `last` one behind it: the next accepted frame then
	 * reads as EXPECTED, and no gap is computed for the reset itself. */
	s->last = (uint8_t)(ctr_base - 1u);
}

enum rframe_ctr_result rframe_ctr_accept(struct rframe_ctr_state *s, uint8_t ctr,
					 uint8_t *out_gap)
{
	uint8_t expected = (uint8_t)(s->last + 1u);
	uint8_t back;
	uint8_t gap;

	if (ctr == expected) {
		s->last = ctr;
		if (out_gap) {
			*out_gap = 0;
		}
		return RFRAME_CTR_EXPECTED;
	}

	/* How far behind `last` this ctr sits, mod 256. 0 means ctr == last
	 * itself (A4's exact case); up to RFRAME_CTR_DUP_WINDOW is still
	 * within the replay window. */
	back = (uint8_t)(s->last - ctr);
	if (back <= RFRAME_CTR_DUP_WINDOW) {
		if (out_gap) {
			*out_gap = 0;
		}
		return RFRAME_CTR_DUPLICATE;
	}

	/* Ahead of `last + 1`: a real gap. Accept it — A5 is explicit that the
	 * event itself must not be withheld because an earlier one was lost. */
	gap = (uint8_t)(ctr - s->last - 1u);
	s->last = ctr;
	if (out_gap) {
		*out_gap = gap;
	}
	return RFRAME_CTR_GAP;
}

const char *rframe_type_name(enum rframe_type t)
{
	switch (t) {
	case RFRAME_UP_INPUT:     return "UP_INPUT";
	case RFRAME_UP_READY:     return "UP_READY";
	case RFRAME_UP_TELEMETRY: return "UP_TELEMETRY";
	case RFRAME_UP_DIAG:            return "UP_DIAG";
	case RFRAME_UP_FACTORY_STATUS:  return "UP_FACTORY_STATUS";
	case RFRAME_DN_HAPTIC:          return "DN_HAPTIC";
	case RFRAME_DN_INDICATOR:       return "DN_INDICATOR";
	case RFRAME_DN_CONFIG:          return "DN_CONFIG";
	case RFRAME_DN_HOST:            return "DN_HOST";
	case RFRAME_DN_SIMSOC:          return "DN_SIMSOC";
	case RFRAME_DN_CAL_TRIGGER:     return "DN_CAL_TRIGGER";
	case RFRAME_DN_OTP_BURN:        return "DN_OTP_BURN";
	default:                        return "?";
	}
}
