/*
 * DRV2605L driver -- register-level implementation. See drv2605.h.
 *
 * Register map, bit positions and the OTP procedure are all from
 * datasheets/drv2605l.pdf: §8.5.6 (auto-calibration), §8.5.7 (OTP), and the
 * Control4 (0x1E) and MODE (0x01) register tables. drv2605_cal_test.c
 * proved this sequence works on the DK's i2c1/P1.04-P1.05 wiring before
 * this file existed; that file now calls into this one instead of
 * duplicating the register logic.
 */
#include "drv2605.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#include <errno.h>
#include <string.h>

#define DRV2605_ADDR 0x5A

#define REG_STATUS          0x00
#define REG_MODE             0x01
#define REG_GO                0x0C
#define REG_WAV_FRM_SEQ1   0x04  /* base of the 8-slot sequence, 0x04-0x0B */
#define REG_RATED_VOLTAGE  0x16
#define REG_OD_CLAMP        0x17
#define REG_A_CAL_COMP     0x18
#define REG_A_CAL_BEMF     0x19
#define REG_FEEDBACK_CTRL 0x1A
#define REG_CONTROL4        0x1E  /* ZC_DET_TIME/AUTO_CAL_TIME/OTP_STATUS/OTP_PROGRAM */

#define MODE_INTERNAL_TRIGGER 0x00
#define MODE_AUTO_CAL         0x07
#define MODE_DEV_RESET        BIT(7)

#define STATUS_DIAG_RESULT BIT(3)

#define CONTROL4_OTP_STATUS  BIT(2)
#define CONTROL4_OTP_PROGRAM BIT(0)

#define WAV_FRM_SEQ_MAX 8u  /* register block is 0x04-0x0B, one byte each */

/*
 * Targets for the Vybronics VZ7AL2B1690002 ERM (datasheets/Vybronics-
 * VZ7AL2B1690002-datasheet.pdf): rated voltage 3.0V, operating voltage
 * 2.2-3.6V. Register values from datasheets/drv2605l.pdf S8.5.2:
 *
 *   RATED_VOLTAGE[7:0] = V_rated / 0.02118          (Eq. 4, closed-loop ERM)
 *                       = 3.0 / 0.02118 = 141.6 -> 142 (0x8E)
 *
 *   OD_CLAMP[7:0] targets the motor's own 3.6V operating-voltage ceiling --
 *   safe to drive continuously per its datasheet, so a brief overdrive
 *   kick to that level is well inside spec. The exact closed-loop formula
 *   (Eq. 8) scales by a factor of t(DRIVE_TIME)-related timing constants
 *   that need actuator-specific tuning no motor datasheet provides; using
 *   the simpler open-loop-style relationship instead (Eq. 6) is a
 *   deliberate, provably-conservative substitute, not a shortcut taken
 *   for convenience -- Eq. 8's timing ratio is always <= 1 (its numerator
 *   subtracts 300us that its denominator doesn't), so for any legal
 *   DRIVE_TIME/IDISS_TIME/BLANKING_TIME the real closed-loop clamp voltage
 *   at this register value is at or below the 3.6V computed here, never
 *   above it:
 *
 *   OD_CLAMP[7:0] = V_od / 0.02159                  (Eq. 6, open-loop ERM,
 *                                                     used here as a safe
 *                                                     upper-bound estimate)
 *                 = 3.6 / 0.02159 = 166.7 -> 167 (0xA7)
 *
 * Both equations are independent of VIN -- they relate a register code to
 * an output voltage directly. VIN only sets a ceiling on what's reachable
 * ("the output driver is unable to reach the clamp voltage value" if VDD
 * is lower, per the datasheet's own note): on the DK bench build (VIN tied
 * to the 3.3V rail per the VIN/pull-up wiring discussion), RATED_VOLTAGE's
 * 3.0V target is reachable but OD_CLAMP's 3.6V is not -- overdrive will be
 * capped at whatever VIN actually is, not a bug, just this bench's
 * supply. The full 3.6V ceiling is only reachable once VIN is the LiPo.
 */
#define RATED_VOLTAGE_TARGET 142u
#define OD_CLAMP_TARGET      167u

