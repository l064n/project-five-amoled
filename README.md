# Project Five — AMOLED

Firmware for the **LilyGO T-Display-S3-AMOLED (Touch)**: an ESP32-S3 dev board
with a 1.91" **RM67162** QSPI AMOLED (240×536) and a **CST816** capacitive
touch screen.

The current build is an **interactive neon 3D demo** rendered by a
self-contained software 3D engine — no external graphics libraries.

## Hardware

| Item | Spec |
| --- | --- |
| MCU | ESP32-S3 @ 240 MHz |
| Display | 1.91" AMOLED, RM67162 driver, QSPI, 240×536, 16-bit RGB565 |
| Touch | CST816 capacitive, I²C (SDA=GPIO3, SCL=GPIO2), addr `0x15` |
| Flash | 16 MB (QIO) |
| PSRAM | 8 MB (OPI) |
| USB-C | Data + 5 V power, native USB-CDC serial |

## Toolchain

Built with [PlatformIO](https://platformio.org/) (Arduino framework,
espressif32 platform). The custom board definition lives in
[`boards/lilygo-t-amoled.json`](boards/lilygo-t-amoled.json) (16 MB flash
partition, OPI PSRAM, USB-CDC-on-boot). The display and touch drivers are
vendored in [`lib/amoled`](lib/amoled).

## Project layout

```
├── boards/lilygo-t-amoled.json   # custom board (16MB, OPI PSRAM, USB CDC)
├── platformio.ini                # env config (espressif32 / arduino)
├── lib/amoled/                   # vendored drivers
│   ├── rm67162.h / rm67162.cpp   # RM67162 QSPI AMOLED display driver
│   ├── touch_cst816.h/.cpp       # CST816 capacitive touch driver
│   ├── pins_config.h             # pin map (SPI, buttons, LED)
│   └── library.json
└── src/
    ├── engine3d.h / engine3d.cpp # software 3D engine
    ├── scene.h / scene.cpp       # neon scene (torus knot, grid, stars)
    └── main.cpp                  # app: touch input + render task + HUD
```

## The neon 3D demo

[`src/main.cpp`](src/main.cpp) brings up the AMOLED + touch and renders a
self-contained 3D scene at ~60 Hz on a dedicated FreeRTOS task:

- **Hero object** — a rotating (p=2, q=3) **torus knot**.
- **Floor** — a scrolling **synthwave perspective grid**.
- **Background** — a **parallax starfield**.
- **Glow** — lines and points use **additive blending** on a pure-black
  background, which is exactly what makes an AMOLED "pop" (true blacks +
  light-adding neon).

### Interactions (CST816 touch)

| Gesture | Effect |
| --- | --- |
| **Drag** | Rotate the whole scene (camera yaw + pitch), with **momentum/inertia** on release |
| **Tap** (quick, no drag) | Cycle the neon **color palette** (SUNSET / CYBER / LIME / SOLAR) |

A small FPS readout (bottom-right) and a palette indicator (top-center) are
drawn as an overlay each frame.

### Touch calibration

The CST816 reports points in its **own raw frame**, which does not map 1:1 to
the portrait 240×536 screen (the 1.91" glass is a 550×480 panel mounted at an
angle, so its active region is a sub-window that must be scaled and mirrored).
On the very first run the firmware **automatically starts a 4-corner
calibration**: tap the four highlighted screen corners in order (top-left →
top-right → bottom-left → bottom-right). The raw values are fit with a
least-squares **affine transform** and saved to **NVS** (`Preferences`,
namespace `touch`), so it only needs to be done once.

- Drag/tap are disabled while the calibration overlay is showing.
- Send `C` over serial at any time to re-run calibration (e.g. after moving
  the panel or if taps drift).
- Send `C` twice in quick succession is not needed — a single `C` restarts it.

## The software 3D engine

[`src/engine3d.h`](src/engine3d.h) / [`src/engine3d.cpp`](src/engine3d.cpp) —
a tiny, dependency-free renderer:

- **Framebuffer** — one full 240×536 RGB565 frame in PSRAM (~254 KB).
- **Math** — `V3` rotation about X/Y/Z, pinhole (perspective) projection with
  a near-plane cull.
- **Primitives** — `drawNeonLine` / `drawNeonDot` using a thickened Bresenham
  with an additive halo + bright core, so overlaps literally add light.
- **Camera-space rendering** — `renderSegment` / `renderPoint` rotate, move to
  camera space, project, and depth-fog automatically (farther = dimmer).
- **Color** — `rgb565` and `hsl565` helpers.

The scene is immediate-mode: `clear` → draw stars → draw grid → draw knot →
draw HUD → blit the whole frame to the panel via `lcd_PushColors`.

## Serial commands

Send these characters over the USB-serial monitor to control the running demo:

| Char | Effect |
| --- | --- |
| `F` / `f` | Freeze the frame and **stream the framebuffer over serial** (for screenshot capture) |
| `P` / `p` | Advance to the next color palette |
| `C` / `c` | (Re)start the 4-corner touch calibration |
| `R` / `r` | Debug: print the current **raw** touch reading (`[raw] x= y= z=`). Hold your finger on the screen and send `R` to see live values. |

## Screenshots

`scripts/capture_frame.py` triggers a frame dump over serial and saves it as a
PNG (pure Python, no dependencies). The PNGs in [`captures/`](captures) are
captured directly from the on-screen framebuffer.

```sh
# needs pyserial on the python you run it with (PlatformIO's venv has it)
python scripts/capture_frame.py my_shot      # -> captures/my_shot.png
python scripts/capture_frame.py cyber        # after sending 'P' to switch palette
```

| Palette | Capture |
| --- | --- |
| SUNSET (magenta) | `captures/amoled_neon_3d.png` |
| CYBER (cyan) | `captures/amoled_neon_cyber.png` |
| LIME (green) | `captures/amoled_neon_lime.png` |
| SOLAR (orange) | `captures/amoled_neon_solar.png` |

## Key pins (see `lib/amoled/pins_config.h` + `touch_cst816.h`)

| Function | GPIO |
| --- | --- |
| QSPI CS / SCK / D0 / D1 / D2 / D3 / RST | 6 / 47 / 18 / 7 / 48 / 5 / 17 |
| Touch SDA / SCL (I²C) | 3 / 2 |
| LED (non-touch) | 38 |
| Button 1 / Button 2 | 0 / 21 |

## Build & flash

```sh
pio run              # build
pio run -t upload    # build + flash (auto-detects /dev/cu.usbmodem*)
pio device monitor   # serial monitor at 115200
```

The first build downloads the ESP32 toolchain; after that builds are fast.

> **Note on touch:** the demo degrades gracefully — if no CST816 is detected
> at boot it logs `[touch] no CST816 detected` and the scene still animates
> (auto-spin + inertia); only the drag/tap input is disabled.

## Serial monitor

`ARDUINO_USB_CDC_ON_BOOT=1` is set, so `Serial` maps to the native USB port —
connect over USB, no UART bridge needed. Monitor at 115200 baud. Boot prints a
banner plus the touch status.

## References

- LilyGO wiki: <https://wiki.lilygo.cc/products/t-display-series/t-display-s3-amoled/>
- Official AMOLED-series library (touch + display reference):
  <https://github.com/Xinyuan-LilyGO/LilyGo-AMOLED-Series>
- CST816 register datasheet: in the official repo under `datasheet/`
