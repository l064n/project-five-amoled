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
static bool     g_wasDown = false;
static uint32_t g_downMs = 0;
static float    g_moved = 0.0f;

// FPS tracking.
static uint32_t g_frames = 0;
static uint32_t g_fpsStart = 0;
static int      g_fps = 0;

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
    scene.render(g_rx, g_ry, t_ms);
    drawPaletteDots();
    drawInt(engine.W - 30, engine.H - 12, g_fps,
            scene.pal().line_r, scene.pal().line_g, scene.pal().line_b);

    // Blit the full framebuffer to the AMOLED (240x536).
    lcd_PushColors(0, 0, engine.W, engine.H, engine.fb);
}

// Dedicated render task on core 1 to keep display + 3D off the loop core.
static void renderTask(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t t = millis();
        renderAndPush(t);
        g_frames++;

        // FPS once per second.
        if (t - g_fpsStart >= 1000) {
            g_fps = (int)(g_frames * 1000.0f / (float)(t - g_fpsStart));
            g_frames = 0;
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
    } else {
        Serial.println("[touch] no CST816 detected — drag disabled");
    }

    // Engine + scene.
    engine.begin(TFT_WIDTH, TFT_HEIGHT);
    scene.begin();

    g_fpsStart = millis();
    xTaskCreatePinnedToCore(renderTask, "render", 4096, nullptr, 2, nullptr, 1);
}

// -------------------------------------------------------------------------
void loop()
{
    // Poll touch (non-blocking) and drive rotation.
    int16_t x = -1, y = -1;
    bool down = touch.read(x, y);

    if (down && x >= 0) {
        if (!g_wasDown) {
            g_lastX = x; g_lastY = y;
            g_downMs = millis();
            g_moved = 0.0f;
        } else {
            int dx = (int)x - (int)g_lastX;
            int dy = (int)y - (int)g_lastY;
            g_moved += (float)sqrtf((float)dx * dx + (float)dy * dy);
            // Drag: move the camera; feed momentum.
            g_ry += dx * 0.011f;
            g_rx += dy * 0.011f;
            g_velRy = dx * 0.011f;
            g_velRx = dy * 0.011f;
            if (g_rx > 1.3f)  g_rx = 1.3f;
            if (g_rx < -0.4f) g_rx = -0.4f;
        }
        g_lastX = x; g_lastY = y;
        g_wasDown = true;
    } else {
        if (g_wasDown) {
            // Released: a quick, small move counts as a tap.
            uint32_t dur = millis() - g_downMs;
            if (dur < 260 && g_moved < 8.0f) {
                scene.nextPalette();
                Serial.printf("[touch] tap -> palette %s\n",
                              scene.pal().name);
            }
            g_wasDown = false;
        }
    }

    delay(5);
}
