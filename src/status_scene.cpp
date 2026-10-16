//
// status_scene.cpp — Scene::drawStatus and its helpers (mode 5 "STATUS").
//
// The STATUS mode is a vertical stack of FIVE full-screen "pages", one per
// metric. Only the focused page fills the screen; dragging UP/DOWN scrolls
// statusScroll (a continuous page index) so the neighbors slide in from the
// top/bottom, and a horizontal drag still PIVOTS the focused 3D widget.
//
//   page 0  WiFi tower (mast + RSSI arcs + connecting sweep / off blink)
//   page 1  RSSI signal bars (5 3D bars + dBm readout)
//   page 2  IP address (4 octet cubes + big address readout)
//   page 3  Weather (rotating 3D globe + orbiting icon + temp/city/wind)
//   page 4  Uptime (big seconds + FPS + progress ring)
//
// Every page's 3D content is authored around the world origin and shifted
// along +Y by (page - statusScroll) * kSH, so the focused page is pinned to
// screen center and the slide is seamless. Labels are screen-space billboards
// positioned relative to each page's projected center, so they track the slide.
//
#include "scene.h"
#include "status_data.h"
#include "weather_client.h"
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
    e.drawNeonDot(x + 1, y + 2, r, g, b, 1);
}

void drawStr(Engine3D &e, int x, int y, const char *s, int r, int g, int b)
{
    for (int i = 0; s && s[i] && x + 4 < e.W; i++) {
        drawGlyph(e, x, y, s[i], r, g, b);
        x += 4;
    }
}

// Scaled 3x5 glyph: each font pixel becomes an SxS block. For big numbers.
void drawGlyphS(Engine3D &e, int x, int y, char c, int S, int r, int g, int b)
{
    for (int i = 0; i < kFontN; i++) {
        if (kFont[i].c != c) continue;
        for (int row = 0; row < 5; row++)
            for (int col = 0; col < 3; col++)
                if (kFont[i].r[row] & (1 << (2 - col))) {
                    int bx = x + col * S, by = y + row * S;
                    for (int yy = 0; yy < S; yy++)
                        for (int xx = 0; xx < S; xx++)
                            if (bx + xx >= 0 && by + yy >= 0 &&
                                bx + xx < e.W && by + yy < e.H)
                                e.addPixel(bx + xx, by + yy, r, g, b);
                }
        return;
    }
}
// Centered scaled string. Returns the total pixel width.
int drawStrSC(Engine3D &e, int cx, int y, const char *s, int S, int r, int g, int b)
{
    int n = 0; while (s && s[n]) n++;
    int w = n * (3 * S + S) - S;
    int x = cx - w / 2;
    for (int i = 0; i < n; i++) { drawGlyphS(e, x, y, s[i], S, r, g, b); x += 3 * S + S; }
    return w;
}

// 0..1 pulse envelope from a timestamp (ms) — fast attack, ~1.1s decay.
float pulseEnv(uint32_t tMs, uint32_t stamp)
{
    if (!stamp) return 0.0f;
    uint32_t d = tMs - stamp;
    if (d > 1100) return 0.0f;
    if (d < 70)  return (float)d / 70.0f;
    return 1.0f - (float)(d - 70) / 1030.0f;
}
float pulseScale(float env) { return 1.0f + 0.8f * env; }
int   pulseBoost(float env) { return (int)(100.0f * env); }

// --- STATUS paged layout (world space) ---
// Each page's 3D content is authored around the world origin, then shifted
// along +Y by (page - statusScroll) * kSH. kSH = one screen height in world
// units (CD*H/focal = 430*536/240 ~ 964), so the focused page (frac ~ 0) sits
// exactly at screen center and its neighbors fill the screen just off the
// top/bottom edge. The shift is applied BEFORE the global rotation, so a
// horizontal drag (ry) pivots the focused widget in place while a vertical
// scroll carries the pages up/down the screen.
const float kCD = 430.0f;   // camera distance (matches Scene::CAM_DIST region)
const float kSH = 964.0f;   // one screen height in world units

// Project a world point under rotation m to screen.
bool proj(Engine3D &e, const V3 &p, const Rot &m, int &sx, int &sy)
{
    V3 r;
    rotateMat(p.x, p.y, p.z, m, r);
    int d;
    return e.project(r.x, r.y, r.z + kCD, sx, sy, d);
}

