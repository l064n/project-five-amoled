# Project Five — AMOLED

Firmware for the **LilyGO T-Display-S3-AMOLED**: an ESP32-S3 dev board with a
1.91" **RM67162** QSPI AMOLED (240×536 logical, 16 MB flash, 8 MB OPI PSRAM).

## Hardware

| Item | Spec |
| --- | --- |
| MCU | ESP32-S3 @ 240 MHz |
| Display | 1.91" AMOLED, RM67162 driver, QSPI, 240×536, 16-bit RGB565 |
| Flash | 16 MB (QIO) |
| PSRAM | 8 MB (OPI) |
| USB-C | Data + 5 V power, native USB-CDC serial |

## Toolchain

Built with [PlatformIO](https://platformio.org/) (Arduino framework, espressif32
platform). The custom board definition lives in [`boards/lilygo-t-amoled.json`](boards/lilygo-t-amoled.json)
(16 MB flash partition, OPI PSRAM, USB-CDC-on-boot). The RM67162 QSPI display
driver is vendored in [`lib/amoled`](lib/amoled) from LilyGO's official factory
example — it is proven on this exact hardware.

UI rendering uses **LVGL 8.3.x**.

## Project layout

```
├── boards/lilygo-t-amoled.json   # custom board (16MB, OPI PSRAM, USB CDC)
├── platformio.ini                # env config (espressif32 / arduino / LVGL)
├── lib/amoled/                   # vendored RM67162 QSPI AMOLED driver
│   ├── rm67162.h / rm67162.cpp
│   ├── pins_config.h             # pin map (SPI, buttons, LED)
│   └── library.json
└── src/main.cpp                  # LVGL starter demo
```

## Key pins (see `lib/amoled/pins_config.h`)

| Function | GPIO |
| --- | --- |
| QSPI CS | 6 |
| QSPI SCK | 47 |
| QSPI D0 (MOSI) | 18 |
| QSPI D1 | 7 |
| QSPI D2 | 48 |
| QSPI D3 | 5 |
| QSPI RST | 17 |
| TE | 9 |
| LED (non-touch) | 38 |
| Button 1 | 0 |
| Button 2 | 21 |

## Build & flash

```sh
# Install dependencies + build
pio run

# Build + flash to the board (auto-detects /dev/cu.usbmodem*)
pio run -t upload

# Open the serial monitor
pio device monitor
```

The first build downloads the ESP32 toolchain and LVGL — this can take a few
minutes. After that, builds are incremental and fast.

## The starter demo

[`src/main.cpp`](src/main.cpp) brings up the AMOLED and runs a small LVGL UI:

- A title label and a live clock.
- A color-swatch row.
- **Button 1** (GPIO0): short click cycles the background color; long press
  toggles the onboard LED (IO38).
- **Button 2** (GPIO21): resets display rotation.

Edit `src/main.cpp` to build your own UI. The LVGL flush callback
(`my_disp_flush`) bridges LVGL to the RM67162 driver.

## Serial monitor

`ARDUINO_USB_CDC_ON_BOOT=1` is set, so `Serial` maps to the native USB port —
connect the board over USB and no separate UART bridge is needed. Monitor at
115200 baud.

## References

- LilyGO wiki: <https://wiki.lilygo.cc/products/t-display-series/t-display-s3-amoled/>
- Official examples: <https://github.com/Xinyuan-LilyGO/T-Display-S3-AMOLED>
- RM67162 datasheet: in the official repo under `datasheet/`
