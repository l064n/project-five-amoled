#include "weather_client.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <string.h>

namespace {
constexpr const char *GEO_HOST = "ipapi.co";
constexpr const char *GEO_PATH = "/json";
constexpr const char *WX_HOST = "api.open-meteo.com";
constexpr uint32_t REFRESH_MS = 30UL * 60UL * 1000UL; // 30 min (after a success)
constexpr uint32_t RETRY_MS   = 30UL * 1000UL;        // 30 s  (after a failure)
constexpr int HTTPS_PORT = 443;
constexpr size_t MAX_BODY = 2048; // both responses are small
}

// -------------------------------------------------------------------------
// Minimal JSON field extraction (no external JSON library).
// -------------------------------------------------------------------------
// Find "key" then the following ':' then read a value token.
static int findKeyColon(const String &body, const char *key)
{
    String pat = String("\"") + key + "\"";
    int k = body.indexOf(pat);
    if (k < 0) return -1;
    k += pat.length();
    int c = body.indexOf(':', k);
    if (c < 0) return -1;
    return c + 1;
}

bool jsonFindNumber(const String &body, const char *key, double &out)
{
    int p = findKeyColon(body, key);
    if (p < 0) return false;
    int start = p;
    while (start < body.length() && body[start] == ' ') start++;
    int end = start;
    while (end < body.length()) {
        char c = body[end];
        if (isdigit((unsigned char)c) || c == '-' || c == '+' ||
            c == '.' || c == 'e' || c == 'E') end++;
        else break;
    }
    if (end == start) return false;
    String tok = body.substring(start, end);
    out = tok.toDouble();
    return true;
}

bool jsonFindString(const String &body, const char *key, String &out, size_t cap)
{
    int p = findKeyColon(body, key);
    if (p < 0) return false;
    int q = body.indexOf('"', p);
    if (q < 0) return false;
    int q2 = body.indexOf('"', q + 1);
    if (q2 < 0) return false;
    out = body.substring(q + 1, q2);
    if (out.length() >= cap) out = out.substring(0, cap - 1);
    return true;
}

// -------------------------------------------------------------------------
// WeatherClient
// -------------------------------------------------------------------------
// Read up to `want` bytes from `stream` into `out` (bounded by a timeout).
static bool streamRead(WiFiClient *stream, String &out, int want)
{
    out.reserve(want);
    int got = 0;
    uint32_t t0 = millis();
    while (got < want && stream->available() && millis() - t0 < 8000) {
        int c = stream->read();
        if (c >= 0) { out += (char)c; got++; }
    }
    return got > 0;
}

// Perform one HTTPS GET. `client` is reused across calls (keeps the TLS
// session warm). Returns true and fills `out` on HTTP 200.
static bool doGet(WiFiClientSecure *client, const char *host,
                  const char *path, String &out)
{
    out = "";
    HTTPClient http;
    if (!http.begin(*client, String("https://") + host + path)) return false;
    http.addHeader("Connection", "close");
    int code = http.GET();
    bool ok = false;
    if (code == 200) {                       // HTTPC_OK == 200
        int len = http.getSize();
        if (len <= 0 || len > (int)MAX_BODY) len = (int)MAX_BODY;
        ok = streamRead(http.getStreamPtr(), out, len);
        if (!ok) Serial.printf("[wx] HTTP 200 but empty body (%s)\n", host);
    }
    http.end();
    if (!ok) Serial.printf("[wx] GET %s%s -> %d\n", host, path, code);
    return ok;
}

bool WeatherClient::httpGet(const char *host, int port, const char *path, String &out)
{
    out = "";
    WiFiClientSecure *client = new WiFiClientSecure;
    if (!client) return false;
    // No CA bundle is embedded, so skip chain verification (documented
    // trade-off for this non-sensitive payload). Bounded timeouts so a
    // stalled connect/handshake can't hang a core into the task watchdog.
    client->setInsecure();
    client->setTimeout(5);             // 5 s socket I/O timeout
    client->setHandshakeTimeout(5000); // 5 s TLS handshake cap
    bool ok = doGet(client, host, path, out);
    delete client;
    return ok;
}

bool WeatherClient::fetchLocation()
{
    String body;
    if (!httpGet(GEO_HOST, HTTPS_PORT, GEO_PATH, body)) return false;
    double lat, lon;
    String city;
    if (!jsonFindNumber(body, "latitude", lat) ||
        !jsonFindNumber(body, "longitude", lon)) {
        Serial.printf("[wx] geo: no lat/lon in response\n");
        return false;
    }
    jsonFindString(body, "city", city, 15);
    if (city.length() == 0) jsonFindString(body, "country_name", city, 15);
    m_lat = (float)lat;
    m_lon = (float)lon;
    Serial.printf("[wx] location: %.4f, %.4f (%s)\n", m_lat, m_lon, city.c_str());

    statusLock();
    strncpy(g_status.city, city.c_str(), sizeof(g_status.city) - 1);
    g_status.city[sizeof(g_status.city) - 1] = 0;
    g_status.lat = m_lat;
    g_status.lon = m_lon;
    statusUnlock();
    m_haveLocation = true;
    return true;
}