// 3D segment with a 0..1 slide-fade (`vis`) multiplied into the intensity.
void seg3(Engine3D &e, const V3 &a, const V3 &b, const Rot &m,
          int r, int g, int bl, int inten, int core, int glow, float vis)
{
    int it = (int)(inten * vis);
    if (it < 8) return;
    e.renderSegmentMat(a, b, m, kCD, r, g, bl, it, core, glow);
}

// Slide visibility for a page at fractional offset `f` (0 = fully visible).
// Fades out over the outer 0.35 of a page so off-screen neighbors are culled.
float pageVis(float f)
{
    float a = fabsf(f);
    if (a >= 1.35f) return 0.0f;
    if (a > 1.0f) return (1.35f - a) / 0.35f;
    return 1.0f;
}

// ===========================================================================
// PAGE 0 — WiFi tower (full screen)
// ===========================================================================
void pageWiFi(Engine3D &e, const Rot &m, float oy, float t,
              uint32_t st, float lvl, float pulse, float vis)
{
    if (vis <= 0.0f) return;
    bool up = (st == WIFI_CONNECTED);
    bool conn = (st == WIFI_CONNECTING);
    int r = up ? 80 : 255, g = up ? 230 : 70, b = up ? 255 : 70;
    if (!up && ((millis() / 400) & 1)) { r = 120; g = 30; b = 30; }
    float s = pulseScale(pulse);
    int boost = pulseBoost(pulse);
    if (r + boost > 255) r = 255; if (g + boost > 255) g = 255; if (b + boost > 255) b = 255;

    float baseY = oy - 120.0f, tipY = oy + 130.0f * s;
    // Mast.
    seg3(e, {0, baseY, 0}, {0, tipY, 0}, m, r, g, b, 200 + boost, 3, 3, vis);
    // Base platform.
    seg3(e, {-44, baseY, 0}, {44, baseY, 0}, m, r, g, b, 130 + boost, 2, 1, vis);
    // Arcs: three concentric, lit by level.
    for (int a = 0; a < 3; a++) {
        float rad = 40.0f + a * 30.0f;
        float lit = (lvl * 3.0f > (float)a) ? 1.0f : 0.18f;
        int ir = (int)(r * lit), ig = (int)(g * lit), ib = (int)(b * lit);
        int inten = (int)(120 + 135.0f * lit) + boost;
        const int N = 22;
        for (int i = 0; i < N; i++) {
            float a0 = M_PI * (0.25f + 0.5f * (float)i / N);
            float a1 = M_PI * (0.25f + 0.5f * (float)(i + 1) / N);
            V3 pa = { rad * s * sinf(a0), tipY + rad * s * cosf(a0) - 10.0f, 0 };
            V3 pb = { rad * s * sinf(a1), tipY + rad * s * cosf(a1) - 10.0f, 0 };
            seg3(e, pa, pb, m, ir, ig, ib, inten, 2, a < 2 ? 1 : 0, vis);
        }
    }
    // Connecting / failed: a sweeping beam from the tip.
    if (conn || st == WIFI_FAILED) {
        float sw = fmodf(t * 0.004f, 2.0f * M_PI);
        seg3(e, {0, tipY, 0}, {54.0f * sinf(sw), tipY + 54.0f * cosf(sw), 0},
             m, r, g, b, 170 + boost, 2, 1, vis);
    }
    // Labels (screen space, relative to projected page center).
    int sx, sy;
    if (proj(e, {0, oy, 0}, m, sx, sy)) {
        drawStrSC(e, sx, sy - 210, "WIFI", 3, r, g, b);
        const char *stStr = up ? "CONNECTED" : (conn ? "CONNECTING" : "OFFLINE");
        drawStr(e, sx - (int)(strlen(stStr) * 2), sy + 195, stStr, r, g, b);
    }
}

