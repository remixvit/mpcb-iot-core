#include "WS2812Strip.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include <string.h>
#include "Arduino.h"

// WS2812B bit timing @ RMT clock 10 MHz (100 ns / tick):
//   Bit-0:  T0H = 4 ticks (400 ns)  T0L = 8 ticks (800 ns)
//   Bit-1:  T1H = 8 ticks (800 ns)  T1L = 4 ticks (400 ns)
//   Reset: >500 µs low  — achieved by delayMicroseconds(60) after transmission

bool WS2812Strip::begin(uint8_t pin, uint16_t numPixels) {
    _count = numPixels;
    _buf   = (uint8_t*)calloc(numPixels * 3, 1);
    if (!_buf) return false;

    rmt_tx_channel_config_t chan_cfg = {};
    chan_cfg.gpio_num          = (gpio_num_t)pin;
    chan_cfg.clk_src           = RMT_CLK_SRC_DEFAULT;
    chan_cfg.resolution_hz     = 10'000'000;  // 10 MHz
    chan_cfg.mem_block_symbols = 64;
    chan_cfg.trans_queue_depth = 4;

    rmt_channel_handle_t ch = nullptr;
    if (rmt_new_tx_channel(&chan_cfg, &ch) != ESP_OK) {
        free(_buf); _buf = nullptr;
        return false;
    }
    _channel = ch;

    rmt_bytes_encoder_config_t enc_cfg = {};
    enc_cfg.bit0.duration0 = 4; enc_cfg.bit0.level0 = 1;
    enc_cfg.bit0.duration1 = 8; enc_cfg.bit0.level1 = 0;
    enc_cfg.bit1.duration0 = 8; enc_cfg.bit1.level0 = 1;
    enc_cfg.bit1.duration1 = 4; enc_cfg.bit1.level1 = 0;
    enc_cfg.flags.msb_first = 1;

    rmt_encoder_handle_t enc = nullptr;
    if (rmt_new_bytes_encoder(&enc_cfg, &enc) != ESP_OK) {
        rmt_del_channel(ch); _channel = nullptr;
        free(_buf);          _buf     = nullptr;
        return false;
    }
    _encoder = enc;

    rmt_enable(ch);
    clear();
    return true;
}

void WS2812Strip::setPixel(uint16_t idx, uint8_t r, uint8_t g, uint8_t b) {
    if (!_buf || idx >= _count) return;
    _buf[idx * 3 + 0] = g;  // WS2812: GRB byte order
    _buf[idx * 3 + 1] = r;
    _buf[idx * 3 + 2] = b;
}

void WS2812Strip::fill(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
    if (!_buf) return;
    uint8_t gr = (uint16_t)g * brightness / 255;
    uint8_t rr = (uint16_t)r * brightness / 255;
    uint8_t br = (uint16_t)b * brightness / 255;
    for (uint16_t i = 0; i < _count; i++) {
        _buf[i * 3 + 0] = gr;
        _buf[i * 3 + 1] = rr;
        _buf[i * 3 + 2] = br;
    }
}

void WS2812Strip::show() {
    if (!_channel || !_buf) return;
    rmt_transmit_config_t tx_cfg = {};
    tx_cfg.loop_count = 0;
    rmt_transmit(
        (rmt_channel_handle_t)_channel,
        (rmt_encoder_handle_t)_encoder,
        _buf, (size_t)_count * 3, &tx_cfg);
    rmt_tx_wait_all_done((rmt_channel_handle_t)_channel, pdMS_TO_TICKS(100));
    delayMicroseconds(60);  // >50 µs reset pulse
}

void WS2812Strip::clear() {
    if (_buf) memset(_buf, 0, (size_t)_count * 3);
    show();
}

void WS2812Strip::end() {
    if (_channel) {
        rmt_disable((rmt_channel_handle_t)_channel);
        rmt_del_channel((rmt_channel_handle_t)_channel);
        _channel = nullptr;
    }
    if (_encoder) {
        rmt_del_encoder((rmt_encoder_handle_t)_encoder);
        _encoder = nullptr;
    }
    if (_buf) { free(_buf); _buf = nullptr; }
    _count = 0;
}
