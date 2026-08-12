#include "provisioning.h"

static int valid_serial_char(uint8_t c)
{
	return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
}

enum provisioning_status provisioning_validate(const struct provisioning_record *r)
{
	int seen_nul = 0;
	int any_char = 0;
	int all_zero_key = 1;

	for (size_t i = 0; i < PROVISIONING_SERIAL_LEN; i++) {
		uint8_t c = (uint8_t)r->set_serial[i];

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

	if (r->role != PROVISIONING_ROLE_DONGLE && r->role != PROVISIONING_ROLE_RED &&
	    r->role != PROVISIONING_ROLE_GREEN) {
		return PROVISIONING_ERR_ROLE;
	}

	for (size_t i = 0; i < PROVISIONING_KEY_LEN; i++) {
		if (r->set_key[i] != 0) {
			all_zero_key = 0;
			break;
		}
	}
	if (all_zero_key) {
		return PROVISIONING_ERR_KEY;
	}

	return PROVISIONING_OK;
}

const char *provisioning_status_str(enum provisioning_status s)
{
	switch (s) {
	case PROVISIONING_OK:          return "ok";
	case PROVISIONING_ERR_SERIAL:  return "bad set_serial";
	case PROVISIONING_ERR_ROLE:    return "bad role";
	case PROVISIONING_ERR_KEY:     return "set_key is all-zero";
	default:                       return "?";
	}
}
