/*
 * T-Display-S3-AMOLED — NEON 3D interactive demo
 *
 * A self-contained software 3D engine renders a neon torus knot over a
 * synthwave perspective grid and a parallax starfield on the AMOLED black.
 *
 * Interactions (CST816 capacitive touch):
 *   * Drag anywhere to rotate the scene (with inertia / momentum).
 *   * Tap (quick, no movement) to switch the neon color palette.
 *
 * Everything is immediate-mode: render into a PSRAM framebuffer, then blit
 * the whole frame to the panel each tick.
 *
 * Build:  pio run -t upload
 */

#include <Arduino.h>
#include <math.h>
#include <Preferences.h>

#include "rm67162.h"          // display driver
#include "pins_config.h"
#include "touch_cst816.h"     // capacitive touch
#include "engine3d.h"         // software 3D engine
#include "scene.h"            // neon scene

// -------------------------------------------------------------------------
static Engine3D engine;
static Scene    scene(engine);
static CST816   touch;

// Camera angles (radians) controlled by touch.
static float g_rx = 0.20f;
static float g_ry = 0.00f;
static float g_velRx = 0.0f;
static float g_velRy = 0.0f;

// Touch state.
static int16_t  g_lastX = -1, g_lastY = -1;
static int16_t  g_lastRawX = -1, g_lastRawY = -1;
static bool     g_wasDown = false;
static uint32_t g_downMs = 0;
static float    g_moved = 0.0f;

// FPS tracking.
static uint32_t g_frames = 0;
static uint32_t g_fpsStart = 0;
static int      g_fps = 0;

// Frame-time accounting (us), printed once per second.
static uint32_t g_sceneUs = 0;
static uint32_t g_blitUs = 0;

// Frame freeze (used to capture a clean frame over serial as a PNG).
static volatile bool g_freeze = false;

// =========================================================================
// Touch coordinate calibration
//
// The CST816 reports points in its own raw frame, which does NOT match the
// portrait 240x536 screen (the 1.91" panel is a 550x480 glass mounted/rotated,
// and the active region is a sub-window). We calibrate by having the user tap
// the 4 screen corners, capturing the raw values, and fitting an affine map:
//   screen_x = ax * raw_x + bx * raw_y + cx
//   screen_y = ay * raw_x + by * raw_y + cy
// Solved as two independent 2x2 systems (x from two corners, y from the other
// two). The result is persisted in NVS so it survives reboots.
// =========================================================================
struct Calib {
    float ax, bx, cx;   // screen_x
    float ay, by, cy;   // screen_y
    bool  valid;
};

static Calib g_calib = { 1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,  false };

static const float kCorners[4][2] = {   // target screen corners (TL,TR,BL,BR)
    { 0.0f, 0.0f }, { (float)(TFT_WIDTH-1), 0.0f },
    { 0.0f, (float)(TFT_HEIGHT-1) }, { (float)(TFT_WIDTH-1), (float)(TFT_HEIGHT-1) }
};

static int g_calibStep = -1;          // 0..3 while calibrating, else -1
static int16_t g_raw[4][2];

// Map raw controller coords to screen coords using the calibration.
static void toScreen(int16_t rx, int16_t ry, float &sx, float &sy)
{
    if (!g_calib.valid) { sx = (float)rx; sy = (float)ry; return; }
    sx = g_calib.ax * rx + g_calib.bx * ry + g_calib.cx;
    sy = g_calib.ay * rx + g_calib.by * ry + g_calib.cy;
    if (sx < 0) sx = 0; if (sx > TFT_WIDTH-1)  sx = TFT_WIDTH-1;
    if (sy < 0) sy = 0; if (sy > TFT_HEIGHT-1) sy = TFT_HEIGHT-1;
}

static void saveCalib()
{
    Preferences p; p.begin("touch", false);
    float v[6] = { g_calib.ax, g_calib.bx, g_calib.cx, g_calib.ay, g_calib.by, g_calib.cy };
    p.putBytes("cal", v, sizeof(v));
    p.end();
}

static void loadCalib()
{
    Preferences p; p.begin("touch", true);
    float v[6];
    if (p.getBytes("cal", v, sizeof(v)) == sizeof(v)) {
        g_calib.ax=v[0]; g_calib.bx=v[1]; g_calib.cx=v[2];
        g_calib.ay=v[3]; g_calib.by=v[4]; g_calib.cy=v[5];
        g_calib.valid = true;
    }
    p.end();
}

