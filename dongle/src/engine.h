/*
 * Protocol state machine: clock state and the 1 Hz heartbeat (§5), link
 * supervision (§5.1), end-to-end confirmation (§6), and the TEST modes (§7.2).
 *
 * Everything here runs on the system workqueue, so there is exactly one
 * producer feeding the transmit path and no locking is needed between the
 * timers, the receive path and the test drivers.
 */
#ifndef DONGLE_ENGINE_H_
#define DONGLE_ENGINE_H_

void engine_init(void);

/* Called by the transport with one complete received line. */
void engine_on_line(char *line);

#endif /* DONGLE_ENGINE_H_ */