/* Auto-cal and the post-OTP device reset both take on the order of tens of
 * ms; these are generous ceilings, not measured figures. */
#define CAL_POLL_ATTEMPTS   50
#define CAL_POLL_DELAY_MS   20
#define RESET_POLL_ATTEMPTS 20
#define RESET_POLL_DELAY_MS 10

/* Registers 0x16-0x1A: the five bytes OTP programs as a block (datasheet
 * §8.5.7). Used to snapshot what was written before OTP_PROGRAM and compare
 * against the post-reset readback, since there is no other record of what
 * "correct" looks like for this specific burn. */
#define OTP_REGION_FIRST REG_RATED_VOLTAGE
#define OTP_REGION_LAST  REG_FEEDBACK_CTRL
#define OTP_REGION_LEN   (OTP_REGION_LAST - OTP_REGION_FIRST + 1)

static const struct device *i2c_dev;

static int drv_write(uint8_t reg, uint8_t val)
{
	uint8_t buf[2] = { reg, val };

	return i2c_write(i2c_dev, buf, sizeof(buf), DRV2605_ADDR);
}

static int drv_read(uint8_t reg, uint8_t *val)
{
	return i2c_write_read(i2c_dev, DRV2605_ADDR, &reg, 1, val, 1);
}

static int drv_read_region(uint8_t first_reg, uint8_t *out, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		int rc = drv_read((uint8_t)(first_reg + i), &out[i]);

		if (rc != 0) {
			return rc;
		}
	}
	return 0;
}

int drv2605_init(void)
{
	i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));
	if (!device_is_ready(i2c_dev)) {
		return -ENODEV;
	}

	/* Out of standby into internal-trigger mode, so a failed read
	 * anywhere else in this module is a wiring/address problem, not a
	 * standby chip. */
	return drv_write(REG_MODE, MODE_INTERNAL_TRIGGER);
}

int drv2605_otp_status(bool *out_programmed)
{
	uint8_t c4;
	int rc = drv_read(REG_CONTROL4, &c4);

	if (rc != 0) {
		return rc;
	}
	*out_programmed = (c4 & CONTROL4_OTP_STATUS) != 0;
	return 0;
}

int drv2605_calibrate(struct drv2605_cal_result *out)
{
	uint8_t go = 1;
	int rc;

	*out = (struct drv2605_cal_result){ 0 };

	/* Datasheet S8.5.2: "these registers must be written before
	 * calibration is performed." Written every call rather than once at
	 * init so a recalibration always starts from the same known target,
	 * not whatever the chip happened to still hold. */
	rc = drv_write(REG_RATED_VOLTAGE, RATED_VOLTAGE_TARGET);
	if (rc != 0) {
		return rc;
	}
	rc = drv_write(REG_OD_CLAMP, OD_CLAMP_TARGET);
	if (rc != 0) {
		return rc;
	}

	rc = drv_write(REG_MODE, MODE_AUTO_CAL);
	if (rc != 0) {
		return rc;
	}
	rc = drv_write(REG_GO, 0x01);
	if (rc != 0) {
		return rc;
	}

	for (int i = 0; i < CAL_POLL_ATTEMPTS && (go & 0x01); i++) {
		k_sleep(K_MSEC(CAL_POLL_DELAY_MS));
		(void)drv_read(REG_GO, &go);
	}
	if (go & 0x01) {
		return -ETIMEDOUT;
	}

	rc = drv_read(REG_STATUS, &out->status);
	if (rc != 0) {
		return rc;
	}
	out->passed = (out->status & STATUS_DIAG_RESULT) == 0;

	(void)drv_read(REG_A_CAL_COMP, &out->a_cal_comp);
	(void)drv_read(REG_A_CAL_BEMF, &out->a_cal_bemf);
	(void)drv_read(REG_FEEDBACK_CTRL, &out->feedback_control);

	/* Leave the chip ready for ordinary playback, not parked in
	 * calibration mode. */
	(void)drv_write(REG_MODE, MODE_INTERNAL_TRIGGER);

	return 0;
}

enum drv2605_otp_result drv2605_burn_otp(void)
{
	bool already;
	uint16_t vdd_mv;
	uint8_t before[OTP_REGION_LEN];
	uint8_t after[OTP_REGION_LEN];
	uint8_t mode;
	int rc;

