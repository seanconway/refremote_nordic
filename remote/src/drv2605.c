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
#define REG_LIBRARY          0x03  /* LIBRARY_SEL[2:0] + HI_Z */
#define REG_GO                0x0C
#define REG_WAV_FRM_SEQ1   0x04  /* base of the 8-slot sequence, 0x04-0x0B */
#define REG_RATED_VOLTAGE  0x16
#define REG_OD_CLAMP        0x17
#define REG_A_CAL_COMP     0x18
#define REG_A_CAL_BEMF     0x19
#define REG_FEEDBACK_CTRL 0x1A
#define REG_CONTROL1        0x1B  /* STARTUP_BOOST/AC_COUPLE/DRIVE_TIME */
#define REG_CONTROL2        0x1C  /* BIDIR_INPUT/BRAKE_STABILIZER/SAMPLE/BLANKING/IDISS */
#define REG_CONTROL3        0x1D  /* NG_THRESH/ERM_OPEN_LOOP/.../LRA_OPEN_LOOP */
#define REG_CONTROL4        0x1E  /* ZC_DET_TIME/AUTO_CAL_TIME/OTP_STATUS/OTP_PROGRAM */
#define REG_CONTROL5        0x1F  /* AUTO_OL_CNT/.../BLANKING_TIME[3:2]/IDISS_TIME[3:2] */
#define REG_VBAT            0x21  /* live VDD, valid only mid-playback (S8.6.27) */
#define REG_LRA_PERIOD      0x22  /* measured resonance, same mid-playback rule */

#define MODE_INTERNAL_TRIGGER 0x00
#define MODE_DIAGNOSTICS      0x06  /* datasheet S8.6.2 MODE=6 -- actuator
				      * open/short/back-EMF sanity check, GO
				      * self-clears, result in DIAG_RESULT.
				      * Narrower and faster than full auto-cal
				      * (S8.5.6): no RATED_VOLTAGE/OD_CLAMP or
				      * other inputs to populate first. */
#define MODE_AUTO_CAL         0x07
#define MODE_DEV_RESET        BIT(7)

#define STATUS_DIAG_RESULT BIT(3)

#define CONTROL4_OTP_STATUS  BIT(2)
#define CONTROL4_OTP_PROGRAM BIT(0)
#define CONTROL4_AUTO_CAL_TIME_SHIFT 4u
#define CONTROL4_AUTO_CAL_TIME_MASK  (0x3u << CONTROL4_AUTO_CAL_TIME_SHIFT)

#define FEEDBACK_CTRL_N_ERM_LRA           BIT(7)  /* 0 = ERM, 1 = LRA */
#define FEEDBACK_CTRL_FB_BRAKE_FACTOR_SHIFT 4u
#define FEEDBACK_CTRL_FB_BRAKE_FACTOR_MASK  (0x7u << FEEDBACK_CTRL_FB_BRAKE_FACTOR_SHIFT)
#define FEEDBACK_CTRL_LOOP_GAIN_SHIFT     2u
#define FEEDBACK_CTRL_LOOP_GAIN_MASK      (0x3u << FEEDBACK_CTRL_LOOP_GAIN_SHIFT)

#define LIBRARY_SEL_MASK 0x07u

#define CONTROL1_DRIVE_TIME_MASK 0x1Fu

#define CONTROL2_SAMPLE_TIME_SHIFT   4u
#define CONTROL2_SAMPLE_TIME_MASK    (0x3u << CONTROL2_SAMPLE_TIME_SHIFT)
#define CONTROL2_BLANKING_TIME_SHIFT 2u
#define CONTROL2_BLANKING_TIME_MASK  (0x3u << CONTROL2_BLANKING_TIME_SHIFT)
#define CONTROL2_IDISS_TIME_MASK     0x3u

#define CONTROL3_ERM_OPEN_LOOP BIT(5)
#define CONTROL3_LRA_OPEN_LOOP BIT(0)

