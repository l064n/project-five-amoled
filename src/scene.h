#pragma once
//
// scene.h — the neon 3D scene rendered by Engine3D.
//
// A small registry of toggleable scenes (button / 'M' serial):
//   0 KNOTS      — dual torus knots (hue-cycling + comet pulse) over a
//                  synthwave perspective grid floor + traveling wave.
//   1 TESSERACT  — a 4D hypercube: two-plane 4D rotation projected 4D->3D,
//                  16 verts / 32 hue-cycling edges.
//   2 MOBIUS     — a rotating wireframe Möbius strip (hue gradient + pulse).
//   3 HELIX      — a double helix with cross rungs, counter-rotating strands.
//   4 VORTEX     — a swirling particle galaxy (spiral disc, hue by radius).
//   5 STATUS     — live WiFi + local-weather dashboard: five full-screen 3D
//                  pages (WiFi tower, RSSI bars, IP octets, weather globe,
//                  uptime ring) you scroll between; drag pivots the focused
//                  widget. Driven by the shared StatusData (see status_data.h).
//
// All modes share: the twinkling parallax starfield, the active palette, and
// drag-to-rotate (rx/ry). A scaled animation clock (spinT) drives every phase,
// so the long-press speed toggle (1x / 3x / paused) affects the whole scene.
//
#include "engine3d.h"

class Scene {
public:
    Scene(Engine3D &e) : e_(e) {}

    void begin();          // build static geometry for all modes
    void render(float rx, float ry, uint32_t t_ms);
    void flushPerf(uint32_t n); // print + clear per-section timing (debug)

    // Camera distance (how far the hero object sits in front of the lens).
    static constexpr float CAM_DIST = 440.0f;

    // ---- scenes ----
    static constexpr int NUM_MODES = 6;
    int mode = 0;
    const char *modeName() const;
    void nextMode() {
        mode = (mode + 1) % NUM_MODES;
        if (mode == 5) statusReset(); // entering STATUS: start on the WiFi page
    }

    // ---- animation speed (long-press): 0 = 1x, 1 = 3x, 2 = paused ----
    static constexpr int NUM_SPEEDS = 3;
    int speedIndex = 0;
    const char *speedName() const;
    void nextSpeed() { speedIndex = (speedIndex + 1) % NUM_SPEEDS; }
    float spinMul() const {
        return speedIndex == 1 ? 3.0f : (speedIndex == 2 ? 0.0f : 1.0f);
    }

    // ---- palette (RGB8) — switchable by tap ----
    struct Palette { const char *name; int line_r, line_g, line_b;
                     int star_r, star_g, star_b;
                     int grid_r, grid_g, grid_b; };
    static constexpr int NUM_PALETTES = 4;
    static const Palette kPalettes[NUM_PALETTES];
    int paletteIndex = 0;
    const Palette &pal() const { return kPalettes[paletteIndex]; }
    void nextPalette() { paletteIndex = (paletteIndex + 1) % NUM_PALETTES; }

    // STATUS-mode tap-to-pulse: set by main.cpp when a tap lands in this mode.
    // pulse[0]=wifi pulse[1]=rssi pulse[2]=ip pulse[3]=weather pulse[4]=uptime
    volatile uint32_t pulseMs[5] = {0,0,0,0,0};
    void pulseWidget(int i) { if (i >= 0 && i < 5) pulseMs[i] = millis(); }

    // ---- STATUS-mode: paged full-screen widgets ----
    // Five full-screen "pages" (0 WiFi, 1 RSSI, 2 IP, 3 Weather, 4 Uptime).
    // Only the focused page fills the screen; SCROLL (vertical drag) moves
    // statusScroll between pages with a smooth slide + snap, and DRAG
    // (horizontal) still pivots the 3D widget via the global g_ry.
    //
    // statusScroll is a continuous page index (0..4). While a finger is down
    // it tracks the drag 1:1 (no clamping, so you feel the slide); on release
    // it eases (with a little fling momentum) to the nearest page. drawStatus
    // renders the focused page and, while 0<frac<1, the neighbor sliding in.
    static constexpr int  STATUS_PAGES = 5;
    int    statusPage    = 0;          // nearest/rounded page (for HUD + taps)
    float  statusScroll  = 0.0f;       // eased continuous page index
    float  statusScrollT = 0.0f;       // target (snapped page)
    float  statusVel     = 0.0f;       // fling velocity (pages/frame-ish)
    bool   statusDragging= false;      // finger is driving the scroll
    int    statusIndex() const { return statusPage; }
    void   statusReset();              // page 0, scroll 0
    void   statusScrollTo(int p);      // set target page (eased)
    void   statusScrollBy(int d);      // relative page step
    void   statusDragBegin();
    void   statusDragBy(float dp);     // 1:1 continuous (pages) while dragging
    void   statusDragEnd(float vel);   // release: flick -> one page, else settle
    void   statusEase();               // called per frame: ease scroll -> target

    // Per-pixel hue offset applied on top of the palette base hue (degrees).
    float hueShift(uint32_t t_ms) const { return fmodf((float)t_ms * 0.02f, 360.0f); }

private:
    Engine3D &e_;

    // Scaled animation clock (ms) — advanced each render by dt * spinMul().
    uint32_t lastT = 0;
    float spinT = 0.0f;

    // ---- KNOTS (mode 0) ----
    static constexpr int KNOT_N  = 150;
    V3 knot_[KNOT_N];
    float knotHue_[KNOT_N];
    static constexpr int KNOT2_N = 100;
    V3 knot2_[KNOT2_N];
    float knot2Hue_[KNOT2_N];

    // ---- TESSERACT (mode 1): 16 4D cube verts, scaled ----
    static constexpr int TESS_N = 16;
    float tess_[TESS_N][4];

    // ---- MOBIUS (mode 2): NV rings x NU segments ----
    static constexpr int MOB_NU = 48;
    static constexpr int MOB_NV = 3;
    V3 mob_[MOB_NV * MOB_NU];

    // ---- HELIX (mode 3) ----
    static constexpr int HEL_N = 60;
    V3 helA_[HEL_N], helB_[HEL_N];

    // ---- VORTEX (mode 4) ----
    static constexpr int VOR_N = 150;
    V3 vor_[VOR_N];
    float vorT_[VOR_N];   // 0..1 param along the spiral (for radius-based hue)

    // ---- STARS (shared) ----
    static constexpr int STARS = 96;
    V3 stars_[STARS];
    uint8_t starDim_[STARS];
    uint8_t starPhase_[STARS];  // twinkle phase offset

    // Per-mode draw routines.
    void drawKnots(float rx, float ry);
    void drawTesseract(float rx, float ry);
    void drawMobius(float rx, float ry);
    void drawHelix(float rx, float ry);
    void drawVortex(float rx, float ry);
    void drawStatus(float rx, float ry);
};
