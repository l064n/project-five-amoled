#!/usr/bin/env python3
"""Live touch diagnostic for the T-Display-S3-AMOLED.

Opens the USB-serial port WITHOUT resetting the board (dtr/rts held low before
open), navigates to the STATUS scene (mode 5), then streams serial output so you
can watch the `[touch] rel t=.. path=.. net=..` line appear each time you lift
your finger.

Usage:
    .venv/bin/python scripts/touch_monitor.py [seconds]

Then, on the physical device, in STATUS mode:
  * single-tap a widget   -> expect: [touch] tap -> widget N   (it pulses)
  * double-tap a widget   -> expect: [touch] double-tap -> focus widget N
  * double-tap empty area -> expect: [touch] double-tap -> overview

The key line to watch is `[touch] rel t=<dur>ms path=<acc> net=<net>`.
A tap now qualifies when net < 30 (net = start->release travel). If you see the
line with a small net but NO follow-up `[touch] tap`/`palette` line, tell me the
numbers.
"""
import sys, time, glob, os, serial

BAUD = 115200

def find_port():
    cands = glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.SLAB*")
    if not cands:
        raise SystemExit("No /dev/cu.usbmodem* port found — is the board connected?")
    return max(cands, key=lambda p: os.path.getmtime(p))

def main():
    dur = float(sys.argv[1]) if len(sys.argv) > 1 else 30.0
    port = find_port()
    print(f"Using port {port}")

    # Open WITHOUT asserting DTR/RTS so we do NOT reset the running firmware.
    s = serial.Serial()
    s.port = port
    s.baudrate = BAUD
    s.timeout = 0.2
    s.dtr = False
    s.rts = False
    s.open()
    time.sleep(0.3)
    s.reset_input_buffer()

    # Navigate to STATUS (mode 5). Boot starts at mode 0; press 'm' up to 5x.
    print("Navigating to STATUS mode (sending 'm' x5)...")
    for _ in range(5):
        s.write(b"m")
        time.sleep(0.12)
    time.sleep(0.3)
    s.reset_input_buffer()

    print(f"\n=== STATUS mode. TAP / DOUBLE-TAP the screen. Monitoring {dur:.0f}s ===\n")
    t_end = time.time() + dur
    while time.time() < t_end:
        n = s.in_waiting
        if n:
            data = s.read(n)
            try:
                txt = data.decode("utf-8", "replace")
            except Exception:
                continue
            for line in txt.splitlines():
                line = line.rstrip()
                if not line:
                    continue
                # Highlight the lines that matter; keep the rest visible too.
                mark = ""
                if line.startswith("[touch]"):
                    mark = "  <== TOUCH"
                elif line.startswith("[mode]"):
                    mark = "  <== MODE"
                print(line + mark)
    s.close()
    print("\nDone.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
