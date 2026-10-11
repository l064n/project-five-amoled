#!/usr/bin/env python3
"""Capture one on-screen frame from the T-Display-S3-AMOLED over USB-serial.

Firmware protocol (triggered by sending 'F'):
    magic 'FRM1' | u16 W | u16 H  (header, 8 bytes, big-endian)
    followed by W*H RGB565 pixels, little-endian.

This script requests a frame, reads it, expands RGB565 -> RGB888, and writes
a PNG (pure Python, no third-party deps). Saves to captures/<name>.png
"""
import sys, time, zlib, struct, serial

PORT = "/dev/cu.usbmodem2101"
BAUD = 115200

def png_chunk(tag, data):
    c = struct.pack(">I", len(data)) + tag + data
    c += struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    return c

def write_png(path, w, h, rgb_bytes):
    raw = bytearray()
    stride = w * 3
    for y in range(h):
        raw.append(0)  # filter type 0 (None) per scanline
        raw += rgb_bytes[y*stride:(y+1)*stride]
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)  # 8-bit, color type 2 (RGB)
    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(b"IHDR", ihdr)
    png += png_chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += png_chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)

def rgb565_to_rgb888(buf):
    """buf: bytes of little-endian RGB565 (2 bytes/px). -> RGB888 bytes."""
    out = bytearray(len(buf) * 3 // 2)
    n = len(buf) // 2
    for i in range(n):
        v = buf[2*i] | (buf[2*i+1] << 8)
        r5 = (v >> 11) & 0x1F
        g6 = (v >> 5) & 0x3F
        b5 = v & 0x1F
        out[3*i]   = (r5 << 3) | (r5 >> 2)
        out[3*i+1] = (g6 << 2) | (g6 >> 4)
        out[3*i+2] = (b5 << 3) | (b5 >> 2)
    return bytes(out)

def main():
    name = sys.argv[1] if len(sys.argv) > 1 else "frame"
    out = f"captures/{name}.png"
    s = serial.Serial(PORT, BAUD, timeout=1)
    time.sleep(1.0)
    s.reset_input_buffer()

    print(f"Requesting frame -> {out}")
    t0 = time.time()
    s.write(b"F")
    time.sleep(0.15)

    # Read header (may have stray bytes; scan for magic).
    hdr = bytearray()
    while len(hdr) < 8:
        chunk = s.read(8 - len(hdr))
        if not chunk:
            time.sleep(0.05)
            continue
        hdr += chunk
        # drop leading garbage until magic
        idx = bytes(hdr).find(b"FRM1")
        if idx > 0:
            hdr = hdr[idx:]
        if idx == -1 and len(hdr) > 64:
            hdr = hdr[-4:]
    magic = hdr[:4]
    w = (hdr[4] << 8) | hdr[5]
    h = (hdr[6] << 8) | hdr[7]
    print(f"magic={magic!r}  {w}x{h}  header in {time.time()-t0:.1f}s")
    if magic != b"FRM1" or not (100 < w < 600 and 100 < h < 600):
        print("ERROR: bad frame header"); s.close(); return 1

    # Read pixel data.
    total = w * h * 2
    data = bytearray()
    while len(data) < total:
        data += s.read(total - len(data))
    dt = time.time() - t0
    print(f"read {len(data)}/{total} bytes in {dt:.1f}s ({len(data)/max(dt,1e-6)/1024:.0f} KB/s)")
    s.close()

    if len(data) < total:
        print("ERROR: incomplete frame"); return 1

    rgb = rgb565_to_rgb888(bytes(data[:total]))
    write_png(out, w, h, rgb)
    print(f"Wrote {out} ({w}x{h})")
    return 0

if __name__ == "__main__":
    sys.exit(main())
