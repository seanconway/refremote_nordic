/*
 * Protocol state machine: link supervision (§8), acknowledgement routing (§5.3,
 * §11), indicator and haptic relay (§6, §9), and the TEST modes (§10.2).
 *
 * It holds no match state — not the clock, not the score, not secondary-clock
 * ownership. Everything it holds is transport state. v2.0's dongle-local clock
 * and 1 Hz heartbeat are deleted, not ported: FS §6.2 forbids a node beating on
 * locally-held ownership state, because it can report one athlete on the wrist
 * while the scoreboard reports another, silently and with nothing to correct it.
 *
 * Everything here runs on ONE cooperative workqueue, owned by this module, so
 * there is exactly one producer feeding the transmit path and no locking is
 * needed between the timers, the receive path and the test drivers. The USB
 * receive path and (later) the Bluetooth callbacks both marshal onto it rather
 * than calling in from their own threads — that is what keeps the invariant
 * true once a second source of events exists.
 *
 * Cooperative rather than preemptible for one reason: a preemptible queue can
 * be descheduled between receiving an ACK and handing the tap to the radio, and
 * that latency is invisible. It shows up as a missing tap under load and as
 * nothing else at all.
 */
#ifndef DONGLE_ENGINE_H_
#define DONGLE_ENGINE_H_

struct k_work_q;

/*
 * Initialisation is split in two because the transport needs the workqueue
 * before it can accept a byte, and the engine needs the transport before it can
 * say anything. main() interleaves them:
 *
 *   engine_init();                                  state and queue, no I/O
 *   usb_link_init(engine_on_line, engine_workq());
 *   engine_start();                                 boot HELLO, timers, radio
 */
void engine_init(void);
struct k_work_q *engine_workq(void);
void engine_start(void);

/* Called by the transport with one complete received line, on the engine
 * workqueue. The buffer is reused after this returns. */
void engine_on_line(char *line);

#endif /* DONGLE_ENGINE_H_ */
