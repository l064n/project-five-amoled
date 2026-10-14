#pragma once
//
// wifi_manager.h — non-blocking WiFi STA connection with credentials stored in
// NVS (no secrets in the repo) and runtime provisioning over serial.
//
// Lifecycle:
//   begin()  -> load creds from NVS. If creds exist, start connecting
//               (state CONNECTING); else state IDLE.
//   update() -> call from loop() every iteration. Cheap and fully
//               poll-driven: it watches WiFi.status(), mirrors live stats
//               (state / rssi / ip / bytes) into the shared g_status block,
//               and auto-reconnects on link loss with a capped backoff.
//
// Serial (handled in main.cpp):
//   N <ssid> <pass>  store creds in NVS + (re)connect
//   X               forget creds + disconnect
//
#include <Arduino.h>
#include "status_data.h"

class WiFiManager {
public:
    bool begin();
    void update();

    // Credentials (NVS namespace "wifi").
    bool hasCreds() const { return m_haveCreds; }
    bool setCredentials(const char *ssid, const char *pass); // save + (re)connect
    void forget();                                           // erase + disconnect

    // Live accessors (mirror of what's already in g_status).
    uint32_t state() const { return g_status.state; }
    int32_t  rssi()  const { return g_status.rssi;  }
    uint32_t ip()    const { return g_status.ip;    }

    // One-line human summary for the 'W' serial command.
    void describe(String &out) const;

private:
    void startConnect();
    void stopConnect();

    char m_ssid[33] = {0};
    char m_pass[65] = {0};
    bool m_haveCreds = false;
    uint32_t m_connectStartMs = 0;  // when the current connect attempt began
    uint32_t m_lastFailMs = 0;      // last failed/dropped timestamp (backoff)
    uint8_t  m_retries = 0;
};
