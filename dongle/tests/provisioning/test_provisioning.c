/*
 * Host unit tests for common/provisioning.c — RADIO_PROTOCOL.md §10.1 and the
 * two things dongle/BUILD_SPEC.md §9 pins beyond it: the CRC32 variant and the
 * role enum's numeric values. A reader and dongle/tools/provision.py that
 * disagree on either produce a record that looks fine and is silently
 * unreadable, so this is the one place that gets exhaustively checked on a
 * host with nothing but gcc — same reasoning as tests/protocol, PROTOCOL.md §11.
 *
 * Deliberately framework-free: `make check`.
 */
#include "../../../common/provisioning.h"

#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

#define CHECK(cond, ...)                                                      \
	do {                                                                   \
		checks++;                                                      \
		if (!(cond)) {                                                 \
			failures++;                                            \
			printf("  FAIL %s:%d: ", __func__, __LINE__);          \
			printf(__VA_ARGS__);                                   \
			printf("\n");                                          \
		}                                                              \
	} while (0)

/* ------------------------------------------------------------------------ */
/* Fixture                                                                   */
/* ------------------------------------------------------------------------ */

static void wr_u16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v & 0xFFu);
	p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void wr_u32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v & 0xFFu);
	p[1] = (uint8_t)((v >> 8) & 0xFFu);
	p[2] = (uint8_t)((v >> 16) & 0xFFu);
	p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

/* Fills a well-formed record for `role`, with the CRC left uncomputed so
 * callers can corrupt a field first and still get a self-consistent CRC by
 * calling finish_crc() last — that is what makes the CRC tests meaningful:
 * a record with a stale CRC would be rejected for the wrong reason. */
static void build(uint8_t raw[PROVISIONING_RECORD_SIZE], enum provisioning_role role)
{
	memset(raw, 0, PROVISIONING_RECORD_SIZE);
	wr_u16(&raw[0], PROVISIONING_MAGIC);
	wr_u16(&raw[2], PROVISIONING_VERSION);
	memcpy(&raw[4], "RR-0001", 7); /* NUL-padded by the memset above */
	raw[16] = (uint8_t)role;
	for (uint8_t i = 0; i < PROVISIONING_ADDR_LEN; i++) {
		raw[17 + i] = (uint8_t)(0xC0u | i); /* static-random top bits set */
	}
	for (uint8_t p = 0; p < PROVISIONING_PEER_COUNT; p++) {
		for (uint8_t i = 0; i < PROVISIONING_ADDR_LEN; i++) {
			raw[23 + p * PROVISIONING_ADDR_LEN + i] = (uint8_t)(0xD0u + p * 16 + i);
		}
	}
	for (uint8_t i = 0; i < PROVISIONING_KEY_LEN; i++) {
		raw[35 + i] = (uint8_t)(0xA0u + i);
	}
}

static void finish_crc(uint8_t raw[PROVISIONING_RECORD_SIZE])
{
	uint32_t crc = provisioning_crc32(raw, PROVISIONING_RECORD_SIZE - 4);

	wr_u32(&raw[PROVISIONING_RECORD_SIZE - 4], crc);
}

/* ------------------------------------------------------------------------ */
/* Tests                                                                     */
/* ------------------------------------------------------------------------ */

/* The canonical check value: CRC-32("123456789") == 0xCBF43926 for this exact
 * variant (poly 0xEDB88320, init/xorout 0xFFFFFFFF). If this fails, the
 * algorithm itself drifted from the header's pinned definition. */
static void crc32_check_value(void)
{
	uint32_t crc = provisioning_crc32((const uint8_t *)"123456789", 9);

	CHECK(crc == 0xCBF43926u, "got 0x%08X", crc);
}

