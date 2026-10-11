#include "engine3d.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void Engine3D::begin(int w, int h)
{
    W = w; H = h;
    focal_ = (W < H) ? W : H;           // ~min dimension as focal length
    if (!fb) {
        fb = (uint16_t *)ps_malloc(sizeof(uint16_t) * W * H);
        memset(fb, 0, sizeof(uint16_t) * W * H);
    }
}

void Engine3D::clear(uint16_t color16)
{
    memset(fb, (color16 >> 8) & 0xFF, sizeof(uint16_t) * W * H); // high byte
    memset(fb + 1, color16 & 0xFF, sizeof(uint16_t) * W * H);   // low byte
    // (memset bytewise; uint16 little-endian: even=high, odd=low.)
}

void Engine3D::setPixel(int x, int y, uint16_t c16)
{
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    fb[y * W + x] = c16;
}

void Engine3D::addPixel(int x, int y, int r, int g, int b)
{
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    uint16_t c = fb[y * W + x];
    int cr = (c >> 11) & 0x1F, cg = (c >> 5) & 0x3F, cb = c & 0x1F;
    // Scale RGB8 -> 565 channel and add, clamp.
    cr += r >> 3; if (cr > 31) cr = 31;
    cg += g >> 2; if (cg > 63) cg = 63;
    cb += b >> 3; if (cb > 31) cb = 31;
    fb[y * W + x] = (uint16_t)((cr << 11) | (cg << 5) | cb);
}

// Thick Bresenham: for `width`>1 we draw a vertical run of pixels at each
// step so lines look solid; additive blending makes overlaps glow.
void Engine3D::bresenham(int x0, int y0, int x1, int y1, int r, int g, int b, int width)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        int half = width >> 1;
        for (int i = -half; i <= half; i++) {
            addPixel(x0 + i, y0, r, g, b);
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void Engine3D::drawNeonDot(int x, int y, int r, int g, int b, int radius)
{
    for (int dy = -radius; dy <= radius; dy++)
        for (int dx = -radius; dx <= radius; dx++)
            if (dx * dx + dy * dy <= radius * radius)
                addPixel(x + dx, y + dy, r, g, b);
}

void Engine3D::drawNeonLine(int x0, int y0, int x1, int y1,
                            int r, int g, int b, int core, int glow)
{
    // Halo first (dimmer, wider), then bright core on top.
    if (glow > 0)
        bresenham(x0, y0, x1, y1, r >> 2, g >> 2, b >> 2, glow * 2);
    bresenham(x0, y0, x1, y1, r, g, b, core);
}

void Engine3D::rotate(float x, float y, float z,
                      float rx, float ry, float rz, V3 &out) const
{
    // Y axis
    float cy = cosf(ry), sy = sinf(ry);
    float x1 = x * cy + z * sy, z1 = -x * sy + z * cy;
    // X axis
    float cx = cosf(rx), sx = sinf(rx);
    float y1 = y * cx - z1 * sx, z2 = y * sx + z1 * cx;
    // Z axis
    float cz = cosf(rz), sz = sinf(rz);
    float x2 = x1 * cz - y1 * sz, y2 = x1 * sz + y1 * cz;
    out.x = x2; out.y = y2; out.z = z2;
}

bool Engine3D::project(float cx, float cy, float cz,
                       int &sx, int &sy, int &depth) const
{
    if (cz < near_) return false;
    float inv = (float)focal_ / cz;
    sx = (int)(W * 0.5f + cx * inv);
    sy = (int)(H * 0.5f - cy * inv); // flip y (screen y grows down)
    depth = (int)cz;
    return true;
}

// Camera model: camera at the origin looking down +z. A world point is
// rotated into view space, then the object center is pulled to z = camDist so
// it sits in front of the camera. Depth = view.z (must be > near_).
void Engine3D::renderSegment(const V3 &pa, const V3 &pb,
                             float rx, float ry, float rz,
                             float camDist,
                             int r, int g, int b, int intensity,
                             int core, int glow)
{
    V3 ra, rb, ca, cb;
    rotate(pa.x, pa.y, pa.z, rx, ry, rz, ra);
    rotate(pb.x, pb.y, pb.z, rx, ry, rz, rb);
    ca.x = ra.x; ca.y = ra.y; ca.z = ra.z + camDist;
    cb.x = rb.x; cb.y = rb.y; cb.z = rb.z + camDist;

    int axi, ayi, azd, bxi, byi, bzd;
    if (!project(ca.x, ca.y, ca.z, axi, ayi, azd)) return;
    if (!project(cb.x, cb.y, cb.z, bxi, byi, bzd)) return;

    int avg = (azd + bzd) >> 1;

    // Depth fog + proximity punch.
    float fog = 1.0f - (float)avg / 2400.0f;
    if (fog < 0.10f) fog = 0.10f;

    int ir = (int)(r * (intensity / 255.0f) * fog);
    int ig = (int)(g * (intensity / 255.0f) * fog);
    int ib = (int)(b * (intensity / 255.0f) * fog);

    int c2 = core, g2 = glow;
    if (avg < 260) { c2 = core + 1; g2 = glow + 1; }
    drawNeonLine(axi, ayi, bxi, byi, ir, ig, ib, c2, g2);
}

void Engine3D::renderPoint(const V3 &p,
                           float rx, float ry, float rz,
                           float camDist,
                           int r, int g, int b, int intensity,
                           int radius)
{
    V3 rp, c;
    rotate(p.x, p.y, p.z, rx, ry, rz, rp);
    c.x = rp.x; c.y = rp.y; c.z = rp.z + camDist;
    int sx, sy, depth;
    if (!project(c.x, c.y, c.z, sx, sy, depth)) return;
    float fog = 1.0f - (float)depth / 2800.0f;
    if (fog < 0.1f) fog = 0.1f;
    int ir = (int)(r * (intensity / 255.0f) * fog);
    int ig = (int)(g * (intensity / 255.0f) * fog);
    int ib = (int)(b * (intensity / 255.0f) * fog);
    int rad = (depth < 600) ? radius + 1 : radius;
    drawNeonDot(sx, sy, ir, ig, ib, rad);
}

uint16_t Engine3D::rgb565(int r, int g, int b)
{
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t Engine3D::hsl565(float h, float s, float v)
{
    h = fmodf(h, 360.0f); if (h < 0) h += 360.0f;
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r, g, b;
    if (h < 60) { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else { r = c; g = 0; b = x; }
    return rgb565((int)((r + m) * 255), (int)((g + m) * 255), (int)((b + m) * 255));
}
