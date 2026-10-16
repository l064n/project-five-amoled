#!/usr/bin/env python3
"""Connect the T-Display-S3-AMOLED to WiFi — one command.

Stores the credentials in the board's NVS and makes it connect. The
credentials stay on the device (never written to the repo).

Usage:
    .venv/bin/python scripts/wifi_connect.py "<SSID>" "<PASSWORD>"

It opens the port WITHOUT resetting the board, sends the firmware's
`N <ssid> <pass>` provisioning command, waits a few seconds, then prints the
live WiFi status (state + IP + RSSI) so you can confirm it connected.

To forget stored credentials later:
    .venv/bin/python scripts/wifi_connect.py
"""
import sys, time, glob, os, serial

BAUD = 115200

def find_port():
    cands = glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.SLAB*")
    if not cands:
        raise SystemExit("No /dev/cu.usbmodem* port found — is the board connected?")
    return max(cands, key=lambda p: os.path.getmtime(p))

def drain(s, secs):
    end = time.time() + secs
    buf = b""
    while time.time() < end:
        if s.in_waiting:
            try:
                buf += s.read(s.in_waiting)
            except serial.SerialException:
                pass
    return buf.decode("utf-8", "replace")

def main():
    ssid = sys.argv[1] if len(sys.argv) > 1 else ""
    pwd  = sys.argv[2] if len(sys.argv) > 2 else ""

    if not ssid:
        # No args -> just print current status (send 'W').
        port = find_port()
        print(f"Using port {port}", flush=True)
        s = serial.Serial()
        s.port = port; s.baudrate = BAUD; s.timeout = 0.2
        s.dtr = False; s.rts = False
        s.open()
        time.sleep(0.3)
        s.reset_input_buffer()
        s.write(b"W")
        print(drain(s, 3), flush=True)
        s.close()
        return 0

    if not pwd:
        raise SystemExit('Usage: wifi_connect.py "<SSID>" "<PASSWORD>"')

    port = find_port()
    print(f"Using port {port}", flush=True)
    # dtr/rts low -> no USB-CDC reset, so the running board keeps its state.
    s = serial.Serial()
    s.port = port; s.baudrate = BAUD; s.timeout = 0.2
    s.dtr = False; s.rts = False
    s.open()
    time.sleep(0.3)
    s.reset_input_buffer()

    print(f"Provisioning SSID={ssid!r} ...", flush=True)
    s.write(("N %s %s\n" % (ssid, pwd)).encode())
    # Give the WiFi stack time to associate + DHCP, with visible progress.
    for i in range(5):
        time.sleep(1)
        print(f"  waiting for association + DHCP... {i+1}/5s", flush=True)
    s.reset_input_buffer()

    # Dump live status.
    print("Querying status ('W')...", flush=True)
    s.write(b"W")
    out = drain(s, 2.5)
    print(out if out.strip() else "  (no status line — board may not be responding; try: ls /dev/cu.usbmodem*)", flush=True)
    s.close()
    print("\nDone. Credentials saved to the board — it will auto-reconnect on boot.", flush=True)
    return 0

if __name__ == "__main__":
    sys.exit(main())
