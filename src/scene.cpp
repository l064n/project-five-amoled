#include "scene.h"
#include "status_data.h"
#include <Arduino.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Palette set — high-contrast neon on AMOLED black.
const Scene::Palette Scene::kPalettes[Scene::NUM_PALETTES] = {
    { "SUNSET", 255, 64, 180,  255, 150, 220, 255, 40, 140 },  // magenta
    { "CYBER",  64, 230, 255, 255, 240, 120, 120, 90, 255  },  // cyan
    { "LIME",   180, 255, 40, 255, 220, 90,  255, 160, 60  },  // green
    { "SOLAR",  255, 170, 40, 255, 120, 200, 255, 70, 60   },  // orange
};

void Scene::begin()
{
    // ---- KNOTS: Hero torus knot (p=2, q=3) ----
    const float p = 2.0f, q = 3.0f;
    const float R = 1.0f, r = 0.42f;
    const float S = 92.0f;
    for (int i = 0; i < KNOT_N; i++) {
        float t = 2.0f * M_PI * (float)i / (float)KNOT_N;
        float ring = R + r * cosf(q * t);
        knot_[i].x = ring * cosf(p * t) * S;
        knot_[i].y = ring * sinf(p * t) * S;
        knot_[i].z = r * sinf(q * t) * S;
        knotHue_[i] = (float)i / (float)KNOT_N * 360.0f;
    }

    // ---- KNOTS: Secondary knot (p=1, q=2), counter-rotating ----
    const float p2 = 1.0f, q2 = 2.0f;
    const float S2 = 78.0f;
    for (int i = 0; i < KNOT2_N; i++) {
        float t = 2.0f * M_PI * (float)i / (float)KNOT2_N;
        float ring = R + r * cosf(q2 * t);
        knot2_[i].x = ring * cosf(p2 * t) * S2;
        knot2_[i].y = ring * sinf(p2 * t) * S2;
        knot2_[i].z = r * sinf(q2 * t) * S2;
        knot2Hue_[i] = (float)i / (float)KNOT2_N * 360.0f;
    }

    // ---- TESSERACT: 16 vertices of a 4D hypercube, scaled ----
    for (int i = 0; i < TESS_N; i++) {
        tess_[i][0] = (i & 1) ? 60.0f : -60.0f;
        tess_[i][1] = (i & 2) ? 60.0f : -60.0f;
        tess_[i][2] = (i & 4) ? 60.0f : -60.0f;
        tess_[i][3] = (i & 8) ? 60.0f : -60.0f;
    }

    // ---- MOBIUS: wireframe ring (NV) x segments (NU) ----
    // (Named MOB_R/MOB_W: 'MR' collides with an Xtensa specreg.h macro.)
    // A Möbius band: centerline circle + a cross-section that does a half
    // twist over the loop. NV rings span the strip's width (w = -1, 0, +1).
    const float MOB_R = 70.0f, MOB_W = 26.0f;   // centerline radius, half-width
    for (int v = 0; v < MOB_NV; v++) {
        float w = (float)v - (float)(MOB_NV - 1) * 0.5f; // -1, 0, +1
        for (int u = 0; u < MOB_NU; u++) {
            float th = 2.0f * M_PI * (float)u / (float)MOB_NU;
            float phi = 0.5f * th;            // half twist over the loop
            // Local frame: radial n=(cos,0,sin), vertical b=(0,1,0).
            // Width direction = cos(phi)*n + sin(phi)*b (twists with phi).
            float cx = MOB_R * cosf(th);
            float cz = MOB_R * sinf(th);
            float dx = MOB_W * cosf(phi) * cosf(th);
            float dy = MOB_W * sinf(phi);
            float dz = MOB_W * cosf(phi) * sinf(th);
            V3 &pt = mob_[v * MOB_NU + u];
            pt.x = cx + w * dx; pt.y = w * dy; pt.z = cz + w * dz;
        }
    }

    // ---- HELIX: two counter-strands of a double helix ----
    const float HR = 46.0f, HZ = 150.0f, Hturns = 2.2f;
    for (int i = 0; i < HEL_N; i++) {
        float t = (float)i / (float)HEL_N;
        float a = 2.0f * M_PI * Hturns * t;
        float y = (t - 0.5f) * HZ;
        helA_[i].x = HR * cosf(a);
        helA_[i].y = y;
        helA_[i].z = HR * sinf(a);
        helB_[i].x = HR * cosf(a + M_PI);
        helB_[i].y = y;
        helB_[i].z = HR * sinf(a + M_PI);
    }

    // ---- VORTEX: spiral galaxy disc (param t: 0 center .. 1 rim) ----
    srand(0xBEEF);
    for (int i = 0; i < VOR_N; i++) {
        float t = powf((float)rand() / (float)RAND_MAX, 0.6f); // bias to rim
        vorT_[i] = t;
        float arm = 2.0f * M_PI * (2.2f * t) + (rand() & 1) * M_PI; // 2 arms
        float jitter = (float)(rand() % 200) / 100.0f - 1.0f; // -1..1
        float rad = t * 130.0f + jitter * 14.0f;
        vor_[i].x = rad * cosf(arm);
        vor_[i].z = rad * sinf(arm);
        vor_[i].y = jitter * (18.0f - 10.0f * t); // thinner at the rim
    }

    // ---- Stars: random directions on a far sphere (shared) ----
    srand(0xC0FFEE);
    for (int i = 0; i < STARS; i++) {
        float u = (float)(rand() / (float)RAND_MAX);
        float v = (float)(rand() / (float)RAND_MAX);
        float theta = 2.0f * M_PI * u;
        float phi = acosf(2.0f * v - 1.0f);
        float rad = 1400.0f + (float)(rand() % 800);
        stars_[i].x = rad * sinf(phi) * cosf(theta);
        stars_[i].y = rad * cosf(phi);
        stars_[i].z = rad * sinf(phi) * sinf(theta);
        starDim_[i] = (uint8_t)(60 + (rand() % 195));
        starPhase_[i] = (uint8_t)(rand() % 256);
    }
}