// ===========================================================================
// PAGE 1 — RSSI signal bars (full screen)
// ===========================================================================
void pageRSSI(Engine3D &e, const Rot &m, float oy, float lvl,
              float pulse, float vis, int32_t rssi, uint32_t st)
{
    if (vis <= 0.0f) return;
    int lit = (int)(lvl * 5.0f + 0.5f);
    const int N = 5, bw = 24, gap = 18;
    float s = pulseScale(pulse);
    int boost = pulseBoost(pulse);
    float baseY = oy - 130.0f;
    for (int i = 0; i < N; i++) {
        float hgt = (70.0f + i * 55.0f) * s;
        bool on = (i < lit);
        int r = on ? (int)(150 - 110.0f * (float)i / N) : 40;
        int g = on ? (int)(90 + 155.0f * (float)i / N) : 40;
        int b = on ? 130 : 60;
        r += boost; if (r > 255) r = 255;
        g += boost; if (g > 255) g = 255;
        b += boost; if (b > 255) b = 255;
        float x = -(float)(N - 1) * (bw + gap) * 0.5f + (float)i * (bw + gap);
        V3 b0 = { x, baseY, 0 }, b1 = { x + bw, baseY, 0 };
        V3 t0 = { x, baseY + hgt, 0 }, t1 = { x + bw, baseY + hgt, 0 };
        int inten = (on ? 230 : 55) + boost;
        seg3(e, b0, b1, m, r, g, b, inten, 3, 1, vis);  // bottom
        seg3(e, t0, t1, m, r, g, b, inten, 3, 1, vis);  // top
        seg3(e, b0, t0, m, r, g, b, inten, 3, 0, vis);  // left
        seg3(e, b1, t1, m, r, g, b, inten, 3, 0, vis);  // right
    }
    // Baseline.
    seg3(e, {-(float)(N - 1) * (bw + gap) * 0.5f - 6, baseY, 0},
           { (float)(N - 1) * (bw + gap) * 0.5f + 6, baseY, 0},
         m, 120, 200, 255, 120 + boost, 2, 0, vis);
    int sx, sy;
    if (proj(e, {0, oy, 0}, m, sx, sy)) {
        drawStrSC(e, sx, sy - 215, "RSSI", 3, 120, 200, 255);
        char db[12];
        if (st == WIFI_CONNECTED && rssi) snprintf(db, sizeof(db), "%d dBm", (int)rssi);
        else snprintf(db, sizeof(db), "-- dBm");
        drawStr(e, sx - (int)(strlen(db) * 2), sy + 190, db, 120, 200, 255);
    }
}

// ===========================================================================
// PAGE 2 — IP address (full screen): 4 octet cubes + big readout
// ===========================================================================
void pageIP(Engine3D &e, const Rot &m, float oy, float t,
            uint32_t ip, float pulse, float vis)
{
    if (vis <= 0.0f) return;
    char ipbuf[20];
    statusIpToStr(ip, ipbuf);
    int boost = pulseBoost(pulse);
    float s = pulseScale(pulse);
    // 4 octet cubes in a row, gently bobbing.
    for (int o = 0; o < 4; o++) {
        float x = -78.0f + (float)o * 52.0f;
        float y = oy + 10.0f + 8.0f * sinf(t * 0.002f + o * 0.8f);
        float cs = 20.0f * s;
        V3 v[8] = {
            { x - cs, y - cs, -cs }, { x + cs, y - cs, -cs },
            { x + cs, y + cs, -cs }, { x - cs, y + cs, -cs },
            { x - cs, y - cs,  cs }, { x + cs, y - cs,  cs },
            { x + cs, y + cs,  cs }, { x - cs, y + cs,  cs } };
        const int E12[4][2] = { {0,1},{1,2},{2,3},{3,0} };
        const int E45[4][2] = { {4,5},{5,6},{6,7},{7,4} };
        const int C48[4][2] = { {0,4},{1,5},{2,6},{3,7} };
        int inten = 200 + boost;
        for (int i = 0; i < 4; i++) {
            seg3(e, v[E12[i][0]], v[E12[i][1]], m, 90, 230, 255, inten, 2, 1, vis);
            seg3(e, v[E45[i][0]], v[E45[i][1]], m, 90, 230, 255, inten, 2, 1, vis);
            seg3(e, v[C48[i][0]], v[C48[i][1]], m, 90, 230, 255, inten, 2, 1, vis);
        }
    }
    int sx, sy;
    if (proj(e, {0, oy, 0}, m, sx, sy)) {
        drawStrSC(e, sx, sy - 200, "IP", 3, 90, 230, 255);
        // Big address below the cubes.
        int S = ip ? 5 : 4;
        drawStrSC(e, sx, sy + 120, ipbuf, S, 120, 235, 255);
    }
}

