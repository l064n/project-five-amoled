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
    // ---- Torus knot (p=2, q=3) ----
    const float p = 2.0f, q = 3.0f;
    const float R = 1.0f, r = 0.42f;
    const float S = 92.0f;
    for (int i = 0; i < KNOT_N; i++) {
        float t = 2.0f * M_PI * (float)i / (float)KNOT_N;
        float ring = R + r * cosf(q * t);
        knot_[i].x = ring * cosf(p * t) * S;
        knot_[i].y = ring * sinf(p * t) * S;
        knot_[i].z = r * sinf(q * t) * S;
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
    }
}

void Scene::render(float rx, float ry, uint32_t t_ms)
{
    e_.clear(0x0000); // black — lets neon additive glow shine

    const Palette &P = pal();

    // ---------- 1) Parallax starfield (rotates with the camera) ----------
    for (int i = 0; i < STARS; i++)
        e_.renderPoint(stars_[i], rx, ry, 0.0f, 1600.0f,
                       P.star_r, P.star_g, P.star_b, starDim_[i], 1);

    // ---------- 2) Synthwave grid floor (axis-aligned, scrolling) ----------
    const float GY = -150.0f;   // ground plane height (below center)
    const float XW = 900.0f;    // half-width of the grid
    const float Z0 = 60.0f;     // near
    const float Z1 = 3200.0f;   // far
    float scroll = fmodf((float)t_ms * 0.18f, 120.0f);

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
        int ir = (int)(P.grid_r * fog), ig = (int)(P.grid_g * fog), ib = (int)(P.grid_b * fog);
        e_.drawNeonLine(x0, y0, x1, y1, ir, ig, ib, 1, 2);
    }

    // ---------- 3) Hero torus knot (rotates with camera + its own spin) ----------
    float spin = (float)(t_ms * 0.00045f);
    float knotRy = ry + spin;
    float knotRx = rx + 0.45f;   // fixed tilt so the knot reads as 3D
    int r = P.line_r, g = P.line_g, b = P.line_b;
    for (int i = 0; i < KNOT_N; i++) {
        const V3 &a = knot_[i];
        const V3 &c = knot_[(i + 1) % KNOT_N];
        e_.renderSegment(a, c, knotRx, knotRy, 0.0f, CAM_DIST, r, g, b, 255, 1, 2);
    }
}
