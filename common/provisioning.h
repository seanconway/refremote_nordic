/*
 * The provisioning record — RADIO_PROTOCOL.md §10.1. This is what tells an
 * otherwise-identical firmware source tree which set it belongs to and which
 * one or two units it is allowed to talk to.
 *
 * Baked into the firmware image at build time, not read from a flash
 * partition written by a separate step — PLAN.md §4's binding decision on
 * this, recorded 2026-08-12, reverses the original design. dongle/tools/
 * provision.py generates a `provisioning_data.h` per unit, defining a single
 * `static const struct provisioning_record PROV_RECORD`; CMakeLists.txt
 * requires `-DPROVISIONING_HEADER_DIR=<dir containing it>` and refuses to
 * configure without it, so there is no such thing as a build that silently
 * carries no identity. provisioning_validate() below is what's left of the
 * old parser: a structural sanity check on an already-populated record
 * (serial charset, role range, and the all-zero-key sentinel that catches a
 * generated header nobody actually ran the generator for), not a parser for
 * bytes read off a fallible medium — that failure mode doesn't exist once
 * the record ships as part of the same image as the code that reads it.
 *
 * NO ZEPHYR DEPENDENCIES, for the same reason as protocol.h (dongle/BUILD_SPEC.md
 * §2.1): host-testable with nothing but gcc. Shared verbatim between the
 * dongle and the remote firmware, and with dongle/tools/provision.py, which
 * generates struct literals against this exact layout — the two disagreeing
 * on role's numeric values, or field order, is a mistake nothing catches
 * short of a live radio pairing to the wrong unit.
 */
#ifndef COMMON_PROVISIONING_H_
#define COMMON_PROVISIONING_H_

#include <stddef.h>
#include <stdint.h>

#define PROVISIONING_SERIAL_LEN  12u
#define PROVISIONING_ADDR_LEN    6u
#define PROVISIONING_PEER_COUNT  2u
#define PROVISIONING_KEY_LEN     16u

enum provisioning_role {
	PROVISIONING_ROLE_DONGLE = 0,
	PROVISIONING_ROLE_RED    = 1,
	PROVISIONING_ROLE_GREEN  = 2,
};

struct provisioning_record {
	char set_serial[PROVISIONING_SERIAL_LEN + 1]; /* NUL-terminated for callers */
	enum provisioning_role role;
	uint8_t own_addr[PROVISIONING_ADDR_LEN];
	/* On a DONGLE record, index 0 is RED and index 1 is GREEN — matching
	 * enum proto_remote's numbering, and how dongle/tools/provision.py's
	 * build_set() orders them ([red_addr, green_addr]) even though
	 * RADIO_PROTOCOL.md §10.1 does not pin the order in writing. Pinned
	 * here for the same reason as the role values below: radio_ble.c
	 * indexes this array by proto_remote directly, and a generator that
	 * assumed the opposite order would swap which remote's presses land on
	 * which lamp with no error anywhere. On a RED/GREEN record, index 0 is
	 * the dongle and index 1 is the zero slot (unused). */
	uint8_t peer_addr[PROVISIONING_PEER_COUNT][PROVISIONING_ADDR_LEN];
	uint8_t set_key[PROVISIONING_KEY_LEN];
};

enum provisioning_status {
	PROVISIONING_OK = 0,
	PROVISIONING_ERR_SERIAL, /* set_serial outside [A-Z0-9-] or not NUL-padded */
	PROVISIONING_ERR_ROLE,   /* role byte not one of the three known values */
	PROVISIONING_ERR_KEY,    /* set_key is all-zero — provision.py never generates
				  * this; it means the header was never really run
				  * through the generator */
};

/*
 * Structural sanity check on an already-populated record — not a parser, and
 * it never fails on a role the *caller* doesn't accept (e.g. a DONGLE record
 * on a remote build). Callers check role acceptance themselves, the same way
 * dongle/src/engine.c and remote/src/main.c always have.
 */
enum provisioning_status provisioning_validate(const struct provisioning_record *r);

const char *provisioning_status_str(enum provisioning_status s);

#endif /* COMMON_PROVISIONING_H_ */
