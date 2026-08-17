/*
 * DRV2605L breadboard bring-up (bench-only, CONFIG_REMOTE_DRV2605_CAL_TEST).
 * Not part of the product haptic path — see drv2605_cal_test.c.
 */
#ifndef DRV2605_CAL_TEST_H
#define DRV2605_CAL_TEST_H

/* Runs the auto-calibration sequence once and logs the result over RTT.
 * Blocking; safe to call before the rest of the app initialises. */
void drv2605_cal_test_run(void);

#endif
