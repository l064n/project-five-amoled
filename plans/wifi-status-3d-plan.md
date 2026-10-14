# Plan — WiFi status page as a 3D interactive scene mode (with live local weather)

Goal: connect the T-Display-S3-AMOLED to WiFi (credentials stored in NVS,
provisioned at runtime, no secrets in git) and add a new toggleable scene mode
("STATUS") that shows a status page driven by cool 3D interactive graphics —
each status is its own 3D interactive visual. One widget shows **local
weather**: the board uses its WiFi connection to get a rough location (from
its own public IP) and fetches current weather. Test on the board, commit each
feature to GitHub, keep all docs + code in the repo.

## Architecture

- `src/wifi_manager.h/.cpp` — non-blocking STA WiFi state machine.
  * States: IDLE -> CONNECTING -> GOT_IP -> CONNECTED, plus FAILED (auto-retry
    with backoff). Never blocks; `update()` is called from loop() and returns
    fast. Auto-reconnect on link loss via `WiFi.onEvent`.
  * Credentials: NVS namespace `wifi`, keys `ssid` (<=32), `pass` (<=64).
    Runtime provisioning over serial (see commands below). No `secrets.h`, no
    hardcoded creds. (The WIFI_SSID/PASSWORD placeholders in pins_config.h
    are left unused, marked deprecated with a comment.)

- `src/weather_client.h/.cpp` — its own low-priority FreeRTOS task (core 1,
  priority below the render task):
  * Runs only while WiFi is CONNECTED. On (re)connect it fetches, then
    refreshes every 30 min; `forceRefresh()` from serial `L`.
  * Step 1: HTTPS GET `https://ipapi.co/json/` (free, no API key; the board's
    pins_config.h already references ipapi.co for timezone) → lat/lon/city.
  * Step 2: HTTPS GET `https://api.open-meteo.com/v1/forecast?latitude=..&
    longitude=..&current_weather=true` (free, no API key) → temperature,
    weathercode (WMO), windspeed.
  * Transport: `WiFiClientSecure` (ships with the Arduino-ESP32 core) with the
    default CA bundle. If the TLS handshake fails, log a loud warning and retry
    once with `setInsecure(true)` — documented trade-off for a personal
    dashboard (no sensitive data in the payload).
  * ~8 KB task stack; TLS buffers land in PSRAM on this board. Idle 30-60 s
    between cycles so it never starves the render task.
  * Writes results into the shared `StatusData` under a mutex.

- Shared `StatusData` (brief `PortMutex`/`Mutex` around read/write — needed
  because it holds a city string; cost is negligible at 60 fps):
  ```
  enum state (u32): IDLE/CONNECTING/GOT_IP/CONNECTED/FAILED
  int32  rssi;      uint32 ip;         uint32 uptime_s;  uint32 fps;
  uint32 rxBytes;   uint32 txBytes;
  bool   w_valid;   int32 temp_x10;     // °C * 10
  uint32 wcode;     int32 wind_x10;     // km/h * 10
  char   city[16];  float lat, lon;
  ```

- Scene: add mode 5 "STATUS" (NUM_MODES 5→6). `drawStatus(rx, ry)` renders a
  constellation of 3D widgets at different z-depths, all reactive to
  `StatusData` and animated by the existing `spinT` clock:
  1. **WiFi tower** — 3D antenna: vertical mast + concentric arcs (3 rings)
     that light up proportional to RSSI; a rotating sweep beam while
     CONNECTING; red/blinking state when FAILED/no creds.
  2. **RSSI bars** — 5 3D vertical bars (box edges) at varying z; heights
     encode rssi buckets; hue shifts with strength.
  3. **IP display** — 4 3D cubes/octet rings in a row (one per octet) + a 2D
     3x5 digit label under each (reuse kDigit font) showing the actual IP;
     "?" when no IP.
  4. **Weather globe** — a slowly rotating 3D wireframe globe (meridian +
     latitude rings) with the current weather icon ORBITING it on a visible
     orbit ring, per WMO weathercode:
     * 0 (clear): 3D sun — glowing core + 8 rotating spikes, pulsing.
     * 1-2 (partly cloudy): sun + 3D cloud (cluster of overlapping glowing
       spheres) drifting across.
     * 3 / 45,48 (overcast/fog): gray 3D cloud, slow drift + fog shimmer.
     * 51-67, 80-82 (rain/drizzle): cloud + animated falling streaks.
     * 71-77, 85-86 (snow): cloud + rotating falling snowflakes (6-arm stars).
     * 95-99 (thunderstorm): cloud + flashing 3D lightning-bolt polyline
       (jagged segments, re-baked per flash).
     Temperature readout (e.g. "-5.2°") under the globe with an extended 3x5
     font (digits + '.' + '-' + '°'), city name (first ~10 chars, uppercase)
     as a second line. "CONNECTING…/NO DATA" state when w_valid is false.
  5. **Uptime ring** — a 3D ring that fills as uptime grows (mod 1 h), + FPS
     readout digits nearby.
  Interactivity: global drag orbits the whole constellation (existing rx/ry);
  a tap maps to the nearest widget by projected 2D center and triggers a
  per-widget "pulse" (scale + glow bump that decays) — each icon is a
  genuinely interactive 3D visual.