bool WeatherClient::fetchWeather()
{
    String path = String("/v1/forecast?latitude=") + String(m_lat, 4) +
                  "&longitude=" + String(m_lon, 4) +
                  "&current_weather=true"
                  "&temperature_unit=fahrenheit";
    String body;
    if (!httpGet(WX_HOST, HTTPS_PORT, path.c_str(), body)) return false;

    // open-meteo returns: {"current_weather":{"temperature":..,"windspeed":..,
    //                                          "weathercode":..,...}}
    // Our findKeyColon is flat, so search for the last occurrence pattern by
    // locating the "current_weather" object substring first, then parse within.
    int cw = body.indexOf("\"current_weather\"");
    String sub = (cw >= 0) ? body.substring(cw) : body;

    double temp = 0, wind = 0, code = 0;
    if (!jsonFindNumber(sub, "temperature", temp)) {
        Serial.printf("[wx] weather: no temperature in response\n");
        return false;
    }
    jsonFindNumber(sub, "windspeed", wind);
    jsonFindNumber(sub, "weathercode", code);

    Serial.printf("[wx] weather: %.1f F  code=%d  wind=%.1f km/h\n",
                  temp, (int)code, wind);

    statusLock();
    g_status.temp_x10 = (int32_t)(temp * 10.0);
    g_status.wcode = (uint32_t)code;
    g_status.wind_x10 = (int32_t)(wind * 10.0);
    g_status.w_valid = 1;
    g_status.lastFetchMs = millis();
    statusUnlock();
    return true;
}

void weatherTaskFn(void *arg)
{
    WeatherClient *self = (WeatherClient *)arg;
    bool wasConnected = false;
    for (;;) {
        bool connected = (g_status.state == WIFI_CONNECTED);
        uint32_t now = millis();

        // Retry cadence: 30 min after a success, 30 s after a failure.
        uint32_t cadence = self->m_lastOk ? REFRESH_MS : RETRY_MS;
        bool due = self->m_lastAttemptMs != 0 &&
                   (now - self->m_lastAttemptMs) >= cadence;
        bool doFetch = connected &&
            (!wasConnected ||              // just (re)connected
             self->m_force ||
             due);

        if (doFetch) {
            self->m_force = false;
            // Need a location before we can query the weather API.
            if (!self->m_haveLocation) self->fetchLocation();
            bool wxOk = false;
            if (self->m_haveLocation) wxOk = self->fetchWeather();
            self->m_lastOk = wxOk;
            self->m_lastAttemptMs = now;
            if (self->m_lastOk) Serial.println("[wx] fetch OK");
            else Serial.printf("[wx] fetch failed — retry in %lu s\n",
                               (unsigned long)(RETRY_MS / 1000UL));
        }
        wasConnected = connected;
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1 Hz poll of wifi state
    }
}

void WeatherClient::begin()
{
    // Core 0: co-resides with lwIP (tcpip thread) + the Arduino loop. Keeping
    // the blocking network/TLS work OFF core 1 — which runs the real-time
    // 60 Hz render/blit task — avoids cross-core spinlock contention that
    // trips the task watchdog.
    xTaskCreatePinnedToCore(weatherTaskFn, "weather", 8192, this, 1, &m_task, 0);
    Serial.println("[wx] weather task started (refresh 30 min, 'L' to force)");
}

void WeatherClient::forceRefresh()
{
    m_force = true;
    Serial.println("[wx] force refresh requested");
}

void WeatherClient::describe(String &out) const
{
    out = "";
    if (!g_status.w_valid) {
        out.concat("weather: NO DATA (waiting for WiFi + first fetch)");
        return;
    }
    char tmp[48];
    snprintf(tmp, sizeof(tmp), "%.1f C", g_status.temp_x10 / 10.0);
    out.concat("weather: ");
    out.concat(tmp);
    out.concat("  code=");
    out.concat(g_status.wcode);
    out.concat(" (");
    out.concat(wmoName(g_status.wcode));
    out.concat(")");
    snprintf(tmp, sizeof(tmp), "  wind %.1f km/h", g_status.wind_x10 / 10.0);
    out.concat(tmp);
    out.concat("  @ ");
    out.concat(g_status.city);
}

const char *WeatherClient::wmoName(uint32_t c)
{
    switch (c) {
        case 0: return "clear";
        case 1: return "mostly clear";
        case 2: return "partly cloudy";
        case 3: return "overcast";
        case 45: case 48: return "fog";
        case 51: case 53: case 55: return "drizzle";
        case 56: case 57: return "freezing drizzle";
        case 61: case 63: case 65: return "rain";
        case 66: case 67: return "freezing rain";
        case 71: case 73: case 75: return "snow";
        case 77: return "snow grains";
        case 80: case 81: case 82: return "rain showers";
        case 85: case 86: return "snow showers";
        case 95: return "thunderstorm";
        case 96: case 99: return "thunderstorm + hail";
        default: return "unknown";
    }
}
