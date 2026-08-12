#include "provisioning.h"

#include <string.h>

static uint32_t rd_u16(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t rd_u32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Standard reflected CRC-32 (poly 0xEDB88320, init/xorout 0xFFFFFFFF) — see the
 * header comment for why the exact variant is pinned rather than assumed. */
uint32_t provisioning_crc32(const uint8_t *data, size_t len)
{
	uint32_t crc = 0xFFFFFFFFu;

	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int b = 0; b < 8; b++) {
			uint32_t mask = 0u - (crc & 1u);

			crc = (crc >> 1) ^ (0xEDB88320u & mask);
		}
	}
	return crc ^ 0xFFFFFFFFu;
}

static int valid_serial_char(uint8_t c)
{
	return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
}

enum provisioning_status provisioning_parse(const uint8_t raw[PROVISIONING_RECORD_SIZE],
					     struct provisioning_record *out)
{
	size_t off;
	const uint8_t *serial;
	int seen_nul = 0;
	int any_char = 0;
	uint8_t role;
	uint32_t crc_stored;
	uint32_t crc_calc;

	if (rd_u16(&raw[0]) != PROVISIONING_MAGIC) {
		return PROVISIONING_ERR_MAGIC;
	}
	if (rd_u16(&raw[2]) != PROVISIONING_VERSION) {
		return PROVISIONING_ERR_VERSION;
	}

	crc_stored = rd_u32(&raw[PROVISIONING_RECORD_SIZE - 4]);
	crc_calc = provisioning_crc32(raw, PROVISIONING_RECORD_SIZE - 4);
	if (crc_stored != crc_calc) {
		return PROVISIONING_ERR_CRC;
	}

	off = 4;
	serial = &raw[off];
	for (size_t i = 0; i < PROVISIONING_SERIAL_LEN; i++) {
		uint8_t c = serial[i];

		if (seen_nul) {
			if (c != 0) {
				return PROVISIONING_ERR_SERIAL;
			}
			continue;
		}
		if (c == 0) {
			seen_nul = 1;
			continue;
		}
		if (!valid_serial_char(c)) {
			return PROVISIONING_ERR_SERIAL;
		}
		any_char = 1;
	}
	if (!any_char) {
		return PROVISIONING_ERR_SERIAL;
	}
	off += PROVISIONING_SERIAL_LEN;

	role = raw[off];
	if (role != PROVISIONING_ROLE_DONGLE && role != PROVISIONING_ROLE_RED &&
	    role != PROVISIONING_ROLE_GREEN) {
		return PROVISIONING_ERR_ROLE;
	}
	off += 1;

	/* Every check above passed, so *out is filled once and completely —
	 * see the header comment on why a partial fill must never happen. */
	memset(out, 0, sizeof(*out));
	memcpy(out->set_serial, serial, PROVISIONING_SERIAL_LEN);
	out->set_serial[PROVISIONING_SERIAL_LEN] = '\0';
	out->role = (enum provisioning_role)role;

	memcpy(out->own_addr, &raw[off], PROVISIONING_ADDR_LEN);
	off += PROVISIONING_ADDR_LEN;

	for (size_t p = 0; p < PROVISIONING_PEER_COUNT; p++) {
		memcpy(out->peer_addr[p], &raw[off], PROVISIONING_ADDR_LEN);
		off += PROVISIONING_ADDR_LEN;
	}

	memcpy(out->set_key, &raw[off], PROVISIONING_KEY_LEN);
	off += PROVISIONING_KEY_LEN;

	/* off is now RECORD_SIZE - 4: the crc32 field, already checked above. */
	return PROVISIONING_OK;
}

const char *provisioning_status_str(enum provisioning_status s)
{
	switch (s) {
	case PROVISIONING_OK:          return "ok";
	case PROVISIONING_ERR_MAGIC:   return "bad magic";
	case PROVISIONING_ERR_VERSION: return "unsupported version";
	case PROVISIONING_ERR_CRC:     return "crc mismatch";
	case PROVISIONING_ERR_SERIAL:  return "bad set_serial";
	case PROVISIONING_ERR_ROLE:    return "bad role";
	default:                       return "?";
	}
}
