#!/usr/bin/env python3
"""
Bench tool for the RefRemote factory calibration/OTP flow. Talks plain
USB-serial to the DONGLE -- not BLE to the remote -- and lets the dongle
relay FACAL/FACOTP down to whichever remote you name over the same
encrypted radio link it already uses for match traffic. See PROTOCOL.md
S17 and RADIO_PROTOCOL.md S17 for the full wire format and the reasoning
behind routing through the dongle instead of a second BLE service on the
remote.

Requires: pip install pyserial

Usage:
    python factory_calibrate.py --port COM5 --remote RED --serial RR-0006
    python factory_calibrate.py --port COM5 --remote RED --serial RR-0006 --burn-otp

Security model: PROTOCOL.md S17 is explicit that this port -- reachable
from any Web-Serial-permitted browser tab -- is NOT the enforcement point.
The remote refuses FACAL/FACOTP on its own once its DRV2605L's OTP is
burned (a hardware fact, drv2605_otp_status()), regardless of what this
tool or anything else sends here. There is no token or credential in this
protocol because there is nothing for one to protect that the remote
doesn't already protect itself.
"""
from __future__ import annotations

import argparse
import csv
import datetime
import sys
import time
from pathlib import Path

import serial

FACSTATUS_STATES_OK = {"CAL_DONE", "CAL_FAILED", "OTP_DONE", "OTP_FAILED"}


def open_port(port: str, baud: int, timeout: float) -> serial.Serial:
    # CDC-ACM ignores the actual baud rate -- pyserial still requires a
    # value to open the port. Chosen for convention, not correctness.
    return serial.Serial(port, baudrate=baud, timeout=timeout)


def send_line(ser: serial.Serial, line: str):
    ser.write((line + "\n").encode("ascii"))


def read_line(ser: serial.Serial) -> str | None:
    raw = ser.readline()
    if not raw:
        return None  # timed out
    return raw.decode("ascii", errors="replace").rstrip("\r\n")


def wait_for_facstatus(ser: serial.Serial, remote: str, deadline: float) -> dict | None:
    """Reads lines until a FACSTATUS for `remote` arrives, the deadline
    passes, or the port stops producing lines. Other traffic (LOG, LINK,
    EVT from a live match on the *other* remote, etc.) is printed and
    skipped -- this is a shared port, not a dedicated one."""
    while time.monotonic() < deadline:
        line = read_line(ser)
        if line is None:
            continue
        if not line.startswith("FACSTATUS "):
            print(f"  (ignored) {line}")
            continue

        parts = line.split()
        if len(parts) != 9 or parts[1] != remote or parts[2] not in FACSTATUS_STATES_OK:
            print(f"  (malformed or not for {remote}) {line}")
            continue

        return {
            "remote": parts[1],
            "state": parts[2],
            "detail": int(parts[3]),
            "diag_pass": parts[4] == "1",
            "vdd_mv": int(parts[5]),
            "a_cal_comp": int(parts[6]),
            "a_cal_bemf": int(parts[7]),
            "feedback_control": int(parts[8]),
        }
    return None


def log_result(path: str, record: dict):
    p = Path(path)
    is_new = not p.exists()
    with p.open("a", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(record.keys()))
        if is_new:
            writer.writeheader()
        writer.writerow(record)