- main.cpp: instantiate WiFiManager + WeatherClient, poll in loop(), feed
  StatusData, new serial commands, keep the render task structure unchanged
  (it takes the mutex briefly once per frame in STATUS mode).

## Serial commands (add to processSerialCommands)

* `W` — print WiFi status + stored SSID (masked) + stats + location
  (lat/lon/city) + last weather fetch result
* `N <ssid> <pass>` — store creds in NVS + (re)connect (also supports
  interactive two-prompt form if sent bare)
* `X` — forget credentials (erase NVS, disconnect)
* `L` — force location + weather re-fetch now (prints the result)
* (existing F/P/C/R/M/S unchanged)

## Files to touch

* NEW `src/wifi_manager.h` / `src/wifi_manager.cpp`
* NEW `src/weather_client.h` / `src/weather_client.cpp`
* NEW `src/status_data.h` — StatusData struct + mutex + small accessors
* `src/scene.h` — NUM_MODES=6, add mode 5 "STATUS", declare drawStatus
* `src/scene.cpp` — STATUS case in render switch, drawStatus impl, modeName,
  extended 3x5 font (A-Z, '.', '-', '°', '?') + drawStr helper
* `src/main.cpp` — WiFiManager + WeatherClient lifecycle, StatusData feed,
  serial W/N/X/L (mode dots already auto-scale with NUM_MODES)
* `README.md` — WiFi section: provisioning, serial commands, STATUS mode
  description (incl. weather data sources + 30-min refresh + TLS note)
* NEW `captures/amoled_status.png` (+ disconnected variant) via capture script

## Board testing procedure

1. `pio run -t upload` (auto-detects /dev/cu.usbmodem*)
2. `pio device monitor` at 115200
3. Send `N <ssid> <pass>`; watch connect state transition + GOT_IP
4. Wait for first weather fetch (or send `L`); `W` should print lat/lon/city +
   temperature + weathercode + wind
5. Button-tap into STATUS mode (or `M` until STATUS); verify each widget
   updates (rssi, ip, weather icon + temp + city, uptime, fps)
6. Send `L` and confirm the fetch re-runs and values refresh
7. Capture frame with `python scripts/capture_frame.py status`
8. Toggle off WiFi (`X`) to verify the disconnected visual; re-provision to
   restore

## Git / commits (one commit per feature, all pushed to main)

1. `feat: WiFi manager with NVS credentials + serial provisioning (W/N/X)`
2. `feat: weather client — IP-based geolocation + Open-Meteo current weather (30-min refresh, L to force)`
3. `feat: STATUS scene mode with 3D interactive status widgets (incl. weather globe)`
4. `test: on-board verification captures + README docs for WiFi status mode`

All code + docs committed to `l064n/project-five-amoled` main branch.

## Open questions (defaults if not answered)

* Weather refresh interval — default 30 min; `L` forces immediate.
* TLS policy — CA bundle first; loud log + one insecure retry if the
  handshake fails (personal dashboard, non-sensitive payload).
* 3D widget interaction — global orbit + tap-to-pulse (not per-widget spin).