static int64_t g_sec[5] = {0,0,0,0,0}; // stars, grid, knot1, knot2, other

void Scene::render(float rx, float ry, uint32_t t_ms)
{
    int64_t T0 = esp_timer_get_time();
    e_.clear(0x0000); // black — lets neon additive glow shine

    // Scaled animation clock (long-press speed: 1x / 3x / paused).
    if (lastT == 0) lastT = t_ms;
    spinT += ((float)(t_ms - lastT)) * spinMul();
    lastT = t_ms;

    const Palette &P = pal();
    float t = spinT;

    // ---------- Starfield (shared by every mode) ----------
    int64_t T1 = esp_timer_get_time();
    Rot mStar = makeRot(rx, ry, 0.0f);
    for (int i = 0; i < STARS; i++) {
        float tw = 0.5f + 0.5f * sinf(0.004f * t + (float)starPhase_[i]);
        int dim = (int)(starDim_[i] * (0.35f + 0.65f * tw));
        e_.renderPointMat(stars_[i], mStar, 1600.0f,
                          P.star_r, P.star_g, P.star_b, dim, 1);
    }
    int64_t T2 = esp_timer_get_time();

    // ---------- Dispatch to the active scene ----------
    switch (mode) {
        case 1: drawTesseract(rx, ry); break;
        case 2: drawMobius(rx, ry);    break;
        case 3: drawHelix(rx, ry);     break;
        case 4: drawVortex(rx, ry);    break;
        case 5: drawStatus(rx, ry);    break;
        default: drawKnots(rx, ry);    break;
    }
    int64_t T5 = esp_timer_get_time();

    g_sec[0] += (T2 - T1);            // stars
    g_sec[1] += (T5 - T2);            // active scene
    g_sec[2] += (T5 - T0) - (T2 - T1) - (T5 - T2); // clear + gaps
}

