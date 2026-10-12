#pragma once
//
// Minimal, dependency-free CST816 capacitive touch driver for the
// LilyGO T-Display-S3-AMOLED (1.91" touch variant).
//
// I2C wiring on this board (confirmed against LilyGO AMOLED-Series and
// espboards.dev pinout):
//   SDA -> GPIO3
//   SCL -> GPIO2
//
// Protocol (CST816 family) — matches the official LilyGo-AMOLED-Series
// TouchClassCST816::getPoint(), which reads a 13-byte report from reg 0x00:
//   b[2]   = touch point count (0 = none, 1 = single touch)
//   b[3]   = X high (low 4 bits)    b[4] = X low (8 bits)
//   b[5]   = Y high (low 4 bits)    b[6] = Y low (8 bits)
//   b[7]   = Z/pressure high        b[8] = Z low
//   x = (b[3] & 0x0F) << 8 | b[4]      y = (b[5] & 0x0F) << 8 | b[6]
// A touch is active when the point count == 1.
//
#include <Arduino.h>
#include <Wire.h>

class CST816 {
public:
    static constexpr int  I2C_ADDR = 0x15;
    static constexpr int  SDA_PIN  = 3;
    static constexpr int  SCL_PIN  = 2;

    // Bring up the I2C bus. Returns true if the touch IC answers.
    bool begin(uint32_t freq = 400000);

    // Non-blocking poll. Fills out_x/out_y in screen space (0..239, 0..535)
    // and returns true while a finger is present.
    bool read(int16_t &out_x, int16_t &out_y);

    // Non-blocking poll returning the RAW controller values (unclamped) plus
    // pressure z. Used for calibrating the screen mapping. Returns true if a
    // finger is present.
    bool readRaw(int16_t &raw_x, int16_t &raw_y, int16_t &raw_z);

    // Convenience: true if any finger is down right now.
    bool isPressed();

    // Rotate/flip touch to match the display orientation you're using.
    // For the default portrait (240x536) mapping no transform is needed.
    enum Orientation : uint8_t { PORTRAIT = 0, LANDSCAPE = 1 };
    void setOrientation(Orientation o);

private:
    Orientation _orient = PORTRAIT;
};
