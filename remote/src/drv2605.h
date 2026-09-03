/*
 * DRV2605L driver -- the real (non-bench) module. Owns the calibration and
 * OTP-burn sequences the factory link protocol (RADIO_PROTOCOL.md's factory
 * addendum, dispatched from link.c) drives, and library-effect playback,
 * haptic.c's DRV2605L back end (CONFIG_REMOTE_HAPTIC_PROXY_LED=n,
 * BUILD_SPEC.md §13). Register I/O only -- no waveform-table policy (which
 * effect means what, priority between waveforms) lives here; that is
 * haptic.c's job, same division as it always was with the PWM proxy.
 */
#ifndef REMOTE_DRV2605_H_
#define REMOTE_DRV2605_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Which physical actuator is on the output, and the register inputs that
 * follow from its datasheet. The DRV2605L drives an ERM and an LRA with
 * different equations, a different ROM library and a different loop, so
 * "which motor" is not one bit -- it is this whole struct, and every field
 * has to be written before auto-calibration for the result to mean
 * anything (datasheet S8.5.6 step 3).
 *
 * Fields the datasheet gives one recommended value for regardless of
 * actuator (FB_BRAKE_FACTOR, LOOP_GAIN, SAMPLE_TIME, BLANKING_TIME,
 * IDISS_TIME, ZC_DET_TIME, AUTO_CAL_TIME -- all "valid for most actuators"
 * in S8.5.6) are deliberately NOT fields: they are constants in drv2605.c,
 * and they stay constants until a bench measurement says one of these
 * motors needs its own value. A knob nothing has yet turned is a knob that
 * gets turned by accident.
 */
enum drv2605_actuator_kind {
	DRV2605_ERM,
	DRV2605_LRA,
};

struct drv2605_actuator {
	enum drv2605_actuator_kind kind;  /* FEEDBACK_CONTROL.N_ERM_LRA */
	uint8_t rated_voltage;            /* 0x16 -- Eq. 4 (ERM) or Eq. 5 (LRA) */
	uint8_t od_clamp;                 /* 0x17 -- Eq. 6 (ERM) or Eq. 9 (LRA) */
	uint8_t drive_time;               /* Control1[4:0]. LRA: half the resonant
					   * period, the auto-resonance engine's
					   * starting guess (S8.5.1.1). ERM: the
					   * back-EMF sample rate, and the reset
					   * default 0x13 is the right answer
					   * absent a reason */
	uint8_t library;                  /* 0x03 LIBRARY_SEL. 1-5 and 7 are the
					   * ERM libraries (Table 1, chosen by the
					   * motor's rated voltage and rise time),
					   * 6 is the LRA library. Never left
					   * implicit: the reset default is 1, so
					   * an LRA on an unwritten chip plays ERM
					   * waveforms and feels wrong for a reason
					   * no register dump shows */
	bool open_loop;                   /* Control3. The ERM libraries were
					   * designed for open-loop drive (S8.6.23)
					   * and ERM_OPEN_LOOP resets to 1 to match;
					   * the LRA library is closed-loop and
					   * LRA_OPEN_LOOP resets to 0. Written
					   * explicitly either way -- in open loop
					   * RATED_VOLTAGE is ignored and OD_CLAMP
					   * alone sets full scale, which changes
					   * what a calibration result even means */
};

/* The product's motor, the Vybronics VZ7AL2B1690002 ERM -- the targets
 * drv2605_calibrate() uses and drv2605_init() applies. Exposed so bench
 * firmware can start from the product's own numbers rather than a second
 * copy of them that drifts. */
extern const struct drv2605_actuator drv2605_actuator_erm_vz7al2b;

struct drv2605_cal_result {
	bool passed;              /* STATUS.DIAG_RESULT == 0 */
	uint8_t status;            /* raw STATUS register (0x00), for diagnostics */
	uint8_t a_cal_comp;         /* 0x18 */
	uint8_t a_cal_bemf;         /* 0x19 */
	uint8_t feedback_control;   /* 0x1A, post-calibration */
	int go_reads_ok;            /* successful I2C reads of GO while waiting for
				      * it to clear -- 0 with a nonzero return code
				      * means the I2C bus itself was failing, not
				      * the calibration engine; only meaningful
				      * when this call returned an error */
};

enum drv2605_otp_result {
	DRV2605_OTP_BURNED,             /* programmed and verified */
	DRV2605_OTP_ALREADY_PROGRAMMED, /* refused: OTP_STATUS was already 1 */
	DRV2605_OTP_VDD_OUT_OF_RANGE,   /* refused: live VDD not 4.0-4.4V */
	DRV2605_OTP_I2C_ERROR,          /* a register access failed */
	DRV2605_OTP_VERIFY_FAILED,      /* readback after reset didn't match */
};

int drv2605_init(void);

/* Reads the chip's own OTP_STATUS bit (Control4, 0x1E) -- a hardware fact,
 * not firmware state. link.c gates every factory command on this reading
 * false: once true, it stays true for the physical chip's life regardless
 * of what this board's own flash remembers. */
int drv2605_otp_status(bool *out_programmed);

/*
 * Forces a DEV_RESET (MODE bit 7, S8.4.1.5) -- "the equivalent of power
 * cycling the device", entirely over I2C, every register back to its
 * power-on default. Leaves the chip in ready/internal-trigger mode but,
 * unlike drv2605_init(), does NOT re-apply an actuator configuration -- a
 * reset that quietly put the settings back would not be a reset. Whatever
 * calls this owns following it with drv2605_apply_actuator().
 * Not part of the ordinary product path (nothing in link.c
 * calls this); it exists for bench use where an actual power cycle can't be
 * trusted to have really happened -- e.g. a bulk capacitor on VIN holding
 * the supply up well after the source was toggled off.
 */
