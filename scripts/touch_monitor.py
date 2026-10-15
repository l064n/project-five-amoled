#!/usr/bin/env python3
"""Live touch diagnostic for the T-Display-S3-AMOLED.

Opens the USB-serial port WITHOUT resetting the board (dtr/rts held low before
open), waits for the boot to finish, navigates to the STATUS scene (mode 5),
then streams serial output so you can watch the `[touch] rel ...` line appear
each time you lift your finger.

Usage:
    .venv/bin/python -u scripts/touch_monitor.py [seconds]

Then, on the physical device, in STATUS mode:
  * single-tap a widget   -> expect: [touch] tap -> widget N   (it pulses)
  * double-tap a widget   -> expect: [touch] double-tap -> focus widget N
  * double-tap empty area -> expect: [touch] double-tap -> overview

The key line is `[touch] rel t=<dur>ms path=<acc> net=<net>`. A tap qualifies
when dur < 420 and net < 30. If you see the `rel` line but NO follow-up
`[touch] tap`/`palette` line, tell me the numbers.
"""
import sys, time, glob, os, serial

BAUD = 115200

def find_port():
    cands = glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.SLAB*")
    if not cands:
        raise SystemExit("No /dev/cu.usbmodem* port found — is the board connected?")
    return max(cands, key=lambda p: os.path.getmtime(p))

def main():
    dur = float(sys.argv[1]) if len(sys.argv) > 1 else 60.0
    port = find_port()
    print(f"Using port {port}", flush=True)

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

    # Wait for the board to be up before navigating. We consider it booted once
    # we see the status-task line (printed ~1.5s after power-on).
    def safe_readline():
        """readline that tolerates pyserial's transient 'readiness but no data'."""
        try:
            return s.readline()
        except serial.SerialException:
            return b""

    print("Waiting for boot...", flush=True)
    t_boot = time.time()
    booted = False
    while time.time() - t_boot < 15:
        if s.in_waiting:
            line = safe_readline().decode("utf-8", "replace").rstrip()
            if line:
                print("  " + line, flush=True)
            if "weather task started" in line or "no credentials" in line:
                booted = True
                break
    s.reset_input_buffer()
    if not booted:
        print("  (no boot marker in 15s — proceeding anyway)", flush=True)

    # Navigate to STATUS (mode 5). Boot starts at mode 0; send 'm' up to 5x.
    print("Navigating to STATUS mode (sending 'm' x5)...", flush=True)
    for _ in range(5):
        s.write(b"m")
        time.sleep(0.2)
    time.sleep(0.5)
    s.reset_input_buffer()

    print(f"\n=== STATUS mode. TAP / DOUBLE-TAP the screen. {dur:.0f}s ===\n", flush=True)
    t_end = time.time() + dur
    while time.time() < t_end:
        n = s.in_waiting
        if n:
            try:
                data = s.read(n)
            except serial.SerialException:
                continue
            if not data:
                continue
            txt = data.decode("utf-8", "replace")
            for line in txt.splitlines():
                line = line.rstrip()
                if not line:
                    continue
                mark = ""
                if line.startswith("[touch]"):
                    mark = "   <== TOUCH"
                elif line.startswith("[mode]"):
                    mark = "   <== MODE"
                print(line + mark, flush=True)
    s.close()
    print("\nDone.", flush=True)
    return 0

if __name__ == "__main__":
    sys.exit(main())
