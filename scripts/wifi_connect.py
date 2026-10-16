#!/usr/bin/env python3
"""Connect the T-Display-S3-AMOLED to WiFi — one command.

Stores the credentials in the board's NVS and makes it connect. The
credentials stay on the device (never written to the repo).

Usage:
    .venv/bin/python scripts/wifi_connect.py "<SSID>" "<PASSWORD>"

IMPORTANT: opening the serial port RESETS the board (USB_UART_CHIP_RESET),
and the board needs ~2 s to boot before its USB CDC input is ready. So this
script always waits for the boot banner / first perf line BEFORE sending
anything, then streams the board's responses as they arrive and polls `W`
until the link is CONNECTED.

To forget stored credentials later:
    .venv/bin/python scripts/wifi_connect.py   (no args: just print status)
    .venv/bin/python scripts/wifi_connect.py forget
"""
import sys, time, glob, os, serial

BAUD = 115200
BOOT_TIMEOUT = 10.0      # max seconds to wait for the board to finish booting
CONN_TIMEOUT = 40.0      # max seconds to wait for WiFi association + DHCP


def find_port():
    cands = glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.SLAB*")
    if not cands:
        raise SystemExit("No /dev/cu.usbmodem* port found — is the board connected?")
    return max(cands, key=lambda p: os.path.getmtime(p))


class Board:
    """Serial link that knows how to wait for a (re)boot before commanding."""

    def __init__(self, port):
        print(f"Using port {port}", flush=True)
        s = serial.Serial()
        s.port = port
        s.baudrate = BAUD
        s.timeout = 0.2
        s.dtr = False
        s.rts = False
        s.open()
        self.s = s
        self.buf = ""

    def drain(self, secs, show=True):
        """Read whatever arrives for `secs`; print it live if show=True.
        Returns the full text received (complete + partial lines)."""
        end = time.time() + secs
        got = ""
        while time.time() < end:
            if self.s.in_waiting:
                self.buf += self.s.read(self.s.in_waiting).decode("utf-8", "replace")
                while "\n" in self.buf:
                    line, self.buf = self.buf.split("\n", 1)
                    got += line + "\n"
                    if show and line.strip():
                        print("  " + line, flush=True)
        got += self.buf
        self.buf = ""
        return got

    def read_until(self, predicate, timeout):
        """Read lines until `predicate(line)` is True or `timeout` elapses.
        Returns True if the predicate fired."""
        end = time.time() + timeout
        while time.time() < end:
            if self.s.in_waiting:
                self.buf += self.s.read(self.s.in_waiting).decode("utf-8", "replace")
                while "\n" in self.buf:
                    line, self.buf = self.buf.split("\n", 1)
                    if line.strip():
                        print("  " + line, flush=True)
                    if predicate(line):
                        return True
            else:
                time.sleep(0.05)
        return False

    def wait_for_boot(self):
        """The port open resets the board; wait for it to be ready for input."""
        print("Waiting for board boot (port open resets it)...", flush=True)
        ok = self.read_until(
            lambda l: "weather task started" in l or "[perf]" in l,
            BOOT_TIMEOUT)
        if not ok:
            raise SystemExit("Board did not finish booting — is it connected?")
        # A small settle so the CDC RX buffer is definitely live.
        time.sleep(0.3)

    def send(self, data):
        self.buf = ""
        self.s.reset_input_buffer()
        self.s.write(data.encode() if isinstance(data, str) else data)

    def status_line(self):
        """Send 'W' and return the combined wifi+weather status text."""
        self.send("W")
        got = self.drain(2.5, show=False)
        lines = [l for l in got.split("\n") if l.startswith("[status]")]
        return "\n".join(lines) if lines else got.strip()

    def close(self):
        self.s.close()


def main():
    port = find_port()
    arg = sys.argv[1] if len(sys.argv) > 1 else ""

    b = Board(port)
    b.wait_for_boot()

    try:
        if arg.lower() == "forget":
            print("Forgetting stored credentials ('X')...", flush=True)
            b.send("X")
            b.drain(2.0)
            print("Status:", flush=True)
            print(b.status_line(), flush=True)
            print("Done. The board will not try to reconnect until new creds are stored.",
                  flush=True)
            return 0

        if not arg:
            # No args -> just print current status.
            print("Status:", flush=True)
            print(b.status_line(), flush=True)
            return 0

        pwd = sys.argv[2] if len(sys.argv) > 2 else ""
        if not pwd:
            raise SystemExit('Usage: wifi_connect.py "<SSID>" "<PASSWORD>"')

        print(f"Provisioning SSID={arg!r} ('N <ssid> <pass>')...", flush=True)
        b.send(f"N {arg} {pwd}\n")

        # Stream the board's reactions while WiFi associates + DHCP runs.
        print("Waiting for association + DHCP (responses below)...", flush=True)
        connected = b.read_until(lambda l: "[wifi] connected" in l, CONN_TIMEOUT)

        final = b.status_line()
        print("Final status:", flush=True)
        print(final, flush=True)

        if connected or "state=CONNECTED" in final:
            print("\nDone. Credentials saved to the board — it will auto-reconnect on boot.",
                  flush=True)
            return 0
        print("\nNot connected yet — the board retries automatically (backoff up to 15 s).",
                  flush=True)
        print("Re-run this script with no arguments to re-check the status.", flush=True)
        return 1
    finally:
        b.close()


if __name__ == "__main__":
    sys.exit(main())