int drv2605_dev_reset(void);

/*
 * Writes RATED_VOLTAGE (0x16) and OD_CLAMP (0x17) from the Vybronics
 * VZ7AL2B1690002 motor's own datasheet (drv2605.c's RATED_VOLTAGE_TARGET/
 * OD_CLAMP_TARGET, with the derivation and its safety margin in a comment
 * there), then runs auto-calibration and reports the three bytes that vary
 * per physical unit. out->passed reflects DIAG_RESULT only -- calibration
 * can pass against a wiring fault that happens to look electrically sane,
 * so a felt/bench check still matters, not just this return value.
 */
int drv2605_calibrate(struct drv2605_cal_result *out);

/*
 * Writes every auto-calibration input for `act` -- kind, library, loop mode,
 * drive time, rated voltage, overdrive clamp, and the fixed S8.5.6
 * recommendations -- without running calibration. Enough on its own to play
 * library effects on a given actuator, uncalibrated. drv2605_init() applies
 * the product ERM through this; the bench firmware applies whichever motor
 * is on the bench.
 *
 * MODE is not touched: the chip stays in whatever interface mode the caller
 * left it in (drv2605_init() and drv2605_dev_reset() both leave it in
 * ready/internal-trigger).
 */
int drv2605_apply_actuator(const struct drv2605_actuator *act);

/*
 * drv2605_apply_actuator() followed by the auto-calibration run of
 * drv2605_calibrate(). Bench entry point: the product path knows exactly
 * one actuator and calls drv2605_calibrate(); this one takes the actuator
 * as an argument because the whole point of the evaluation firmware is that
 * the answer isn't decided yet.
 *
 * The calibration result is only meaningful for the actuator it ran
 * against. Switching actuators and NOT re-running this leaves A_CAL_COMP/
 * A_CAL_BEMF describing the previous motor -- which is not an error the
 * chip reports, just a wrong feel, so the bench UI is responsible for
 * making the staleness visible.
 */
int drv2605_calibrate_actuator(const struct drv2605_actuator *act,
			       struct drv2605_cal_result *out);

/*
 * Clears GO (0x0C), ending playback early. The ROM library's effect 118
 * ("Long buzz for programmatic stopping") exists to be run against this:
 * it is the only way to hold the actuator at steady drive long enough to
 * read VBAT (0x21) or LRA_PERIOD (0x22), both of which the datasheet says
 * only read valid "while the device is actively sending a waveform".
 */
int drv2605_stop(void);

/* Raw register read, bench diagnostics only -- lets the eval firmware dump
 * chip state after a failure instead of inferring it. Not a product API. */
int drv2605_read_reg(uint8_t reg, uint8_t *out_val);

/*
 * Runs the DRV2605L's standalone actuator diagnostic (MODE=6, datasheet
 * S8.6.2/S8.3.2.6) instead of the full auto-calibration routine -- a
 * narrower open/short/back-EMF sanity check with no RATED_VOLTAGE/OD_CLAMP/
 * timing inputs to populate first. Useful to isolate "is the actuator
 * electrically sane at all" from "did the auto-cal convergence search
 * succeed", since the two can fail for different reasons and DIAG_RESULT
 * alone doesn't say which routine produced it.
 */
int drv2605_diagnose(bool *out_actuator_ok, uint8_t *out_status, int *out_go_reads_ok);

/*
 * Plays up to 7 library effect indices (TI's ROM waveform library,
 * datasheets/drv2605l.pdf S12.1.2) in sequence via internal-trigger mode --
 * the actual product haptic path, not the calibration/OTP one above.
 * `count` == 1 is a single effect. Retriggering while something is already
 * playing interrupts and restarts it, which is RADIO_PROTOCOL.md S8's "a
 * frame arriving mid-render wins and restarts the motor" rule -- this
 * function does not decide *whether* to interrupt (haptic.c's BEAT-vs-TAP
 * priority check happens before this is called), only how to render once
 * that decision is made.
 */
int drv2605_play_sequence(const uint8_t *effects, size_t count);

/*
 * Burns the DRV2605L's one-time-programmable memory (registers 0x16-0x1A)
 * with whatever is currently in them -- normally the result of a prior
 * drv2605_calibrate(). Irreversible per physical chip. Refuses if OTP is
 * already programmed or if live VDD is outside the 4.0-4.4V window the
 * datasheet requires for the write to take (drv2605_read_vdd_mv()) --
 * both checks happen here, not just in the caller, so a caller mistake
 * can't burn OTP under unsafe conditions.
 */
enum drv2605_otp_result drv2605_burn_otp(void);

/*
 * Millivolts on the DRV2605L's own VDD/VIN net -- on the product PCB, the
 * nPM1300's fuel-gauge reading (PLAN.md S17/S18); on the DK, there is no
 * battery to read, and this returns CONFIG_REMOTE_SYNTHETIC_VDD_MV, a
 * fixed, obviously-synthetic placeholder -- same convention as link.c's
 * SYNTHETIC_BATTERY_PCT. It defaults below the 4.0V OTP floor deliberately:
 * the DK has no real battery to safely burn OTP against, and refusing is
 * the correct behaviour there, not an inconvenience to work around.
 */
int drv2605_read_vdd_mv(uint16_t *out_mv);

#endif /* REMOTE_DRV2605_H_ */