	if (drv2605_otp_status(&already) != 0) {
		return DRV2605_OTP_I2C_ERROR;
	}
	if (already) {
		return DRV2605_OTP_ALREADY_PROGRAMMED;
	}

	/* Authoritative here, not just in the caller -- a caller mistake
	 * must not be able to burn OTP under an unsafe supply. */
	if (drv2605_read_vdd_mv(&vdd_mv) != 0) {
		return DRV2605_OTP_I2C_ERROR;
	}
	if (vdd_mv < 4000u || vdd_mv > 4400u) {
		return DRV2605_OTP_VDD_OUT_OF_RANGE;
	}

	if (drv_read_region(OTP_REGION_FIRST, before, OTP_REGION_LEN) != 0) {
		return DRV2605_OTP_I2C_ERROR;
	}

	/* datasheet §8.5.7 step 3: write exactly 0x01 to Control4, not a
	 * read-modify-write -- that is what the datasheet specifies. */
	if (drv_write(REG_CONTROL4, CONTROL4_OTP_PROGRAM) != 0) {
		return DRV2605_OTP_I2C_ERROR;
	}

	/* step 4: reset via DEV_RESET rather than a power cycle, then wait
	 * for the self-clearing bit to confirm the reset actually ran. */
	if (drv_write(REG_MODE, MODE_DEV_RESET) != 0) {
		return DRV2605_OTP_I2C_ERROR;
	}
	mode = MODE_DEV_RESET;
	for (int i = 0; i < RESET_POLL_ATTEMPTS && (mode & MODE_DEV_RESET); i++) {
		k_sleep(K_MSEC(RESET_POLL_DELAY_MS));
		(void)drv_read(REG_MODE, &mode);
	}

	if (drv_read_region(OTP_REGION_FIRST, after, OTP_REGION_LEN) != 0) {
		return DRV2605_OTP_I2C_ERROR;
	}
	rc = memcmp(before, after, OTP_REGION_LEN);

	/* Back to internal-trigger mode either way -- DEV_RESET returns the
	 * chip to its power-on MODE value, which is standby. */
	(void)drv_write(REG_MODE, MODE_INTERNAL_TRIGGER);

	if (rc != 0) {
		return DRV2605_OTP_VERIFY_FAILED;
	}
	return DRV2605_OTP_BURNED;
}

int drv2605_play_sequence(const uint8_t *effects, size_t count)
{
	uint8_t seq[WAV_FRM_SEQ_MAX] = { 0 };

	if (count > WAV_FRM_SEQ_MAX - 1u) {
		/* One slot short of the block: a trailing 0 terminates the
		 * sequence (datasheet S8.5.4) and must survive even when the
		 * caller fills every effect slot it can. */
		return -EINVAL;
	}
	memcpy(seq, effects, count);
	/* seq[count] is already 0 from the initializer -- the terminator. */

	/*
	 * No explicit stop before writing: internal-trigger mode plus GO is
	 * exactly RADIO_PROTOCOL.md S8's "a frame arriving mid-render wins
	 * and restarts the motor" rule already implemented in Zephyr/the
	 * DRV2605L itself -- retriggering GO while a waveform is still
	 * playing interrupts it and starts the new one. haptic.c is what
	 * decides *whether* to call this (its BEAT-vs-TAP priority check
	 * happens before this function is reached, not inside it).
	 */
	for (size_t i = 0; i < WAV_FRM_SEQ_MAX; i++) {
		int rc = drv_write((uint8_t)(REG_WAV_FRM_SEQ1 + i), seq[i]);

		if (rc != 0) {
			return rc;
		}
	}

	return drv_write(REG_GO, 0x01);
}

int drv2605_read_vdd_mv(uint16_t *out_mv)
{
	/* No nPM1300 on the DK -- see drv2605.h's doc comment on this
	 * function and remote/Kconfig's CONFIG_REMOTE_SYNTHETIC_VDD_MV. */
	*out_mv = (uint16_t)CONFIG_REMOTE_SYNTHETIC_VDD_MV;
	return 0;
}
