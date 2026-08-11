/*
 * USB CDC-ACM transport. The only file that touches the UART API.
 *
 * The protocol owns this port exclusively — console, shell and logging are all
 * disabled in prj.conf, and a BUILD_ASSERT fails the build if a second CDC-ACM
 * instance ever appears. Anything else writing to the port interleaves bytes
 * into the protocol stream: a log line over 120 bytes trips the receiver's
 * discard-and-resync path, and a write landing mid-line corrupts that line.
 * Both failures are silent and intermittent. See PROTOCOL.md §13.
 */
#ifndef DONGLE_USB_LINK_H_
#define DONGLE_USB_LINK_H_

#include <stdbool.h>
#include <stdint.h>

struct k_work_q;

/* Called with one complete, NUL-terminated line, on the workqueue handed to
 * usb_link_init() — never in ISR context. The buffer is reused afterwards. */
typedef void (*usb_link_line_fn)(char *line);

/*
 * `workq` is the engine's cooperative queue, not the system one. Received lines
 * are dispatched there so that the engine keeps exactly one producer feeding
 * the transmit path once the Bluetooth callbacks become a second source of
 * events. See engine.h.
 */
int usb_link_init(usb_link_line_fn on_line, struct k_work_q *workq);

/*
 * Queues one line for transmission, appending the \n terminator. Returns 0, or
 * a negative errno if the line was dropped.
 *
 * Whole lines are dropped rather than truncated when the buffer is full — a
 * partial line would corrupt framing for the receiver, which is worse than
 * losing the message.
 */
int usb_link_send(const char *line);

/*
 * Lines dropped since boot.
 *
 * This counter is not optional. Both drop paths were silent before, and the
 * function returned void, so a dropped line was indistinguishable from a line
 * that was never sent. PLAN.md §3.10 B2 makes ring saturation a two-remote
 * condition, which means it will first appear exactly when it is hardest to
 * diagnose — so the drop path is instrumented before the traffic that
 * saturates it exists.
 */
uint32_t usb_link_tx_drops(void);

#endif /* DONGLE_USB_LINK_H_ */