/* Blanking and current-dissipation time are 2-bit fields in LRA mode's
 * bottom half only -- their upper two bits live over here (S8.6.25), and a
 * chip that was last configured for something else can be holding them.
 * Both target values are 1, so both upper halves must read back zero. */
#define CONTROL5_BLANKING_TIME_HI_MASK (0x3u << 2)
#define CONTROL5_IDISS_TIME_HI_MASK    0x3u

#define CONTROL4_ZC_DET_TIME_SHIFT 6u
#define CONTROL4_ZC_DET_TIME_MASK  (0x3u << CONTROL4_ZC_DET_TIME_SHIFT)

/*
 * Datasheet S8.5.6 step 3a-3c: ERM_LRA, FB_BRAKE_FACTOR and LOOP_GAIN are
 * calibration-engine inputs like RATED_VOLTAGE/OD_CLAMP/AUTO_CAL_TIME above
 * -- "should be set prior to running auto calibration" (S8.6.20's own field
 * descriptions) -- but nothing in this driver ever wrote them, so every
 * calibration ran against whatever FEEDBACK_CONTROL happened to power up
 * with: FB_BRAKE_FACTOR=3 (4x) and LOOP_GAIN=1 (Medium), neither of which is
 * the datasheet's own "valid for most actuators" starting point (2 and 2).
 * N_ERM_LRA is included for the same reason even though it already resets
 * to 0/ERM and nothing here sets it to LRA -- explicit beats implicit,
 * especially on a register three separate un-set inputs share.
 */
#define FB_BRAKE_FACTOR_TARGET 2u  /* 3x */
#define LOOP_GAIN_TARGET       2u  /* High */

/*
 * The rest of S8.5.6 step 3's "a value of N is valid for most actuators"
 * inputs, in one place. Constants rather than struct drv2605_actuator
 * fields on purpose (see the struct's own comment): the datasheet gives one
 * recommendation each, independent of which motor is attached, and none of
 * the three actuators on the bench has produced a measurement arguing for a
 * different value. SAMPLE_TIME in particular is not free to change without
 * consequence -- it is a term in Eq. 5, so moving it silently invalidates
 * every LRA RATED_VOLTAGE derived against it.
 */
#define SAMPLE_TIME_TARGET   3u  /* 300 us (step 3h) */
#define BLANKING_TIME_TARGET 1u  /* step 3i */
#define IDISS_TIME_TARGET    1u  /* step 3j */
#define ZC_DET_TIME_TARGET   0u  /* step 3k */

/*
 * Datasheet S8.5.6 step 3f: "AUTO_CAL_TIME[1:0] -- A value of 3 is valid
 * for most actuators." Reset default is 2 (S8.6.24) -- one notch short of
 * that recommendation -- and drv2605_calibrate() never wrote this register
 * before now, so every calibration ran on the shorter, un-tuned window.
 * S8.3.2.5.3: "the auto-calibration routine expects the actuator to have
 * reached a steady acceleration before the calibration factors are
 * calculated... the start-time characteristic can be different for each
 * actuator" -- exactly the axis AUTO_CAL_TIME exists to adjust, and exactly
 * the symptom (DIAG_RESULT never converging) a too-short window produces.
 */
#define AUTO_CAL_TIME_TARGET 3u

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

/*
 * Library A is what this motor has always been driven with -- the reset
 * default, back when nothing wrote LIBRARY_SEL at all. It is stated here
 * rather than left implicit, but stating it is not endorsing it: Table 1
 * puts Library A at a 1.3-V rated ERM and Libraries B-E at 3 V, which is
 * this motor's rating, so B is the better-argued choice on paper. The bench
 * evaluation firmware is what decides that by feel (remote_haptic_eval,
 * `haptic lib`); until it reports, the product keeps playing the waveforms
 * that were actually evaluated on hardware rather than the ones that look
 * right in a table.
 *
 * ERM_OPEN_LOOP likewise: it resets to 1, the ERM libraries were designed
 * for open-loop drive (S8.3.5.2/S8.6.23), and open loop is therefore what
 * every ERM effect felt so far has been. Note what that means for the two
 * voltage targets above -- in open loop RATED_VOLTAGE is ignored entirely
 * and OD_CLAMP alone sets full scale (S8.5.2.1).
 */
