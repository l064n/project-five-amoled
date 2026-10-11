/*
 * LilyGO T-Display-S3-AMOLED — starter demo
 *
 * Boots the RM67162 QSPI AMOLED (240x536) and runs a small LVGL UI:
 * a title label, a live clock, and a color swatch row. This proves the
 * full stack: USB-CDC serial, QSPI display driver, and LVGL rendering.
 *
 * Flash with:  pio run -t upload
 * Monitor:     pio device monitor
 */

#include <Arduino.h>
#include <lvgl.h>

#include "rm67162.h"     // local amoled driver
#include "pins_config.h" // board pin map (buttons, LED, etc.)

// Pack a raw RGB565 value into an lv_color_t (LVGL 8.x, 16-bit color depth).
static inline lv_color_t c565(uint16_t v)
{
    lv_color_t c;
    c.full = v;
    return c;
}

// ---------------------------------------------------------------------------
// Display + LVGL globals
// ---------------------------------------------------------------------------
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t       disp_drv;
static lv_color_t         *buf;

static lv_obj_t *clock_label = nullptr;

// 16-bit full frame in PSRAM (240 * 536 * 2 bytes ~= 254 KB)
#define LVGL_BUF_PIXELS (TFT_WIDTH * TFT_HEIGHT)

// ---------------------------------------------------------------------------
// LVGL flush: blit the rendered area to the AMOLED
// ---------------------------------------------------------------------------
static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    (void)disp;
    uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);
    lcd_PushColors(area->x1, area->y1, w, h, (uint16_t *)&color_p->full);
    lv_disp_flush_ready(&disp_drv);
}

// ---------------------------------------------------------------------------
// Button wiring (user buttons on the board)
//   BTN1 = GPIO0 (bootside), BTN2 = GPIO21
// A short click cycles the demo background color; a long press toggles
// the backlight LED on IO38 (onboard LED on the non-touch variant).
// ---------------------------------------------------------------------------
static const uint16_t bg_colors[] = {
    0x0000, 0xF800, 0x07E0, 0x001F, 0xFFE0, 0x120F, 0xFC00, 0x7BEF,
};
static uint8_t bg_index = 0;

static void set_bg(uint16_t rgb565)
{
    lv_obj_set_style_bg_color(lv_scr_act(), c565(rgb565), 0);
}

static void on_btn1_click()
{
    bg_index = (bg_index + 1) % (sizeof(bg_colors) / sizeof(bg_colors[0]));
    set_bg(bg_colors[bg_index]);
    Serial.printf("[btn1] background -> %d\n", bg_index);
}

static void on_btn1_long()
{
    static bool led_on = true;
    led_on = !led_on;
    digitalWrite(PIN_LED, led_on ? HIGH : LOW);
    Serial.printf("[btn1] long press -> LED %s\n", led_on ? "ON" : "OFF");
}

static void on_btn2_click()
{
    lcd_setRotation(0); // toggle-ish: reset to portrait
    Serial.println("[btn2] reset rotation");
}

// ---------------------------------------------------------------------------
// LVGL tick: increment the internal clock every 5 ms from a dedicated task
// ---------------------------------------------------------------------------
static void lvgl_tick_task(void *arg)
{
    (void)arg;
    for (;;) {
        lv_tick_inc(5);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ---------------------------------------------------------------------------
// setup / loop
// ---------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("\n========================================");
    Serial.println(" LilyGO T-Display-S3-AMOLED  (LVGL demo)");
    Serial.println("========================================");

    // Onboard LED: IO38 is the LED on the non-touch variant.
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);

    // --- AMOLED ---
    rm67162_init();
    lcd_setRotation(0); // portrait, 240 x 536

    // --- LVGL ---
    lv_init();
    buf = (lv_color_t *)ps_malloc(sizeof(lv_color_t) * LVGL_BUF_PIXELS);
    if (!buf) {
        Serial.println("FATAL: could not allocate LVGL draw buffer in PSRAM");
        for (;;)
            ;
    }
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, LVGL_BUF_PIXELS);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = TFT_WIDTH; // 240
    disp_drv.ver_res = TFT_HEIGHT; // 536
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // LVGL tick driver: dedicated task calls lv_tick_inc every 5 ms
    xTaskCreatePinnedToCore(lvgl_tick_task, "lvgl_tick", 2048, nullptr, 1, nullptr, 0);

    // --- UI ---
    set_bg(bg_colors[0]);

    // Title
    lv_obj_t *title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "T-Display S3\nAMOLED");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);

    // Live clock (updated in loop)
    clock_label = lv_label_create(lv_scr_act());
    lv_label_set_text(clock_label, "--:--:--");
    lv_obj_align(clock_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(clock_label, lv_color_white(), 0);

    // Sub-hint
    lv_obj_t *hint = lv_label_create(lv_scr_act());
    lv_label_set_text(hint, "BTN1: color / long: LED\nBTN2: rotation");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_text_color(hint, lv_color_make(200, 200, 200), 0);

    // Color swatch row
    for (int i = 0; i < (int)(sizeof(bg_colors) / sizeof(bg_colors[0])); i++) {
        lv_obj_t *sw = lv_obj_create(lv_scr_act());
        lv_obj_set_size(sw, 20, 20);
        lv_obj_set_pos(sw, 8 + i * 24, TFT_HEIGHT - 60);
        lv_obj_set_style_bg_color(sw, c565(bg_colors[i]), 0);
        lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
        lv_obj_clear_flag(sw, LV_OBJ_FLAG_SCROLLABLE);
    }

    // --- Buttons (interrupt-free polling via OneButton is optional;
    //      here we poll the user buttons directly in loop()).
    pinMode(PIN_BUTTON_1, INPUT_PULLUP);
    pinMode(PIN_BUTTON_2, INPUT_PULLUP);
}

// Simple edge-detected button polling with long-press for BTN1
static uint32_t btn1_down_ms = 0;
static bool     btn1_long_fired = false;
static bool     btn1_was_pressed = false;

void loop()
{
    uint32_t now = millis();

    // --- Button 1: click = next color, long (>= 600 ms) = toggle LED ---
    bool btn1_now = (digitalRead(PIN_BUTTON_1) == LOW);
    if (btn1_now && !btn1_was_pressed) {
        btn1_down_ms = now;
        btn1_long_fired = false;
    }
    if (btn1_now && !btn1_long_fired && (now - btn1_down_ms) >= 600) {
        btn1_long_fired = true;
        on_btn1_long();
    }
    if (!btn1_now && btn1_was_pressed && !btn1_long_fired) {
        if ((now - btn1_down_ms) < 600)
            on_btn1_click();
    }
    btn1_was_pressed = btn1_now;

    // --- Button 2: click = reset rotation ---
    static bool btn2_was = false;
    bool btn2_now = (digitalRead(PIN_BUTTON_2) == LOW);
    if (btn2_now && !btn2_was)
        on_btn2_click();
    btn2_was = btn2_now;

    // --- LVGL ---
    lv_timer_handler();

    // --- Clock (update once per second) ---
    static uint32_t last_sec = 0;
    uint32_t sec = (now / 1000);
    if (sec != last_sec) {
        last_sec = sec;
        struct tm t = {};
        time_t tt = time(nullptr);
        localtime_r(&tt, &t);
        char s[16];
        if (t.tm_hour || t.tm_min || t.tm_sec)
            snprintf(s, sizeof(s), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
        else
            snprintf(s, sizeof(s), "%04d-%02d-%02d", 1970 + t.tm_year, t.tm_mon + 1, t.tm_mday);
        lv_label_set_text(clock_label, s);
    }

    delay(5);
}
