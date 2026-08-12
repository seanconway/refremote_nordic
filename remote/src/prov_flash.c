#include "prov_flash.h"

#include <zephyr/storage/flash_map.h>
#include <string.h>

enum provisioning_status prov_flash_load(struct provisioning_record *out)
{
	const struct flash_area *fa;
	uint8_t raw[PROVISIONING_RECORD_SIZE];
	enum provisioning_status status;
	int rc;

	rc = flash_area_open(FIXED_PARTITION_ID(storage_partition), &fa);
	if (rc != 0) {
		return PROVISIONING_ERR_MAGIC;
	}
	rc = flash_area_read(fa, 0, raw, sizeof(raw));
	flash_area_close(fa);
	if (rc != 0) {
		return PROVISIONING_ERR_MAGIC;
	}

	status = provisioning_parse(raw, out);
	if (status != PROVISIONING_OK) {
		return status;
	}
	if (out->role != PROVISIONING_ROLE_RED && out->role != PROVISIONING_ROLE_GREEN) {
		memset(out, 0, sizeof(*out));
		return PROVISIONING_ERR_ROLE;
	}
	return PROVISIONING_OK;
}
