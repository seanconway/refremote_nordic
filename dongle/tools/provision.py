#!/usr/bin/env python3
"""Bench tool for the provisioning record — RADIO_PROTOCOL.md §10.1, dongle/BUILD_SPEC.md §9.

Generates the three provisioning records for one set (dongle, RED, GREEN) from
a serial: random LE static-random addresses, a random shared set key, and the
CRC32 dongle/src/provisioning_flash.c and remote/src/prov_flash.c will check
at boot. Writes each as an Intel HEX file targeting that board's storage
partition — 0xf0000 (16 KB) for the dongle per dongle/BUILD_SPEC.md §9, and
0xf8000 (32 KB) for RED/GREEN per remote/BUILD_SPEC.md §9, since the DK's
stock devicetree partition sits at a different address than the dongle's —
plus one human-readable manifest.

    python provision.py RR-0001
    python provision.py RR-0001 -o out/

Deliberately dependency-free beyond the Python standard library — no
`intelhex`, no third-party crypto package. `zlib.crc32` is used for the CRC
rather than reimplementing it, because it computes exactly the variant pinned
in common/provisioning.h (poly 0xEDB88320, init/xorout 0xFFFFFFFF, the same
"CRC-32" zlib/PNG/gzip/Ethernet all use) — a bench tool re-implementing that
by hand is exactly the kind of thing that could drift from the firmware
reader silently. common/provisioning.c's host tests pin the same check value
this relies on: CRC-32("123456789") == 0xCBF43926.

The manifest this writes contains the set's shared key in the clear. Treat it
as manufacturing-floor material, not something to commit or mail — RADIO_PROTOCOL.md
§10.4 already notes set_key is stored in plain flash and depends on APPROTECT,
not this tool, for protection against physical access.
"""
import argparse
import re
import secrets
import struct
import sys
import zlib
from pathlib import Path

# ---------------------------------------------------------------------------
# Record layout — must track common/provisioning.h exactly. See that header
# for why the magic/version split and role values are pinned rather than
# assumed.
# ---------------------------------------------------------------------------

MAGIC = 0x5252          # ASCII "RR", little-endian
VERSION = 1
SERIAL_LEN = 12
ADDR_LEN = 6
PEER_COUNT = 2
KEY_LEN = 16
RECORD_SIZE = 55         # 2 + 2 + 12 + 1 + 6 + 2*6 + 16 + 4

ROLE_DONGLE, ROLE_RED, ROLE_GREEN = 0, 1, 2
ROLE_NAMES = {ROLE_DONGLE: "dongle", ROLE_RED: "red", ROLE_GREEN: "green"}

# dongle/BUILD_SPEC.md §9: 16 KB at 0xf0000, below the nRF5 bootloader.
STORAGE_PARTITION_BASE = 0xF0000

# remote/BUILD_SPEC.md §9: 32 KB at 0xf8000 on the nrf52840dk — the DK's stock
# nordic/nrf52840_partition.dtsi puts storage_partition at a different address
# than the dongle's, so RED/GREEN hex files must target this instead of
# STORAGE_PARTITION_BASE above. A hex written at the wrong address doesn't
# corrupt anything on this board (0xf0000-0xf8000 sits below storage_partition
# and above the application), but the remote would still read as unprovisioned
# — silently, since nothing at 0xf8000 was ever written.
REMOTE_STORAGE_PARTITION_BASE = 0xF8000

STORAGE_PARTITION_BASE_BY_ROLE = {
    ROLE_DONGLE: STORAGE_PARTITION_BASE,
    ROLE_RED: REMOTE_STORAGE_PARTITION_BASE,
    ROLE_GREEN: REMOTE_STORAGE_PARTITION_BASE,
}

SERIAL_RE = re.compile(r"^[A-Z0-9-]{1,%d}$" % SERIAL_LEN)


def zero_addr():
    return bytes(ADDR_LEN)


def random_static_addr():
    """An LE static random identity address — RADIO_PROTOCOL.md §10.2: the two
    most significant bits of the most significant octet must be 1. Zephyr's
    bt_addr_t.val[] is ordered so val[5] is that octet."""
    addr = bytearray(secrets.token_bytes(ADDR_LEN))
    addr[5] |= 0xC0
    return bytes(addr)


def pack_record(set_serial, role, own_addr, peer_addrs, set_key):
    assert len(own_addr) == ADDR_LEN
    assert len(peer_addrs) == PEER_COUNT
    assert all(len(a) == ADDR_LEN for a in peer_addrs)
    assert len(set_key) == KEY_LEN

    serial_bytes = set_serial.encode("ascii").ljust(SERIAL_LEN, b"\x00")
    assert len(serial_bytes) == SERIAL_LEN

    body = struct.pack("<HH", MAGIC, VERSION)
    body += serial_bytes
    body += bytes([role])
    body += own_addr
    for a in peer_addrs:
        body += a
    body += set_key

    assert len(body) == RECORD_SIZE - 4, len(body)

    crc = zlib.crc32(body) & 0xFFFFFFFF
    record = body + struct.pack("<I", crc)

    assert len(record) == RECORD_SIZE
    return record