// Mode 0 — dual torus knots + synthwave grid + traveling wave.
void Scene::drawKnots(float rx, float ry)
{
    const Palette &P = pal();
    float t = spinT;

    // ---------- Synthwave grid floor (axis-aligned, scrolling + wave) ----------
    const float GY = -150.0f;   // ground plane height (below center)
    const float XW = 900.0f;    // half-width of the grid
    const float Z0 = 60.0f;     // near
    const float Z1 = 3200.0f;   // far
    // Traveling brightness wave along depth (makes the floor "pulse").
    float waveT = 0.0009f * t;

    // Depth lines (run from near to far at fixed x positions). 1px core-only:
    // the additive halo was the scene's biggest cost and grid reads fine crisp.
    for (int k = -6; k <= 6; k++) {
        float gx = (float)k * 120.0f;
        int x0, y0, d0, x1, y1, d1;
        if (!e_.project(gx, GY, Z0, x0, y0, d0)) continue;
        if (!e_.project(gx, GY, Z1, x1, y1, d1)) continue;
        float fog = 1.0f - (float)(d0 + d1) / 2.0f / 3200.0f;
        if (fog < 0.08f) fog = 0.08f;
        int ir = (int)(P.grid_r * fog), ig = (int)(P.grid_g * fog), ib = (int)(P.grid_b * fog);
        e_.drawNeonLine(x0, y0, x1, y1, ir, ig, ib, 1, 0);
    }

    // Horizontal lines: geometric spacing (true perspective) scrolling toward
    // the viewer. Fewer + shorter lines than a uniform grid at the same look.
    float phase = fmodf(t * 0.00035f, 1.0f);
    float fr = 1.0f - phase;
    for (int i = 0; i < 16; i++) {
        float z = Z0 * powf(1.35f, (float)i + fr);
        if (z >= Z1) break;
        if (z < Z0) continue;
        int x0, y0, d0, x1, y1, d1;
        if (!e_.project(-XW, GY, z, x0, y0, d0)) continue;
        if (!e_.project( XW, GY, z, x1, y1, d1)) continue;
        float fog = 1.0f - (float)z / 3400.0f;
        if (fog < 0.08f) fog = 0.08f;
        float wv = 0.55f + 0.45f * sinf(waveT - (float)z * 0.004f);
        int ir = (int)(P.grid_r * fog * wv), ig = (int)(P.grid_g * fog * wv), ib = (int)(P.grid_b * fog * wv);
        e_.drawNeonLine(x0, y0, x1, y1, ir, ig, ib, 1, 0);
    }
    // ---------- Hero torus knot (hue-cycling + traveling pulse) ----------
    float spin = t * 0.00045f;
    float bob = 0.12f * sinf(0.0012f * t);
    float knotRy = ry + spin;
    float knotRx = rx + 0.45f + bob;   // fixed tilt + gentle rock
    Rot m1 = makeRot(knotRx, knotRy, 0.0f);   // build once, reuse
    float hs = hueShift((uint32_t)t);
    // A bright "comet" pulse traveling along the loop.
    float pulsePos = fmodf(t * 0.00025f, 1.0f) * (float)KNOT_N;
    for (int i = 0; i < KNOT_N; i++) {
        float hue = knotHue_[i] + hs;
        uint16_t c16 = e_.hsl565(hue, 1.0f, 1.0f);
        int r = (c16 >> 11) & 0x1F, g = (c16 >> 5) & 0x3F, b = c16 & 0x1F;
        r <<= 3; g <<= 2; b <<= 3;  // back to 8-bit
        // Pulse: brighten + thicken near the comet head (wrap distance).
        float dd = (float)i - pulsePos;
        if (dd < 0) dd += (float)KNOT_N;
        int core = 1, glow = 1, inten = 255;   // thin halo; comet head gets a fat one
        if (dd < 24.0f) { inten = 255; core = 2; glow = 3; }
        else if (dd < 60.0f) { inten = 190; }
        else { inten = 150; }
        e_.renderSegmentMat(knot_[i], knot_[(i + 1) % KNOT_N], m1,
                            CAM_DIST, r, g, b, inten, core, glow);
    }
    // ---------- Secondary knot (thinner, counter-rotating, dimmer) ----------
    float spin2 = t * -0.0006f;
    float k2Ry = ry + spin2;
    float k2Rx = rx + 0.75f - bob;   // counter-phase rock
    Rot m2 = makeRot(k2Rx, k2Ry, 0.0f);   // build once, reuse
    for (int i = 0; i < KNOT2_N; i++) {
        float hue = knot2Hue_[i] + hs + 180.0f; // offset hue for contrast
        uint16_t c16 = e_.hsl565(hue, 0.85f, 0.85f);
        int r = (((c16 >> 11) & 0x1F) << 3);
        int g = (((c16 >> 5) & 0x3F) << 2);
        int b = ((c16 & 0x1F) << 3);
        e_.renderSegmentMat(knot2_[i], knot2_[(i + 1) % KNOT2_N], m2,
                            CAM_DIST, r, g, b, 120, 1, 1);
    }
}

