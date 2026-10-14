//
// status_scene.cpp — Scene::drawStatus and its helpers (mode 5 "STATUS").
//
// A constellation of interactive 3D widgets rendered from the shared
// StatusData (WiFi state/rssi/ip + live local weather + uptime/fps). The whole
// constellation orbits with the global drag (rx/ry); a tap on a widget fires a
// decaying pulse. Widgets:
//   0 WiFi tower (mast + RSSI arcs + connecting sweep / off blink)
//   1 RSSI bars (5 3D bars)
//   2 IP octet cubes + 3x5 digit labels
//   3 Weather globe (rotating wireframe + orbiting 3D weather icon + temp/city)
//   4 Uptime ring + FPS
//
#include "scene.h"
#include "status_data.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

// ---------------------------------------------------------------------------
// 3x5 font (LSB = leftmost pixel). Digits + A-Z + a few symbols, for labels.
// ---------------------------------------------------------------------------
struct Glyph { char c; uint8_t r[5]; };
const Glyph kFont[] = {
    {'0',{0b111,0b101,0b101,0b101,0b111}},{'1',{0b010,0b110,0b010,0b010,0b111}},
    {'2',{0b111,0b001,0b111,0b100,0b111}},{'3',{0b111,0b001,0b111,0b001,0b111}},
    {'4',{0b101,0b101,0b111,0b001,0b001}},{'5',{0b111,0b100,0b111,0b001,0b111}},
    {'6',{0b111,0b100,0b111,0b101,0b111}},{'7',{0b111,0b001,0b001,0b001,0b001}},
    {'8',{0b111,0b101,0b111,0b101,0b111}},{'9',{0b111,0b101,0b111,0b001,0b111}},
    {'A',{0b111,0b101,0b111,0b101,0b101}},{'B',{0b110,0b101,0b110,0b101,0b110}},
    {'C',{0b111,0b100,0b100,0b100,0b111}},{'D',{0b110,0b101,0b101,0b101,0b110}},
    {'E',{0b111,0b100,0b110,0b100,0b111}},{'F',{0b111,0b100,0b110,0b100,0b100}},
    {'G',{0b111,0b100,0b101,0b101,0b111}},{'H',{0b101,0b101,0b111,0b101,0b101}},
    {'I',{0b111,0b010,0b010,0b010,0b111}},{'J',{0b011,0b001,0b001,0b101,0b110}},
    {'K',{0b101,0b110,0b100,0b110,0b101}},{'L',{0b100,0b100,0b100,0b100,0b111}},
    {'M',{0b101,0b111,0b111,0b101,0b101}},{'N',{0b101,0b111,0b111,0b111,0b101}},
    {'O',{0b111,0b101,0b101,0b101,0b111}},{'P',{0b111,0b101,0b111,0b100,0b100}},
    {'Q',{0b111,0b101,0b101,0b111,0b011}},{'R',{0b111,0b101,0b110,0b110,0b101}},
    {'S',{0b111,0b100,0b111,0b001,0b111}},{'T',{0b111,0b010,0b010,0b010,0b010}},
    {'U',{0b101,0b101,0b101,0b101,0b111}},{'V',{0b101,0b101,0b101,0b101,0b010}},
    {'W',{0b101,0b101,0b111,0b111,0b101}},{'X',{0b101,0b101,0b010,0b101,0b101}},
    {'Y',{0b101,0b101,0b010,0b010,0b010}},{'Z',{0b111,0b001,0b010,0b100,0b111}},
    {'-', {0b000,0b000,0b111,0b000,0b000}},{'.', {0b000,0b000,0b000,0b000,0b010}},
    {'*', {0b101,0b111,0b111,0b111,0b101}},{'?', {0b111,0b001,0b011,0b000,0b010}},
    {'o', {0b000,0b110,0b101,0b101,0b110}},{'d', {0b001,0b001,0b101,0b101,0b111}},
    {'c', {0b000,0b000,0b111,0b100,0b111}},{'e', {0b000,0b000,0b111,0b101,0b111}},
    {'b', {0b100,0b100,0b110,0b101,0b110}},{'m',{0b000,0b000,0b110,0b110,0b110}},
    {'h', {0b100,0b100,0b110,0b101,0b101}},{'/', {0b001,0b001,0b010,0b100,0b100}},
    {'%', {0b101,0b001,0b010,0b100,0b101}},{'~', {0b000,0b010,0b101,0b000,0b000}},
};
const int kFontN = sizeof(kFont) / sizeof(kFont[0]);

