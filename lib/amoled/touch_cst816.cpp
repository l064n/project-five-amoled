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

    Wire.beginTransmission(I2C_ADDR);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) return false;

    int n = Wire.requestFrom(I2C_ADDR, 6);
    if (n < 6) return false;

    uint8_t b0 = Wire.read(), b1 = Wire.read();
    uint8_t b2 = Wire.read(), b3 = Wire.read();
    uint8_t b4 = Wire.read(), b5 = Wire.read();

    int16_t x = (int16_t)((b0 << 4) | (b1 >> 4));
    int16_t y = (int16_t)((b2 << 4) | (b3 >> 4));
    int16_t z = (int16_t)((b4 << 4) | (b5 >> 4));

    bool valid_flag = (b0 & 0x01);
    if (z <= 0 && !valid_flag) return false;

    raw_x = x; raw_y = y; raw_z = z;
    return true;
}

bool CST816::isPressed()
{
    int16_t x, y;
    return read(x, y);
}

void CST816::setOrientation(Orientation o) { _orient = o; }
