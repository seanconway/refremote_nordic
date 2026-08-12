/*
 * The dongle-side reader for the provisioning record — BUILD_SPEC.md §9.
 * Opens the "storage" flash partition, reads the raw PROVISIONING_RECORD_SIZE
 * bytes, and hands them to the Zephyr-free parser in common/provisioning.c.
 * This file and provisioning_flash.c are the only Zephyr-specific part of
 * provisioning; the record's shape and validity live in common/, shared
 * unchanged with the remote firmware.
 */
#ifndef DONGLE_PROVISIONING_FLASH_H_
#define DONGLE_PROVISIONING_FLASH_H_

#include "provisioning.h"

/*
 * Reads and validates the record, and additionally requires role ==
 * PROVISIONING_ROLE_DONGLE — a record provisioned for a remote and flashed
 * onto a dongle by mistake is exactly the manufacturing error this exists to
 * catch, not a case to let through because the CRC happened to be fine.
 *
 * On any status other than PROVISIONING_OK, *out is untouched. A role that
 * parses but is not DONGLE comes back as PROVISIONING_ERR_ROLE, the same
 * status a genuinely malformed role byte gets — the caller only needs to know
 * "usable or not", and BUILD_SPEC.md §9 sends both to the same ERR line.
 */
enum provisioning_status provisioning_load(struct provisioning_record *out);

#endif /* DONGLE_PROVISIONING_FLASH_H_ */
