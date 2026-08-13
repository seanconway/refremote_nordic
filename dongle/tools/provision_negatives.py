#!/usr/bin/env python3
"""Bench tool: the W1 negative-case provisioning headers (RADIO_PROTOCOL.md §14
A12, A13, A19), derived from a matched set `provision.py` already generated.

    python provision.py RR-0007 -o out/          # the positive control, first
    python provision_negatives.py RR-0007 -o out/

Reads out/RR-0007_red/provisioning_data.h and writes three mutated copies,
each changing exactly the one field its case is about and leaving every other
field — most importantly `own_addr` and `peer_addr` — untouched. The dongle's
connect attempt targets the RED unit's own_addr literally (dongle/BUILD_SPEC.md
§7.1: "filtered on the literal address", no scanning); a build that also
changed the address would never be dialled at all, and the case under test
would never run.

    out/RR-0007_red_A12/provisioning_data.h   wrong set_key — encryption fails
    out/RR-0007_red_A13/provisioning_data.h   wrong set_serial — RR_IDENTITY disagrees
    out/RR-0007_red_A19/provisioning_data.h   all-zero set_key — refuses to operate

A14 (`radio_proto_major` mismatch) needs no header of its own: that field has
no provisioning-record representation (RADIO_PROTOCOL.md §3.2 fixes it at 1),
so it is a build-time override instead — build the ordinary <serial>_red
header with -DCONFIG_REMOTE_TEST_RADIO_PROTO_MAJOR=2 (remote/Kconfig).

Same handling as provision.py's own output: these headers carry the set's real
key in the clear (for A12 and A13, which keep a valid key just not the *right*
one). Bench/manufacturing material, not something to commit or mail.
"""
import argparse
import re
import secrets
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import provision as prov  # noqa: E402  (path must be set up first)

HEADER_RE = re.compile(
    r'\.set_serial\s*=\s*"([^"]*)",\s*'
    r'\.role\s*=\s*(\w+),\s*'
    r'\.own_addr\s*=\s*\{([^}]*)\},\s*'
    r'\.peer_addr\s*=\s*\{\s*\{([^}]*)\},\s*\{([^}]*)\},\s*\},\s*'
    r'\.set_key\s*=\s*\{([^}]*)\},',
    re.S,
)


def parse_byte_list(s):
    return bytes(int(x, 16) for x in s.split(","))


def parse_header(path):
    text = path.read_text(encoding="utf-8")
    m = HEADER_RE.search(text)
    if not m:
        print("error: could not parse %s — not a provision.py-generated header?" % path,
              file=sys.stderr)
        sys.exit(1)
    set_serial, role_name, own_addr, peer0, peer1, set_key = m.groups()
    if role_name != "PROVISIONING_ROLE_RED":
        print("error: %s is role %s, expected PROVISIONING_ROLE_RED" % (path, role_name),
              file=sys.stderr)
        sys.exit(1)
    return {
        "set_serial": set_serial,
        "own_addr": parse_byte_list(own_addr),
        "peer_addr": (parse_byte_list(peer0), parse_byte_list(peer1)),
        "set_key": parse_byte_list(set_key),
    }


def wrong_key(real_key):
    k = secrets.token_bytes(prov.KEY_LEN)
    while k == real_key:
        k = secrets.token_bytes(prov.KEY_LEN)
    return k


def wrong_serial(real_serial):
    candidate = (real_serial + "-BAD")[: prov.SERIAL_LEN]
    return candidate if candidate != real_serial else "WRONGSET"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("serial", help="the base set's serial, e.g. RR-0007 — "
                                    "provision.py <serial> must already have been run")
    ap.add_argument("-o", "--outdir", default=".",
                     help="the base set's output directory (default: .) — "
                          "same one passed to provision.py -o")
    args = ap.parse_args()

    outdir = Path(args.outdir)
    red_header = outdir / ("%s_red" % args.serial) / "provisioning_data.h"
    if not red_header.exists():
        print("error: %s not found — run provision.py %s -o %s first" %
              (red_header, args.serial, args.outdir), file=sys.stderr)
        return 1

    base = parse_header(red_header)

    cases = {
        # A12: valid address, wrong set_key. bt_conn_set_security() fails —
        # no GATT access at any point.
        "A12": dict(base, set_key=wrong_key(base["set_key"])),
        # A13: valid key, wrong set_serial. Associates and encrypts fine;
        # RR_IDENTITY disagrees with the dongle's own set_serial (S1's check).
        "A13": dict(base, set_serial=wrong_serial(base["set_serial"])),
        # A19 (remote side): all-zero set_key trips provisioning_validate()'s
        # PROVISIONING_ERR_KEY sentinel at boot — the same one a header nobody
        # ran the generator for would produce.
        "A19": dict(base, set_key=bytes(prov.KEY_LEN)),
    }

    for case, fields in cases.items():
        case_dir = outdir / ("%s_red_%s" % (args.serial, case))
        case_dir.mkdir(parents=True, exist_ok=True)
        header_path = case_dir / "provisioning_data.h"
        prov.write_header(header_path, fields["set_serial"], prov.ROLE_RED,
                           fields["own_addr"], fields["peer_addr"], fields["set_key"])
        print("wrote %s" % header_path)

    print()
    print("Build commands (A14 needs no header — see this script's own docstring):")
    for case in cases:
        header_dir = outdir / ("%s_red_%s" % (args.serial, case))
        print("  %s" % prov.build_command(prov.ROLE_RED, header_dir))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
