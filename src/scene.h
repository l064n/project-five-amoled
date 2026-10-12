#pragma once
//
// scene.h — the neon 3D scene rendered by Engine3D.
//   * A rotating (p,q) torus knot — the hero object — with a hue-cycling
//     gradient and a bright pulse traveling along its length.
//   * A second, thinner, counter-rotating knot (p=1, q=2) that interlaces
//     with the hero for extra depth and motion.
//   * A synthwave perspective grid floor that scrolls + has a traveling wave.
//   * A parallax starfield that twinkles.
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
    static constexpr float CAM_DIST = 440.0f;

    // ---- palette (RGB8) — switchable by tap ----
    struct Palette { const char *name; int line_r, line_g, line_b;
                     int star_r, star_g, star_b;
                     int grid_r, grid_g, grid_b; };
    static constexpr int NUM_PALETTES = 4;
    static const Palette kPalettes[NUM_PALETTES];
    int paletteIndex = 0;
    const Palette &pal() const { return kPalettes[paletteIndex]; }
    void nextPalette() { paletteIndex = (paletteIndex + 1) % NUM_PALETTES; }

    // Per-pixel hue offset applied on top of the palette base hue (degrees).
    // Kept here so the HUD can show which palette is active via the dots.
    float hueShift(uint32_t t_ms) const { return fmodf((float)t_ms * 0.02f, 360.0f); }

private:
    Engine3D &e_;

    // Hero torus knot (p=2, q=3) vertices (precomputed base frame).
    static constexpr int KNOT_N  = 200;
    V3 knot_[KNOT_N];
    float knotHue_[KNOT_N];   // base hue per segment (for the traveling pulse)

    // Secondary knot (p=1, q=2), counter-rotating.
    static constexpr int KNOT2_N = 140;
    V3 knot2_[KNOT2_N];
    float knot2Hue_[KNOT2_N];

    // Stars (random directions on a far sphere).
    static constexpr int STARS = 96;
    V3 stars_[STARS];
    uint8_t starDim_[STARS];
    uint8_t starPhase_[STARS];  // twinkle phase offset
};
