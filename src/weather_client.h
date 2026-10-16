#pragma once
//
// weather_client.h — fetches the board's rough location (from its own public
// IP) and current weather over the WiFi link, on its own low-priority task.
//
// Pipeline (only runs while WiFi is CONNECTED):
//   1. GET https://ipapi.co/json/            -> latitude, longitude, city
//   2. GET https://api.open-meteo.com/...    -> temperature, weathercode, wind
//
// Both endpoints are free and need no API key. Transport is WiFiClientSecure
// (ships with the Arduino-ESP32 core). No CA bundle is embedded on this
// personal dashboard, so chain verification is skipped (setInsecure) — a
// documented trade-off (the payload is non-sensitive weather data). All
// socket + handshake timeouts are bounded so a stalled fetch can never hang
// a core long enough to trip the task watchdog.
//
// Results are written into the shared g_status block (under its mutex) for the
// STATUS scene to render. Refreshes every 30 min; forceRefresh() (serial 'L')
// triggers an immediate re-fetch.
//
#include <Arduino.h>
#include "status_data.h"

class WeatherClient {
public:
    friend void weatherTaskFn(void *arg); // background task touches privates

    // Create + start the background task (call once at boot, after the display
    // is up; the task itself idles until WiFi is CONNECTED).
    void begin();

    // Trigger an immediate fetch on the next task wake (thread-safe).
    void forceRefresh();

    // Human summary for the 'W' serial command.
    void describe(String &out) const;

    // WMO weather code -> short label (for serial + optional on-screen use).
    static const char *wmoName(uint32_t code);

private:
    void taskFn(void *arg);
    bool fetchLocation();   // ipapi.co
    bool fetchWeather();    // open-meteo
    bool httpGet(const char *host, int port, const char *path, String &out);

    TaskHandle_t m_task = nullptr;
    volatile bool m_force = false;
    bool m_haveLocation = false; // location fetched at least once
    bool m_lastOk = false;       // last fetch attempt succeeded
    uint32_t m_lastAttemptMs = 0;// last fetch attempt (success OR failure)
    float m_lat = 0.0f, m_lon = 0.0f;
};

// Minimal JSON field extractors (no external JSON lib). Operate on a String
// body. findNumber handles int + float by parsing the matched token.
bool jsonFindNumber(const String &body, const char *key, double &out);
bool jsonFindString(const String &body, const char *key, String &out, size_t cap);
