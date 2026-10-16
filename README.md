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
    ├── scene.h / scene.cpp       # neon scene (knots, tesseract, mobius, helix, vortex)
    ├── status_scene.cpp          # STATUS scene: 3D interactive WiFi + weather widgets
    ├── status_data.h / .cpp      # shared status block (loop core -> render core)
    ├── wifi_manager.h / .cpp     # WiFi STA + NVS creds + serial provisioning
    ├── weather_client.h / .cpp   # geolocation (ipapi.co) + weather (open-meteo)
    └── main.cpp                  # app: touch + button + serial + render task + HUD
```

## The neon 3D demo

[`src/main.cpp`](src/main.cpp) brings up the AMOLED + touch and renders a
self-contained 3D scene at 45+ fps on a dedicated FreeRTOS task. There are
**six toggleable scene modes** — KNOTS, TESSERACT, MOBIUS, HELIX, VORTEX, and
the live **STATUS** page (see below) — cycled by the button or `M`. The hero
**KNOTS** mode is:

- **Hero object** — a rotating (p=2, q=3) **torus knot** with a hue-cycling
  ribbon and a bright "comet" pulse traveling along its length.
- **Second knot** — a thinner, counter-rotating (p=1, q=2) knot that interlaces
  with the hero for depth and extra motion.
- **Floor** — a scrolling **synthwave perspective grid** with a traveling
  brightness wave (geometric perspective spacing).
- **Background** — a **parallax starfield** that twinkles.
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

### The STATUS mode (live WiFi + local weather)

There is a 6th scene mode — **STATUS** — a status dashboard driven by 3D
interactive graphics. It is a vertical stack of **five full-screen pages**, one
per metric. Only the focused page fills the screen; scrolling moves between
them with a smooth slide, while dragging still pivots the focused 3D widget:

| Page | What it shows |
| --- | --- |
| **0 · WiFi** | A large 3D mast with concentric signal arcs that light up with RSSI; a rotating sweep beam while connecting, red blink when down, `CONNECTED` / `CONNECTING` / `OFFLINE` state |
| **1 · RSSI** | Five large 3D signal bars; hue shifts red→green with strength, plus a live `dBm` readout |
| **2 · IP** | Four 3D octet cubes (bobbing) + a large address readout (`?` when none) |
| **3 · Weather** | A large rotating 3D wireframe globe with a big temperature readout, WMO condition label, wind + city, and a large weather icon (sun / sun+cloud / cloud / fog / rain / snow / lightning) |
| **4 · Uptime** | A large uptime-seconds readout, a circular progress ring (fills over the hour) + FPS |

Toggle to it with the **button** (short press) or **`M`** over serial.

#### Navigating (scroll + pivot)

- **Drag up / down** — scroll between the full-screen pages (they slide in and
  out; a quick flick flings to the next page and snaps).
- **Drag left / right** — pivot (yaw) the focused 3D widget in place, with
  momentum on release.
- **Single-tap** — fire the current page's pulse.
- **Double-tap** — cycle the neon **color palette**.
- **`+` / `-`** over serial — next / previous page; **`G`** — jump to the next
  page (wraps); **`0`** — jump back to page 0 (WiFi).

In the other modes a drag still rotates the whole scene and a tap cycles the
palette.

#### Local weather (no API key)

While connected to WiFi, a low-priority background task resolves the board's
rough location from its own public IP and fetches current conditions — both
free, no API key:

1. `GET https://ipapi.co/json/` → latitude / longitude / city
2. `GET https://api.open-meteo.com/v1/forecast?...&current_weather=true` →
   temperature, WMO weather code, wind

Results feed the weather globe and the `W` serial dump. It re-fetches every
**30 min**; send **`L`** to force an immediate refresh.

> **TLS note:** connections use `WiFiClientSecure` with certificate
> verification. If a handshake ever fails (e.g. a missing/rotated CA on a
> constrained build) the firmware logs a clear warning and retries **once**
> insecure. This is a documented, intentional trade-off for a personal
> dashboard — the payload is non-sensitive and nothing user-specific is sent.

#### Provisioning WiFi (credentials stay off the repo)

No SSID/password is committed. Credentials are stored in **NVS** (namespace
`wifi`) and set at runtime over the USB-serial monitor.

**To connect, type exactly this one line** (replace the values, keep the single
space between SSID and password) and press Enter:

```
N YourWiFiName YourWiFiPassword
```

Then wait a few seconds and type `W` to see the state, IP, RSSI, location, and
weather. That's it — the credentials survive reboot.

**Or use the helper script** (it waits for the board to finish booting — opening
the port resets it — then provisions and polls until connected):

```
.venv/bin/python scripts/wifi_connect.py "YourWiFiName" "YourWiFiPassword"
.venv/bin/python scripts/wifi_connect.py            # status only
.venv/bin/python scripts/wifi_connect.py forget     # erase stored creds
```

