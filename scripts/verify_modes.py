#!/usr/bin/env python
# Cycle through all 5 scenes via 'M', report each one's FPS, and save a PNG
# per mode. Uses the same FRM1 serial protocol as capture_frame.py.
import importlib.util, os, struct, sys, time

import serial

PORT = os.environ.get("T_AMOLED_PORT", "/dev/cu.usbmodem101")
spec = importlib.util.spec_from_file_location("cf", "scripts/capture_frame.py")
cf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cf)

NAMES = ["KNOTS", "TESSERACT", "MOBIUS", "HELIX", "VORTEX"]

s = serial.Serial(PORT, 115200, timeout=1)
time.sleep(1)
s.reset_input_buffer()

def read_lines(sect):
    buf = b""
    end = time.time() + sect
    while time.time() < end:
        buf += s.read(2048)
    out = []
    for line in buf.decode(errors="replace").splitlines():
        if "[perf]" in line or "[sec]" in line:
            out.append(line.strip())
    return out

def grab_frame():
    """Send 'F', then read the FRM1 frame. Returns (W,H,fb_bytes) or None."""
    s.reset_input_buffer()
    s.write(b"F")
    data = b""
    # wait for the header
    end = time.time() + 3
    while data[:4] != b"FRM1" and time.time() < end:
        data += s.read(4096)
    if data[:4] != b"FRM1":
        return None
    W = data[4] | (data[5] << 8)
    H = data[6] | (data[7] << 8)
    need = W * H * 2
    data = data + s.read(0)
    got = len(data) - 8
    end = time.time() + 20
    while got < need and time.time() < end:
        d = s.read(need - got)
        if not d:
            continue
        data += d
        got += len(d)
    return W, H, data[8:8 + need]

def current_mode():
    for line in read_lines(1.2):
        if "[sec]" in line and "mode=" in line:
            try:
                return int(line.split("mode=")[1].split()[0])
            except (ValueError, IndexError):
                pass
    return None

# Sync to KNOTS (mode 0) before starting so labels are correct.
cm = current_mode()
if cm is not None and cm != 0:
    for _ in range((0 - cm) % 5):
        s.write(b"M")
        time.sleep(0.6)
    time.sleep(1.0)

for i, nm in enumerate(NAMES):
    if i > 0:
        s.write(b"M")
        time.sleep(1.5)
    perf = read_lines(2.5)
    fps_line = [p for p in perf if "[perf]" in p]
    sec_line = [p for p in perf if "[sec]" in p]
    print("==", nm, "==")
    if fps_line:
        print("   ", fps_line[-1])
    if sec_line:
        print("   ", sec_line[-1])
    frame = grab_frame()
    if frame:
        W, H, fb = frame
        rgb = cf.rgb565_to_rgb888(fb)
        path = f"captures/amoled_{nm.lower()}.png"
        cf.write_png(path, W, H, rgb)
        print("    saved", path)
    else:
        print("    (no frame captured)")

s.close()
print("done")
