#!/usr/bin/env python3
"""Stands in for stcgal 1.10 in the config tool's tests: prints what stcgal
prints (formats copied from its source: frontend.py, protocols.py,
models.py, options.py), without a chip.

Environment:
  FAKE_STCGAL_CHIP   chip name to report (default STC15W4K56S4)
  FAKE_STCGAL_FAIL   "connect" or "write": fail there with a protocol error
  FAKE_STCGAL_ERASE  "True" to report eeprom_erase_enabled=True
  FAKE_STCGAL_ARGS   file to write the received arguments to
"""
import os
import sys
import time

args = sys.argv[1:]
if os.environ.get("FAKE_STCGAL_ARGS"):
    with open(os.environ["FAKE_STCGAL_ARGS"], "w") as f:
        f.write("\n".join(args))

chip = os.environ.get("FAKE_STCGAL_CHIP", "STC15W4K56S4")
fail = os.environ.get("FAKE_STCGAL_FAIL", "")
image = [a for a in args if a.endswith(".hex")]
auto = "auto" in args

out = sys.stdout
print("Waiting for MCU, please cycle power: ", end="", flush=True)
time.sleep(0.5)  # the rider switching the controller on
if fail == "connect":
    print("", flush=True)
    print("Protocol error: incorrect magic in status packet", file=sys.stderr, flush=True)
    sys.exit(1)
print("done", flush=True)
if auto:
    print("Protocol detected: stc15", flush=True)
print("Target model:")
print("  Name: %s" % chip)
print("  Magic: F528")
print("  Code flash: 56.0 KB")
print("  EEPROM flash: 3.0 KB")
print("Target frequency: 20.000 MHz")
print("Target BSL version: 7.2.5T")
print("Target wakeup frequency: 37.050 KHz")
print("Target options:")
print("  reset_pin_enabled=False")
print("  clock_source=internal")
print("  eeprom_erase_enabled=%s" % os.environ.get("FAKE_STCGAL_ERASE", "False"))
print("  bsl_pindetect_enabled=False", flush=True)

if not image:
    print("Disconnected!", flush=True)
    sys.exit(0)

print("Trimming frequency: ", end="", flush=True)
print("20.000 MHz")
print("Switching to 19200 baud: ", end="")
print("done")
print("Erasing 74 blocks: ", end="")
print("done", flush=True)
print("Loading flash: 37773 bytes (Intel HEX)", flush=True)
total = 37888
for i in range(0, 11):
    if fail == "write" and i == 5:
        print("", file=sys.stderr, flush=True)
        print("Protocol error: incorrect checksum in packet", file=sys.stderr, flush=True)
        sys.exit(1)
    pct = i * 10
    sys.stderr.write("\rWriting flash: %3d%%|%s%s| %d/%d [00:01<00:01, 9000.00 Bytes/s]" %
                     (pct, "#" * i, " " * (10 - i), total * i // 10, total))
    sys.stderr.flush()
    time.sleep(0.05)
sys.stderr.write("\n")
print("Finishing write: done")
print("Setting options: done")
print("Target UID: F528C4610E1B52")
print("Disconnected!", flush=True)
sys.exit(0)