// ===========================================================================
// Weather icon primitives (screen-space billboards, scalable by k).
// ===========================================================================
void iconSun(Engine3D &e, int cx, int cy, float t, int k, int inten)
{
    int R = 16 * k;
    int ir = (int)(255 * inten), ig = (int)(210 * inten), ib = (int)(90 * inten);
    e.drawNeonDot(cx, cy, ir, ig, ib, R);
    float spin = t * 0.001f;
    for (int i = 0; i < 8; i++) {
        float a = spin + (float)i * M_PI / 4.0f;
        int x0 = cx + (int)((R + 5 * k) * cosf(a));
        int y0 = cy + (int)((R + 5 * k) * sinf(a));
        int x1 = cx + (int)((R + 11 * k) * cosf(a));
        int y1 = cy + (int)((R + 11 * k) * sinf(a));
        e.drawNeonLine(x0, y0, x1, y1, ir, ig, ib, k, 1);
    }
}

void iconCloud(Engine3D &e, int cx, int cy, float drift, int k, int inten)
{
    int r = 11 * k;
    int ir = (int)(200 * inten), ig = (int)(215 * inten), ib = (int)(235 * inten);
    int d = (int)(drift * k);
    int base = cy + 4 * k;
    e.drawNeonDot(cx - r - 2 * k + d, base, ir, ig, ib, r);
    e.drawNeonDot(cx + r + 2 * k + d, base, ir, ig, ib, r);
    e.drawNeonDot(cx + d, base - r, ir, ig, ib, r + k);
    e.drawNeonDot(cx + d, base + 2 * k, ir, ig, ib, r);
}

void iconRain(Engine3D &e, int cx, int cy, float t, int k, int inten)
{
    int ir = (int)(90 * inten), ig = (int)(170 * inten), ib = (int)(255 * inten);
    for (int i = 0; i < 3; i++) {
        int dx = cx - 11 * k + i * 11 * k;
        float ph = fmodf(t * 0.006f + i * 0.5f, 1.0f);
        int y = cy + 12 * k + (int)(ph * 22 * k);
        e.drawNeonLine(dx, y, dx - 4 * k, y + 8 * k, ir, ig, ib, k, 0);
    }
}

void iconSnow(Engine3D &e, int cx, int cy, float t, int k, int inten)
{
    int ir = (int)(210 * inten), ig = (int)(235 * inten), ib = (int)(255 * inten);
    for (int i = 0; i < 3; i++) {
        int dx = cx - 11 * k + i * 11 * k;
        float ph = fmodf(t * 0.004f + i * 0.5f, 1.0f);
        int y = cy + 12 * k + (int)(ph * 22 * k);
        for (int a = 0; a < 6; a++) {
            float ang = (float)a * M_PI / 3.0f;
            int x2 = dx + (int)(6 * k * cosf(ang));
            int y2 = y + (int)(6 * k * sinf(ang));
            e.drawNeonLine(dx, y, x2, y2, ir, ig, ib, k, 0);
        }
    }
}

void iconLightning(Engine3D &e, int cx, int cy, float t, int k, int inten)
{
    uint32_t cyc = (uint32_t)t % 1200;
    int flash = (cyc < 70 || (cyc > 240 && cyc < 300)) ? 255 : 40;
    flash = (int)(flash * inten / 100);
    int ir = flash, ig = (int)(flash * 0.9f), ib = (int)(flash * 0.4f);
    int x0 = cx + 2 * k, y0 = cy + 8 * k;
    int pts[5][2] = {
        { x0, y0 }, { x0 - 9 * k, y0 + 12 * k }, { x0 - 2 * k, y0 + 14 * k },
        { x0 - 11 * k, y0 + 28 * k }, { x0 - 4 * k, y0 + 26 * k } };
    for (int i = 0; i < 4; i++)
        e.drawNeonLine(pts[i][0], pts[i][1], pts[i+1][0], pts[i+1][1], ir, ig, ib, k, 1);
}

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

