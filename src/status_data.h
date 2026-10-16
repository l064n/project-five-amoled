#pragma once
//
// status_data.h — the shared status block read by the STATUS scene and written
// by the WiFi manager (loop core) + weather client (its own task).
//
// Every scalar field is a single aligned 32-bit word, so it can be written and
// read as atomic on the ESP32-S3. The block also holds a short city string, so
// a lightweight mutex guards read/write of the WHOLE struct: the writer (loop
// or weather task) takes it, updates fields, releases; the render task (core 1)
// takes it once per frame in STATUS mode, copies to locals, releases. That is a
// handful of us per frame — negligible against the ~13 ms scene budget.
//
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

enum WifiState : uint8_t {
    WIFI_IDLE = 0,      // no credentials stored
    WIFI_CONNECTING,    // associated / waiting for IP
    WIFI_CONNECTED,     // have an IP, link up
    WIFI_FAILED,        // connect failed; auto-retrying
};

struct StatusData {
    // ---- wifi ----
    volatile uint32_t state;     // WifiState
    volatile int32_t  rssi;      // dBm (0 = n/a)
    volatile uint32_t ip;        // a<<24 | b<<16 | c<<8 | d (0 = none)
    volatile uint32_t rxBytes;   // bytes since boot
    volatile uint32_t txBytes;
    // ---- system ----
    volatile uint32_t uptime_s;
    volatile uint32_t fps;
    // ---- weather (fetched over the WiFi link) ----
    volatile uint32_t w_valid;   // 1 = have a current reading
    volatile int32_t  temp_x10;  // °F * 10 (e.g. 715 = 71.5)
    volatile uint32_t wcode;     // WMO weather code
    volatile int32_t  wind_x10;  // km/h * 10
    char              city[16];
    float             lat, lon;
    volatile uint32_t lastFetchMs; // millis() of last successful fetch (0 = none)
};

// Global block + its guard.
extern StatusData g_status;
extern SemaphoreHandle_t g_statusMtx;

// Call once at boot (before any task touches g_status).
void statusBegin();

// Take / release the guard. Use around any multi-field read or write.
static inline void statusLock()   { xSemaphoreTake(g_statusMtx, portMAX_DELAY); }
static inline void statusUnlock() { xSemaphoreGive(g_statusMtx); }

// Format a packed 4-octet IP into "a.b.c.d" (or "?" when 0).
void statusIpToStr(uint32_t ip, char *out8);