def run(args) -> int:
    ser = open_port(args.port, args.baud, args.timeout)

    record = {
        "serial": args.serial,
        "remote": args.remote,
        "timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "cal_state": "",
        "diag_pass": "",
        "vdd_mv_at_cal": "",
        "a_cal_comp": "",
        "a_cal_bemf": "",
        "feedback_control": "",
        "otp_state": "",
        "vdd_mv_at_otp": "",
    }

    print(f"Requesting calibration on {args.remote} ({args.serial}) ...")
    send_line(ser, f"FACAL {args.remote}")
    deadline = time.monotonic() + args.cal_timeout
    cal = wait_for_facstatus(ser, args.remote, deadline)

    if cal is None:
        print("No FACSTATUS reply -- the remote refused (already calibrated?) "
              "or is unreachable. See PROTOCOL.md S17: silence is the signal.",
              file=sys.stderr)
        log_result(args.log_file, record)
        return 1

    record["cal_state"] = cal["state"]
    record["diag_pass"] = cal["diag_pass"]
    record["vdd_mv_at_cal"] = cal["vdd_mv"]
    record["a_cal_comp"] = f"0x{cal['a_cal_comp']:02x}"
    record["a_cal_bemf"] = f"0x{cal['a_cal_bemf']:02x}"
    record["feedback_control"] = f"0x{cal['feedback_control']:02x}"

    if cal["state"] != "CAL_DONE" or not cal["diag_pass"]:
        print(f"Calibration did not pass: {cal}")
        log_result(args.log_file, record)
        return 1

    print(f"Calibration OK: vdd={cal['vdd_mv']}mV comp=0x{cal['a_cal_comp']:02x} "
          f"bemf=0x{cal['a_cal_bemf']:02x} fb=0x{cal['feedback_control']:02x}")

    if not args.burn_otp:
        print("--burn-otp not given; stopping after calibrate-and-verify.")
        log_result(args.log_file, record)
        return 0

    # Irreversible per physical DRV2605L -- require the operator to type
    # the unit's serial back, not just answer y/n, so a copy-pasted "yes"
    # across a batch script can't silently confirm the wrong unit.
    typed = input(f"Type the serial \"{args.serial}\" to confirm burning OTP "
                   f"on {args.remote} (irreversible): ")
    if typed != args.serial:
        print("Serial did not match; aborting before OTP burn.", file=sys.stderr)
        log_result(args.log_file, record)
        return 1

    print("Burning OTP ...")
    send_line(ser, f"FACOTP {args.remote}")
    deadline = time.monotonic() + args.otp_timeout
    otp = wait_for_facstatus(ser, args.remote, deadline)

    if otp is None:
        print("No FACSTATUS reply for the OTP burn -- check the unit before "
              "retrying. A burn that the remote actually started but this "
              "tool never heard back from should be treated as unknown, not "
              "as failed: re-run FACAL to check drv2605_otp_status() rather "
              "than assuming it is safe to retry FACOTP blind.", file=sys.stderr)
        log_result(args.log_file, record)
        return 1

    record["otp_state"] = otp["state"]
    record["vdd_mv_at_otp"] = otp["vdd_mv"]
    log_result(args.log_file, record)

    if otp["state"] != "OTP_DONE":
        print(f"OTP burn failed: {otp} (detail={otp['detail']}, see "
              f"remote/src/drv2605.h's enum drv2605_otp_result)", file=sys.stderr)
        return 1

    print(f"OTP burned and verified on {args.remote} ({args.serial}).")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    ap.add_argument("--port", required=True, help="dongle's serial port, e.g. COM5")
    ap.add_argument("--baud", type=int, default=115200, help="ignored by CDC-ACM; kept for pyserial")
    ap.add_argument("--remote", required=True, choices=["RED", "GREEN"])
    ap.add_argument("--serial", required=True, help="this unit's set_serial, for the log and OTP confirmation")
    ap.add_argument("--log-file", default="factory_calibration_log.csv",
                     help="CSV to append each unit's result to (default: ./factory_calibration_log.csv)")
    ap.add_argument("--burn-otp", action="store_true",
                     help="proceed to burn OTP if calibration passes (default: calibrate-and-verify only)")
    ap.add_argument("--cal-timeout", type=float, default=15.0)
    ap.add_argument("--otp-timeout", type=float, default=10.0)
    ap.add_argument("--timeout", type=float, default=1.0, help="per-readline serial timeout")
    args = ap.parse_args()

    sys.exit(run(args))


if __name__ == "__main__":
    main()
