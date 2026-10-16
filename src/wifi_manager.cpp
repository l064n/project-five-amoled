#include "wifi_manager.h"
#include <WiFi.h>
#include <Preferences.h>
#include <string.h>

namespace {
constexpr const char *NS_WIFI = "wifi";
constexpr const char *KEY_SSID = "ssid";
constexpr const char *KEY_PASS = "pass";
constexpr uint32_t CONNECT_TIMEOUT_MS = 15000; // give up on one attempt after this
constexpr uint32_t MAX_RETRY_BACKOFF_MS = 15000;
}

bool WiFiManager::begin()
{
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // keep latency low; weather + status need it

    Preferences p;
    if (p.begin(NS_WIFI, true)) {
        m_ssid[0] = 0;
        m_pass[0] = 0;
        p.getString(KEY_SSID, m_ssid, sizeof(m_ssid) - 1);
        p.getString(KEY_PASS, m_pass, sizeof(m_pass) - 1); // may be "" (open net)
        m_haveCreds = (m_ssid[0] != 0);
        p.end();
    }

    if (m_haveCreds) {
        Serial.printf("[wifi] creds found (ssid=%.15s) — connecting\n", m_ssid);
        startConnect();
    } else {
        g_status.state = WIFI_IDLE;
        Serial.println("[wifi] no credentials stored — send: N <ssid> <pass>");
    }
    return true;
}

void WiFiManager::startConnect()
{
    WiFi.disconnect(true);
    WiFi.begin(m_ssid, m_pass);
    m_connectStartMs = millis();
    m_retries = 0;
    g_status.state = WIFI_CONNECTING;
    g_status.ip = 0;
    g_status.rssi = 0;
}

void WiFiManager::stopConnect()
{
    WiFi.disconnect(true, true);
    g_status.state = WIFI_IDLE;
    g_status.ip = 0;
    g_status.rssi = 0;
}

bool WiFiManager::setCredentials(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0]) return false;
    strncpy(m_ssid, ssid, sizeof(m_ssid) - 1);
    m_ssid[sizeof(m_ssid) - 1] = 0;
    strncpy(m_pass, pass ? pass : "", sizeof(m_pass) - 1);
    m_pass[sizeof(m_pass) - 1] = 0;

    Preferences p;
    if (!p.begin(NS_WIFI, false)) return false; // read-write: creates the namespace
    p.putString(KEY_SSID, m_ssid);
    p.putString(KEY_PASS, m_pass);
    p.end();

    m_haveCreds = true;
    Serial.printf("[wifi] credentials stored (ssid=%.15s) — connecting\n", m_ssid);
    startConnect();
    return true;
}

void WiFiManager::forget()
{
    Preferences p;
    if (p.begin(NS_WIFI, false)) {
        p.clear();
        p.end();
    }
    m_ssid[0] = 0;
    m_pass[0] = 0;
    m_haveCreds = false;
    Serial.println("[wifi] credentials erased — disconnected");
    stopConnect();
}

// Poll-driven state machine (called from loop(), core 0). Cheap: a few status
// reads + at most 1 Hz RSSI sampling.
void WiFiManager::update()
{
    if (!m_haveCreds) {
        if (g_status.state != WIFI_IDLE) g_status.state = WIFI_IDLE;
        return;
    }

    uint32_t now = millis();
    bool wl = (WiFi.status() == WL_CONNECTED);
    WifiState st = (WifiState)g_status.state;

    if (wl) {
        if (st != WIFI_CONNECTED) {
            // Just acquired the link.
            IPAddress ip = WiFi.localIP();
            uint32_t packed = ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) |
                              ((uint32_t)ip[2] << 8) | (uint32_t)ip[3];
            g_status.ip = packed;
            g_status.rssi = WiFi.RSSI();
            g_status.state = WIFI_CONNECTED;
            char buf[20];
            statusIpToStr(packed, buf);
            Serial.printf("[wifi] connected — ip %s rssi %d dBm\n",
                          buf, WiFi.RSSI());
        }
        // Keep RSSI fresh (~1 Hz; the read is cheap).
        static uint32_t lastRssi = 0;
        if (now - lastRssi >= 1000) {
            g_status.rssi = WiFi.RSSI();
            lastRssi = now;
        }
        return;
    }

    // Not connected.
    if (st == WIFI_CONNECTED) {
        // Link dropped.
        g_status.state = WIFI_FAILED;
        g_status.ip = 0;
        m_lastFailMs = now;
        Serial.println("[wifi] link dropped — will retry");
    } else if (st == WIFI_CONNECTING) {
        // Waiting for an IP; give up after the timeout and fall through to
        // the retry path below on the next call (via FAILED).
        if (now - m_connectStartMs > CONNECT_TIMEOUT_MS) {
            g_status.state = WIFI_FAILED;
            m_lastFailMs = now;
            Serial.println("[wifi] connect timed out — retrying");
        }
        return;
    }

    // WIFI_FAILED: capped exponential backoff, then retry.
    uint32_t backoff = 2000u * (1u << (m_retries < 4 ? m_retries : 4));
    if (backoff > MAX_RETRY_BACKOFF_MS) backoff = MAX_RETRY_BACKOFF_MS;
    if (now - m_lastFailMs >= backoff) {
        m_lastFailMs = now;
        m_retries++;
        Serial.printf("[wifi] retry #%u (backoff %u ms)\n",
                      (unsigned)m_retries, (unsigned)backoff);
        startConnect();
    }
}

void WiFiManager::describe(String &out) const
{
    const char *stName = "IDLE";
    switch ((WifiState)g_status.state) {
        case WIFI_IDLE:       stName = "IDLE (no creds — send: N <ssid> <pass>)"; break;
        case WIFI_CONNECTING: stName = "CONNECTING"; break;
        case WIFI_CONNECTED:  stName = "CONNECTED"; break;
        case WIFI_FAILED:     stName = "FAILED (retrying)"; break;
    }
    char ipbuf[20];
    statusIpToStr(g_status.ip, ipbuf);
    out = "";
    out.concat("state=");
    out.concat(stName);
    out.concat("  ip=");
    out.concat(ipbuf);
    if (g_status.rssi) {
        out.concat("  rssi=");
        out.concat(g_status.rssi);
        out.concat(" dBm");
    }
    if (m_haveCreds) {
        out.concat("  ssid=");
        out.concat(m_ssid);
    }
}