// Composite weather icon at (cx,cy), scale k (1x ~ 40px, 2x ~ 80px).
// inten is 0..100.
void drawWeatherIcon(Engine3D &e, int cx, int cy, float t, int k, WIcon kind)
{
    float drift = 3.0f * sinf(t * 0.0015f);
    int inten = 100;
    switch (kind) {
        case WI_SUN:       iconSun(e, cx, cy, t, k, inten); break;
        case WI_SUN_CLOUD: iconSun(e, cx - 8 * k, cy - 8 * k, t, k, 80);
                           iconCloud(e, cx + 4 * k, cy + 4 * k, drift, k, inten); break;
        case WI_CLOUD:     iconCloud(e, cx, cy, drift, k, inten); break;
        case WI_FOG:       iconCloud(e, cx, cy - 2 * k, drift, k, 80);
                           e.drawNeonLine(cx - 18 * k, cy + 12 * k, cx + 18 * k, cy + 12 * k, 150, 160, 180, k, 1);
                           e.drawNeonLine(cx - 14 * k, cy + 16 * k, cx + 14 * k, cy + 16 * k, 120, 130, 150, k, 0);
                           break;
        case WI_RAIN:      iconCloud(e, cx, cy - 5 * k, drift, k, inten); iconRain(e, cx, cy - 5 * k, t, k, inten); break;
        case WI_SNOW:      iconCloud(e, cx, cy - 5 * k, drift, k, inten); iconSnow(e, cx, cy - 5 * k, t, k, inten); break;
        case WI_STORM:     iconCloud(e, cx, cy - 6 * k, drift, k, inten); iconLightning(e, cx, cy - 6 * k, t, k, inten); break;
        case WI_NONE:
        default: break;
    }
}

// ===========================================================================
// PAGE 3 — Weather (full screen): rotating globe + big temp + icon + meta
// ===========================================================================
void drawRing3D(Engine3D &e, const V3 &c, float r, const V3 &u, const V3 &v,
                const Rot &m, int ir, int ig, int ib, int inten)
{
    const int N = 24;
    for (int i = 0; i < N; i++) {
        float a0 = 2.0f * M_PI * (float)i / N;
        float a1 = 2.0f * M_PI * (float)(i + 1) / N;
        V3 pa = { c.x + r * (u.x * cosf(a0) + v.x * sinf(a0)),
                  c.y + r * (u.y * cosf(a0) + v.y * sinf(a0)),
                  c.z + r * (u.z * cosf(a0) + v.z * sinf(a0)) };
        V3 pb = { c.x + r * (u.x * cosf(a1) + v.x * sinf(a1)),
                  c.y + r * (u.y * cosf(a1) + v.y * sinf(a1)),
                  c.z + r * (u.z * cosf(a1) + v.z * sinf(a1)) };
        e.renderSegmentMat(pa, pb, m, kCD, ir, ig, ib, inten, 1, 0);
    }
}

void pageWeather(Engine3D &e, const Rot &m, float oy, float t,
                 uint32_t wvalid, uint32_t wcode, int32_t temp10,
                 int32_t wind10, const char *city, float pulse, float vis)
{
    if (vis <= 0.0f) return;
    const float R = 95.0f;             // big globe, upper half of the page
    V3 c = { 0.0f, oy - 40.0f, 0.0f }; // globe center (world y)
    const float spin = t * 0.0006f;
    int inten = (int)((wvalid ? 200 : 70) * vis);
    int gr = wvalid ? 70 : 60, gg = wvalid ? 150 : 70, gb = wvalid ? 200 : 90;
    // Rotating local frame: tilt ~0.4 rad, spin about Y.
    Rot mLocal = makeRot(0.4f, spin, 0.0f);
    V3 u = { 1, 0, 0 }, v = { 0, 0, 1 }, ur, vr;
    rotateMat(u.x, u.y, u.z, mLocal, ur);
    rotateMat(v.x, v.y, v.z, mLocal, vr);
    // Longitude rings (4) + equator.
    for (int i = 0; i < 4; i++) {
        float a = (float)i * M_PI / 4.0f;
        V3 uu = { cosf(a), 0.0f, sinf(a) }, uuR;
        rotateMat(uu.x, uu.y, uu.z, mLocal, uuR);
        drawRing3D(e, c, R, uuR, vr, m, gr, gg, gb, inten);
    }
    // Latitude rings + equator.
    const float lats[5] = { -0.7f, -0.35f, 0.0f, 0.35f, 0.7f };
    for (int i = 0; i < 5; i++) {
        float ph = lats[i];
        V3 cc = { c.x, c.y + R * sinf(ph), c.z };
        drawRing3D(e, cc, R * cosf(ph), ur, vr, m, gr, gg, gb, (int)(inten * 0.8f));
    }
    e.renderPointMat(c, m, kCD, gr, gg, gb, inten, 1);
}