// Fit the affine map (screen = A*raw + b) per axis via least squares over the
// 4 captured corners, solving the 3x3 normal equations with Gaussian
// elimination. Returns false if the system is singular (degenerate taps).
static bool solve3(double M[3][3], double B[3])
{
    double a[3][4];
    for (int i = 0; i < 3; i++) { for (int j = 0; j < 3; j++) a[i][j] = M[i][j]; a[i][3] = B[i]; }
    for (int c = 0; c < 3; c++) {
        int p = c;
        for (int r = c + 1; r < 3; r++) if (fabs(a[r][c]) > fabs(a[p][c])) p = r;
        for (int j = 0; j < 4; j++) { double t = a[c][j]; a[c][j] = a[p][j]; a[p][j] = t; }
        if (fabs(a[c][c]) < 1e-9) return false;
        for (int r = c + 1; r < 3; r++) {
            double f = a[r][c] / a[c][c];
            for (int j = c; j < 4; j++) a[r][j] -= f * a[c][j];
        }
    }
    for (int r = 2; r >= 0; r--) {
        double s = a[r][3];
        for (int j = r + 1; j < 3; j++) s -= a[r][j] * (a[j][3] / a[j][j]);
        a[r][3] = s / a[r][r];
    }
    B[0] = a[0][3]; B[1] = a[1][3]; B[2] = a[2][3];
    return true;
}

static void buildNormal(double M[3][3], double B[3], bool isX)
{
    double Sxx=0,Syy=0,Sxy=0,Sr=0,Sxxs=0,Syys=0,Srs=0;
    for (int i = 0; i < 4; i++) {
        double rx = g_raw[i][0], ry = g_raw[i][1];
        double t  = isX ? kCorners[i][0] : kCorners[i][1];
        Sxx += rx*rx; Syy += ry*ry; Sxy += rx*ry; Sr += rx;
        Sxxs += rx*t; Syys += ry*t; Srs += t;
    }
    M[0][0]=Sxx; M[0][1]=Sxy; M[0][2]=Sr;  B[0]=Sxxs;
    M[1][0]=Sxy; M[1][1]=Syy; M[1][2]=Sr;  B[1]=Syys;
    M[2][0]=Sr;  M[2][1]=Sr;  M[2][2]=4.0; B[2]=Srs;
}

static bool finishCalib()
{
    double Ax[3][3], Bx[3], Ay[3][3], By[3];
    buildNormal(Ax, Bx, true);
    buildNormal(Ay, By, false);
    if (!solve3(Ax, Bx) || !solve3(Ay, By)) return false;
    g_calib.ax=Bx[0]; g_calib.bx=Bx[1]; g_calib.cx=Bx[2];
    g_calib.ay=By[0]; g_calib.by=By[1]; g_calib.cy=By[2];
    g_calib.valid = true;
    saveCalib();
    return true;
}

// Minimal 3x5 digit font (rows of 3 bits, LSB = leftmost pixel) for FPS HUD.
static const uint8_t kDigit[10][5] = {
    {0b111,0b101,0b101,0b101,0b111}, // 0
    {0b010,0b110,0b010,0b010,0b111}, // 1
    {0b111,0b001,0b111,0b100,0b111}, // 2
    {0b111,0b001,0b111,0b001,0b111}, // 3
    {0b101,0b101,0b111,0b001,0b001}, // 4
    {0b111,0b100,0b111,0b001,0b111}, // 5
    {0b111,0b100,0b111,0b101,0b111}, // 6
    {0b111,0b001,0b001,0b001,0b001}, // 7
    {0b111,0b101,0b111,0b101,0b111}, // 8
    {0b111,0b101,0b111,0b001,0b111}, // 9
};

static void drawDigit(int x, int y, int d, int r, int g, int b)
{
    for (int row = 0; row < 5; row++)
        for (int col = 0; col < 3; col++)
            if (kDigit[d][row] & (1 << (2 - col)))
                engine.addPixel(x + col, y + row, r, g, b);
}

static void drawInt(int x, int y, int val, int r, int g, int b)
{
    if (val < 0) val = 0; if (val > 999) val = 999;
    int digits = (val >= 100) ? 3 : (val >= 10 ? 2 : 1);
    for (int i = 0; i < digits; i++) {
        int place = (int)powf(10.0f, (float)(digits - 1 - i));
        drawDigit(x + i * 4, y, (val / place) % 10, r, g, b);
    }
}

// Top-center palette indicator: one dot per palette, active is lit.
static void drawPaletteDots()
{
    const Scene::Palette &P = scene.pal();
    int n = Scene::NUM_PALETTES;
    int startX = (engine.W - (n * 10)) / 2;
    for (int i = 0; i < n; i++) {
        int cx = startX + i * 10 + 4;
        int cy = 8;
        if (i == scene.paletteIndex)
            engine.drawNeonDot(cx, cy, P.line_r, P.line_g, P.line_b, 3);
        else
            engine.drawNeonDot(cx, cy, 70, 70, 70, 1);
    }
}

