/*
 * BLE peripheral, advertising, the RefRemote Link Service, uplink CTR —
 * remote/BUILD_SPEC.md §5-§8. The GATT server side of the connection
 * lifecycle dongle/BUILD_SPEC.md §7.1 specifies from the central's end.
 */
#ifndef REMOTE_LINK_H_
#define REMOTE_LINK_H_

#include "protocol.h"
#include "provisioning.h"

struct k_work_q;

/* prov must already be validated (role RED or GREEN, CRC good) — main.c does
 * not call this at all on a failed prov_flash_load(), per A19. */
int link_init(const struct provisioning_record *prov, struct k_work_q *workq);

/* From buttons.c. CTR advances for every classified press, including ones
 * that cannot be delivered (remote/BUILD_SPEC.md §6.2) — link_on_gesture()
 * assigns and advances CTR regardless of connection state. */
void link_on_gesture(enum proto_button b, enum proto_gesture g);

#endif /* REMOTE_LINK_H_ */