static void valid_record_parses(void)
{
	uint8_t raw[PROVISIONING_RECORD_SIZE];
	struct provisioning_record rec;

	build(raw, PROVISIONING_ROLE_DONGLE);
	finish_crc(raw);

	enum provisioning_status st = provisioning_parse(raw, &rec);

	CHECK(st == PROVISIONING_OK, "status %s", provisioning_status_str(st));
	CHECK(strcmp(rec.set_serial, "RR-0001") == 0, "got '%s'", rec.set_serial);
	CHECK(rec.role == PROVISIONING_ROLE_DONGLE, "got %d", rec.role);
	CHECK(rec.own_addr[0] == 0xC0u, "got 0x%02X", rec.own_addr[0]);
	CHECK(rec.peer_addr[0][0] == 0xD0u, "got 0x%02X", rec.peer_addr[0][0]);
	CHECK(rec.peer_addr[1][0] == 0xE0u, "got 0x%02X", rec.peer_addr[1][0]);
	CHECK(rec.set_key[0] == 0xA0u, "got 0x%02X", rec.set_key[0]);
}

static void all_three_roles_accepted(void)
{
	enum provisioning_role roles[] = {
		PROVISIONING_ROLE_DONGLE, PROVISIONING_ROLE_RED, PROVISIONING_ROLE_GREEN,
	};

	for (size_t i = 0; i < sizeof(roles) / sizeof(roles[0]); i++) {
		uint8_t raw[PROVISIONING_RECORD_SIZE];
		struct provisioning_record rec;

		build(raw, roles[i]);
		finish_crc(raw);

		enum provisioning_status st = provisioning_parse(raw, &rec);

		CHECK(st == PROVISIONING_OK, "role %d: status %s", roles[i],
		      provisioning_status_str(st));
	}
}

/* Erased NOR flash reads as all-0xFF, and A19 (RADIO_PROTOCOL.md §14) calls
 * that case "absent" rather than "corrupt" — but the reader makes no such
 * distinction, and this is why: 0xFFFF is not PROVISIONING_MAGIC, so an
 * absent record and a corrupt one take the identical path with no special
 * case needed. */
static void erased_flash_is_bad_magic(void)
{
	uint8_t raw[PROVISIONING_RECORD_SIZE];
	struct provisioning_record rec;

	memset(raw, 0xFF, sizeof(raw));

	enum provisioning_status st = provisioning_parse(raw, &rec);

	CHECK(st == PROVISIONING_ERR_MAGIC, "got %s", provisioning_status_str(st));
}

static void bad_version_rejected(void)
{
	uint8_t raw[PROVISIONING_RECORD_SIZE];
	struct provisioning_record rec;

	build(raw, PROVISIONING_ROLE_DONGLE);
	wr_u16(&raw[2], PROVISIONING_VERSION + 1u);
	finish_crc(raw);

	enum provisioning_status st = provisioning_parse(raw, &rec);

	CHECK(st == PROVISIONING_ERR_VERSION, "got %s", provisioning_status_str(st));
}

/* The CRC test that matters: a single flipped bit anywhere in the record, with
 * the stored CRC left as it was, must be caught. Corrupting a field and then
 * calling finish_crc() (as other tests do) would make the corruption
 * self-consistent and prove nothing about the CRC check itself. */
static void single_bit_flip_caught(void)
{
	uint8_t raw[PROVISIONING_RECORD_SIZE];
	struct provisioning_record rec;

	build(raw, PROVISIONING_ROLE_DONGLE);
	finish_crc(raw);
	raw[40] ^= 0x01u; /* inside set_key, well past every other field's checks */

	enum provisioning_status st = provisioning_parse(raw, &rec);

	CHECK(st == PROVISIONING_ERR_CRC, "got %s", provisioning_status_str(st));
}

static void serial_bad_char_rejected(void)
{
	uint8_t raw[PROVISIONING_RECORD_SIZE];
	struct provisioning_record rec;

	build(raw, PROVISIONING_ROLE_DONGLE);
	raw[4] = (uint8_t)'r'; /* lowercase: outside [A-Z0-9-] */
	finish_crc(raw);

	enum provisioning_status st = provisioning_parse(raw, &rec);

	CHECK(st == PROVISIONING_ERR_SERIAL, "got %s", provisioning_status_str(st));
}

static void serial_garbage_after_nul_rejected(void)
{
	uint8_t raw[PROVISIONING_RECORD_SIZE];
	struct provisioning_record rec;

	build(raw, PROVISIONING_ROLE_DONGLE); /* "RR-0001\0\0\0\0\0" */
	raw[4 + 11] = (uint8_t)'X'; /* last serial byte, past the NUL build() left */
	finish_crc(raw);

	enum provisioning_status st = provisioning_parse(raw, &rec);

	CHECK(st == PROVISIONING_ERR_SERIAL, "got %s", provisioning_status_str(st));
}

