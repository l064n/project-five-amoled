#include "scene.h"
#include <Arduino.h>
#include <math.h>
#include <stdlib.h>

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
    // ---- Hero torus knot (p=2, q=3) ----
    const float p = 2.0f, q = 3.0f;
    const float R = 1.0f, r = 0.42f;
    const float S = 92.0f;
    for (int i = 0; i < KNOT_N; i++) {
        float t = 2.0f * M_PI * (float)i / (float)KNOT_N;
        float ring = R + r * cosf(q * t);
        knot_[i].x = ring * cosf(p * t) * S;
        knot_[i].y = ring * sinf(p * t) * S;
        knot_[i].z = r * sinf(q * t) * S;
        // Base hue varies along the loop (0..360) -> rainbow ribbon.
        knotHue_[i] = (float)i / (float)KNOT_N * 360.0f;
    }

    // ---- Secondary knot (p=1, q=2), counter-rotating ----
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

    // ---- Stars: random directions on a far sphere ----
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

    const Palette &P = pal();

    // ---------- 1) Parallax starfield (rotates with the camera, twinkles) ----------
    int64_t T1 = esp_timer_get_time();
    Rot mStar = makeRot(rx, ry, 0.0f);
    for (int i = 0; i < STARS; i++) {
        // Twinkle: 0..255 via a phase-offset sine.
        float tw = 0.5f + 0.5f * sinf(0.004f * (float)t_ms + (float)starPhase_[i]);
        int dim = (int)(starDim_[i] * (0.35f + 0.65f * tw));
        e_.renderPointMat(stars_[i], mStar, 1600.0f,
                          P.star_r, P.star_g, P.star_b, dim, 1);
    }
    int64_t T2 = esp_timer_get_time();

    // ---------- 2) Synthwave grid floor (axis-aligned, scrolling + wave) ----------
    const float GY = -150.0f;   // ground plane height (below center)
    const float XW = 900.0f;    // half-width of the grid
    const float Z0 = 60.0f;     // near
    const float Z1 = 3200.0f;   // far
    // Traveling brightness wave along depth (makes the floor "pulse").
    float waveT = 0.0009f * (float)t_ms;

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
    float phase = fmodf((float)t_ms * 0.00035f, 1.0f);
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
    int64_t T3 = esp_timer_get_time();

    // ---------- 3) Hero torus knot (hue-cycling + traveling pulse) ----------
    float spin = (float)(t_ms * 0.00045f);
    float bob = 0.12f * sinf(0.0012f * (float)t_ms);
    float knotRy = ry + spin;
    float knotRx = rx + 0.45f + bob;   // fixed tilt + gentle rock
    Rot m1 = makeRot(knotRx, knotRy, 0.0f);   // build once, reuse
    float hs = hueShift(t_ms);
    // A bright "comet" pulse traveling along the loop.
    float pulsePos = fmodf((float)t_ms * 0.00025f, 1.0f) * (float)KNOT_N;
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
    int64_t T4 = esp_timer_get_time();

    // ---------- 4) Secondary knot (thinner, counter-rotating, dimmer) ----------
    float spin2 = (float)(t_ms * -0.0006f);
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
    int64_t T5 = esp_timer_get_time();

    g_sec[0] += (T2 - T1); // stars
    g_sec[1] += (T3 - T2); // grid
    g_sec[2] += (T4 - T3); // hero knot
    g_sec[3] += (T5 - T4); // knot2
    g_sec[4] += (T5 - T0) - (T2 - T1) - (T3 - T2) - (T4 - T3) - (T5 - T4); // clear+gaps
}

void Scene::flushPerf(uint32_t n) {
    if (n == 0) return;
    Serial.printf("[sec] stars=%d grid=%d knot1=%d knot2=%d clr=%d ms\n",
                  (int)(g_sec[0] / n / 1000), (int)(g_sec[1] / n / 1000),
                  (int)(g_sec[2] / n / 1000), (int)(g_sec[3] / n / 1000),
                  (int)(g_sec[4] / n / 1000));
    for (int i = 0; i < 5; i++) g_sec[i] = 0;
}