# ---------------------------------------------------------------------------
# Intel HEX — written directly rather than pulling in the `intelhex` package.
# Only what this tool needs: an extended linear address record (the storage
# partition sits above the 16-bit offset an ordinary data record can address),
# one data record, and EOF.
# ---------------------------------------------------------------------------

def _ihex_checksum(byte_values):
    return (-sum(byte_values)) & 0xFF


def _ihex_line(byte_count, address16, record_type, data):
    fields = [byte_count, (address16 >> 8) & 0xFF, address16 & 0xFF, record_type]
    fields += list(data)
    fields.append(_ihex_checksum(fields))
    return ":" + "".join("%02X" % b for b in fields)


def write_ihex(path, base_address, data):
    lines = []
    # Extended linear address record (type 04): the upper 16 bits of the
    # address, big-endian in the data field. Needed because the storage
    # partition's base (0xf0000) doesn't fit the 16-bit offset an ordinary
    # data record addresses.
    lines.append(_ihex_line(2, 0, 0x04, struct.pack(">H", (base_address >> 16) & 0xFFFF)))

    offset = base_address & 0xFFFF
    CHUNK = 16
    for i in range(0, len(data), CHUNK):
        chunk = data[i : i + CHUNK]
        lines.append(_ihex_line(len(chunk), offset + i, 0x00, chunk))

    lines.append(_ihex_line(0, 0, 0x01, []))  # EOF

    path.write_text("\n".join(lines) + "\n", encoding="ascii")


# ---------------------------------------------------------------------------

def build_set(set_serial):
    dongle_addr = random_static_addr()
    red_addr = random_static_addr()
    green_addr = random_static_addr()
    set_key = secrets.token_bytes(KEY_LEN)

    records = {
        ROLE_DONGLE: pack_record(set_serial, ROLE_DONGLE, dongle_addr,
                                  [red_addr, green_addr], set_key),
        ROLE_RED: pack_record(set_serial, ROLE_RED, red_addr,
                               [dongle_addr, zero_addr()], set_key),
        ROLE_GREEN: pack_record(set_serial, ROLE_GREEN, green_addr,
                                 [dongle_addr, zero_addr()], set_key),
    }
    addrs = {ROLE_DONGLE: dongle_addr, ROLE_RED: red_addr, ROLE_GREEN: green_addr}
    return records, addrs, set_key


def addr_str(a):
    return ":".join("%02X" % b for b in a)


def write_manifest(path, set_serial, addrs, set_key, filenames):
    lines = [
        "RefRemote provisioning manifest",
        "set_serial: %s" % set_serial,
        "set_key:    %s" % set_key.hex().upper(),
        "",
        "SENSITIVE - the key above is this set's shared secret in the clear.",
        "Bench/manufacturing record only. Do not commit, mail, or archive",
        "alongside anything public. RADIO_PROTOCOL.md section 10.4.",
        "",
    ]
    for role in (ROLE_DONGLE, ROLE_RED, ROLE_GREEN):
        lines.append("[%s]" % ROLE_NAMES[role])
        lines.append("  own_addr: %s" % addr_str(addrs[role]))
        lines.append("  hex file: %s" % filenames[role].name)
        lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("serial", help="set serial, [A-Z0-9-], up to %d chars — printed on all three cases" % SERIAL_LEN)
    ap.add_argument("-o", "--outdir", default=".", help="output directory (default: .)")
    args = ap.parse_args()

    if not SERIAL_RE.match(args.serial):
        print("error: serial must match [A-Z0-9-]{1,%d}, got %r" % (SERIAL_LEN, args.serial),
              file=sys.stderr)
        return 1

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    records, addrs, set_key = build_set(args.serial)

    filenames = {}
    for role in (ROLE_DONGLE, ROLE_RED, ROLE_GREEN):
        base = STORAGE_PARTITION_BASE_BY_ROLE[role]
        fn = outdir / ("%s_%s.hex" % (args.serial, ROLE_NAMES[role]))
        write_ihex(fn, base, records[role])
        filenames[role] = fn
        print("wrote %s (%d bytes @ 0x%06X)" % (fn, len(records[role]), base))

    manifest = outdir / ("%s_manifest.txt" % args.serial)
    write_manifest(manifest, args.serial, addrs, set_key, filenames)
    print("wrote %s" % manifest)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