// (continuation) Screen-space labels for the weather page, relative to the
// projected globe center so they slide with the page.
void pageWeatherLabels(Engine3D &e, const Rot &m, float oy, float t,
                       uint32_t wvalid, uint32_t wcode, int32_t temp10,
                       int32_t wind10, const char *city, float vis)
{
    if (vis <= 0.0f) return;
    V3 c = { 0.0f, oy - 40.0f, 0.0f };
    int sx, sy;
    if (!proj(e, c, m, sx, sy)) return;
    // Title. Other pages sit at (page center - 210); this frame's origin is
    // the globe center (oy - 40), so the same height is sy - 170 here.
    drawStrSC(e, sx, sy - 170, "WEATHER", 3, 120, 200, 255);
    if (wvalid) {
        // Big temperature readout.
        char tmp[8];
        snprintf(tmp, sizeof(tmp), "%d.%d", (int)(temp10 / 10),
                 (int)((temp10 < 0 ? -temp10 : temp10) % 10));
        int S = 9;
        drawStrSC(e, sx, sy + 45, tmp, S, 120, 230, 255);
        e.drawNeonDot(sx + 70, sy + 55, 255, 120, 90, 7); // degree dot
        // Condition label.
        const char *cond = WeatherClient::wmoName(wcode);
        drawStr(e, sx - (int)(strlen(cond) * 2), sy + 130, cond, 90, 200, 240);
        // Wind + city.
        char wind[12];
        snprintf(wind, sizeof(wind), "W %d.%d", (int)(wind10 / 10),
                 (int)((wind10 < 0 ? -wind10 : wind10) % 10));
        drawStr(e, sx - (int)(strlen(wind) * 2), sy + 158, wind, 120, 180, 220);
        char up[16];
        for (int i = 0; i < 12 && city[i]; i++) up[i] = (char)toupper((unsigned char)city[i]);
        up[12] = 0;
        drawStr(e, sx - (int)(strlen(up) * 2), sy + 183, up, 90, 180, 220);
        // Big orbiting weather icon, lower area of the page.
        float oa = t * 0.0009f;
        int ix = (int)(e.W * 0.5 + 150.0f * cosf(oa));
        int iy = (int)(sy + 235 + 24.0f * sinf(oa * 2.0f));
        drawWeatherIcon(e, ix, iy, t, 2, iconForCode(wcode));
    } else {
        drawStr(e, sx - 24, sy + 110, "NO DATA", 120, 120, 140);
        drawStr(e, sx - 30, sy + 138, "WAIT FOR WIFI", 90, 90, 110);
    }
}

