/*
 * USB bridge dongle: relays referee events from the BLE wrist remotes to the
 * scoreboard web app over USB CDC-ACM. See PROTOCOL.md for the wire protocol
 * and PLAN.md for the build/validate plan.
 *
 * BLE is not implemented yet. The TEST modes of PROTOCOL.md §7.2 stand in for
 * real remotes, which is what lets the whole USB interface be validated first.
 */
#include "engine.h"
#include "indicator.h"
#include "usb_link.h"

#include <zephyr/kernel.h>

int main(void)
{
	if (indicator_init() != 0) {
		return 0;
	}

	/*
	 * USB itself is already up: CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT
	 * brings up the device stack and the single CDC-ACM instance before
	 * main() runs. All we do here is claim the UART side of it.
	 */
	if (usb_link_init(engine_on_line) != 0) {
		/* Nowhere to report this — the port is the only output we have,
		 * and it is what just failed. Blink and stop. */
		indicator_error();
		return 0;
	}

	engine_init();

	/* Everything from here runs on the system workqueue. */
	return 0;
}
