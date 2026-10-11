#include "touch_cst816.h"

bool CST816::begin(uint32_t freq)
{
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(freq);

    // Probe the I2C address. A well-configured CST816 answers with NACK-free.
    Wire.beginTransmission(I2C_ADDR);
    uint8_t ok = (Wire.endTransmission() == 0);
    if (ok) {
        // Nudge it out of any low-power state by reading the version register.
        Wire.beginTransmission(I2C_ADDR);
        Wire.write(0xF4); // firmware major version register
        Wire.endTransmission();
        Wire.requestFrom(I2C_ADDR, 1);
        Wire.available();
    }
    return ok;
}

bool CST816::read(int16_t &out_x, int16_t &out_y)
{
    out_x = out_y = -1;

    // Classic CST816 read: write pointer 0x00, read 6 bytes.
    //   X = (b0 << 4) | (b1 >> 4)   -- b0 high nibble is the valid-flag
    //   Y = (b2 << 4) | (b3 >> 4)
    //   Z = (b4 << 4) | (b5 >> 4)   -- pressure; >0 means finger present
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

    // Apply orientation mapping.
    int16_t rx = x, ry = y;
    if (_orient == LANDSCAPE) {
        // Swap and mirror to a 536x240 frame.
        rx = y;
        ry = 239 - x;
    } else {
        // Portrait 240x536: CST816 reports raw panel coords; the active
        // region is the lower-left 240x536 of the 550x480 glass. Empirically
        // the driver's x/y map directly to screen x (0..239) / y (0..535).
        rx = x;
        ry = y;
    }

    // Clamp to the logical panel.
    rx = constrain(rx, 0, 239);
    ry = constrain(ry, 0, 535);

    out_x = rx;
    out_y = ry;
    return true;
}

bool CST816::isPressed()
{
    int16_t x, y;
    return read(x, y);
}

void CST816::setOrientation(Orientation o) { _orient = o; }
