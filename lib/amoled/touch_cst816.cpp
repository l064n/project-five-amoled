#include "touch_cst816.h"

bool CST816::begin(uint32_t freq)
{
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(freq);

    // Probe the I2C address.
    Wire.beginTransmission(I2C_ADDR);
    uint8_t ok = (Wire.endTransmission() == 0);
    if (ok) {
        // Nudge out of low-power by reading the version register.
        Wire.beginTransmission(I2C_ADDR);
        Wire.write(0xF4);
        Wire.endTransmission();
        Wire.requestFrom(I2C_ADDR, 1);
        Wire.available();
    }
    return ok;
}

// Returns the controller's RAW (untransformed) coordinates. Detection is
// based on pressure z (and the valid-flag bit). No clamping here — the
// application maps raw -> screen, since the CST816 reports a frame that
// must be scaled/mirrored to match the portrait 240x536 layout.
bool CST816::read(int16_t &out_x, int16_t &out_y)
{
    out_x = out_y = -1;
    int16_t z;
    return readRaw(out_x, out_y, z);
}

bool CST816::readRaw(int16_t &raw_x, int16_t &raw_y, int16_t &raw_z)
{
    raw_x = raw_y = raw_z = -1;

    // Point at register 0x00 and read the 13-byte report block, EXACTLY like
    // the official LilyGo-AMOLED-Series TouchClassCST816::getPoint():
    //   b[2]   = touch point count  (0 = none, 1 = single touch)
    //   b[3]   = X high (low 4 bits)    b[4] = X low (8 bits)
    //   b[5]   = Y high (low 4 bits)    b[6] = Y low (8 bits)
    //   b[7]   = Z/pressure high        b[8] = Z low
    // x = (b[3] & 0x0F) << 8 | b[4]      y = (b[5] & 0x0F) << 8 | b[6]
    // (The earlier 6-byte "(b0<<4)|(b1>>4)" parse read two RESERVED zero bytes
    //  and always returned x~0 — that is why touch was unresponsive.)
    Wire.beginTransmission(I2C_ADDR);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) return false;

    int n = Wire.requestFrom(I2C_ADDR, 13);
    if (n < 7) return false;

    uint8_t b[13];
    for (int i = 0; i < n; i++) b[i] = (uint8_t)Wire.read();

    uint8_t point = b[2] & 0x0F;
    if (point != 1) return false;        // CST816 is single-touch

    int16_t x = (int16_t)(((b[3] & 0x0F) << 8) | b[4]);
    int16_t y = (int16_t)(((b[5] & 0x0F) << 8) | b[6]);
    int16_t z = (n >= 9) ? (int16_t)(((b[7] & 0x0F) << 8) | b[8]) : 1;

    raw_x = x; raw_y = y; raw_z = z;
    return true;
}

bool CST816::isPressed()
{
    int16_t x, y;
    return read(x, y);
}

void CST816::setOrientation(Orientation o) { _orient = o; }