const struct drv2605_actuator drv2605_actuator_erm_vz7al2b = {
	.kind = DRV2605_ERM,
	.rated_voltage = RATED_VOLTAGE_TARGET,
	.od_clamp = OD_CLAMP_TARGET,
	.drive_time = 0x13,  /* Control1 reset default: ERM back-EMF sample
			      * rate, no reason on this motor to move it */
	.library = 1,
	.open_loop = true,
};

/*
 * Auto-cal's poll ceiling is deliberately far above the datasheet's nominal
 * figures. S8.6.24 gives AUTO_CAL_TIME=3 as 1000-1200 ms, but that bounds
 * the drive phase, not the whole routine: bench-measured on the Vybronics
 * ERM (2026-08-18), a calibration can hold GO past 1600 ms and then still
 * complete with DIAG_RESULT=0 and valid outputs -- a register dump taken
 * seconds after a "timeout" showed GO self-cleared and a passed result.
 * (S8.3.2.5.3's "expects the actuator to have reached a steady
 * acceleration" is the license the routine has to run long.) Two previous
 * ceilings here, 1000 ms and 1600 ms, both sat inside real completion
 * times and misreported in-progress calibrations as -ETIMEDOUT; 5 s is
 * empirically clear of observed durations while still bounding a genuine
 * hang. Calibration is a bench/factory operation -- nothing latency-
 * sensitive shares this path.
 */
#define CAL_POLL_ATTEMPTS   250
#define CAL_POLL_DELAY_MS   20
/* Post-OTP device reset is a different, much shorter operation (datasheet
 * S8.5.7 step 4) -- unrelated to AUTO_CAL_TIME, kept at its own ceiling. */
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
	int rc;

	i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));
	if (!device_is_ready(i2c_dev)) {
		return -ENODEV;
	}

	/* Out of standby into internal-trigger mode, so a failed read
	 * anywhere else in this module is a wiring/address problem, not a
	 * standby chip. */
	rc = drv_write(REG_MODE, MODE_INTERNAL_TRIGGER);
	if (rc != 0) {
		return rc;
	}

	/* Every actuator input the chip needs, written explicitly rather than
	 * inherited from reset defaults that happen to suit an ERM. Not a
	 * substitute for calibration -- this only makes the un-calibrated
	 * state a stated one. */
	return drv2605_apply_actuator(&drv2605_actuator_erm_vz7al2b);
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

/*
 * Blocks until GO (0x0C) self-clears or CAL_POLL_ATTEMPTS is exhausted --
 * shared by drv2605_calibrate() (S8.5.6) and drv2605_diagnose() (S8.6.2
 * MODE=6), both of which trigger GO and then wait the same way for the
 * same reason.
 *
 * The earlier "return the last read's error instead of -ETIMEDOUT" version
 * of this function did not actually distinguish a bus fault from a
 * genuinely-stuck GO bit: Zephyr's I2C driver returns -ETIMEDOUT for a
 * real bus-level timeout too, so both cases produced the identical -116 and
 * told a caller nothing. *out_reads_ok counts how many of the polls in this
 * call got a successful I2C transaction at all, regardless of what GO read
 * as -- 0 means every single read failed (an electrical/bus problem, not a
 * DRV2605L calibration/diagnostic-engine problem); a nonzero count with GO
 * still set means the bus is fine and the chip itself never cleared GO.
 */