// Mode 1 — 4D hypercube (tesseract). Two-plane 4D rotation, projected 4D->3D.
void Scene::drawTesseract(float rx, float ry)
{
    const float t = spinT;
    // Two independent 4D rotation planes: xw and yz, at different rates.
    float aw = 0.0007f * t, az = 0.0009f * t;
    float cw = cosf(aw), sw = sinf(aw);
    float cz = cosf(az), sz = sinf(az);
    // 4D perspective: world scale = K / (pers - w2). w2 in [-85,85], pers=150
    // keeps the denominator positive; nearer w-axis corners project bigger.
    const float pers = 150.0f, K = 180.0f;
    V3 wpt[TESS_N];
    for (int i = 0; i < TESS_N; i++) {
        float x = tess_[i][0], y = tess_[i][1], z = tess_[i][2], w = tess_[i][3];
        // xw-plane rotation
        float x2 = cw * x - sw * w;
        float w2 = sw * x + cw * w;
        // yz-plane rotation
        float y2 = cz * y - sz * z;
        float z2 = sz * y + cz * z;
        float s = K / (pers - w2);
        wpt[i].x = x2 * s; wpt[i].y = y2 * s; wpt[i].z = z2 * s;
    }
    // 32 edges: connect each vertex to those differing in exactly one bit.
    Rot m = makeRot(rx, ry, 0.0f);
    float hs = hueShift((uint32_t)t);
    for (int i = 0; i < TESS_N; i++) {
        for (int bit = 0; bit < 4; bit++) {
            int j = i ^ (1 << bit);
            if (j <= i) continue;
            float hue = fmodf(hs + bit * 80.0f, 360.0f);
            uint16_t c16 = e_.hsl565(hue, 1.0f, 1.0f);
            int r = ((c16 >> 11) & 0x1F) << 3;
            int g = ((c16 >> 5) & 0x3F) << 2;
            int b = (c16 & 0x1F) << 3;
            e_.renderSegmentMat(wpt[i], wpt[j], m, CAM_DIST, r, g, b, 200, 1, 1);
        }
    }
}

