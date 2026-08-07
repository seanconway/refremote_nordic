/*
 * USB CDC-ACM transport. The only file that touches the UART API.
 *
 * The protocol owns this port exclusively — console, shell and logging are all
 * disabled in prj.conf so nothing else can interleave bytes into the stream.
 * See PLAN.md §1.4.
 */
#ifndef DONGLE_USB_LINK_H_
#define DONGLE_USB_LINK_H_

#include <stdbool.h>

/* Called on the system workqueue (never in ISR context) with one complete,
 * NUL-terminated line. The buffer is reused afterwards. */
typedef void (*usb_link_line_fn)(char *line);

int usb_link_init(usb_link_line_fn on_line);

/*
 * Queues one line for transmission, appending the \n terminator.
 *
 * Whole lines are dropped rather than truncated when the buffer is full —
 * a partial line would corrupt framing for the receiver, which is worse than
 * losing the message.
 */
void usb_link_send(const char *line);

#endif /* DONGLE_USB_LINK_H_ */