static int wait_for_go(int *out_reads_ok)
{
	uint8_t go = 1;
	int read_rc = 0;
	int reads_ok = 0;

	for (int i = 0; i < CAL_POLL_ATTEMPTS && (go & 0x01); i++) {
		k_sleep(K_MSEC(CAL_POLL_DELAY_MS));
		read_rc = drv_read(REG_GO, &go);
		if (read_rc == 0) {
			reads_ok++;
		}
	}
	if (out_reads_ok != NULL) {
		*out_reads_ok = reads_ok;
	}
	if (go & 0x01) {
		return reads_ok == 0 ? read_rc : -ETIMEDOUT;
	}
	return 0;
}

/*
 * DEV_RESET (MODE bit 7, S8.4.1.5) -- "the equivalent of power cycling the
 * device", self-clearing, every register back to its power-on default.
 * Shared by drv2605_burn_otp() (S8.5.7 step 4, already used this before this
 * function existed as its own inline copy) and the standalone
 * drv2605_dev_reset() below -- both just need "write it, wait for it to
 * self-clear" and nothing else.
 */
static int dev_reset_and_wait(void)
{
	uint8_t mode;
	int rc = drv_write(REG_MODE, MODE_DEV_RESET);

	if (rc != 0) {
		return rc;
	}

	mode = MODE_DEV_RESET;
	for (int i = 0; i < RESET_POLL_ATTEMPTS && (mode & MODE_DEV_RESET); i++) {
		k_sleep(K_MSEC(RESET_POLL_DELAY_MS));
		rc = drv_read(REG_MODE, &mode);
	}
	if (mode & MODE_DEV_RESET) {
		return rc != 0 ? rc : -ETIMEDOUT;
	}
	return 0;
}

/*
 * Forces the DRV2605L back to a known-clean internal state without needing
 * an actual power cycle -- useful on this bench specifically because a bulk
 * capacitor added across VIN/GND to fix a current-transient problem also
 * defeats a real power cycle as a way to clear any latched internal fault
 * state (S8.4.1.6's short-before-playback note): the cap holds VIN up long
 * after the supply is toggled off, so "turn it off and on" may not actually
 * reset anything. DEV_RESET sidesteps that entirely -- it is a register
 * write, not a supply event.
 */
int drv2605_dev_reset(void)
{
	int rc = dev_reset_and_wait();

	if (rc != 0) {
		return rc;
	}

	/* DEV_RESET leaves the chip in its power-on MODE value (STANDBY,
	 * register 0x01's own reset default) -- bring it back to ready/
	 * internal-trigger the same way drv2605_init() does, so a caller
	 * gets the chip in the same usable state every other entry point
	 * here leaves it in. */
	return drv_write(REG_MODE, MODE_INTERNAL_TRIGGER);
}

/*
 * Read-modify-write helper for the control registers below: every one of
 * them carries bits this driver has no opinion about (STARTUP_BOOST,
 * BIDIR_INPUT, NG_THRESH, OTP_STATUS...), so a blind write would set policy
 * on fields nothing here has thought about.
 */
static int drv_update(uint8_t reg, uint8_t clear_mask, uint8_t set_bits)
{
	uint8_t val;
	int rc = drv_read(reg, &val);

	if (rc != 0) {
		return rc;
	}
	val = (uint8_t)((val & ~clear_mask) | set_bits);
	return drv_write(reg, val);
}

