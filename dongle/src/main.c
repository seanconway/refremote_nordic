/*
 * USB bridge dongle: relays referee presses from the wrist remotes to the
 * scoreboard web app over USB CDC-ACM, and routes the app's acknowledgements,
 * indicator state and haptic commands back to the wrists.
 *
 * PROTOCOL.md is the wire contract, RADIO_PROTOCOL.md the radio one, and
 * BUILD_SPEC.md the implementable contract for this firmware. PLAN.md says what
 * state the project is in and why the work is ordered as it is.
 *
 * With CONFIG_DONGLE_RADIO=n the radio is compiled out entirely and the TEST
 * modes of §10.2 stand in for real remotes. That configuration is retained for
 * the life of the project — it is the baseline every later radio regression is
 * attributed against, not scaffolding.
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
	 * The order below is a dependency cycle broken in the only place it
	 * can be. The transport needs the engine's workqueue before it can
	 * accept a byte; the engine needs the transport before it can say
	 * anything. So the engine builds its queue and state first, the
	 * transport is handed that queue, and only then does the engine start
	 * talking.
	 *
	 * USB itself is already up: CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT
	 * brings up the device stack and the single CDC-ACM instance before
	 * main() runs. All we do here is claim the UART side of it.
	 */
	engine_init();

	if (usb_link_init(engine_on_line, engine_workq()) != 0) {
		/* Nowhere to report this — the port is the only output we have,
		 * and it is what just failed. Blink and stop. */
		indicator_error();
		return 0;
	}

	/*
	 * This one call runs on the main thread rather than the engine queue,
	 * so for the few microseconds it takes there are technically two
	 * producers. It is left as is: the transmit ring is mutex-protected
	 * either way, and the only way a line could arrive in that window is if
	 * the host had already enumerated the port and an operator had already
	 * opened it — which cannot happen before main() returns.
	 */
	engine_start();

	/* Everything from here runs on the engine workqueue. */
	return 0;
}
