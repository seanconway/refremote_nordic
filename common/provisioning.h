/*
 * The provisioning record — RADIO_PROTOCOL.md §10.1, 55 bytes, written once at
 * manufacture into a dedicated flash partition and never written again in the
 * field. This is what tells an otherwise-identical firmware image which set it
 * belongs to and which two units it is allowed to talk to.
 *
 * NO ZEPHYR DEPENDENCIES, for the same reason as protocol.h (dongle/BUILD_SPEC.md
 * §2.1): the parser is the one place a mistake between the reader and the bench
 * tool that writes the record is silent on both ends, and it is the one part
 * that can be exhaustively tested on a host with nothing but gcc. Shared
 * verbatim between the dongle and the remote firmware.
 *
 * RADIO_PROTOCOL.md §10.1 leaves two things to the implementation, pinned here
 * because a reader and a bench tool that disagree on either produce a record
 * that looks fine and is silently unreadable — dongle/BUILD_SPEC.md §9 carries
 * the same note, so a change here is a change there too:
 *
 *   - the role enum's numeric values (below), and the magic/version constants
 *   - the CRC32 variant: the ubiquitous "CRC-32" — poly 0xEDB88320 (reflected),
 *     init 0xFFFFFFFF, xorout 0xFFFFFFFF, same as zlib/PNG/gzip/Ethernet FCS —
 *     computed over bytes [0, PROVISIONING_RECORD_SIZE - 4), i.e. everything
 *     in the record except the crc32 field itself.
 */
#ifndef COMMON_PROVISIONING_H_
#define COMMON_PROVISIONING_H_

#include <stddef.h>
#include <stdint.h>

#define PROVISIONING_MAGIC       0x5252u   /* ASCII "RR", little-endian */
#define PROVISIONING_VERSION     1u

#define PROVISIONING_SERIAL_LEN  12u
#define PROVISIONING_ADDR_LEN    6u
#define PROVISIONING_PEER_COUNT  2u
#define PROVISIONING_KEY_LEN     16u

/* Wire size, RADIO_PROTOCOL.md §10.1: 2 + 2 + 12 + 1 + 6 + 2*6 + 16 + 4. */
#define PROVISIONING_RECORD_SIZE 55u

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
	 * build_set() already orders them ([red_addr, green_addr]) even though
	 * RADIO_PROTOCOL.md §10.1 does not pin the order in writing. Pinned
	 * here for the same reason as the CRC32 variant above: radio_ble.c
	 * indexes this array by proto_remote directly, and a reader that
	 * assumed the opposite order would swap which remote's presses land on
	 * which lamp with no error anywhere. On a RED/GREEN record, index 0 is
	 * the dongle and index 1 is the zero slot (unused). */
	uint8_t peer_addr[PROVISIONING_PEER_COUNT][PROVISIONING_ADDR_LEN];
	uint8_t set_key[PROVISIONING_KEY_LEN];
};

enum provisioning_status {
	PROVISIONING_OK = 0,
	PROVISIONING_ERR_MAGIC,    /* not this format at all — includes erased flash */
	PROVISIONING_ERR_VERSION,  /* a format this reader predates or postdates */
	PROVISIONING_ERR_CRC,      /* corrupt, or a partial/torn write */
	PROVISIONING_ERR_SERIAL,   /* set_serial outside [A-Z0-9-] or not NUL-padded */
	PROVISIONING_ERR_ROLE,     /* role byte not one of the three known values */
};

/*
 * Parses PROVISIONING_RECORD_SIZE raw bytes (the on-flash layout, exactly as
 * written by dongle/tools/provision.py) into *out.
 *
 * Never partially fills *out — on any status other than PROVISIONING_OK, *out
 * is untouched, so a caller cannot accidentally act on a half-parsed record.
 */
enum provisioning_status provisioning_parse(const uint8_t raw[PROVISIONING_RECORD_SIZE],
					     struct provisioning_record *out);

const char *provisioning_status_str(enum provisioning_status s);

uint32_t provisioning_crc32(const uint8_t *data, size_t len);

#endif /* COMMON_PROVISIONING_H_ */
