#include "RingLog.h"

RingLog Log;

void RingLog::log(const char* tag, const String& msg) {
    if (Serial) {
        if (tag && tag[0]) {
            Serial.print('['); Serial.print(tag); Serial.print("] ");
        }
        Serial.println(msg);
    }

    // Build timestamped entry
    String entry;
    entry.reserve(ENTRY_LEN - 1);
    uint32_t s = millis() / 1000;
    entry  = '[';
    entry += String(s / 60);
    entry += ':';
    if ((s % 60) < 10) entry += '0';
    entry += String(s % 60);
    entry += ']';
    if (tag && tag[0]) { entry += '['; entry += tag; entry += ']'; }
    entry += ' ';
    entry += msg;

    entry.toCharArray(_buf[_head], ENTRY_LEN);
    _head = (_head + 1) % MAX_ENTRIES;
    if (_count < MAX_ENTRIES) _count++;
}

void RingLog::tick() {
    if (_count == 0) return;

    // Primary: detect edge on Serial (terminal plugged in)
    bool nowSerial = (bool)Serial;
    if (nowSerial && !_wasSerial) {
        delay(200);
        _dump();
        _wasSerial = true;
        return;
    }
    _wasSerial = nowSerial;

    // Fallback: dump 4 seconds after boot even if edge not detected,
    // and then every 60s if buffer isn't empty (covers HWCDC quirks)
    uint32_t now = millis();
    if (!_firstDumpDone && now > 4000) {
        _firstDumpDone = true;
        if (Serial && _count > 0) _dump();
        return;
    }
    if (_firstDumpDone && _count > 0 && now - _lastPeriodicDump > 60000) {
        _lastPeriodicDump = now;
        if (Serial) _dump();
    }
}

void RingLog::_dump() {
    if (_count == 0) return;

    Serial.println(F("\n╔══════════════════════════════════╗"));
    Serial.print  (F("║  mpcb-iot log  ("));
    Serial.print  (_count);
    Serial.println(F(" entries buffered) ║"));
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
