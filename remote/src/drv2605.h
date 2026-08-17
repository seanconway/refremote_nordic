/*
 * DRV2605L driver -- the real (non-bench) module. Owns the calibration and
 * OTP-burn sequences the factory link protocol (RADIO_PROTOCOL.md's factory
 * addendum, dispatched from link.c) drives, and is the intended eventual
 * product haptic back end replacing haptic.c's PWM proxy (BUILD_SPEC.md
 * §13) once a driver IC decision is made -- that swap is not done here yet,
 * this module only owns calibration/OTP for now.
 */
#ifndef REMOTE_DRV2605_H_
#define REMOTE_DRV2605_H_

#include <stdbool.h>
#include <stdint.h>

struct drv2605_cal_result {
	bool passed;              /* STATUS.DIAG_RESULT == 0 */
	uint8_t status;            /* raw STATUS register (0x00), for diagnostics */
	uint8_t a_cal_comp;         /* 0x18 */
	uint8_t a_cal_bemf;         /* 0x19 */
	uint8_t feedback_control;   /* 0x1A, post-calibration */
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
 * Runs auto-calibration and reports the three bytes that vary per physical
 * unit. RATED_VOLTAGE (0x16) and OD_CLAMP (0x17) are deliberately left at
 * whatever they currently hold rather than computed here -- see the
 * CONFIG_REMOTE_DRV2605_RATED_VOLTAGE/OD_CLAMP placeholders in Kconfig and
 * their warning. A wrong constant there risks overdriving the motor; this
 * function calibrates against whatever is actually programmed, correct or
 * not, and out->passed only reflects DIAG_RESULT, not whether those two
 * inputs were ever set correctly.
 */
int drv2605_calibrate(struct drv2605_cal_result *out);

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
