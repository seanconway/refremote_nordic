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

int drv2605_read_vdd_mv(uint16_t *out_mv)
{
	/* No nPM1300 on the DK -- see drv2605.h's doc comment on this
	 * function and remote/Kconfig's CONFIG_REMOTE_SYNTHETIC_VDD_MV. */
	*out_mv = (uint16_t)CONFIG_REMOTE_SYNTHETIC_VDD_MV;
	return 0;
}
