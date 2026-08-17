/*
 * DRV2605L breadboard bring-up (bench-only, CONFIG_REMOTE_DRV2605_CAL_TEST).
 *
 * A bare hardware smoke test for the i2c1/P1.04-P1.05 wiring
 * (nrf52840dk_nrf52840.overlay) that exercises drv2605.c's real driver
 * directly, without needing the dongle, BLE, or the factory link protocol
 * (link.c's DN_CAL_TRIGGER/DN_OTP_BURN handling) working first. Register
 * logic lives in drv2605.c now; this file only calls it and prints the
 * result over RTT.
 */
#include "drv2605_cal_test.h"
#include "drv2605.h"

#include <zephyr/sys/printk.h>

void drv2605_cal_test_run(void)
{
	struct drv2605_cal_result cal;
	int rc;

	rc = drv2605_init();
	if (rc != 0) {
		printk("[drv2605] init failed (%d) -- check VIN, GND, SDA/SCL "
		       "wiring and the 0x5A address\n", rc);
		return;
	}
	printk("[drv2605] I2C link OK.\n");
	printk("[drv2605] NOTE: RATED_VOLTAGE/OD_CLAMP left at power-on default. "
	       "Set them from datasheets/drv2605l.pdf S8.5/8.6 against the real "
	       "motor and VIN before trusting this as a real calibration.\n");

	rc = drv2605_calibrate(&cal);
	if (rc != 0) {
		printk("[drv2605] calibration failed (%d)\n", rc);
		return;
	}

	printk("[drv2605] calibration %s (STATUS=0x%02x)\n",
	       cal.passed ? "passed" : "FAILED -- check motor wiring/OD_CLAMP",
	       cal.status);
	printk("[drv2605] A_CAL_COMP=0x%02x A_CAL_BEMF=0x%02x FEEDBACK_CONTROL=0x%02x "
	       "-- the three values a per-unit calibration record would store\n",
	       cal.a_cal_comp, cal.a_cal_bemf, cal.feedback_control);
}