// Calibration overlay: full-screen slate background + big bright target at the
// corner to tap next + a progress bar. Deliberately high-contrast — on an
// AMOLED a near-black "dim" band is invisible, so the whole screen is filled
// with a mid slate so the user can't miss it.
static void drawCalibOverlay()
{
    if (g_calibStep < 0) return;

    // Full-screen slate (~RGB 40,40,80): clearly visible, not blinding.
    const uint16_t bg = 0x0A5A;
    for (uint32_t i = 0; i < (uint32_t)engine.W * engine.H; i++)
        engine.fb[i] = bg;

    // Big bright target at the corner to tap, nudged in from the very edge so
    // the marker is fully visible.
    int tx = (int)kCorners[g_calibStep][0];
    int ty = (int)kCorners[g_calibStep][1];
    tx = tx < 14 ? 14 : (tx > engine.W - 15 ? engine.W - 15 : tx);
    ty = ty < 14 ? 14 : (ty > engine.H - 15 ? engine.H - 15 : ty);
    engine.drawNeonDot(tx, ty, 255, 255, 255, 11);

    // Crosshair arms so the target reads as "tap here" even at a glance.
    for (int d = 18; d <= 34; d += 2) {
        engine.addPixel(tx + d, ty, 255, 255, 255);
        engine.addPixel(tx - d, ty, 255, 255, 255);
        engine.addPixel(tx, ty + d, 255, 255, 255);
        engine.addPixel(tx, ty - d, 255, 255, 255);
    }

    // Progress: four big dots across the middle — green = done,
    // amber = current, dim = pending.
    for (int i = 0; i < 4; i++) {
        int bx = 40 + i * 47;
        int by = engine.H / 2;
        if (i < g_calibStep)
            engine.drawNeonDot(bx, by, 0, 255, 120, 7);
        else if (i == g_calibStep)
            engine.drawNeonDot(bx, by, 255, 220, 40, 8);
        else
            engine.drawNeonDot(bx, by, 110, 110, 150, 4);
    }
}

// -------------------------------------------------------------------------
static void renderAndPush(uint32_t t_ms)
{
    // Apply inertia when not dragging.
    if (!g_wasDown) {
        g_rx += g_velRx;
        g_ry += g_velRy;
        g_velRx *= 0.94f;
        g_velRy *= 0.94f;
        if (g_rx > 1.3f)  g_rx = 1.3f;
        if (g_rx < -0.4f) g_rx = -0.4f;
    }

    // HUD drawn last so it stays crisp on top.
    int64_t t0 = esp_timer_get_time();
    scene.render(g_rx, g_ry, t_ms);
    drawPaletteDots();
    drawInt(engine.W - 30, engine.H - 12, g_fps,
            scene.pal().line_r, scene.pal().line_g, scene.pal().line_b);
    drawCalibOverlay();
    int64_t t1 = esp_timer_get_time();

    // Blit the full framebuffer to the AMOLED (240x536).
    lcd_PushColors(0, 0, engine.W, engine.H, engine.fb);
    int64_t t2 = esp_timer_get_time();

    g_sceneUs += (uint32_t)(t1 - t0);
    g_blitUs  += (uint32_t)(t2 - t1);
}

// Dedicated render task on core 1 to keep display + 3D off the loop core.
static void renderTask(void *arg)
{
    (void)arg;
    for (;;) {
        if (g_freeze) { vTaskDelay(5); continue; } // hold frame for capture
        uint32_t t = millis();
        renderAndPush(t);
        g_frames++;

        // FPS once per second.
        if (t - g_fpsStart >= 1000) {
            g_fps = (int)(g_frames * 1000.0f / (float)(t - g_fpsStart));
            if (g_frames > 0)
                Serial.printf("[perf] fps=%d scene=%dms blit=%dms\n",
                              g_fps, g_sceneUs / g_frames / 1000,
                              g_blitUs / g_frames / 1000);
            g_frames = 0;
            g_sceneUs = 0;
            g_blitUs = 0;
            g_fpsStart = t;
        }
        // ~60 Hz budget; sleep the remainder to be cooperative.
        uint32_t elapsed = millis() - t;
        uint32_t target = 16;
        if (elapsed < target) vTaskDelay(pdMS_TO_TICKS(target - elapsed));
        else vTaskDelay(1);
    }
}

// -------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== T-Display-S3-AMOLED NEON 3D ===");

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);

    // Display.
    rm67162_init();
    lcd_setRotation(0); // portrait 240x536

    // Touch.
    if (touch.begin()) {
        touch.setOrientation(CST816::PORTRAIT);
        Serial.println("[touch] CST816 online (SDA=3, SCL=2)");
        loadCalib();
        if (!g_calib.valid) {
            Serial.println("[touch] no saved calibration — auto-starting (tap 4 corners)");
            g_calibStep = 0;
        }
    } else {
        Serial.println("[touch] no CST816 detected — drag disabled");
    }

    // Engine + scene.
    engine.begin(TFT_WIDTH, TFT_HEIGHT);
    scene.begin();

    g_fpsStart = millis();
    xTaskCreatePinnedToCore(renderTask, "render", 4096, nullptr, 2, nullptr, 1);
}

