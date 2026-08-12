#include "provisioning_flash.h"

#include <zephyr/storage/flash_map.h>

#include <string.h>

enum provisioning_status provisioning_load(struct provisioning_record *out)
{
	const struct flash_area *fa;
	uint8_t raw[PROVISIONING_RECORD_SIZE];
	enum provisioning_status status;
	int rc;

	/* storage_partition is the board's own fstab-stock.dtsi node — not an
	 * overlay this project owns — 16 KB at 0xf0000, dongle/BUILD_SPEC.md
	 * §9. Failing to open it reads the same as a blank one to every
	 * caller: both mean "nothing usable is here". */
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

	if (out->role != PROVISIONING_ROLE_DONGLE) {
		memset(out, 0, sizeof(*out));
		return PROVISIONING_ERR_ROLE;
	}

	return PROVISIONING_OK;
}
