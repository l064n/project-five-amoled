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
//   5 STATUS     — live WiFi + local-weather status page: a constellation of
//                  interactive 3D widgets (WiFi tower, RSSI bars, IP octets,
//                  weather globe, uptime ring) driven by the shared
//                  StatusData (see status_data.h).
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
        if (mode != 5) resetCam(); // leaving STATUS: start fresh next time
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
    // pulse[0]=tower pulse[1]=bars pulse[2]=ip pulse[3]=globe pulse[4]=uptime
    volatile uint32_t pulseMs[5] = {0,0,0,0,0};
    void pulseWidget(int i) { if (i >= 0 && i < 5) pulseMs[i] = millis(); }
    // Projected 2D anchors of the 5 widgets, updated each frame by drawStatus.
    // main.cpp hit-tests a tap against the nearest of these.
    int tapX[5] = {0,0,0,0,0};
    int tapY[5] = {0,0,0,0,0};

    // ---- STATUS-mode camera: smooth zoom + focus-on-widget (world space) ----
    // The whole STATUS constellation is rendered through a camera transform
    //   v = (world - camC) * z
    // applied to every vertex AND label before orbit-rotation + projection,
    // so meshes and their labels always stay locked together.
    // zCur/camC are eased toward zT/camCT by smoothCam() each frame.
    // When statusFocus >= 0 the camera center targets that widget's world
    // anchor, pinning it to screen-center (stays centered even while you
    // drag-orbit). In overview (statusFocus < 0) the center eases back to 0.
    // Camera zoom levels for STATUS.
    static constexpr float ZOOM_OVERVIEW = 1.5f; // default view (nicely close)
    static constexpr float ZOOM_FOCUS    = 3.0f; // double-tap / G lock-on zoom
    float  zCur = 1.0f;                         // eased zoom (ramps in on entry)
    float  cxCur = 0.0f, cyCur = 0.0f, czCur = 0.0f; // eased center (world)
    float  zT = ZOOM_OVERVIEW;                  // zoom target
    float  cxT = 0.0f, cyT = 0.0f, czT = 0.0f;  // center target (world)
    int    statusFocus = -1;                    // widget index locked, or -1
    int    focusIdx() const { return statusFocus; }
    float  zoom() const { return zCur; }
    // World-space anchors of the 5 STATUS widgets (fixed layout), set by
    // drawStatus each frame and read by focusWidget/main for hit-testing.
    V3     widgetAnchorW[5];
    void   setZoom(float z);            // clamp; zooming out also releases focus
    void   focusWidget(int i, float z); // lock the camera to widget i at zoom z
    void   resetCam();                   // overview: zoom 1, no focus, center 0
    void   smoothCam();                  // ease the camera toward targets/frame

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
    // Weather widget (called by drawStatus): rotating wireframe globe with a
    // billboard weather icon orbiting it, per WMO code.
    void drawWeatherGlobe(const V3 &c, const Rot &m, float camDist, float t,
                          uint32_t wvalid, uint32_t wcode, int32_t temp10,
                          const char *city, float pulse);
};
