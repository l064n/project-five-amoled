#!/usr/bin/env python3
"""One-time 4-corner touch calibration for the T-Display-S3-AMOLED.

WHY: the CST816 reports points in its own glass frame, which does NOT match the
portrait 240x536 screen. Until you calibrate, `toScreen()` passes raw coords
through unchanged, so DRAG (relative deltas) works but TAP hit-testing (absolute
screen coords) lands in the wrong place and misses every widget. This script
starts the on-screen calibration and walks you through the 4 corners.

Usage:
    .venv/bin/python -u scripts/calibrate_touch.py

Then on the device, tap the bright target in order:
    top-left -> top-right -> bottom-left -> bottom-right
The script streams the serial log; you should see one
`[calib] corner N raw=(x,y)` line per tap and finally `[calib] done`.
After that, restart is NOT needed — the map is live and saved to NVS.
"""
import sys, time, glob, os, serial

BAUD = 115200

def find_port():
    cands = glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.SLAB*")
    if not cands:
        raise SystemExit("No /dev/cu.usbmodem* port found — is the board connected?")
    return max(cands, key=lambda p: os.path.getmtime(p))

def main():
    port = find_port()
    print(f"Using port {port}", flush=True)

    s = serial.Serial()
    s.port = port
    s.baudrate = BAUD
    s.timeout = 0.2
    s.dtr = False
    s.rts = False
    s.open()
    time.sleep(0.3)
    s.reset_input_buffer()

    print("Sending 'c' to start 4-corner calibration...", flush=True)
    s.write(b"c")

    order = ["TOP-LEFT", "TOP-RIGHT", "BOTTOM-LEFT", "BOTTOM-RIGHT"]
    print(flush=True)
    print("=== TAP THE BRIGHT TARGET IN THIS ORDER ===", flush=True)
    for i, name in enumerate(order, 1):
        print(f"  {i}. {name}", flush=True)
    print("Watch for '[calib] corner N' after each tap.\n", flush=True)

    t_end = time.time() + 60
    done = False
    while time.time() < t_end and not done:
        if s.in_waiting:
            try:
                data = s.read(s.in_waiting)
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
                if "[calib]" in line:
                    mark = "   <== CALIB"
                print(line + mark, flush=True)
                if "calib] done" in line:
                    done = True
    s.close()
    if done:
        print("\nCalibration saved. Taps should now hit the right widgets.", flush=True)
    else:
        print("\nTimed out before 'done' — re-run and tap the 4 corners.", flush=True)
    return 0

if __name__ == "__main__":
    sys.exit(main())