// Mode 2 — Möbius strip wireframe (hue gradient along the loop + pulse).
void Scene::drawMobius(float rx, float ry)
{
    const float t = spinT;
    float spin = t * 0.0006f;
    float bob = 0.1f * sinf(0.001f * t);
    Rot m = makeRot(rx + 0.5f + bob, ry + spin, 0.0f);
    float hs = hueShift((uint32_t)t);
    // A pulse traveling along the u-direction of the strip.
    float pulseU = fmodf(t * 0.0004f, 1.0f) * (float)MOB_NU;
    for (int v = 0; v < MOB_NV; v++) {
        for (int u = 0; u < MOB_NU; u++) {
            int a = v * MOB_NU + u;
            int b = v * MOB_NU + (u + 1) % MOB_NU;   // along u
            // Hue varies with u (around the loop) + palette offset.
            float hue = fmodf(hs + 360.0f * (float)u / (float)MOB_NU, 360.0f);
            uint16_t c16 = e_.hsl565(hue, 0.9f, 0.9f);
            int r = ((c16 >> 11) & 0x1F) << 3;
            int g = ((c16 >> 5) & 0x3F) << 2;
            int b2 = (c16 & 0x1F) << 3;
            // Pulse brightens segments near the head.
            float dd = (float)u - pulseU;
            if (dd < 0) dd += (float)MOB_NU;
            int inten = 140;
            int core = 1, glow = 0;
            if (dd < 12.0f) { inten = 255; core = 2; glow = 2; }
            else if (dd < 30.0f) { inten = 200; }
            e_.renderSegmentMat(mob_[a], mob_[b], m, CAM_DIST, r, g, b2, inten, core, glow);
        }
    }
    // Cross segments (v-direction) every 8th u so the strip reads as a band.
    for (int u = 0; u < MOB_NU; u += 8) {
        float hue = fmodf(hs + 360.0f * (float)u / (float)MOB_NU, 360.0f);
        uint16_t c16 = e_.hsl565(hue, 0.7f, 0.7f);
        int r = ((c16 >> 11) & 0x1F) << 3;
        int g = ((c16 >> 5) & 0x3F) << 2;
        int b2 = (c16 & 0x1F) << 3;
        for (int v = 0; v < MOB_NV - 1; v++) {
            e_.renderSegmentMat(mob_[v * MOB_NU + u], mob_[(v + 1) * MOB_NU + u],
                                m, CAM_DIST, r, g, b2, 90, 1, 0);
        }
    }
}

// Mode 3 — double helix with cross rungs.
void Scene::drawHelix(float rx, float ry)
{
    const float t = spinT;
    const Palette &P = pal();
    float spin = t * 0.0008f;
    Rot m = makeRot(rx + 0.2f, ry + spin, 0.0f);
    float hs = hueShift((uint32_t)t);
    // Pulse traveling along the helix.
    float pulseP = fmodf(t * 0.0003f, 1.0f) * (float)HEL_N;
    for (int i = 0; i < HEL_N; i++) {
        // Hue varies along the strand (A = one hue band, B = offset).
        float hueA = fmodf(hs + 360.0f * (float)i / (float)HEL_N, 360.0f);
        float hueB = fmodf(hueA + 180.0f, 360.0f);
        uint16_t cA = e_.hsl565(hueA, 1.0f, 1.0f);
        uint16_t cB = e_.hsl565(hueB, 1.0f, 1.0f);
        int rA = ((cA >> 11) & 0x1F) << 3, gA = ((cA >> 5) & 0x3F) << 2, bA = (cA & 0x1F) << 3;
        int rB = ((cB >> 11) & 0x1F) << 3, gB = ((cB >> 5) & 0x3F) << 2, bB = (cB & 0x1F) << 3;
        float dd = (float)i - pulseP;
        if (dd < 0) dd += (float)HEL_N;
        int inten = 150; int core = 1, glow = 0;
        if (dd < 12.0f) { inten = 255; core = 2; glow = 2; }
        else if (dd < 30.0f) { inten = 200; }
        // Two strands.
        e_.renderSegmentMat(helA_[i], helA_[(i + 1) % HEL_N], m, CAM_DIST, rA, gA, bA, inten, core, glow);
        e_.renderSegmentMat(helB_[i], helB_[(i + 1) % HEL_N], m, CAM_DIST, rB, gB, bB, inten, core, glow);
        // Cross rung every 4th segment.
        if (i % 4 == 0) {
            e_.renderSegmentMat(helA_[i], helB_[i], m, CAM_DIST,
                                P.line_r, P.line_g, P.line_b, 90, 1, 0);
        }
    }
}