int drv2605_apply_actuator(const struct drv2605_actuator *act)
{
	const bool lra = act->kind == DRV2605_LRA;
	int rc;

	/* Datasheet S8.5.2: "these registers must be written before
	 * calibration is performed." Written every call rather than once at
	 * init so a recalibration always starts from the same known target,
	 * not whatever the chip happened to still hold. */
	rc = drv_write(REG_RATED_VOLTAGE, act->rated_voltage);
	if (rc != 0) {
		return rc;
	}
	rc = drv_write(REG_OD_CLAMP, act->od_clamp);
	if (rc != 0) {
		return rc;
	}

	/* Which ROM library the GO bit plays out of. Nothing wrote this
	 * before the actuator struct existed, so every effect ever felt on
	 * this project came out of the reset default (1, TS2200 Library A) --
	 * fine by accident for an ERM, silently wrong for an LRA, which needs
	 * library 6. */
	rc = drv_update(REG_LIBRARY, LIBRARY_SEL_MASK, act->library & LIBRARY_SEL_MASK);
	if (rc != 0) {
		return rc;
	}

	/* AUTO_CAL_TIME and ZC_DET_TIME are calibration-engine inputs like
	 * the two voltage registers above (S8.5.6 steps 3f/3k), just sharing
	 * a byte with OTP_STATUS/OTP_PROGRAM instead of having their own
	 * register -- read-modify-write, and explicitly clear OTP_PROGRAM on
	 * the way back out so this can never be the write that triggers an
	 * OTP burn, regardless of what happened to be in the register
	 * beforehand. */
	rc = drv_update(REG_CONTROL4,
			(uint8_t)(CONTROL4_AUTO_CAL_TIME_MASK | CONTROL4_ZC_DET_TIME_MASK |
				  CONTROL4_OTP_PROGRAM),
			(uint8_t)((AUTO_CAL_TIME_TARGET << CONTROL4_AUTO_CAL_TIME_SHIFT) |
				  (ZC_DET_TIME_TARGET << CONTROL4_ZC_DET_TIME_SHIFT)));
	if (rc != 0) {
		return rc;
	}

	/* ERM_LRA/FB_BRAKE_FACTOR/LOOP_GAIN -- S8.5.6 step 3a-3c, see
	 * FB_BRAKE_FACTOR_TARGET/LOOP_GAIN_TARGET above. BEMF_GAIN[1:0]
	 * (bits 1-0 of this same register) is left untouched -- that's a
	 * calibration *output* the engine populates itself (S8.3.2.5.5),
	 * not an input to set here. */
	rc = drv_update(REG_FEEDBACK_CTRL,
			(uint8_t)(FEEDBACK_CTRL_N_ERM_LRA |
				  FEEDBACK_CTRL_FB_BRAKE_FACTOR_MASK |
				  FEEDBACK_CTRL_LOOP_GAIN_MASK),
			(uint8_t)((lra ? FEEDBACK_CTRL_N_ERM_LRA : 0) |
				  (FB_BRAKE_FACTOR_TARGET << FEEDBACK_CTRL_FB_BRAKE_FACTOR_SHIFT) |
				  (LOOP_GAIN_TARGET << FEEDBACK_CTRL_LOOP_GAIN_SHIFT)));
	if (rc != 0) {
		return rc;
	}

	/* S8.5.6 step 3g. On an LRA this is the auto-resonance engine's
	 * starting guess at the half-period and a wrong value costs startup
	 * time or stability (S8.5.1.1); on an ERM it is only the back-EMF
	 * sample rate. Same register field, two unrelated meanings. */
	rc = drv_update(REG_CONTROL1, CONTROL1_DRIVE_TIME_MASK,
			(uint8_t)(act->drive_time & CONTROL1_DRIVE_TIME_MASK));
	if (rc != 0) {
		return rc;
	}

	/* S8.5.6 steps 3h-3j. Both time fields are 2 bits here and 4 bits in
	 * LRA mode, the upper 2 living in Control5 -- clear those rather than
	 * assume, since the same physical chip gets switched between
	 * actuators on the bench and Control5 does not reset in between. */
	rc = drv_update(REG_CONTROL2,
			(uint8_t)(CONTROL2_SAMPLE_TIME_MASK | CONTROL2_BLANKING_TIME_MASK |
				  CONTROL2_IDISS_TIME_MASK),
			(uint8_t)((SAMPLE_TIME_TARGET << CONTROL2_SAMPLE_TIME_SHIFT) |
				  (BLANKING_TIME_TARGET << CONTROL2_BLANKING_TIME_SHIFT) |
				  IDISS_TIME_TARGET));
	if (rc != 0) {
		return rc;
	}
	rc = drv_update(REG_CONTROL5,
			(uint8_t)(CONTROL5_BLANKING_TIME_HI_MASK | CONTROL5_IDISS_TIME_HI_MASK),
			0);
	if (rc != 0) {
		return rc;
	}

	/* Open- vs closed-loop, on whichever of the two bits applies to this
	 * actuator kind. The other bit is cleared, not left alone: a chip
	 * carried over from the other kind of motor can be holding it set,
	 * and the pair is confusing enough read cold without a stale one. */
	return drv_update(REG_CONTROL3,
			  (uint8_t)(CONTROL3_ERM_OPEN_LOOP | CONTROL3_LRA_OPEN_LOOP),
			  (uint8_t)(!act->open_loop         ? 0 :
				    lra ? CONTROL3_LRA_OPEN_LOOP : CONTROL3_ERM_OPEN_LOOP));
}

