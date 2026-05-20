#pragma once
#include <stdint.h>
#include <stdlib.h>

// Minimal WS2812/WS2812B strip driver using ESP-IDF 5 RMT hardware.
// No external library required — uses driver/rmt_tx + rmt_encoder from IDF 5.x.
class WS2812Strip {
public:
    // Allocate RMT TX channel on pin and heap pixel buffer.
    bool     begin(uint8_t pin, uint16_t numPixels);

    // Set one pixel color (GRB reordering is handled internally).
    void     setPixel(uint16_t idx, uint8_t r, uint8_t g, uint8_t b);

    // Fill the whole strip with one color, scaled by brightness 0-255.
    void     fill(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness = 255);

    // Transmit pixel buffer to the strip via RMT. Blocks until done + 60 µs reset.
    void     show();

    // Fill black and show.
    void     clear();

    // Release RMT channel and free pixel buffer.
    void     end();

    uint16_t count() const { return _count; }

private:
    void*    _channel = nullptr;  // rmt_channel_handle_t (opaque to avoid IDF headers here)
    void*    _encoder = nullptr;  // rmt_encoder_handle_t
    uint8_t* _buf     = nullptr;  // GRB pixel buffer, _count * 3 bytes
    uint16_t _count   = 0;
};