// A 5x5 ring template (rows of 5 bits, LSB = leftmost) for the uptime ring.
const uint8_t kRing5[5] = {0b010, 0b101, 0b101, 0b101, 0b111};

void drawGlyph(Engine3D &e, int x, int y, char c, int r, int g, int b)
{
    for (int i = 0; i < kFontN; i++) {
        if (kFont[i].c != c) continue;
        for (int row = 0; row < 5; row++)
            for (int col = 0; col < 3; col++)
                if (kFont[i].r[row] & (1 << (2 - col)))
                    e.addPixel(x + col, y + row, r, g, b);
        return;
    }
    // unknown char -> small box
    e.drawNeonDot(x + 1, y + 2, r, g, b, 1);
}

void drawStr(Engine3D &e, int x, int y, const char *s, int r, int g, int b)
{
    for (int i = 0; s && s[i] && x + 4 < e.W; i++) {
        drawGlyph(e, x, y, s[i], r, g, b);
        x += 4;
    }
}

// 0..1 pulse envelope from a timestamp (ms) — rises fast, decays over ~700ms.
float pulseEnv(uint32_t tMs, uint32_t stamp)
{
    if (!stamp) return 0.0f;
    uint32_t d = tMs - stamp;
    if (d > 700) return 0.0f;
    if (d < 90) return (float)d / 90.0f;
    return 1.0f - (float)(d - 90) / 610.0f;
}

// --- STATUS camera (world space): v = (world - C) * z, applied to EVERY
// vertex and label so the 3D meshes + their text stay locked together and the
// focused widget stays pinned to screen-center while you orbit. Set once per
// frame in drawStatus() from the eased Scene camera.
struct Cam { float z, cx, cy, cz; };
Cam   g_cam    = {1.0f, 0.0f, 0.0f, 0.0f};
float g_lscale = 1.0f;   // label / icon pixel scale (derived from zoom)

inline V3 camX(const V3 &p) {
    return { (p.x - g_cam.cx) * g_cam.z,
             (p.y - g_cam.cy) * g_cam.z,
             (p.z - g_cam.cz) * g_cam.z };
}
// Scale a length by the label scale (min 1 px).
inline int scl(float v) { int s = (int)(v * g_lscale + 0.5f); return s < 1 ? 1 : s; }

// Project a world point (through the camera) under rotation m to screen.
bool proj(Engine3D &e, const V3 &p, const Rot &m, float camDist, int &sx, int &sy)
{
    V3 pc = camX(p);
    V3 r;
    rotateMat(pc.x, pc.y, pc.z, m, r);
    int d;
    return e.project(r.x, r.y, r.z + camDist, sx, sy, d);
}

// Draw a 3D segment between two world points (through the camera). Culls if
// either end is behind the camera.
void seg3(Engine3D &e, const V3 &a, const V3 &b, const Rot &m, float camDist,
          int r, int g, int bl, int inten, int core, int glow)
{
    e.renderSegmentMat(camX(a), camX(b), m, camDist, r, g, bl, inten, core, glow);
}

} // namespace