// ===========================================================================
// PAGE 4 — Uptime (full screen): big seconds + FPS + circular progress ring
// ===========================================================================
void pageUptime(Engine3D &e, const Rot &m, float oy, float t,
                uint32_t upt, uint32_t fps, float pulse, float vis)
{
    if (vis <= 0.0f) return;
    int boost = pulseBoost(pulse);
    int sx, sy;
    if (!proj(e, {0, oy, 0}, m, sx, sy)) return;
    // Title.
    drawStrSC(e, sx, sy - 210, "UPTIME", 3, 255, 180, 90);
    // Big seconds readout, center.
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)upt);
    int S = 12;
    drawStrSC(e, sx, sy - 30, buf, S, 255, 180, 90 + boost);
    e.addPixel(sx, sy - 30, 255, 220, 160);
    // Circular progress ring (uptime mod 3600 / 3600).
    float frac = (float)(upt % 3600) / 3600.0f;
    int rad = 150;
    int N = 64;
    int filled = (int)(frac * N + 0.5f);
    for (int i = 0; i < N; i++) {
        float a0 = -M_PI / 2.0f + 2.0f * M_PI * (float)i / N;
        float a1 = -M_PI / 2.0f + 2.0f * M_PI * ((float)i + 0.7f) / N;
        int x0 = sx + (int)(rad * cosf(a0)), y0 = sy + 150 + (int)(rad * sinf(a0));
        int x1 = sx + (int)(rad * cosf(a1)), y1 = sy + 150 + (int)(rad * sinf(a1));
        bool on = (i < filled);
        int r = 255, g = (int)(120 + 80.0f * (float)filled / N) + boost, b = 60 + boost;
        if (g > 255) g = 255; if (b > 255) b = 255;
        if (on) e.drawNeonLine(x0, y0, x1, y1, r, g, b, 3, 1);
        else    e.drawNeonLine(x0, y0, x1, y1, 40, 30, 30, 1, 0);
    }
    // FPS at the bottom.
    char fb[12];
    snprintf(fb, sizeof(fb), "FPS %u", (unsigned)fps);
    drawStr(e, sx - (int)(strlen(fb) * 2), sy + 320, fb, 255, 220, 120);
    (void)t; (void)m;
}

} // namespace

// ===========================================================================
// Scene::drawStatus — dispatch the focused page (and sliding neighbors)
// ===========================================================================
void Scene::drawStatus(float rx, float ry)
{
    // Snapshot the shared status (brief mutex) so the whole frame is coherent.
    uint32_t st, ip, wcode, wvalid, upt, fps;
    int32_t rssi, temp10, wind10;
    char city[16];
    statusLock();
    st = g_status.state; ip = g_status.ip; rssi = g_status.rssi;
    wcode = g_status.wcode; wvalid = g_status.w_valid;
    temp10 = g_status.temp_x10; wind10 = g_status.wind_x10;
    upt = g_status.uptime_s; fps = g_status.fps;
    memcpy(city, g_status.city, sizeof(city));
    city[sizeof(city) - 1] = 0;
    statusUnlock();

    statusEase(); // advance the eased page-scroll (snap / fling settle)
    const float t = spinT;
    // Dampened user orbit: horizontal drag (ry) pivots the focused widget;
    // vertical scroll is handled by the per-page world offset below.
    Rot mG = makeRot(rx * 0.5f, ry * 0.5f, 0.0f);
    const float sc = statusScroll;

    // Pulse envelopes per page.
    float p0 = pulseEnv(millis(), pulseMs[0]);
    float p1 = pulseEnv(millis(), pulseMs[1]);
    float p2 = pulseEnv(millis(), pulseMs[2]);
    float p3 = pulseEnv(millis(), pulseMs[3]);
    float p4 = pulseEnv(millis(), pulseMs[4]);

    // RSSI -> 0..1 level (map -90..-35 dBm).
    float lvl = 0.0f;
    if (st == WIFI_CONNECTED && rssi) {
        lvl = (float)(rssi + 90) / 55.0f;
        if (lvl < 0) lvl = 0; if (lvl > 1) lvl = 1;
    }

    // Render each visible page (offset by its distance from the scroll value).
    for (int page = 0; page < STATUS_PAGES; page++) {
        float f = (float)page - sc;
        if (fabsf(f) >= 1.35f) continue;       // fully off-screen
        float vis = pageVis(f);
        if (vis <= 0.0f) continue;
        float oy = f * kSH;                    // world offset for this page
        switch (page) {
            case 0: pageWiFi(e_, mG, oy, t, st, lvl, p0, vis); break;
            case 1: pageRSSI(e_, mG, oy, lvl, p1, vis, rssi, st); break;
            case 2: pageIP(e_, mG, oy, t, ip, p2, vis); break;
            case 3: pageWeather(e_, mG, oy, t, wvalid, wcode, temp10, wind10, city, p3, vis);
                    pageWeatherLabels(e_, mG, oy, t, wvalid, wcode, temp10, wind10, city, vis);
                    break;
            case 4: pageUptime(e_, mG, oy, t, upt, fps, p4, vis); break;
        }
    }
}