int drv2605_calibrate_actuator(const struct drv2605_actuator *act,
			       struct drv2605_cal_result *out)
{
	int rc;

	*out = (struct drv2605_cal_result){ 0 };

	rc = drv2605_apply_actuator(act);
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

	rc = wait_for_go(&out->go_reads_ok);
	if (rc != 0) {
		/* Abort path: don't leave the chip parked in auto-cal mode
		 * with the routine possibly still running -- a post-timeout
		 * register dump once caught exactly that (MODE still 0x07,
		 * calibration quietly finishing after we'd walked away). */
		(void)drv_write(REG_MODE, MODE_INTERNAL_TRIGGER);
		return rc;
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

int drv2605_calibrate(struct drv2605_cal_result *out)
{
	/* The product path: the Vybronics motor's own datasheet targets,
	 * derivation in the comment above RATED_VOLTAGE_TARGET. The
	 * _actuator variant exists for the bench, which is still deciding
	 * which motor this constant should describe -- see drv2605.h. */
	return drv2605_calibrate_actuator(&drv2605_actuator_erm_vz7al2b, out);
}

int drv2605_read_reg(uint8_t reg, uint8_t *out_val)
{
	return drv_read(reg, out_val);
}

int drv2605_stop(void)
{
	/* GO is the only thing to clear: MODE stays internal-trigger, so the
	 * next drv2605_play_sequence() starts a new waveform normally. */
	return drv_write(REG_GO, 0x00);
}

int drv2605_diagnose(bool *out_actuator_ok, uint8_t *out_status, int *out_go_reads_ok)
{
	int rc = drv_write(REG_MODE, MODE_DIAGNOSTICS);

	if (rc != 0) {
		return rc;
	}
	rc = drv_write(REG_GO, 0x01);
	if (rc != 0) {
		return rc;
	}

	rc = wait_for_go(out_go_reads_ok);
	if (rc != 0) {
		/* Same abort-path rule as drv2605_calibrate_with_targets():
		 * never return with the chip parked in a process mode. */
		(void)drv_write(REG_MODE, MODE_INTERNAL_TRIGGER);
		return rc;
	}

	rc = drv_read(REG_STATUS, out_status);
	if (rc != 0) {
		return rc;
	}
	/* Diagnostic-mode semantics of the same bit auto-cal uses (S8.6.1):
	 * 0 = "actuator is functioning normally", 1 = "not present or is
	 * shorted, timing out, or giving out-of-range back-EMF". */
	*out_actuator_ok = (*out_status & STATUS_DIAG_RESULT) == 0;

	(void)drv_write(REG_MODE, MODE_INTERNAL_TRIGGER);
	return 0;
}

enum drv2605_otp_result drv2605_burn_otp(void)
{
	bool already;
	uint16_t vdd_mv;
	uint8_t before[OTP_REGION_LEN];
	uint8_t after[OTP_REGION_LEN];
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

	/* step 4: reset via DEV_RESET rather than a power cycle. Poll failure
	 * ignored on purpose, same as before this was factored out -- the
	 * readback-vs-before comparison below is the real check that the
	 * reset (and the burn) actually completed, not this return value. */
	(void)dev_reset_and_wait();

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