// ===========================================================================
// Scene::drawStatus
// ===========================================================================
void Scene::drawStatus(float rx, float ry)
{
    // Snapshot the shared status (brief mutex) so the whole frame is coherent.
    uint32_t st, ip, wcode, wvalid, upt, fps, lastF;
    int32_t rssi, temp10, wind10;
    char city[16];
    statusLock();
    st = g_status.state; ip = g_status.ip; rssi = g_status.rssi;
    wcode = g_status.wcode; wvalid = g_status.w_valid;
    temp10 = g_status.temp_x10; wind10 = g_status.wind_x10;
    upt = g_status.uptime_s; fps = g_status.fps; lastF = g_status.lastFetchMs;
    memcpy(city, g_status.city, sizeof(city));
    city[sizeof(city) - 1] = 0;
    statusUnlock();

    smoothCam(); // advance the eased zoom + focus-center camera
    const float t = spinT;
    // Dampen user orbit so the widgets stay readable, but still respond.
    Rot mG = makeRot(rx * 0.5f, ry * 0.5f, 0.0f);
    const float CD = 430.0f;

    // Layout anchors (world space), at different depths for parallax.
    const V3 towerC  = { -55.0f,  120.0f,   0.0f };
    const V3 barsC   = {  55.0f,  120.0f,   0.0f };
    const V3 ipC     = {   0.0f,   40.0f,  40.0f };
    const V3 globeC  = {  -45.0f,  -45.0f,  60.0f };
    const V3 ringC   = {  55.0f,  -55.0f,  30.0f };
    widgetAnchorW[0] = towerC; widgetAnchorW[1] = barsC; widgetAnchorW[2] = ipC;
    widgetAnchorW[3] = globeC; widgetAnchorW[4] = ringC;

    // Apply the eased camera (zoom + center) for this frame, and derive the
    // label/icon pixel scale from the zoom so text grows with the widgets.
    g_cam.z  = zCur;  g_cam.cx = cxCur;  g_cam.cy = cyCur;  g_cam.cz = czCur;
    g_lscale = 0.55f + 0.45f * zCur;

    // Pulse envelopes per widget.
    float pTower = pulseEnv(millis(), pulseMs[0]);
    float pBars  = pulseEnv(millis(), pulseMs[1]);
    float pIp    = pulseEnv(millis(), pulseMs[2]);
    float pGlobe = pulseEnv(millis(), pulseMs[3]);
    float pRing  = pulseEnv(millis(), pulseMs[4]);

    // RSSI -> 0..1 level (map -90..-35 dBm).
    float lvl = 0.0f;
    if (st == WIFI_CONNECTED && rssi) {
        lvl = (float)(rssi + 90) / 55.0f;
        if (lvl < 0) lvl = 0; if (lvl > 1) lvl = 1;
    }

    // ------------------------------------------------------------------ TOWER
    {
        bool up = (st == WIFI_CONNECTED);
        bool conn = (st == WIFI_CONNECTING);
        bool fail = (st == WIFI_FAILED);
        int r = up ? 80 : 255, g = up ? 230 : 70, b = up ? 255 : 70;
        // Off/failed: blink.
        if (!up && ((millis() / 400) & 1)) { r = 120; g = 30; b = 30; }
        float s = 1.0f + 0.25f * pTower;
        V3 base = towerC, tip = towerC; tip.y += 60.0f * s;
        // Mast.
        V3 mb = { towerC.x, towerC.y, towerC.z }, mt = { towerC.x, towerC.y + 60.0f * s, towerC.z };
        seg3(e_, mb, mt, mG, CD, r, g, b, 200, 2, 2);
        // Arcs: three concentric, lit by level.
        for (int a = 0; a < 3; a++) {
            float rad = 14.0f + a * 13.0f;
            float lit = (lvl * 3.0f > (float)a) ? 1.0f : 0.18f;
            int ir = (int)(r * lit), ig = (int)(g * lit), ib = (int)(b * lit);
            int inten = (int)(120 + 135.0f * lit);
            int N = 14;
            V3 pa, pb;
            for (int i = 0; i < N; i++) {
                float a0 = M_PI * (0.25f + 0.5f * (float)i / (float)N);
                float a1 = M_PI * (0.25f + 0.5f * (float)(i + 1) / (float)N);
                pa.x = tip.x + rad * s * sinf(a0); pa.y = tip.y + rad * s * cosf(a0) - 6.0f; pa.z = tip.z;
                pb.x = tip.x + rad * s * sinf(a1); pb.y = tip.y + rad * s * cosf(a1) - 6.0f; pb.z = tip.z;
                seg3(e_, pa, pb, mG, CD, ir, ig, ib, inten, 1, a < 2 ? 1 : 0);
            }
        }
        // Connecting: a sweeping beam from the tip.
        if (conn || fail) {
            float sw = fmodf(t * 0.004f, 2.0f * M_PI);
            V3 a0 = { tip.x, tip.y, tip.z };
            V3 a1 = { tip.x + 34.0f * sinf(sw), tip.y + 34.0f * cosf(sw), tip.z };
            seg3(e_, a0, a1, mG, CD, r, g, b, 160, 1, 1);
        }
        // Label.
        int sx, sy;
        if (proj(e_, towerC, mG, CD, sx, sy)) { tapX[0] = sx; tapY[0] = sy; drawStr(e_, sx - scl(16), sy + scl(30), "WIFI", r, g, b); }
        else { tapX[0] = e_.W / 2; tapY[0] = 0; }
    }

    // ------------------------------------------------------------------ BARS
    {
        int lit = (int)(lvl * 5.0f + 0.5f);
        int N = 5, bw = 8, gap = 5;
        for (int i = 0; i < N; i++) {
            float hgt = (14.0f + i * 12.0f) * (1.0f + 0.2f * pBars);
            bool on = (i < lit);
            float hue = fmodf(200.0f - 160.0f * (float)i / (float)N + 0.0f, 360.0f);
            // Strength hue: red->green as i rises.
            int r = on ? (int)(120 - 90.0f * (float)i / (float)N) : 40;
            int g = on ? (int)(80 + 150.0f * (float)i / (float)N) : 40;
            int b = on ? 120 : 60;
            (void)hue;
            float x = barsC.x - (float)(N - 1) * (bw + gap) * 0.5f + (float)i * (bw + gap);
            V3 b0 = { x, barsC.y, barsC.z };
            V3 b1 = { x + bw, barsC.y, barsC.z };
            V3 t0 = { x, barsC.y + hgt, barsC.z };
            V3 t1 = { x + bw, barsC.y + hgt, barsC.z };
            int inten = on ? 220 : 60;
            seg3(e_, b0, b1, mG, CD, r, g, b, inten, 2, 0);      // bottom
            seg3(e_, t0, t1, mG, CD, r, g, b, inten, 2, 0);      // top
            seg3(e_, b0, t0, mG, CD, r, g, b, inten, 2, 0);      // left
            seg3(e_, b1, t1, mG, CD, r, g, b, inten, 2, 0);      // right
        }
        int sx, sy;
        if (proj(e_, barsC, mG, CD, sx, sy)) { tapX[1] = sx; tapY[1] = sy; drawStr(e_, sx - scl(12), sy + scl(26), "RSSI", 120, 200, 255); }
        else { tapX[1] = e_.W / 2; tapY[1] = 0; }
    }

    // -------------------------------------------------------------------- IP
    {
        char ipbuf[20];
        statusIpToStr(ip, ipbuf);
        int sx, sy;
        // 4 small cubes (one per octet) in a row, + digit label below.
        for (int o = 0; o < 4; o++) {
            float x = ipC.x - 33.0f + (float)o * 22.0f;
            float y = ipC.y;
            float z = ipC.z;
            float s = 9.0f * (1.0f + 0.3f * pIp);
            V3 v[8] = {
                { x - s, y - s, z - s }, { x + s, y - s, z - s },
                { x + s, y + s, z - s }, { x - s, y + s, z - s },
                { x - s, y - s, z + s }, { x + s, y - s, z + s },
                { x + s, y + s, z + s }, { x - s, y + s, z + s } };
            int e12[6][2] = { {0,1},{1,2},{2,3},{3,0} };
            int e45[4][2] = { {4,5},{5,6},{6,7},{7,4} };
            int c48[4][2] = { {0,4},{1,5},{2,6},{3,7} };
            int inten = 200;
            for (int i = 0; i < 4; i++) { seg3(e_, v[e12[i][0]], v[e12[i][1]], mG, CD, 90, 230, 255, inten, 1, 1); seg3(e_, v[e45[i][0]], v[e45[i][1]], mG, CD, 90, 230, 255, inten, 1, 1); seg3(e_, v[c48[i][0]], v[c48[i][1]], mG, CD, 90, 230, 255, inten, 1, 1); }
        }
        if (proj(e_, ipC, mG, CD, sx, sy)) {
            tapX[2] = sx; tapY[2] = sy;
            drawStr(e_, sx - scl(24), sy + scl(16), ipbuf, 120, 235, 255);
        } else { tapX[2] = e_.W / 2; tapY[2] = 0; }
    }

    // --------------------------------------------------------------- UPTIME
    {
        // A 5x5 neon ring filled by uptime fraction (mod 3600 s), + FPS.
        float frac = (float)(upt % 3600) / 3600.0f;
        int filled = (int)(frac * 12.0f); // ~12 ring pixels
        int sx, sy;
        if (proj(e_, ringC, mG, CD, sx, sy)) {
            tapX[4] = sx; tapY[4] = sy;
            // Draw ring template; light `filled` cells, dim the rest.
            for (int row = 0; row < 5; row++)
                for (int col = 0; col < 5; col++) {
                    int px = sx - scl(8) + col * scl(4), py = sy - scl(10) + row * scl(4);
                    if (px < 0 || py < 0 || px >= e_.W || py >= e_.H) continue;
                    bool on = kRing5[row] & (1 << (4 - col));
                    if (!on) continue;
                    int idx = row * 5 + col;
                    int inten = (idx < filled) ? 230 : 45;
                    int r = 255, g = (int)(120 + 80.0f * (float)filled / 12.0f), b = 60;
                    e_.drawNeonDot(px, py, r, g, b, 1);
                    e_.addPixel(px, py, (r >> 2), (g >> 2), (b >> 2));
                }
            char buf[16];
            snprintf(buf, sizeof(buf), "UP %lu", (unsigned long)upt);
            drawStr(e_, sx - scl(18), sy + scl(16), buf, 255, 180, 90);
            char fb[8];
            snprintf(fb, sizeof(fb), "%u", (unsigned)fps);
            drawStr(e_, sx + scl(2), sy + scl(16), "FPS " , 255, 180, 90);
            drawStr(e_, sx + scl(22), sy + scl(16), fb, 255, 220, 120);
        } else { tapX[4] = e_.W / 2; tapY[4] = 0; }
    }

    // ------------------------------------------------------------- WEATHER
    {
        int sx, sy;
        if (proj(e_, globeC, mG, CD, sx, sy)) { tapX[3] = sx; tapY[3] = sy; }
        else { tapX[3] = e_.W / 2; tapY[3] = 0; }
        drawWeatherGlobe(globeC, mG, CD, t, wvalid, wcode, temp10, city, pGlobe);
        // Temp + city labels (screen space, below the globe).
        if (sx && sy) {
            if (wvalid) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d.%dC", (int)(temp10 / 10), (int)((temp10 < 0 ? -temp10 : temp10) % 10));
                drawStr(e_, sx - scl(16), sy + scl(34), buf, 120, 230, 255);
                char up[16];
                for (int i = 0; i < 12 && city[i]; i++) up[i] = (char)toupper((unsigned char)city[i]);
                up[12] = 0;
                drawStr(e_, sx - scl(18), sy + scl(44), up, 90, 180, 220);
            } else {
                drawStr(e_, sx - scl(22), sy + scl(34), "NO DATA", 120, 120, 140);
                drawStr(e_, sx - scl(26), sy + scl(44), "WAIT WIFI", 90, 90, 110);
            }
        }
    }
}

