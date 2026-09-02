#!/usr/bin/env python3
"""Reports drift between eAIS's copies of shared modules and eNMEA's originals.

The two projects run on the same hardware and share their network, settings,
power and display layers. Those files carry fixes that were expensive to find on
real hardware - e-ink double buffering, the lwIP socket leak on TCP retry, the
SoftAP subnet collision with marine gateways - and a fix applied to one copy and
forgotten in the other is exactly how they come back.

    python3 scripts/check_shared.py            # summary
    python3 scripts/check_shared.py --diff     # full unified diff

Exits non-zero when anything has drifted, so it can gate a build if wanted.
"""
import argparse
import difflib
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent.parent
ENMEA = HERE.parent / "eNMEA"

# Files copied from eNMEA. Anything not listed here is eAIS's own.
SHARED = [
    "src/BoardPins.h", "src/Product.h", "src/PowerControl.h", "src/PowerControl.cpp",
    "src/nmea/NmeaLineReader.h", "src/nmea/NmeaLineReader.cpp",
    "src/settings/AppSettings.h", "src/settings/AppSettings.cpp",
    "src/settings/ProvisioningPortal.h", "src/settings/ProvisioningPortal.cpp",
    "src/ui/EinkCanvas.h", "src/ui/EinkCanvas.cpp",
    "src/ui/Font5x7.h", "src/ui/Font5x7.cpp",
    "test/stubs/Arduino.h",
    "partitions.csv", "scripts/gen_font.py", "scripts/preview_font.py",
]

# The provenance banner is eAIS's own addition and is not drift.
BANNER = re.compile(r"^// SHARED WITH eNMEA.*?do not casually diverge\.\n", re.S | re.M)


def body(path):
    text = path.read_text()
    return BANNER.sub("", text, count=1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--diff", action="store_true", help="print the full diff for each drifted file")
    args = ap.parse_args()

    if not ENMEA.is_dir():
        print(f"eNMEA not found at {ENMEA} - cannot compare.")
        return 2

    drifted, missing, same = [], [], 0
    for rel in SHARED:
        mine, theirs = HERE / rel, ENMEA / rel
        if not mine.exists() or not theirs.exists():
            missing.append(rel)
            continue
        a, b = body(theirs), body(mine)
        if a == b:
            same += 1
            continue
        drifted.append(rel)
        if args.diff:
            print("".join(difflib.unified_diff(
                a.splitlines(keepends=True), b.splitlines(keepends=True),
                fromfile=f"eNMEA/{rel}", tofile=f"eAIS/{rel}")))

    print(f"{same} identical, {len(drifted)} drifted, {len(missing)} missing")
    for rel in drifted:
        print(f"  DRIFT   {rel}")
    for rel in missing:
        print(f"  MISSING {rel}")
    if drifted and not args.diff:
        print("\nRun with --diff to see what changed.")
    return 1 if (drifted or missing) else 0


if __name__ == "__main__":
    sys.exit(main())
