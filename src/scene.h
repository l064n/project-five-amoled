#pragma once
//
// scene.h — the neon 3D scene rendered by Engine3D.
//   * A rotating (p,q) torus knot — the hero object.
//   * A synthwave perspective grid floor that scrolls toward the viewer.
//   * A parallax starfield in the background.
// All geometry lives in float world space; the scene rotates the whole
// "camera object" via two angles (ry, rx) that touch-drag controls.
//
#include "engine3d.h"

class Scene {
public:
    Scene(Engine3D &e) : e_(e) {}

    void begin();          // build static geometry
    void render(float rx, float ry, uint32_t t_ms);

    // Camera distance (how far the hero object sits in front of the lens).
    static constexpr float CAM_DIST = 360.0f;

    // ---- palette (RGB8) — switchable by tap ----
    struct Palette { const char *name; int line_r, line_g, line_b;
                     int star_r, star_g, star_b;
                     int grid_r, grid_g, grid_b; };
    static constexpr int NUM_PALETTES = 4;
    static const Palette kPalettes[NUM_PALETTES];
    int paletteIndex = 0;
    const Palette &pal() const { return kPalettes[paletteIndex]; }
    void nextPalette() { paletteIndex = (paletteIndex + 1) % NUM_PALETTES; }

private:
    Engine3D &e_;

    // Torus knot vertices (precomputed base frame); rotated at render time.
    static constexpr int KNOT_N = 240;
    V3 knot_[KNOT_N];

    // Stars (random directions on a far sphere).
    static constexpr int STARS = 120;
    V3 stars_[STARS];
    uint8_t starDim_[STARS];
};