// Mode 4 — swirling particle galaxy (spiral disc, hue by radius).
void Scene::drawVortex(float rx, float ry)
{
    const float t = spinT;
    float spin = t * 0.0005f;
    Rot m = makeRot(rx + 0.7f, ry + spin, 0.0f);
    float hs = hueShift((uint32_t)t);
    for (int i = 0; i < VOR_N; i++) {
        // Hue by radius: center = warm, rim = cool.
        float hue = fmodf(hs + vorT_[i] * 180.0f, 360.0f);
        // Twinkle by radius (rim twinkles faster).
        float tw = 0.5f + 0.5f * sinf(0.003f * t + vorT_[i] * 6.0f);
        int dim = (int)(120 + 135.0f * tw * vorT_[i]);
        uint16_t c16 = e_.hsl565(hue, 0.9f, 0.85f);
        int r = ((c16 >> 11) & 0x1F) << 3;
        int g = ((c16 >> 5) & 0x3F) << 2;
        int b = (c16 & 0x1F) << 3;
        e_.renderPointMat(vor_[i], m, CAM_DIST, r, g, b, dim, 1 + (int)(vorT_[i] * 2));
    }
}

void Scene::flushPerf(uint32_t n) {
    if (n == 0) return;
    Serial.printf("[sec] stars=%d scene=%d clr=%d ms  [mode=%d %s speed=%s]\n",
                  (int)(g_sec[0] / n / 1000),
                  (int)(g_sec[1] / n / 1000),
                  (int)(g_sec[2] / n / 1000),
                  mode, modeName(), speedName());
    for (int i = 0; i < 3; i++) g_sec[i] = 0;
}

void Scene::setZoom(float z) {
    if (z < ZOOM_OVERVIEW) z = ZOOM_OVERVIEW;
    if (z > 5.0f) z = 5.0f;
    zT = z;
    if (z <= ZOOM_OVERVIEW + 0.05f) { statusFocus = -1; cxT = cyT = czT = 0.0f; }
}
void Scene::focusWidget(int i, float z) {
    if (i < 0 || i > 4) return;
    statusFocus = i;
    setZoom(z);
    // Lock the camera center onto the widget's world anchor; smoothCam()
    // eases there. It stays pinned to screen-center even while you orbit.
    cxT = widgetAnchorW[i].x;
    cyT = widgetAnchorW[i].y;
    czT = widgetAnchorW[i].z;
}
void Scene::resetCam() {
    statusFocus = -1; zT = ZOOM_OVERVIEW; cxT = cyT = czT = 0.0f;
}
void Scene::smoothCam() {
    static uint32_t lastCamMs = 0;
    uint32_t now = millis();
    float dt = (lastCamMs ? (float)(now - lastCamMs) : 16.0f);
    lastCamMs = now;
    if (dt < 0.0f) dt = 16.0f;
    if (dt > 100.0f) dt = 100.0f;
    float a = 1.0f - expf(-dt / 110.0f); // ~95% settled in ~330 ms
    zCur  += (zT  - zCur)  * a;
    cxCur += (cxT - cxCur) * a;
    cyCur += (cyT - cyCur) * a;
    czCur += (czT - czCur) * a;
    // Snap when settled to avoid endless sub-pixel drift.
    if (fabsf(zCur - zT)   < 0.002f) zCur   = zT;
    if (fabsf(cxCur - cxT) < 0.05f)  cxCur  = cxT;
    if (fabsf(cyCur - cyT) < 0.05f)  cyCur  = cyT;
    if (fabsf(czCur - czT) < 0.05f)  czCur  = czT;
}

const char *Scene::modeName() const {
    switch (mode) {
        case 0: return "KNOTS";
        case 1: return "TESSERACT";
        case 2: return "MOBIUS";
        case 3: return "HELIX";
        case 4: return "VORTEX";
        case 5: return "STATUS";
        default: return "?";
    }
}
const char *Scene::speedName() const {
    switch (speedIndex) {
        case 0: return "1x";
        case 1: return "3x";
        case 2: return "PAUSED";
        default: return "?";
    }
}