// Stream the current framebuffer over USB-serial so a host can save a PNG.
// Protocol (big-endian):  magic 'FRM1' | u16 W | u16 H | W*H RGB565 bytes.
static void sendFrameOverSerial()
{
    g_freeze = true;
    delay(30); // let render task hold a settled frame
    uint32_t n = (uint32_t)engine.W * engine.H;
    uint8_t hdr[8] = { 'F','R','M','1',
                       (uint8_t)(engine.W >> 8), (uint8_t)(engine.W & 0xFF),
                       (uint8_t)(engine.H >> 8), (uint8_t)(engine.H & 0xFF) };
    Serial.write(hdr, sizeof(hdr));
    for (uint32_t i = 0; i < n; i++) {
        uint16_t c = engine.fb[i];
        Serial.write((uint8_t)(c & 0xFF));        // low byte first (LE)
        Serial.write((uint8_t)(c >> 8));
    }
    g_freeze = false;
}

static void processSerialCommands()
{
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == 'F' || c == 'f') {
            sendFrameOverSerial();
        } else if (c == 'P' || c == 'p') {
            scene.nextPalette();
            Serial.printf("[serial] palette %s\n", scene.pal().name);
        } else if (c == 'C' || c == 'c') {
            // Start 4-corner calibration.
            g_calibStep = 0;
            Serial.println("[touch] calibration start: tap the highlighted corners in order");
        } else if (c == 'R' || c == 'r') {
            // Debug: dump the current raw touch reading. Hold your finger on
            // the screen and send 'R' to see the live raw X/Y/Z.
            int16_t rx, ry, rz;
            bool down = touch.readRaw(rx, ry, rz);
            if (down)
                Serial.printf("[raw] down x=%d y=%d z=%d\n", rx, ry, rz);
            else
                Serial.println("[raw] no touch");
        }
    }
}

// -------------------------------------------------------------------------
void loop()
{
    processSerialCommands();

    // Single, non-blocking raw touch poll per iteration.
    int16_t rx = -1, ry = -1;
    bool down = touch.read(rx, ry);

    if (down) {
        // Authoritative last-seen position while the finger is down.
        g_lastRawX = rx;
        g_lastRawY = ry;

        float sx, sy;
        toScreen(rx, ry, sx, sy);
        int x = (int)sx, y = (int)sy;

        if (!g_wasDown) {
            // Finger just landed.
            g_downMs = millis();
            g_moved = 0.0f;
            g_lastX = x;
            g_lastY = y;
        } else {
            int dx = x - g_lastX;
            int dy = y - g_lastY;
            g_moved += sqrtf((float)dx * dx + (float)dy * dy);
            g_lastX = x;
            g_lastY = y;

            if (g_calibStep < 0) {
                // Normal mode only: drag to rotate + feed momentum.
                g_ry += dx * 0.020f;
                g_rx += dy * 0.020f;
                g_velRy = dx * 0.020f;
                g_velRx = dy * 0.020f;
                if (g_rx > 1.3f)  g_rx = 1.3f;
                if (g_rx < -0.4f) g_rx = -0.4f;
            }
        }
        g_wasDown = true;
    } else if (g_wasDown) {
        // Finger released.
        uint32_t dur = millis() - g_downMs;
        if (g_calibStep >= 0) {
            // Calibration: a clean, short tap records the current corner.
            // NOTE: g_moved here is in RAW units (calib not yet valid), so the
            // movement budget is generous to tolerate normal finger wobble.
            if (dur < 600 && g_moved < 60.0f) {
                g_raw[g_calibStep][0] = g_lastRawX;
                g_raw[g_calibStep][1] = g_lastRawY;
                Serial.printf("[calib] corner %d raw=(%d,%d)\n",
                              g_calibStep, g_raw[g_calibStep][0], g_raw[g_calibStep][1]);
                g_calibStep++;
                if (g_calibStep >= 4) {
                    bool ok = finishCalib();
                    g_calibStep = -1;
                    Serial.printf("[calib] %s\n", ok ? "done" : "failed");
                }
            }
        } else {
            // Normal mode: a quick, small move counts as a tap.
            if (dur < 300 && g_moved < 12.0f) {
                scene.nextPalette();
                Serial.printf("[touch] tap -> palette %s\n", scene.pal().name);
            }
        }
        g_wasDown = false;
    }

    delay(5);
}