Other WiFi serial commands:

```
W   print WiFi state, IP, RSSI + location + weather
X   forget credentials + disconnect
L   force an immediate weather/location re-fetch
```

Auto-reconnect with capped backoff runs automatically if the link drops.

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
- On this panel the raw frame is **rotated ~90°** relative to the screen
  (raw-X drives screen-Y and raw-Y drives screen-X, with one axis mirrored) —
  the affine fit absorbs that, which is why the fitted `map x=` line can show
  negative coefficients. That's expected, not an error.
- **Helper:** `.venv/bin/python -u scripts/calibrate_touch.py` opens the port
  (no reset), starts calibration, and walks you through the 4 corners. When
  done it prints the fitted map and **round-trips each corner** — each
  `raw=(..) -> (..)` should land near its target `(0,0) / (239,0) / (0,535) /
  (239,535)`. If screen-x stays ~0 across corners, the fit is degenerate.

## The software 3D engine

[`src/engine3d.h`](src/engine3d.h) / [`src/engine3d.cpp`](src/engine3d.cpp) —
a tiny, dependency-free renderer:

- **Framebuffer** — one full 240×536 RGB565 frame (~257 KB) allocated in
  **internal DRAM** (with a PSRAM fallback if no contiguous block fits).
  Internal RAM is far faster than PSRAM for the per-frame clear + QSPI blit —
  this single change took the demo from ~40 to ~60 fps.
- **Math** — `V3` rotation about X/Y/Z (with a `makeRot`/`rotateMat` fast path
  that precomputes the rotation once per object), pinhole (perspective)
  projection with a near-plane cull.
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
| `M` / `m` | Next scene mode (same as the button short-press) |
| `S` / `s` | Cycle animation speed (same as the button long-press) |
| `C` / `c` | (Re)start the 4-corner touch calibration |
| `R` / `r` | Debug: print the current **raw** touch reading (`[raw] x= y= z=`). Hold your finger on the screen and send `R` to see live values. |
| `W` / `w` | Print WiFi state, IP, RSSI + resolved location + current weather |
| `N <ssid> <pass>` | Store WiFi credentials in NVS + connect (runtime provisioning) |
| `X` / `x` | Forget stored WiFi credentials + disconnect |
| `L` / `l` | Force an immediate location + weather re-fetch |
| `+` / `=` | **STATUS:** scroll to the next page |
| `-` | **STATUS:** scroll to the previous page |
| `0` | **STATUS:** jump back to page 0 (WiFi) |
| `G` / `g` | **STATUS:** jump to the next page (wraps 0 → 1 → … → 4 → 0) |

The demo also prints a performance line to serial every second while running:

```
[perf] fps=47 scene=13ms blit=6ms
[sec]  stars=0 grid=9 knot1=1 knot2=0 clr=0 ms
```

`fps` is the measured frame rate; `scene`/`blit` split the frame time into
software rendering vs the QSPI push to the panel; `[sec]` further splits the
scene time across starfield / grid / hero knot / second knot / clear. Use this
to tune geometry density against your target frame rate.

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

### Scene modes

| Mode | Capture |
| --- | --- |
| KNOTS | `captures/amoled_knots.png` |
| TESSERACT | `captures/amoled_tesseract.png` |
| MOBIUS | `captures/amoled_mobius.png` |
| HELIX | `captures/amoled_helix.png` |
| VORTEX | `captures/amoled_vortex.png` |
| **STATUS** (overview) | `captures/amoled_status_overview.png` |
| **STATUS** — focused + zoomed (WiFi tower) | `captures/amoled_status_focus_tower.png` |
| **STATUS** — focused + zoomed (weather globe) | `captures/amoled_status_focus_globe.png` |

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

**Touch diagnostic** — `scripts/touch_monitor.py` opens the port *without*
resetting the board, jumps to the STATUS scene, and streams every touch event.
Each finger-up prints a line like
`[touch] rel t=<dur>ms path=<accum> net=<start→release travel>` followed by the
action (`[touch] tap -> pulse page N` in STATUS mode, `double-tap -> palette ...`,
or `tap -> palette ...` in the other modes). A tap counts when `net < 30`
(net displacement, not accumulated path — the CST816 jitters its repeated point,
and its total variation was previously tripping the old threshold).

```
.venv/bin/python -u scripts/touch_monitor.py 180   # 3-minute window
```

## References

- LilyGO wiki: <https://wiki.lilygo.cc/products/t-display-series/t-display-s3-amoled/>
- Official AMOLED-series library (touch + display reference):
  <https://github.com/Xinyuan-LilyGO/LilyGo-AMOLED-Series>
- CST816 register datasheet: in the official repo under `datasheet/`