static void serial_all_nul_rejected(void)
{
	uint8_t raw[PROVISIONING_RECORD_SIZE];
	struct provisioning_record rec;

	build(raw, PROVISIONING_ROLE_DONGLE);
	memset(&raw[4], 0, PROVISIONING_SERIAL_LEN);
	finish_crc(raw);

	enum provisioning_status st = provisioning_parse(raw, &rec);

	CHECK(st == PROVISIONING_ERR_SERIAL, "got %s", provisioning_status_str(st));
}

static void bad_role_rejected(void)
{
	uint8_t raw[PROVISIONING_RECORD_SIZE];
	struct provisioning_record rec;

	build(raw, PROVISIONING_ROLE_DONGLE);
	raw[16] = 3u; /* one past the last defined role */
	finish_crc(raw);

	enum provisioning_status st = provisioning_parse(raw, &rec);

	CHECK(st == PROVISIONING_ERR_ROLE, "got %s", provisioning_status_str(st));
}

/* PROVISIONING_OK must fill every field it documents, not just the ones the
 * other tests happen to check — a field silently left zeroed would pass
 * valid_record_parses() if that test didn't ask about it. */
static void full_record_fidelity(void)
{
	uint8_t raw[PROVISIONING_RECORD_SIZE];
	struct provisioning_record rec;

	build(raw, PROVISIONING_ROLE_GREEN);
	finish_crc(raw);

	enum provisioning_status st = provisioning_parse(raw, &rec);

	CHECK(st == PROVISIONING_OK, "status %s", provisioning_status_str(st));
	for (uint8_t i = 0; i < PROVISIONING_ADDR_LEN; i++) {
		CHECK(rec.own_addr[i] == (uint8_t)(0xC0u | i), "own_addr[%u]", i);
		CHECK(rec.peer_addr[0][i] == (uint8_t)(0xD0u + i), "peer_addr[0][%u]", i);
		CHECK(rec.peer_addr[1][i] == (uint8_t)(0xE0u + i), "peer_addr[1][%u]", i);
	}
	for (uint8_t i = 0; i < PROVISIONING_KEY_LEN; i++) {
		CHECK(rec.set_key[i] == (uint8_t)(0xA0u + i), "set_key[%u]", i);
	}
}

/* On any failure *out must be untouched — the header's contract, and the one
 * that keeps a caller from acting on a half-parsed record if it forgets to
 * check the return value carefully. */
static void failure_leaves_out_untouched(void)
{
	uint8_t raw[PROVISIONING_RECORD_SIZE];
	struct provisioning_record rec;
	struct provisioning_record sentinel;

	memset(&sentinel, 0x5A, sizeof(sentinel));
	rec = sentinel;

	memset(raw, 0xFF, sizeof(raw)); /* bad magic */
	(void)provisioning_parse(raw, &rec);

	CHECK(memcmp(&rec, &sentinel, sizeof(rec)) == 0, "out was modified on failure");
}

/* ------------------------------------------------------------------------ */

int main(void)
{
	struct {
		const char *name;
		void (*fn)(void);
	} tests[] = {
		{ "CRC-32 check value",             crc32_check_value },
		{ "valid record parses",            valid_record_parses },
		{ "all three roles accepted",       all_three_roles_accepted },
		{ "erased flash is bad magic",      erased_flash_is_bad_magic },
		{ "bad version rejected",           bad_version_rejected },
		{ "single bit flip caught",         single_bit_flip_caught },
		{ "serial bad char rejected",       serial_bad_char_rejected },
		{ "serial garbage after NUL",       serial_garbage_after_nul_rejected },
		{ "serial all-NUL rejected",        serial_all_nul_rejected },
		{ "bad role rejected",              bad_role_rejected },
		{ "full record fidelity",           full_record_fidelity },
		{ "failure leaves *out untouched",  failure_leaves_out_untouched },
	};

	for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		int before = failures;

		tests[i].fn();
		printf("%s %s\n", failures == before ? "ok  " : "FAIL", tests[i].name);
	}

	printf("\n%d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
