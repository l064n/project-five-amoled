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
// Protocol (CST816 family, see datasheet/CST816_Register.pdf in the
// official LilyGO repo):
//   Write register pointer 0x00, then read 6 bytes:
//     x = (b0 << 4) | (b1 >> 4)   // b0 high nibble = valid-flag
//     y = (b2 << 4) | (b3 >> 4)
//     z = (b4 << 4) | (b5 >> 4)   // pressure; >0 => finger present
//   A touch is considered active when z > 0 or the valid-flag bit is set.
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

    // Convenience: true if any finger is down right now.
    bool isPressed();

    // Rotate/flip touch to match the display orientation you're using.
    // For the default portrait (240x536) mapping no transform is needed.
    enum Orientation : uint8_t { PORTRAIT = 0, LANDSCAPE = 1 };
    void setOrientation(Orientation o);

private:
    Orientation _orient = PORTRAIT;
};
