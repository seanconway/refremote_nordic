/*
 * Debounce and gesture classification — remote/BUILD_SPEC.md §4. Runs
 * entirely locally: classification never depends on link state, so a
 * referee pressing a button on a dead link gets identical local behaviour;
 * only delivery fails (link.c's problem, not this file's).
 */
#ifndef REMOTE_BUTTONS_H_
#define REMOTE_BUTTONS_H_

#include "protocol.h"

struct k_work_q;

typedef void (*buttons_gesture_cb)(enum proto_button b, enum proto_gesture g);

/* Runs every callback on workq, the same discipline engine.c uses on the
 * dongle: one producer, no locking needed against link.c's own callbacks. */
int buttons_init(buttons_gesture_cb cb, struct k_work_q *workq);

#endif /* REMOTE_BUTTONS_H_ */
