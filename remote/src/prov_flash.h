/*
 * Reads the provisioning partition — remote/BUILD_SPEC.md §9. Identical
 * mechanism to dongle/src/provisioning_flash.c, sharing common/provisioning.c;
 * the only difference is which role this reader will accept.
 */
#ifndef REMOTE_PROV_FLASH_H_
#define REMOTE_PROV_FLASH_H_

#include "provisioning.h"

/* Accepts ROLE_RED or ROLE_GREEN; PROVISIONING_ERR_ROLE for anything else,
 * including ROLE_DONGLE — a dongle-provisioned unit flashed onto a remote by
 * mistake is a manufacturing error, the same class as a bad CRC. */
enum provisioning_status prov_flash_load(struct provisioning_record *out);

#endif /* REMOTE_PROV_FLASH_H_ */
