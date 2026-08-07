/*
 * Bench stand-in for the BLE haptics of PROTOCOL.md §5.2.
 *
 * Until the remotes exist there is nothing to buzz, so the three patterns are
 * rendered on the dongle's own LEDs instead. That makes the heartbeat visible
 * on the bench, which is worth having in its own right — it is the cheapest
 * oscilloscope you will get on this link.
 *
 * When BLE lands, these become the routing points for real haptic commands.
 */
#ifndef DONGLE_INDICATOR_H_
#define DONGLE_INDICATOR_H_

int indicator_init(void);

void indicator_heartbeat(void);  /* single short pulse, ~30 ms  (green) */
void indicator_confirm(void);    /* double pulse, 40/60/40 ms   (green) */
void indicator_expire(void);     /* long pulse, ~500 ms         (red)   */
void indicator_error(void);      /* short pulse, ~150 ms        (red)   */

#endif /* DONGLE_INDICATOR_H_ */
