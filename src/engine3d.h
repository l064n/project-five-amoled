#pragma once
//
// engine3d.h — a tiny, self-contained software 3D engine for the
// T-Display-S3-AMOLED (240x536, RGB565). No external 3D libs.
//
// What it provides:
//   * A full-frame RGB565 framebuffer allocated in PSRAM.
//   * Vector rotation (X/Y/Z) + pinhole (perspective) projection.
//   * A neon line primitive with additive "glow" (perfect on AMOLED black).
//   * Color helpers (HSL -> RGB565).
//
// The render model is immediate-mode: clear the framebuffer, draw 3D
// segments/points into it, then blit the whole frame to the display.
//
#include <Arduino.h>
#include <stdint.h>

// 3D vector (float).
struct V3 {
    float x, y, z;
};

class Engine3D {
public:
    int W = 240, H = 536;
    uint16_t *fb = nullptr; // RGB565 framebuffer in PSRAM

    void begin(int w, int h);
    void clear(uint16_t color16);

    // --- low-level 2D primitives (operate on the framebuffer) ---
    void setPixel(int x, int y, uint16_t c16);
    // Additive blend of an RGB8 triple (clamped) — this is what makes neon
    // lines "add light" on the black background instead of just overwriting.
    void addPixel(int x, int y, int r, int g, int b);

    // Draw a glowing line from (x0,y0) to (x1,y1). `core` is the line width
    // of the bright center; `glow` is the additive halo radius. Colors are
    // RGB8. Uses additive blending so overlaps brighten (neon look).
    void drawNeonLine(int x0, int y0, int x1, int y1,
                      int r, int g, int b, int core = 1, int glow = 2);

    // Draw a single glowing point.
    void drawNeonDot(int x, int y, int r, int g, int b, int radius = 1);

    // --- 3D math ---
    // Rotate (x,y,z) about X by rx, then Y by ry, then Z by rz (radians).
    void rotate(float x, float y, float z,
                float rx, float ry, float rz, V3 &out) const;

    // Pinhole projection. cx,cy,cz are already in *camera* space (viewer at
    // z=0 looking down +z). Returns false if the point is behind/too close.
    bool project(float cx, float cy, float cz,
                 int &sx, int &sy, int &depth) const;

    // Render one 3D line segment end-to-end: rotate -> camera-space ->
    // project -> neon draw. Culls if either endpoint is behind the camera.
    // `intensity` scales the color 0..255.
    void renderSegment(const V3 &pa, const V3 &pb,
                       float rx, float ry, float rz,
                       float camDist,
                       int r, int g, int b, int intensity,
                       int core = 1, int glow = 2);

    // Project + draw a 3D point (for stars).
    void renderPoint(const V3 &p,
                     float rx, float ry, float rz,
                     float camDist,
                     int r, int g, int b, int intensity,
                     int radius = 1);

    // --- color helpers ---
    static uint16_t rgb565(int r, int g, int b);
    // hue in [0,360), sat/val in [0,1].
    static uint16_t hsl565(float h, float s, float v);

private:
    int   focal_ = 240;  // projection focal length in px
    float near_  = 24.0f; // near cull plane in camera space
    void  bresenham(int x0, int y0, int x1, int y1, int r, int g, int b, int width);
};