// ===========================================================================
// Weather globe: a slowly-rotating 3D wireframe sphere (meridians + latitudes)
// with the current weather icon ORBITING it. The icon is a screen-space
// billboard (crisp neon) positioned at the projected orbit point, so it always
// faces the viewer while the globe tumbles behind it.
// ===========================================================================
namespace {

// Draw a 3D ring of `N` segments centered at `c`, radius `r`, in the plane
// defined by unit axes u,v, rotated by `m`.
void drawRing3D(Engine3D &e, const V3 &c, float r, const V3 &u, const V3 &v,
                const Rot &m, float camDist, int ir, int ig, int ib, int inten)
{
    const int N = 20;
    for (int i = 0; i < N; i++) {
        float a0 = 2.0f * M_PI * (float)i / (float)N;
        float a1 = 2.0f * M_PI * (float)(i + 1) / (float)N;
        V3 pa, pb;
        pa.x = c.x + r * (u.x * cosf(a0) + v.x * sinf(a0));
        pa.y = c.y + r * (u.y * cosf(a0) + v.y * sinf(a0));
        pa.z = c.z + r * (u.z * cosf(a0) + v.z * sinf(a0));
        pb.x = c.x + r * (u.x * cosf(a1) + v.x * sinf(a1));
        pb.y = c.y + r * (u.y * cosf(a1) + v.y * sinf(a1));
        pb.z = c.z + r * (u.z * cosf(a1) + v.z * sinf(a1));
        e.renderSegmentMat(pa, pb, m, camDist, ir, ig, ib, inten, 1, 0);
    }
}

// --- billboard icon primitives (screen space). Sizes scale with the zoom
// (g_lscale) so the weather icon grows with the 3D globe when focused. ---
void iconSun(Engine3D &e, int cx, int cy, float t, float pulse, int inten)
{
    float s = g_lscale;
    int R = (int)(scl(9 + (int)(3.0f * pulse)));
    int ir = (int)(255 * inten), ig = (int)(210 * inten), ib = (int)(90 * inten);
    e.drawNeonDot(cx, cy, ir, ig, ib, R);
    e.addPixel(cx, cy, 60, 50, 20);
    float spin = t * 0.001f;
    for (int k = 0; k < 8; k++) {
        float a = spin + (float)k * M_PI / 4.0f;
        int x0 = cx + (int)((R + scl(3)) * cosf(a));
        int y0 = cy + (int)((R + scl(3)) * sinf(a));
        int x1 = cx + (int)((R + scl(7)) * cosf(a));
        int y1 = cy + (int)((R + scl(7)) * sinf(a));
        e.drawNeonLine(x0, y0, x1, y1, ir, ig, ib, scl(1), 1);
    }
}

void iconCloud(Engine3D &e, int cx, int cy, float drift, float pulse, int inten)
{
    int r = scl(4 + (int)(2.0f * pulse));
    int ir = (int)(200 * inten), ig = (int)(215 * inten), ib = (int)(235 * inten);
    int d = (int)(drift * g_lscale);
    int base = cy + scl(2);
    e.drawNeonDot(cx - r - scl(1) + d, base, ir, ig, ib, r);
    e.drawNeonDot(cx + r + scl(1) + d, base, ir, ig, ib, r);
    e.drawNeonDot(cx + d, base - r, ir, ig, ib, r + 1);
    e.drawNeonDot(cx + d, base + scl(1), ir, ig, ib, r);
}

void iconRain(Engine3D &e, int cx, int cy, float t, int inten)
{
    int ir = (int)(90 * inten), ig = (int)(170 * inten), ib = (int)(255 * inten);
    for (int i = 0; i < 3; i++) {
        int dx = cx - scl(5) + i * scl(5);
        float ph = fmodf(t * 0.006f + i * 0.5f, 1.0f);
        int y = cy + scl(6) + (int)(ph * scl(12));
        e.drawNeonLine(dx, y, dx - scl(2), y + scl(4), ir, ig, ib, 1, 0);
    }
}

void iconSnow(Engine3D &e, int cx, int cy, float t, int inten)
{
    int ir = (int)(210 * inten), ig = (int)(235 * inten), ib = (int)(255 * inten);
    for (int i = 0; i < 3; i++) {
        int dx = cx - scl(5) + i * scl(5);
        float ph = fmodf(t * 0.004f + i * 0.5f, 1.0f);
        int y = cy + scl(6) + (int)(ph * scl(12));
        for (int a = 0; a < 6; a++) {
            float ang = (float)a * M_PI / 3.0f;
            int x2 = dx + (int)(scl(3) * cosf(ang));
            int y2 = y + (int)(scl(3) * sinf(ang));
            e.drawNeonLine(dx, y, x2, y2, ir, ig, ib, 1, 0);
        }
    }
}

void iconLightning(Engine3D &e, int cx, int cy, float t, int inten)
{
    uint32_t cyc = (uint32_t)(t) % 1200;
    int flash = (cyc < 70 || (cyc > 240 && cyc < 300)) ? 255 : 40;
    flash = (int)(flash * inten);
    int ir = flash, ig = (int)(flash * 0.9f), ib = (int)(flash * 0.4f);
    int x0 = cx + scl(1), y0 = cy + scl(4);
    int pts[5][2] = {
        { x0, y0 }, { x0 - scl(4), y0 + scl(6) }, { x0 - scl(1), y0 + scl(7) },
        { x0 - scl(5), y0 + scl(13) }, { x0 - scl(2), y0 + scl(12) } };
    for (int i = 0; i < 4; i++)
        e.drawNeonLine(pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], ir, ig, ib, scl(1), 1);
}

