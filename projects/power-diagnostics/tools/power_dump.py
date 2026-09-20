"""Collect the recorded log from a finished POWER_PROBE run.

    micromamba run -n platformio python projects/power-diagnostics/tools/power_dump.py

Run it after the device's screen shows a verdict. It sends `dump`, saves the
CSV next to this script, and prints a per-pin summary independent of the
device's own analysis.
"""
from collections import defaultdict
from pathlib import Path
import sys
import time

import serial

PORT = "/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_44:1B:F6:CE:4E:40-if00"
OUT = Path(__file__).resolve().parents[1] / "last_run.csv"


def collect(port, timeout=20):
    s = serial.Serial(port, 115200, timeout=1)
    time.sleep(0.3)
    s.reset_input_buffer()
    s.write(b"dump\n")
    lines, deadline = [], time.time() + timeout
    while time.time() < deadline:
        raw = s.readline()
        if not raw:
            continue
        line = raw.decode(errors="replace").rstrip()
        lines.append(line)
        if line.startswith("# end"):
            break
    s.close()
    return lines


# Used only when the device's header line did not survive the transfer; it
# must match CANDIDATES in src/main.cpp.
FALLBACK_PINS = ["gpio2", "gpio3", "gpio6", "gpio14", "gpio21", "gpio38",
                 "gpio39", "gpio40", "gpio41", "gpio42", "gpio47", "gpio48"]


def summarise(lines):
    header = next((l for l in lines if l.startswith("# columns:")), None)
    if header:
        columns = header.split(":", 1)[1].strip().split(",")
        pins = columns[3:]
        note = ""
    else:
        pins = FALLBACK_PINS
        columns = ["ms", "usb", "battery_mv"] + pins
        note = "No column header in the reply; assuming the pin order in main.cpp.\n"
    rows = []
    for line in lines:
        if line.startswith("#") or not line:
            continue
        parts = line.split(",")
        if len(parts) != len(columns):
            continue
        rows.append(parts)
    if not rows:
        return note + "No samples recorded."

    # Drop the samples either side of a cable change; those catch the
    # transition rather than either steady state.
    seen = defaultdict(lambda: (set(), set()))
    battery = ([], [])
    for i, row in enumerate(rows):
        if i == 0 or i + 1 >= len(rows):
            continue
        usb = row[1]
        if rows[i - 1][1] != usb or rows[i + 1][1] != usb:
            continue
        slot = 0 if usb == "1" else 1
        battery[slot].append(int(row[2]))
        for p, pin in enumerate(pins):
            seen[pin][slot].add(row[3 + p])

    out = ([note.rstrip()] if note else []) + [f"{len(rows)} samples"]
    for slot, label in ((0, "USB present"), (1, "battery only")):
        mv = battery[slot]
        out.append(f"  {label:<13} {len(mv):>3} samples"
                   + (f", battery divider avg {sum(mv) // len(mv)} mV" if mv else ""))
    out.append("")
    out.append(f"  {'pin':<8}{'USB present':<22}{'battery only':<22}verdict")
    for pin in pins:
        on, off = seen[pin]
        if len(on) == 1 and len(off) == 1 and on != off:
            verdict = "TRACKS USB POWER"
        elif len(on) > 1 or len(off) > 1:
            verdict = "unstable"
        else:
            verdict = "no change"
        out.append(f"  {pin:<8}{'/'.join(sorted(on)) or '-':<22}"
                   f"{'/'.join(sorted(off)) or '-':<22}{verdict}")
    return "\n".join(out)


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else PORT
    lines = collect(port)
    if not lines:
        print("No reply from the device.")
        return 1
    OUT.write_text("\n".join(lines) + "\n")
    for line in lines:
        if line.startswith("#"):
            print(line)
    print()
    print(summarise(lines))
    print()
    print(f"Full log written to {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
