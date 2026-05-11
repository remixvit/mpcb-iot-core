#include "RingLog.h"

RingLog Log;

void RingLog::log(const char* tag, const String& msg) {
    // Print to serial immediately if connected
    if (Serial) {
        if (tag && tag[0]) {
            Serial.print('['); Serial.print(tag); Serial.print("] ");
        }
        Serial.println(msg);
    }

    // Build timestamped entry for the ring buffer
    String entry;
    entry.reserve(ENTRY_LEN - 1);
    uint32_t s = millis() / 1000;
    entry  = '[';
    entry += String(s / 60);   // minutes
    entry += ':';
    if ((s % 60) < 10) entry += '0';
    entry += String(s % 60);   // seconds
    entry += ']';
    if (tag && tag[0]) { entry += '['; entry += tag; entry += ']'; }
    entry += ' ';
    entry += msg;

    entry.toCharArray(_buf[_head], ENTRY_LEN);
    _head = (_head + 1) % MAX_ENTRIES;
    if (_count < MAX_ENTRIES) _count++;
}

void RingLog::tick() {
    // On ESP32-C6 HWCDC: (bool)Serial is true when USB terminal is reading
    bool nowSerial = (bool)Serial;

    if (nowSerial && !_wasSerial) {
        // Terminal just connected — give it 150ms to initialise
        delay(150);
        _dump();
    }
    _wasSerial = nowSerial;
}

void RingLog::_dump() {
    if (_count == 0) return;

    Serial.println(F("\n╔══════════════════════════════════╗"));
    Serial.print  (F("║  mpcb-iot log buffer — "));
    Serial.print  (_count);
    Serial.println(F(" entries  ║"));
    Serial.println(F("╚══════════════════════════════════╝"));

    uint8_t start = (_head - _count + MAX_ENTRIES) % MAX_ENTRIES;
    for (uint8_t i = 0; i < _count; i++) {
        Serial.println(_buf[(start + i) % MAX_ENTRIES]);
    }

    Serial.println(F("══════════════════════════════════════\n"));
    clear();
}

void RingLog::clear() {
    _head  = 0;
    _count = 0;
}