// Map WMO code to an icon kind.
enum WIcon { WI_NONE, WI_SUN, WI_SUN_CLOUD, WI_CLOUD, WI_FOG, WI_RAIN, WI_SNOW, WI_STORM };
WIcon iconForCode(uint32_t c)
{
    if (c == 0) return WI_SUN;
    if (c == 1 || c == 2) return WI_SUN_CLOUD;
    if (c == 3) return WI_CLOUD;
    if (c == 45 || c == 48) return WI_FOG;
    if ((c >= 51 && c <= 67) || (c >= 80 && c <= 82)) return WI_RAIN;
    if ((c >= 71 && c <= 77) || (c == 85 || c == 86)) return WI_SNOW;
    if (c >= 95) return WI_STORM;
    return WI_CLOUD;
}

} // namespace

void Scene::drawWeatherGlobe(const V3 &c, const Rot &m, float camDist, float t,
                             uint32_t wvalid, uint32_t wcode, int32_t temp10,
                             const char *city, float pulse)
{
    const float R = 26.0f;
    const float spin = t * 0.0006f;
    (void)temp10; (void)city;
    int inten = wvalid ? 200 : 70;
    // Globe tint: cool cyan when valid, dim gray when not.
    int gr = wvalid ? 70 : 60, gg = wvalid ? 150 : 70, gb = wvalid ? 200 : 90;

    // Rotating frame: tilt the globe a bit, spin about Y.
    Rot mLocal = makeRot(0.35f, spin, 0.0f);
    // Combine: we apply mLocal to build world-space ring axes, then m (global).
    // Build the two basis axes in local space, rotate by mLocal, use as ring axes.
    V3 u = { 1.0f, 0.0f, 0.0f }, v = { 0.0f, 0.0f, 1.0f };
    V3 ur, vr;
    rotateMat(u.x, u.y, u.z, mLocal, ur);
    rotateMat(v.x, v.y, v.z, mLocal, vr);

    // Longitude rings (4, rotated about Y by mLocal) + equator.
    for (int i = 0; i < 4; i++) {
        float a = (float)i * M_PI / 4.0f;
        V3 uu = { cosf(a), 0.0f, sinf(a) };
        V3 uuR;
        rotateMat(uu.x, uu.y, uu.z, mLocal, uuR);
        drawRing3D(e_, c, R, uuR, vr, m, camDist, gr, gg, gb, inten);
    }
    // Latitude rings (3) + equator.
    const float lats[4] = { -0.6f, -0.2f, 0.2f, 0.6f };
    for (int i = 0; i < 4; i++) {
        float ph = lats[i];
        float rr = R * cosf(ph);
        V3 cc = { c.x, c.y + R * sinf(ph), c.z };
        drawRing3D(e_, cc, rr, ur, vr, m, camDist, gr, gg, gb, (int)(inten * 0.8f));
    }
    // Center dot.
    e_.renderPointMat(c, m, camDist, gr, gg, gb, inten, 1);

    // ---- Orbiting weather icon (billboard at projected orbit point) ----
    float orbitR = R + 12.0f;
    float oa = t * 0.0012f;
    V3 ic = { c.x + orbitR * cosf(oa), c.y + 6.0f + 4.0f * sinf(oa * 2.0f), c.z + orbitR * sinf(oa) };
    int sx, sy;
    if (!proj(e_, ic, m, camDist, sx, sy)) return;
    float inten1 = (float)(wvalid ? 1.0 : 0.35) * (1.0f + 0.5f * pulse);
    WIcon kind = wvalid ? iconForCode(wcode) : WI_NONE;
    float drift = 3.0f * sinf(t * 0.0015f);
    switch (kind) {
        case WI_SUN:       iconSun(e_, sx, sy, t, pulse, (int)inten1); break;
        case WI_SUN_CLOUD: iconSun(e_, sx - 5, sy - 5, t, pulse, 0.8f * inten1); iconCloud(e_, sx + 3, sy + 2, drift, pulse, inten1); break;
        case WI_CLOUD:     iconCloud(e_, sx, sy, drift, pulse, inten1); break;
        case WI_FOG:       iconCloud(e_, sx, sy - 2, drift, pulse, 0.8f * inten1);
                           e_.drawNeonLine(sx - 10, sy + 6, sx + 10, sy + 6, 150, 160, 180, 1, 1);
                           e_.drawNeonLine(sx - 8, sy + 9, sx + 8, sy + 9, 120, 130, 150, 1, 0);
                           break;
        case WI_RAIN:      iconCloud(e_, sx, sy - 3, drift, pulse, inten1); iconRain(e_, sx, sy - 3, t, inten1); break;
        case WI_SNOW:      iconCloud(e_, sx, sy - 3, drift, pulse, inten1); iconSnow(e_, sx, sy - 3, t, inten1); break;
        case WI_STORM:     iconCloud(e_, sx, sy - 4, drift, pulse, inten1); iconLightning(e_, sx, sy - 4, t, inten1); break;
        case WI_NONE:
        default:
            // Idle: a slow-spinning dim dot (no data yet).
            e_.drawNeonDot(sx, sy, 90, 90, 110, 2 + (int)pulse);
            break;
    }
}
