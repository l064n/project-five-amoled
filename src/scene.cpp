#include "scene.h"
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

void Scene::render(float rx, float ry, uint32_t t_ms)
{
    e_.clear(0x0000); // black — lets neon additive glow shine

    const Palette &P = pal();

    // ---------- 1) Parallax starfield (rotates with the camera, twinkles) ----------
    Rot mStar = makeRot(rx, ry, 0.0f);
    for (int i = 0; i < STARS; i++) {
        // Twinkle: 0..255 via a phase-offset sine.
        float tw = 0.5f + 0.5f * sinf(0.004f * (float)t_ms + (float)starPhase_[i]);
        int dim = (int)(starDim_[i] * (0.35f + 0.65f * tw));
        e_.renderPointMat(stars_[i], mStar, 1600.0f,
                          P.star_r, P.star_g, P.star_b, dim, 1);
    }

    // ---------- 2) Synthwave grid floor (axis-aligned, scrolling + wave) ----------
    const float GY = -150.0f;   // ground plane height (below center)
    const float XW = 900.0f;    // half-width of the grid
    const float Z0 = 60.0f;     // near
    const float Z1 = 3200.0f;   // far
    float scroll = fmodf((float)t_ms * 0.18f, 120.0f);
    // Traveling brightness wave along depth (makes the floor "pulse").
    float waveT = 0.0009f * (float)t_ms;

    // Depth lines (run from near to far at fixed x positions).
    for (int k = -8; k <= 8; k++) {
        float gx = (float)k * 120.0f;
        int x0, y0, d0, x1, y1, d1;
        if (!e_.project(gx, GY, Z0, x0, y0, d0)) continue;
        if (!e_.project(gx, GY, Z1, x1, y1, d1)) continue;
        float fog = 1.0f - (float)(d0 + d1) / 2.0f / 3200.0f;
        if (fog < 0.08f) fog = 0.08f;
        int ir = (int)(P.grid_r * fog), ig = (int)(P.grid_g * fog), ib = (int)(P.grid_b * fog);
        e_.drawNeonLine(x0, y0, x1, y1, ir, ig, ib, 1, 1);
    }

    // Horizontal lines (across width, scrolling toward viewer).
    for (float z = Z0 + (120.0f - scroll); z < Z1; z += 120.0f) {
        int x0, y0, d0, x1, y1, d1;
        if (!e_.project(-XW, GY, z, x0, y0, d0)) continue;
        if (!e_.project( XW, GY, z, x1, y1, d1)) continue;
        float fog = 1.0f - (float)z / 3400.0f;
        if (fog < 0.08f) fog = 0.08f;
        float wv = 0.55f + 0.45f * sinf(waveT - (float)z * 0.004f);
        int ir = (int)(P.grid_r * fog * wv), ig = (int)(P.grid_g * fog * wv), ib = (int)(P.grid_b * fog * wv);
        e_.drawNeonLine(x0, y0, x1, y1, ir, ig, ib, 1, 2);
    }

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
        int core = 1, glow = 2, inten = 255;
        if (dd < 24.0f) { inten = 255; core = 2; glow = 3; }
        else if (dd < 60.0f) { inten = 190; }
        else { inten = 150; }
        e_.renderSegmentMat(knot_[i], knot_[(i + 1) % KNOT_N], m1,
                            CAM_DIST, r, g, b, inten, core, glow);
    }

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
}
